#include "pch.h"
#include "Worker/Route/Route.h"
#include "Network/GameSession.h"
#include "Content/Field/ZoneLayout.h"
#include "Content/Field/ZoneManager.h"
#include "User/User.h"
#include "User/UserHelper.h"
#include "Database/DBThread.h"
#include "Content/ContentHelper.h"
#include "Content/Chat/ChatHelper.h"
#include "Content/Combat/CombatHelper.h"
#include "Content/Inventory/ItemHelper.h"
#include "Content/Npc/NpcHelper.h"
#include "Content/Quest/QuestHelper.h"
#include "Content/Skill/UserSkillHelper.h"
#include "Content/Skill/SkillGrow.h"
#include "Content/Guild/GuildService.h"
#include "Content/Auction/AuctionService.h"
#include "Content/Party/PartyHelper.h"
#include "Content/Craft/CraftHelper.h"
#include "Content/Transfer/TransferHelper.h"
#include "Content/Social/SocialHelper.h"
#include "Content/Achieve/AchieveHelper.h"
#include "Database/GameData/Item/ItemTable.h"
#include "Database/GameData/ItemRules.h"
#include <array>

#include "Worker/Route/_Auth.inl"
#include "Worker/Route/_User.inl"
#include "Worker/Route/_Skill.inl"
#include "Worker/Route/_Inven.inl"
#include "Worker/Route/_Npc.inl"
#include "Worker/Route/_Quest.inl"
#include "Worker/Route/_Chat.inl"
#include "Worker/Route/_Guild.inl"
#include "Worker/Route/_Party.inl"
#include "Worker/Route/_Transfer.inl"
#include "Worker/Route/_Social.inl"
#include "Worker/Route/_Achieve.inl"

namespace
{
    using Handler = void (*)(const SessionSharedPtr&, const char*, UInt8);

    // 다른 Zone으로 넘기는 횟수 상한 — Zone을 아주 빠르게 오가도 패킷이 끝없이 돌지 않게
    constexpr UInt8 kMaxForwardHops = 4;

    void Forward(const SessionSharedPtr& pSession, ZoneId zoneId, const char* packet, UInt8 hops);

    struct RouteEntry
    {
        UInt16   size    = 0;          // 이 타입 패킷의 정확한 크기 — 다르면 버린다
        Handler  handler = nullptr;
    };

    // 로그인 전 요청 — 세션만 넘긴다.
    template <typename _Packet, void (*Fn)(const SessionSharedPtr&, const _Packet&)>
    void PreLogin(const SessionSharedPtr& pSession, const char* packet, UInt8 hops)
    {
        Fn(pSession, *reinterpret_cast<const _Packet*>(packet));
    }

    // 게임 중 요청 — 상태 · 소유 CUser · 소유 Zone을 여기서 한 번에 거른다.
    template <typename _Packet, void (*Fn)(const UserSharedPtr&, const _Packet&)>
    void InGame(const SessionSharedPtr& pSession, const char* packet, UInt8 hops)
    {
        if (pSession->m_eState != SOCKET_STATE::ST_INGAME)
            return;
        const auto pUser = pSession->GetOwner();
        if (pUser == nullptr)
            return;

        // 이 스레드가 CUser를 가진 Zone이 아니거나 Zone을 옮기는 중이면(옛 Zone 큐에 남아 있던 패킷 · 새 Zone 입장 전에 온 패킷)
        // 가진 Zone 큐 뒤로 넘긴다 — 새 Zone에서는 입장 처리(HandleEnterZone) 뒤에 돈다. 예전엔 옛 Zone 스레드가 그대로 처리해
        // 두 Zone 스레드가 같은 CUser의 인벤토리 · HP를 동시에 만졌다.
        const ZoneId ownerZone = pUser->GetZoneId();
        if (pUser->IsTransferring() || ownerZone != LCurrentZoneId)
        {
            Forward(pSession, ownerZone, packet, hops);
            return;
        }
        Fn(pUser, *reinterpret_cast<const _Packet*>(packet));
    }

    using RouteTable = std::array<RouteEntry, 256>;

    RouteTable BuildRouteTable()
    {
        RouteTable table{};
#define ROUTE_PRE_LOGIN(TYPE, NS) table[static_cast<UInt8>(PacketType::TYPE)] = { sizeof(TYPE##_PACKET), &PreLogin<TYPE##_PACKET, &NS::OnRecv_##TYPE> }
#define ROUTE_IN_GAME(TYPE, NS)   table[static_cast<UInt8>(PacketType::TYPE)] = { sizeof(TYPE##_PACKET), &InGame<TYPE##_PACKET, &NS::OnRecv_##TYPE> }
        ROUTE_PRE_LOGIN(USER_LOGIN_REQ, RouteAuth);

        ROUTE_IN_GAME(USER_MOVE_REQ,       RouteUser);
        ROUTE_IN_GAME(USER_RESTART_REQ,    RouteUser);
        ROUTE_IN_GAME(STAT_ALLOC_REQ,      RouteUser);

        ROUTE_IN_GAME(SKILL_REQ,           RouteSkill);
        ROUTE_IN_GAME(SKILL_GROW_REQ,      RouteSkill);

        ROUTE_IN_GAME(ITEM_EQUIP_REQ,      RouteItem);
        ROUTE_IN_GAME(ITEM_UNEQUIP_REQ,    RouteItem);
        ROUTE_IN_GAME(ITEM_SWAP_REQ,       RouteItem);
        ROUTE_IN_GAME(ITEM_DISCARD_REQ,    RouteItem);
        ROUTE_IN_GAME(ITEM_PICKUP_REQ,     RouteItem);
        ROUTE_IN_GAME(ITEM_USE_REQ,        RouteItem);
        ROUTE_IN_GAME(ITEM_SORT_REQ,       RouteItem);
        ROUTE_IN_GAME(ITEM_ENCHANT_REQ,   RouteItem);
        ROUTE_IN_GAME(CRAFT_REQ,           RouteItem);

        ROUTE_IN_GAME(SHOP_BUY_REQ,        RouteNpc);
        ROUTE_IN_GAME(SHOP_SELL_REQ,       RouteNpc);
        ROUTE_IN_GAME(NPC_TELEPORT_REQ,    RouteNpc);
        ROUTE_IN_GAME(NPC_TALK_REQ,        RouteNpc);
        ROUTE_IN_GAME(NPC_SERVICE_REQ,     RouteNpc);

        ROUTE_IN_GAME(QUEST_ACCEPT_REQ,    RouteQuest);
        ROUTE_IN_GAME(QUEST_COMPLETE_REQ,  RouteQuest);
        ROUTE_IN_GAME(QUEST_ABANDON_REQ,   RouteQuest);

        ROUTE_IN_GAME(CS_CHAT,            RouteChat);
        ROUTE_IN_GAME(GUILD_REQ,           RouteGuild);
        ROUTE_IN_GAME(GUILD_EX_REQ,        RouteGuild);
        ROUTE_IN_GAME(AUCTION_REQ,         RouteGuild);
        ROUTE_IN_GAME(PARTY_REQ,           RouteParty);
        ROUTE_IN_GAME(TRANSFER_REQ,        RouteTransfer);
        ROUTE_IN_GAME(SOCIAL_REQ,          RouteSocial);
        ROUTE_IN_GAME(ACHIEVE_REQ,         RouteAchieve);
#undef ROUTE_PRE_LOGIN
#undef ROUTE_IN_GAME
        return table;
    }

    const RouteTable& GetRouteTable()
    {
        static const RouteTable table = BuildRouteTable();
        return table;
    }
}

namespace
{
    void DispatchImpl(const SessionSharedPtr& pSession, const char* packet, UInt8 hops)
    {
        UInt16 size = 0;
        std::memcpy(&size, packet, sizeof(size));
        // 타입은 1바이트. char로 읽으면 128 이상에서 음수가 되므로 부호 없이 읽는다.
        const UInt8 type = static_cast<UInt8>(packet[2]);

        const RouteEntry& entry = GetRouteTable()[type];
        if (entry.handler == nullptr)
            return;     // 모르는(또는 없앤) 타입
        if (size != entry.size)
            return;     // 크기가 다른 패킷 — 짧은 패킷이 앞 패킷의 남은 바이트를 읽던 문제를 막는다

        // 기본 로직 스레드(Zone 밖 — GSector 없음)에서는 로그인 패킷만 받는다. 로그인 완료 전에 보낸 게임 패킷이
        // 여기 남아 있다가 게임 중이 된 뒤 처리되면 섹터 없이 돌아 크래시가 났다. 정상 클라는 로그인 응답 뒤에 보낸다.
        if (LCurrentZoneId == InvalidZoneId && type != static_cast<UInt8>(PacketType::USER_LOGIN_REQ))
            return;

        entry.handler(pSession, packet, hops);
    }

    void Forward(const SessionSharedPtr& pSession, ZoneId zoneId, const char* packet, UInt8 hops)
    {
        if (hops >= kMaxForwardHops || !CZoneLayout::IsValidZoneId(zoneId))
            return;
        UInt16 size = 0;
        std::memcpy(&size, packet, sizeof(size));
        std::array<char, MAX_CLIENT_PACKET_SIZE> copy{};
        std::memcpy(copy.data(), packet, std::min<size_t>(size, copy.size()));
        GZoneManager->EnqueueByZone(zoneId, [pSession, copy, hops]()
        {
            DispatchImpl(pSession, copy.data(), static_cast<UInt8>(hops + 1));
        });
    }
}

namespace Route
{
    void Dispatch(const SessionSharedPtr& pSession, const char* packet)
    {
        DispatchImpl(pSession, packet, 0);
    }
}
