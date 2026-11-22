#include "pch.h"
#include "Content/Transfer/TransferHelper.h"
#include "Content/Chat/ChatHelper.h"
#include "Content/Guild/GuildService.h"
#include "Content/Auction/AuctionService.h"
#include "Content/Combat/CombatHelper.h"
#include "Database/GameData/MoveRule.h"
#include "User/UserHelper.h"
#include "User/UserManager.h"
#include "Content/Skill/UserSkillHelper.h"
#include "Content/Skill/SkillGrow.h"
#include "Base/GameObjectManager.h"
#include "Entity/Subject/Subject.h"
#include "User/User.h"
#include "Entity/Subject/Monster/Monster.h"
#include "Database/DBThread.h"
#include "Content/Field/Sector.h"
#include "Worker/TimerThread.h"
#include "Entity/Subject/SubjectHelper.h"
#include "Content/Field/SectorHelper.h"
#include "Content/Navigator/Collision.h"
#include "Base/Core/CoreTLS.h"
#include "Content/Inventory/Item/EquipmentItem.h"
#include "Content/Inventory/ItemHelper.h"
#include "Database/GameData/Item/ItemTableData.h"
#include "Database/GameData/Item/ItemTable.h"
#include "Database/GameData/ItemRules.h"
#include "Database/GameData/LevelTable.h"
#include "Database/GameData/MonsterTable.h"
#include "Database/GameData/WorldData.h"
#include "Content/ContentHelper.h"
#include "Content/Quest/QuestHelper.h"
#include "Entity/Subject/Monster/MonsterHelper.h"
#include "Entity/Component/Behavior/EntityBehaviorHelper.h"
#include "Network/GameSessionManager.h"
#include "Content/Field/ZoneLayout.h"
#include "Content/Field/ZoneManager.h"
#include "Content/Party/PartyHelper.h"
#include "Content/Reward/RewardHelper.h"
#include "Content/Achieve/AchieveHelper.h"

namespace
{
    template<typename Packet>
    void InitializePacket(Packet& packet, PacketType type)
    {
        packet = {};
        packet.size = sizeof(Packet);
        packet.type = static_cast<char>(type);
    }

    SessionSharedPtr GetSession(const CSubject::SharedPtr& sender)
    {
        auto user = static_pointer_cast<CUser>(sender);
        if (!user)
            return nullptr;
        return user->GetGameSession();
    }

    bool IsValidWorldPosition(int x, int y)
    {
        if (x < 0 || x >= W_WIDTH || y < 0 || y >= W_HEIGHT)
            return false;

        return !isCollision(static_cast<short>(x), static_cast<short>(y));
    }

    // ITEM_SLOT_DATA를 채우는 로직은 ITEM_EQUIP_ACK/ITEM_UNEQUIP_ACK/ITEM_SWAP_ACK/
    // ITEM_DISCARD_ACK/ITEM_USE_ACK가 전부 공유한다 — "슬롯을 어떻게 와이어 포맷으로 바꾸는가"를
    // 한 곳에만 두기 위함.
    void FillItemSlotData(ITEM_SLOT_DATA& data, UInt16 slotIndex, const CItem::SharedPtr& item)
    {
        data.slotIndex = slotIndex;
        if (item == nullptr)
        {
            data.itemId = 0;
            data.count  = 0;
            data.equipped = 0;
            return;
        }

        const CEquipmentItem* equipment = AsEquipment(item);
        data.itemId   = item->GetItemTableId();
        data.count    = item->GetCount();
        data.equipped = (equipment != nullptr && equipment->IsEquipped()) ? 1 : 0;
    }

    // 게임 안(ST_INGAME)에 있는 유저인가(CSubject가 CUser일 때만 true)
    bool IsInGameViewer(const CSubject::SharedPtr& subject)
    {
        if (subject == nullptr || subject->GetObjID().GetCategory<EnumCategory>() != EnumCategory::eUser)
            return false;
        const auto session = static_pointer_cast<CUser>(subject)->GetGameSession();
        return session != nullptr && session->m_eState == SOCKET_STATE::ST_INGAME;
    }

    UInt8 BuildSubjectFlags(const UserSharedPtr& user)
    {
        const UInt32 now = GetNowTime();
        UInt8 flags = 0;
        if (user->IsPurple(now))                   flags |= SUBJECT_FLAG_PURPLE;
        if (user->IsHasted(now))                   flags |= SUBJECT_FLAG_HASTE;
        if (user->GetStat()->IsDead())             flags |= SUBJECT_FLAG_DEAD;
        if (WorldData::IsSafeZone(user->GetX(), user->GetY())) flags |= SUBJECT_FLAG_SAFE;
        return flags;
    }

    void QueueUserSave(const UserSharedPtr& user, short saveX, short saveY)
    {
        if (user == nullptr)
            return;

        DB_USER_INFO info{};
        info.m_PlayerId = user->GetPlayerId();
        info.m_Name  = user->GetName();
        info.m_X     = saveX;
        info.m_Y     = saveY;
        info.m_Level = user->GetStat()->GetLevel();
        info.m_Exp   = user->GetStat()->GetExp();

        GDBThread->RequestSaveUser(user->GetObjID(), info);
    }
}

namespace CUserHelper
{
    void SendSUBJECT_MOVE_NFY(CSubject::SharedPtr sender, const ObjID& targetId)
    {
        const auto target = ::GetGameObject<CSubject>(targetId);
        if (target == nullptr)
            return;

        SendSUBJECT_MOVE_NFY(sender, target);
    }

    void SendSUBJECT_MOVE_NFY(CSubject::SharedPtr sender, CSubject::SharedPtr target)
    {
        if (target == nullptr)
            return;

        auto session = GetSession(sender);
        if (!session)
            return;

        SUBJECT_MOVE_NFY_PACKET packet;
        InitializePacket(packet, PacketType::SUBJECT_MOVE_NFY);
        packet.id = target->GetObjID();
        packet.x = target->GetX();
        packet.y = target->GetY();
        // 예전엔 항상 0이던 필드 — 이 걸음에 걸린 시간(ms, 0 = 순간이동). 봇은 읽지 않는다.
        packet.move_time = target->GetLastStepMs();

        session->PostSend(packet);
    }

    void SendSUBJECT_ADD_NFY(CSubject::SharedPtr sender, const ObjID& targetId)
    {
        const auto target = ::GetGameObject<CSubject>(targetId);
        if (target == nullptr)
            return;

        SendSUBJECT_ADD_NFY(sender, target);
    }

    void SendSUBJECT_ADD_NFY(CSubject::SharedPtr sender, CSubject::SharedPtr target)
    {
        if (target == nullptr)
            return;

        auto session = GetSession(sender);
        if (!session)
            return;

        SUBJECT_ADD_NFY_PACKET packet;
        InitializePacket(packet, PacketType::SUBJECT_ADD_NFY);
        const EnumCategory category = target->GetObjID().GetCategory<EnumCategory>();
        if (category == EnumCategory::eMonster)
        {
            const auto monster = static_pointer_cast<CMonster>(target);
            packet.monster_type = static_cast<char>(monster->GetType());
        }
        else if (category == EnumCategory::eUser)
        {
            // 지금 장착 중인 무기 — 새로 시야에 들어온 플레이어를 처음 그릴 때부터
            // 바로 보이게 한다. 이후 장착/탈착이 바뀌면 SUBJECT_EQUIP_CHANGE_NFY로 갱신.
            const auto user = static_pointer_cast<CUser>(target);
            packet.itemId = static_cast<UInt16>(user->GetInventory()->GetEquippedItemId());
        }
        else if (category == EnumCategory::eItem)
        {
            const auto item = static_pointer_cast<CItem>(target);
            packet.itemId = item->GetItemTableId();
        }
        packet.id = target->GetObjID();
        packet.x = target->GetX();
        packet.y = target->GetY();
        ::strncpy_s(packet.name, NAME_SIZE, target->GetName().c_str(), _TRUNCATE);

        session->PostSend(packet);

        // 레벨/성향치/강화 수치 같은 추가 정보를 바로 이어서 보낸다.
        // (몬스터는 종족이 ObjID KIND에 실려 있어 추가 정보가 필요 없다 — 다만 이미
        //  깎인 HP로 시야에 들어오면 HP 바를 맞추기 위해 보낸다.)
        if (category == EnumCategory::eUser)
        {
            SendSUBJECT_INFO_NFY(sender, target);
            SendPLAYER_LOOK_NFY(sender, static_pointer_cast<CUser>(target));
            if (sender->GetObjID().GetCategory<EnumCategory>() == EnumCategory::eUser)
                CGuildHelper::SendTag(static_pointer_cast<CUser>(sender), static_pointer_cast<CUser>(target));   // Phase 5 머리 위 결사
        }
        else if (category == EnumCategory::eItem)
        {
            // 강화됐거나 희귀도가 일반보다 높은 장비는 바닥에서도 빛나야 한다(빛기둥 색).
            const CEquipmentItem* equipment = AsEquipment(static_cast<CItem*>(target.get()));
            if (equipment != nullptr)
            {
                const ItemTableRow* row = ItemTableData::Find(equipment->GetItemTableId());
                const bool rare = row != nullptr && ItemRules::EffectiveRarity(*row, equipment->GetExtra()) != Rarity::Common;
                if (equipment->GetEnchant() > 0 || rare)
                    SendSUBJECT_INFO_NFY(sender, target);
            }
        }
        else if (category == EnumCategory::eMonster)
        {
            // 정예는 늘(표시 · 배율이 걸린 최대 HP — 클라는 종족에서 짐작한다), 아니면 깎인 HP로 들어올 때만
            if (static_pointer_cast<CMonster>(target)->IsElite() || target->GetStat()->GetHp() < target->GetStat()->GetMaxHp())
                SendSUBJECT_INFO_NFY(sender, target);
        }
    }

    void SendSUBJECT_EQUIP_CHANGE_NFY(CSubject::SharedPtr sender, const ObjID& targetId, UInt16 itemId)
    {
        auto session = GetSession(sender);
        if (!session)
            return;

        SUBJECT_EQUIP_CHANGE_NFY_PACKET packet;
        InitializePacket(packet, PacketType::SUBJECT_EQUIP_CHANGE_NFY);
        packet.id     = targetId;
        packet.itemId = itemId;

        session->PostSend(packet);
    }

    void BroadcastEquipChange(const UserSharedPtr& player)
    {
        if (player == nullptr)
            return;

        const UInt16 itemId = static_cast<UInt16>(player->GetInventory()->GetEquippedItemId());
        const ObjID playerId = player->GetObjID();

        GSector->ForEachNeighborObject(player->GetSectorX(), player->GetSectorY(), EnumCategory::eUser, [&](const SubjectSharedPtr& object)
        {
            if (object->GetObjID().GetCategory<EnumCategory>() != EnumCategory::eUser)
                return;

            auto viewer = static_pointer_cast<CUser>(object);
            auto session = viewer->GetGameSession();
            if (!session || session->m_eState != SOCKET_STATE::ST_INGAME)
                return;
            if (!CSubjectHelper::CanSee(object, player))
                return;

            SendSUBJECT_EQUIP_CHANGE_NFY(viewer, playerId, itemId);
            // 전 부위 외형(방어구·망토·희귀도 빛)도 함께
            SendPLAYER_LOOK_NFY(viewer, player);
        });

        // 장비가 바뀌면 능력치(공격력·방어·최대 HP·이동속도…)도 바뀐다 — 본인에게 새 값.
        CCombatHelper::SendStats(player);
        CCombatHelper::SendResource(player);
        CQuestHelper::OnEquipChanged(player);   // "N부위 착용" 목표
    }

    void SendPLAYER_LOOK_NFY(CSubject::SharedPtr viewer, const UserSharedPtr& player)
    {
        if (player == nullptr || !IsInGameViewer(viewer))
            return;
        auto session = GetSession(viewer);
        if (!session)
            return;

        PLAYER_LOOK_NFY_PACKET packet;
        InitializePacket(packet, PacketType::PLAYER_LOOK_NFY);
        packet.id = player->GetObjID();
        auto inventory = player->GetInventory();
        const auto& slots = inventory->GetSlots();
        // 전 부위를 한 번에(부위마다 FindEquipped를 부르던 것과 같은 답) — 시야에 들 때마다 보내는 패킷이다.
        const CInventory::EquippedParts parts = inventory->GetEquippedParts();
        static_assert(kLookSlots < static_cast<int>(EquipSlot::Count), "외형 칸은 장비 부위 안에 있어야 한다");
        for (int part = 1; part <= kLookSlots; ++part)
        {
            const UInt16 slotIndex = parts[part];
            if (slotIndex >= MAX_INVENTORY_SLOTS)
                continue;
            const auto& item = slots.at(slotIndex);
            const ItemTableRow* row = ItemTableData::Find(item->GetItemTableId());
            if (row == nullptr)
                continue;
            packet.items[part - 1] = item->GetItemTableId();
            packet.rarity[part - 1] = static_cast<UInt8>(ItemRules::EffectiveRarity(*row, inventory->GetSlotExtra(slotIndex)));
        }
        const UInt32 enchant = inventory->GetEquippedEnchant();
        packet.enchant = EnchantRule::LegacyLevel(enchant);
        session->PostSend(packet);
        SUBJECT_ENCHANT_LEVEL_INF_PACKET level{};
        InitializePacket(level, PacketType::SUBJECT_ENCHANT_LEVEL_INF);
        level.id = player->GetObjID(); level.level = enchant;
        session->PostSend(level);
    }

    void SendSUBJECT_REMOVE_NFY(CSubject::SharedPtr sender, const ObjID& targetId)
    {
        auto session = GetSession(sender);
        if (!session)
            return;

        SUBJECT_REMOVE_NFY_PACKET packet;
        InitializePacket(packet, PacketType::SUBJECT_REMOVE_NFY);
        packet.id = targetId;

        session->PostSend(packet);
    }

    void SendUSER_LOGIN_ACK(CSubject::SharedPtr sender)
    {
        auto session = GetSession(sender);
        if (!session)
            return;

        USER_LOGIN_ACK_PACKET packet;
        InitializePacket(packet, PacketType::USER_LOGIN_ACK);
        packet.id    = sender->GetObjID();
        packet.x     = sender->GetX();
        packet.y     = sender->GetY();
        packet.maxhp = sender->GetStat()->GetMaxHp();
        packet.hp    = sender->GetStat()->GetHp();
        packet.level = sender->GetStat()->GetLevel();
        packet.exp   = sender->GetStat()->GetExp();

        session->PostSend(packet);
    }

    void SendUSER_LOGIN_FAIL_ACK(CSubject::SharedPtr sender)
    {
        auto session = GetSession(sender);
        if (!session)
            return;

        USER_LOGIN_FAIL_ACK_PACKET packet;
        InitializePacket(packet, PacketType::USER_LOGIN_FAIL_ACK);

        session->PostSend(packet);
    }

    void SendSUBJECT_DIE_NFY(CSubject::SharedPtr sender, const ObjID& targetId)
    {
        const auto target = ::GetGameObject<CSubject>(targetId);
        SendSUBJECT_DIE_NFY(sender, target);
    }

    void SendSUBJECT_DIE_NFY(CSubject::SharedPtr sender, CSubject::SharedPtr target)
    {
        if (target == nullptr)
            return;

        auto session = GetSession(sender);
        if (!session)
            return;

        SUBJECT_DIE_NFY_PACKET packet;
        InitializePacket(packet, PacketType::SUBJECT_DIE_NFY);
        packet.id = target->GetObjID();
        packet.hp = target->GetStat()->GetHp();

        session->PostSend(packet);
    }

    void SendSUBJECT_RESPAWN_NFY(CSubject::SharedPtr sender, const ObjID& targetId)
    {
        const auto target = ::GetGameObject<CSubject>(targetId);
        SendSUBJECT_RESPAWN_NFY(sender, target);
    }

    void SendSUBJECT_RESPAWN_NFY(CSubject::SharedPtr sender, CSubject::SharedPtr target)
    {
        if (target == nullptr)
            return;

        auto session = GetSession(sender);
        if (!session)
            return;

        SUBJECT_RESPAWN_NFY_PACKET packet;
        InitializePacket(packet, PacketType::SUBJECT_RESPAWN_NFY);
        if (target->GetObjID().GetCategory<EnumCategory>() == EnumCategory::eMonster)
        {
            const auto monster = static_pointer_cast<CMonster>(target);
            packet.monster_type = static_cast<char>(monster->GetType());
        }
        packet.id = target->GetObjID();
        packet.x = target->GetX();
        packet.y = target->GetY();
        packet.hp = target->GetStat()->GetHp();
        ::strncpy_s(packet.name, NAME_SIZE, target->GetName().c_str(), _TRUNCATE);

        session->PostSend(packet);

        if (target->GetObjID().GetCategory<EnumCategory>() == EnumCategory::eUser)
        {
            SendSUBJECT_INFO_NFY(sender, target);
            if (sender->GetObjID().GetCategory<EnumCategory>() == EnumCategory::eUser)
                CGuildHelper::SendTag(static_pointer_cast<CUser>(sender), static_pointer_cast<CUser>(target));   // Phase 5
        }
    }

    void SendUSER_HEAL_INF(CSubject::SharedPtr sender)
    {
        auto session = GetSession(sender);
        if (!session)
            return;

        USER_HEAL_INF_PACKET packet;
        InitializePacket(packet, PacketType::USER_HEAL_INF);
        packet.hp = sender->GetStat()->GetHp();

        session->PostSend(packet);
    }

    void SendUSER_STAT_CHANGE_INF(CSubject::SharedPtr sender)
    {
        auto session = GetSession(sender);
        if (!session)
            return;

        USER_STAT_CHANGE_INF_PACKET packet;
        InitializePacket(packet, PacketType::USER_STAT_CHANGE_INF);
        packet.level = sender->GetStat()->GetLevel();
        packet.hp    = sender->GetStat()->GetHp();
        packet.maxhp = sender->GetStat()->GetMaxHp();
        packet.exp   = sender->GetStat()->GetExp();

        session->PostSend(packet);
    }

    void SendITEM_EQUIP_ACK(CSubject::SharedPtr sender, UInt16 slotIndex, bool success)
    {
        auto session = GetSession(sender);
        if (!session)
            return;

        auto user = static_pointer_cast<CUser>(sender);
        if (user == nullptr)
            return;

        ITEM_EQUIP_ACK_PACKET packet;
        InitializePacket(packet, PacketType::ITEM_EQUIP_ACK);
        packet.success = success ? 1 : 0;
        packet.slot.slotIndex = slotIndex;

        if (success)
        {
            const auto& slots = user->GetInventory()->GetSlots();
            const auto it = slots.find(slotIndex);
            // TryEquip이 true를 반환했다는 건 이 슬롯이 방금 장착된 장비라는 뜻이므로
            // 여기서 못 찾는 건 있을 수 없다 — 이 함수는 TryEquip과 같은 Zone 스레드
            // 위에서, 중간에 다른 mutate 없이 곧바로 호출되기 때문(단일 스레드 소유 모델).
            ASSERT_CRASH(it != slots.end());
            FillItemSlotData(packet.slot, slotIndex, it->second);
        }

        session->PostSend(packet);
    }

    void SendITEM_UNEQUIP_ACK(CSubject::SharedPtr sender, UInt16 slotIndex, bool success)
    {
        auto session = GetSession(sender);
        if (!session)
            return;

        auto user = static_pointer_cast<CUser>(sender);
        if (user == nullptr)
            return;

        ITEM_UNEQUIP_ACK_PACKET packet;
        InitializePacket(packet, PacketType::ITEM_UNEQUIP_ACK);
        packet.success = success ? 1 : 0;
        packet.slot.slotIndex = slotIndex;

        if (success)
        {
            const auto& slots = user->GetInventory()->GetSlots();
            const auto it = slots.find(slotIndex);
            ASSERT_CRASH(it != slots.end());  // SendITEM_EQUIP_ACK와 같은 이유로 항상 존재해야 한다
            FillItemSlotData(packet.slot, slotIndex, it->second);
        }

        session->PostSend(packet);
    }

    void SendITEM_SWAP_ACK(CSubject::SharedPtr sender, UInt16 slotIndexA, UInt16 slotIndexB, bool success)
    {
        auto session = GetSession(sender);
        if (!session)
            return;

        auto user = static_pointer_cast<CUser>(sender);
        if (user == nullptr)
            return;

        ITEM_SWAP_ACK_PACKET packet;
        InitializePacket(packet, PacketType::ITEM_SWAP_ACK);
        packet.success = success ? 1 : 0;
        packet.slotA.slotIndex = slotIndexA;
        packet.slotB.slotIndex = slotIndexB;

        if (success)
        {
            const auto& slots = user->GetInventory()->GetSlots();
            // 스왑 결과 어느 한쪽이 비게 될 수 있다(한쪽만 아이템이 있던 경우) — 그건
            // 정상 결과이므로 find 실패를 ASSERT_CRASH로 다루지 않고 nullptr로 넘겨
            // FillItemSlotData가 "빈 슬롯"(itemId=0, count=0)으로 채우게 한다.
            const auto itA = slots.find(slotIndexA);
            const auto itB = slots.find(slotIndexB);
            FillItemSlotData(packet.slotA, slotIndexA, itA != slots.end() ? itA->second : nullptr);
            FillItemSlotData(packet.slotB, slotIndexB, itB != slots.end() ? itB->second : nullptr);
        }

        session->PostSend(packet);
    }

    void SendSYSTEM_MESSAGE_INF(CSubject::SharedPtr sender, SystemMessageCode code, Int32 param1, Int32 param2)
    {
        auto session = GetSession(sender);
        if (!session)
            return;

        SYSTEM_MESSAGE_INF_PACKET packet;
        InitializePacket(packet, PacketType::SYSTEM_MESSAGE_INF);
        packet.code   = static_cast<UInt16>(code);
        packet.param1 = param1;
        packet.param2 = param2;

        session->PostSend(packet);
    }

    void SendITEM_DISCARD_ACK(CSubject::SharedPtr sender, UInt16 slotIndex, bool success)
    {
        auto session = GetSession(sender);
        if (!session)
            return;

        auto user = static_pointer_cast<CUser>(sender);
        if (user == nullptr)
            return;

        ITEM_DISCARD_ACK_PACKET packet;
        InitializePacket(packet, PacketType::ITEM_DISCARD_ACK);
        packet.success = success ? 1 : 0;
        packet.slot.slotIndex = slotIndex;

        if (success)
        {
            const auto& slots = user->GetInventory()->GetSlots();
            const auto it = slots.find(slotIndex);
            // it가 end()인 경우(슬롯 전량을 버려서 비워짐)도 FillItemSlotData가
            // nullptr을 받아 itemId=0, count=0으로 정상 처리한다.
            FillItemSlotData(packet.slot, slotIndex, it != slots.end() ? it->second : nullptr);
        }

        session->PostSend(packet);
    }

    void SendITEM_USE_ACK(CSubject::SharedPtr sender, UInt16 slotIndex, bool success)
    {
        auto session = GetSession(sender);
        if (!session)
            return;

        auto user = static_pointer_cast<CUser>(sender);
        if (user == nullptr)
            return;

        ITEM_USE_ACK_PACKET packet;
        InitializePacket(packet, PacketType::ITEM_USE_ACK);
        packet.success = success ? 1 : 0;
        packet.slot.slotIndex = slotIndex;

        if (success)
        {
            const auto& slots = user->GetInventory()->GetSlots();
            const auto it = slots.find(slotIndex);
            // it가 end()인 경우(마지막 1개를 소비해 슬롯이 비워짐)도 FillItemSlotData가
            // nullptr을 받아 itemId=0, count=0으로 정상 처리한다(ITEM_DISCARD_ACK와 같은 관례).
            FillItemSlotData(packet.slot, slotIndex, it != slots.end() ? it->second : nullptr);
        }

        session->PostSend(packet);
    }

    void SendITEM_PICKUP_ACK(CSubject::SharedPtr sender, bool success)
    {
        auto session = GetSession(sender);
        if (!session)
            return;

        ITEM_PICKUP_ACK_PACKET packet;
        InitializePacket(packet, PacketType::ITEM_PICKUP_ACK);
        packet.success = success ? 1 : 0;

        session->PostSend(packet);
    }

    void OnSessionClosed(const SessionSharedPtr& session)
    {
        if (session == nullptr)
            return;

        if (const auto player = session->MarkClosed())
        {
            // 객체 Zone은 로그인 때 세션을 게임 중으로 바꾸기 전에 등록된다 — 여기서 항상 가진 Zone으로 간다.
            const ObjID objId = player->GetObjID();
            GZoneManager->EnqueueByObject(objId, [objId]() { LeaveWorld(objId); });
            return;
        }

        // 로그인 전 — CUser가 없다. 늦게 온 로그인 결과는 ST_CLOSED를 보고 버린다. 잡고 있던(기다리던) 계정은 놓는다.
        if (const int playerId = session->GetPlayerId(); playerId > 0)
            GUserManager->Release(playerId, session.get());
        GZoneManager->Enqueue([session]()
        {
            session->CloseSession();
            GSessionManager->ReleaseSession(session.get());
        });
    }

    void LeaveWorld(ObjID clientId)
    {
        auto user = ::GetGameObject<CUser>(clientId);
        if (user == nullptr)
            return;

        // Zone을 옮기는 중이면 새 Zone 스레드로 — HandleEnterZone(같은 큐 앞쪽)이 끝난 뒤에 빼야 섹터 상태가 맞는다.
        if (user->IsTransferring())
        {
            const ZoneId targetZone = user->GetZoneId();
            if (CZoneLayout::IsValidZoneId(targetZone))
            {
                GZoneManager->EnqueueByZone(targetZone, [clientId]() { LeaveWorld(clientId); });
                return;
            }
        }

        // 가진 Zone 스레드에서만 섹터를 만진다(기본 스레드에는 GSector가 없다).
        const ZoneId ownerZone = GZoneManager->GetObjectZone(clientId);
        if (CZoneLayout::IsValidZoneId(ownerZone) && ownerZone != LCurrentZoneId)
        {
            GZoneManager->EnqueueByZone(ownerZone, [clientId]() { LeaveWorld(clientId); });
            return;
        }

        if (CTransferHelper::DeferLogout(user)) return;
        CTransferHelper::Cancel(user);

        if (GSector != nullptr && ownerZone == LCurrentZoneId)
        {
            GSector->ForEachNeighborObject(user->GetSectorX(), user->GetSectorY(), EnumCategory::eUser, [&](const SubjectSharedPtr& subject)
            {
                const auto subjectId = subject->GetObjID();
                if (subjectId == clientId || subjectId.GetCategory<EnumCategory>() != EnumCategory::eUser)
                    return;
                auto neighbor = static_pointer_cast<CUser>(subject);
                auto session = neighbor->GetGameSession();
                if (session == nullptr || session->m_eState != SOCKET_STATE::ST_INGAME)
                    return;
                if (!CSubjectHelper::CanSee(subject, user))
                    return;
                SendSUBJECT_REMOVE_NFY(neighbor, clientId);
            });
            GSector->RemoveObject(clientId, user->RefSectorX(), user->RefSectorY());
            (void)SaveUserInfo(clientId);
        }

        CGuildHelper::OnLeaveWorld(user);     // Phase 5: 결사원 접속 표시 · 마지막 접속 · 받은 초대 지우기
        CAuctionHelper::OnLeaveWorld(user);   // Phase 5: 알림 대상에서 뺀다
        CPartyHelper::OnLeaveWorld(user);     // 파티: 접속 끊김 표시(시트 offline_keep_sec 동안 파티원으로 남는다) · 받은 초대 · 낸 신청 거두기

        const auto session = user->GetGameSession();
        if (session != nullptr)
        {
            (void)session->MarkClosed();
            session->CloseSession();
            GSessionManager->ReleaseSession(session.get());
        }

        (void)GGameObjectManager->Delete(clientId);
        GZoneManager->RemoveObject(clientId);

        // 저장 요청을 다 넣은 뒤에 계정을 놓는다 — 같은 계정으로 기다리던 로그인은 이 저장 뒤에 읽는다(같은 DB 샤드).
        GUserManager->Release(user->GetPlayerId(), session.get());
    }

    void NotifyLoggedInElsewhere(const SessionSharedPtr& session)
    {
        if (session == nullptr || session->m_eState != SOCKET_STATE::ST_INGAME)
            return;
        SYSTEM_MESSAGE_INF_PACKET packet;
        InitializePacket(packet, PacketType::SYSTEM_MESSAGE_INF);
        packet.code = static_cast<UInt16>(SystemMessageCode::LoggedInElsewhere);
        session->PostSend(packet);
    }

    bool SaveUserInfo(const ObjID& targetId)
    {
        const auto target = ::GetGameObject<CUser>(targetId);
        if (target == nullptr)
            return false;

        // 걸을 수 없는 칸이어도 그대로 저장한다 — 다음 로그인(GetLoginZone)이 마을로 보낸다.
        const short saveX = target->GetX();
        const short saveY = target->GetY();
        QueueUserSave(target, saveX, saveY);
        CContentHelper::SaveAlignment(target, true);
        SaveProgress(target);   // 드롭 천장 카운터 등
        return true;
    }

    void GainExp(const UserSharedPtr& player, UInt32 amount)
    {
        if (player == nullptr)
            return;

        const UInt8 gained = player->GetStat()->AddExp(amount);
        if (gained > 0)
        {
            // 레벨업: 최대 HP/기본 공격력 재계산 + HP 가득(연출상 "레벨업 = 부활의 순간")
            player->RecomputeStats(true);
            CCombatHelper::SendStats(player);
            CCombatHelper::SendResource(player);
        }

        // USER_STAT_CHANGE_INF는 경험치가 바뀔 때마다 보낸다.
        SendUSER_STAT_CHANGE_INF(player);

        SendUSER_INFO_INF(player);
        if (gained > 0)
        {
            SkillGrow::SendTree(player);   // 기술 점수 +2(레벨마다)
            SendSystem(player, SystemMessageCode::LevelUp, player->GetStat()->GetLevel());
            BroadcastEffect(player, player->GetObjID(), EffectId::LevelUp, player->GetX(), player->GetY(), player->GetStat()->GetLevel());
        }

        // 주변 세션에는 레벨/HP가 바뀐 모습을 다시 알린다(레벨업 때만).
        if (gained > 0)
        {
            BroadcastInfo(player);
            CGuildHelper::OnLevelUp(player);   // Phase 5: 결사 구성원 목록의 레벨
            CAchieveHelper::OnLevel(player);   // 개인 업적(레벨)
        }
    }

    void AttackMonster(ObjID& monsterId, ObjID& playerId, int damage, UInt8 damageFlags,
        UInt16 skillId, UInt8 comboId, UInt8 flags2, UInt8 hitIndex)
    {
        auto monster = ::GetGameObject<CMonster>(monsterId);
        auto attacker = ::GetGameObject<CUser>(playerId);
        if (monster == nullptr || attacker == nullptr)
            return;
        if (monster->GetStat()->IsDead())
            return;

        const UInt16 clamped = static_cast<UInt16>(std::clamp(damage, 1, 65535));
        // Phase 2-E 허수아비: 맞기만 하고 죽지 않는다(HP를 다시 채움) · 반격 · 경험치 · 전리품 없음
        if (monster->IsDummy())
        {
            BroadcastDamage(monster, playerId, clamped, damageFlags, skillId, comboId, flags2, hitIndex);
            monster->GetStat()->SetHp(monster->GetStat()->GetMaxHp());
            return;
        }
        const UInt16 hpBefore = monster->GetStat()->GetHp();
        const UInt16 remaining = monster->GetStat()->TakeDamage(clamped);
        // 그로시 · 경험치 나눔 · 퀘스트 인정의 근거 — 실제로 깎은 HP만(넘친 막타는 남은 HP만큼)
        monster->m_DamageLedger.Add(playerId, static_cast<UInt32>(hpBefore > remaining ? hpBefore - remaining : 0));
        if (remaining > 0)
        {
            BroadcastDamage(monster, playerId, clamped, damageFlags, skillId, comboId, flags2, hitIndex);
            // 리니지처럼 맞으면 반격한다(선공 몬스터가 아니어도 때린 사람을 쫓는다) — 위협은 준 피해만큼
            CEntityBehaviorHelper::OnDamaged(monster, attacker, clamped);
            return;
        }

        // RemoveObject는 섹터 좌표만 지우고 월드 좌표(x,y)는 건드리지 않지만,
        // "죽은 자리"라는 의도를 코드에서 바로 알아보도록 미리 값으로 떼어둔다.
        const short deathX = monster->GetX();
        const short deathY = monster->GetY();

        BroadcastDamage(monster, playerId, clamped, static_cast<UInt8>(damageFlags | DAMAGE_KILLED), skillId, comboId, flags2, hitIndex);
        monster->Status().Clear();

        GSector->ForEachNeighborObject(monster->GetSectorX(), monster->GetSectorY(), EnumCategory::eUser, [&](const SubjectSharedPtr& object)
        {
            ObjID id = object->GetObjID();
            if (id.GetCategory<EnumCategory>() != EnumCategory::eUser) return;

            auto viewer = static_pointer_cast<CUser>(object);
            auto session = viewer->GetGameSession();
            if (!session || session->m_eState != SOCKET_STATE::ST_INGAME) return;

            if (CSubjectHelper::CanSee(object, monster))
                SendSUBJECT_DIE_NFY(viewer, monster);
        });

        // 행동을 먼저 정리한다(시전 · 장판 · 오라 — 섹터에서 빠지기 전에, 주인에 묶인 장판이 남지 않게)
        CEntityBehaviorHelper::OnDeath(monster);
        GSector->RemoveObject(monsterId, monster->RefSectorX(), monster->RefSectorY());
        monster->ClearPath();
        monster->ClearViewList(); // 다음 리스폰 시 oldList가 빈 상태로 시작하도록 초기화

        const MonsterTableRow* species = monster->GetSpecies();
        // 보상: 아이템은 막타(자동 획득) · 그로시 · 경험치는 피해를 준 모두에게 피해 비례 · 퀘스트는 피해를 준 모두(Content/Reward/RewardHelper)
        CRewardHelper::HandleKill(attacker, monster, deathX, deathY);
        CContentHelper::OnMonsterKilled(attacker, monster);

        const bool isBoss = species != nullptr && species->boss;
        // V2 퀘스트: 보스는 가까이 있던 모두(처치 · 수집은 HandleKill이 피해를 준 모두에게)
        if (isBoss)
            CQuestHelper::OnBossKilled(species->id, deathX, deathY);
        GTimerThread->ScheduleAfter(monsterId, isBoss ? std::chrono::seconds(WorldData::kBossRespawnSeconds) : std::chrono::seconds(10),
            TIMER_EVENT_TYPE::EV_MONSTER_RESPAWN);
    }

    void HandleHeal(const ObjID& playerId)
    {
        auto player = ::GetGameObject<CUser>(playerId);
        if (player == nullptr)
            return;

        auto session = player->GetGameSession();
        if (!session || session->m_eState != SOCKET_STATE::ST_INGAME)
            return;

        if (player->GetStat()->IsDead())
            return;

        // 능력치 기반 회복. 전투에서 벗어나 4초가 지나면 세 배(쉬면 빨리 찬다), 마을(안전지대)에서는 네 배.
        const bool inSafeZone = WorldData::IsSafeZone(player->GetX(), player->GetY());
        const UInt16 before = player->GetStat()->GetHp();
        const UInt32 now = GetNowTime();
        const bool resting = now - player->m_LastCombatAt > 4000;
        const UInt32 mul = inSafeZone ? 4u : (resting ? 3u : 1u);
        player->GetStat()->HealHp(static_cast<UInt16>(std::min<UInt32>(65535, player->m_Derived.hpRegen * mul)), player->GetStat()->GetMaxHp());
        const UInt16 mpBefore = player->m_Mp;
        player->m_Mp = static_cast<UInt16>(std::min<UInt32>(player->m_Derived.maxMp, player->m_Mp + player->m_Derived.mpRegen * mul));
        if (player->GetStat()->GetHp() != before || player->m_Mp != mpBefore)
            CCombatHelper::SendResource(player);
        GTimerThread->ScheduleAfter(playerId, 2500ms, TIMER_EVENT_TYPE::EV_HEAL);
    }

    void HandleRespawn(const ObjID& playerId, UInt32 deathSeq)
    {
        auto player = ::GetGameObject<CUser>(playerId);
        if (player == nullptr)
            return;
        // 그 사이에 이미 리스타트했거나(살아 있음), 다시 죽어서 새 타이머가 걸렸다면 무시한다.
        if (!player->GetStat()->IsDead() || player->m_DeathSeq != deathSeq)
            return;

        // 리니지처럼 마을에서 부활한다. 보통은 사망 창의 "마을에서 다시 시작" 버튼(USER_RESTART_REQ)이 먼저 오고,
        // 이 타이머는 그걸 누르지 않았을 때의 안전장치다.
        CContentHelper::RestartInTown(player);
    }

    void HandleLoginFail(const SessionSharedPtr& session)
    {
        if (session == nullptr)
            return;

        session->CancelLogin();
        if (const int playerId = session->GetPlayerId(); playerId > 0)
        {
            session->SetPlayerId(0);
            GUserManager->Release(playerId, session.get());
        }
        USER_LOGIN_FAIL_ACK_PACKET packet;
        InitializePacket(packet, PacketType::USER_LOGIN_FAIL_ACK);
        session->PostSend(packet);
    }

    ZoneId GetLoginZone(int savedX, int savedY)
    {
        // 저장 위치가 걸을 수 없는 칸(지형 데이터가 바뀜 · 범위 밖)이면 마을로 보낸다 — 그 Zone 스레드가 마을 광장에 세운다.
        if (IsValidWorldPosition(savedX, savedY))
            return CZoneLayout::GetZoneIdByWorld(savedX, savedY);
        return static_cast<ZoneId>(WorldData::kTownZoneId);
    }

    namespace
    {
        // 로그인 위치 — 반드시 지금 Zone 안에서 고른다. 다른 Zone 좌표면 섹터 등록이 실패해 CUser가 반쯤 등록된 채로 남는다.
        pair<short, short> PickLoginPosition(int savedX, int savedY, bool preferTownSpawn)
        {
            if (!preferTownSpawn && IsValidWorldPosition(savedX, savedY)
                && CZoneLayout::GetZoneIdByWorld(savedX, savedY) == LCurrentZoneId)
                return { static_cast<short>(savedX), static_cast<short>(savedY) };

            const auto& spawns = WorldData::TownSpawnPoints();
            if (LCurrentZoneId == static_cast<ZoneId>(WorldData::kTownZoneId) && !spawns.empty())
            {
                const auto& spawn = spawns[LRng() % spawns.size()];
                return { spawn.x, spawn.y };
            }
            return CSectorHelper::PickRandomPositionInZone(false);
        }

        // 로그인 완료 직전에 세션이 끊겼으면 만들던 CUser를 거둔다(세션 반납은 끊김 처리가 이미 했다).
        void AbortLogin(const UserSharedPtr& player)
        {
            ObjID objId = player->GetObjID();
            if (GSector != nullptr && player->GetZoneId() == LCurrentZoneId)
                GSector->RemoveObject(objId, player->RefSectorX(), player->RefSectorY());
            (void)GGameObjectManager->Delete(objId);
            GZoneManager->RemoveObject(objId);
        }

        // 위치를 이 Zone에 등록한 뒤(라우팅 · 객체 Zone이 정해진 뒤)에야 세션을 게임 중으로 바꾼다.
        // 순서가 반대면 그 사이 패킷 · 끊김 처리가 GSector가 없는 기본 스레드로 가서 크래시가 났다.
        bool EnterWorld(const SessionSharedPtr& session, const UserSharedPtr& player, short x, short y)
        {
            ObjID objId = player->GetObjID();
            CSectorHelper::UpdatePosition(objId, x, y);
            if (player->GetZoneId() != LCurrentZoneId)
            {
                AbortLogin(player);
                HandleLoginFail(session);
                return false;
            }
            if (!session->TryEnterGame(player))
            {
                AbortLogin(player);   // 그 사이 끊김 — 세션 · 계정은 끊김 처리가 이미 놓았다
                return false;
            }
            return true;
        }
    }

    void HandleGetUserInfo(const SessionSharedPtr& session, const DB_USER_INFO& userInfo, const vector<DB_ITEM_INFO>& items,
        const std::vector<UInt8>& progress, const std::vector<DB_SKILL_INFO>& skills, const std::vector<DB_ENCHANT_GRACE>& grace)
    {
        // 로그인 요청 중인 세션만 — 그 사이 끊겼으면(ST_CLOSED) 버린다(예전엔 유령 CUser가 월드에 남았다).
        if (session == nullptr || session->m_eState != SOCKET_STATE::ST_LOGIN)
            return;
        ASSERT_CRASH(GSector != nullptr);

        auto player = std::make_shared<CUser>();
        player->SetObjID(EnumCategory::eUser, static_cast<UInt64>(session->m_ObjectId));
        player->InitInstance();
        player->SetName(userInfo.m_Name);
        player->SetPlayerId(userInfo.m_PlayerId);
        player->GetStat()->SetLevel(std::clamp<UInt8>(userInfo.m_Level, 1, LevelTable::kMaxLevel));
        player->GetStat()->SetExp(userInfo.m_Exp);
        player->SetAlignment(userInfo.m_Alignment);
        player->m_SavedAlignment = userInfo.m_Alignment;
        player->SetPkCount(userInfo.m_PkCount);
        // CGameObjectManager에 공개되기 전(아직 이 스레드만 접근 가능한 시점)에 복원한다.
        player->GetInventory()->LoadFromDB(items);
        player->GetInventory()->LoadEnchantGrace(grace);
        player->GetInventory()->LoadAdena(userInfo.m_Adena);
        // 레벨에 맞는 최대 HP/공격력으로 맞추고 HP를 채운다(LoadFromDB가 장착 무기까지 복원한 뒤).
        player->RecomputeStats(true);

        if (!GGameObjectManager->Insert(player->GetObjID(), player))
        {
            HandleLoginFail(session);
            return;
        }
        player->SetGameSession(session);

        const auto [x, y] = PickLoginPosition(userInfo.m_X, userInfo.m_Y, false);
        if (!EnterWorld(session, player, x, y))
            return;
        if (x != userInfo.m_X || y != userInfo.m_Y)
        {
            QueueUserSave(player, x, y);
            cout << "Recovered invalid login position for [" << player->GetName()
                << "] from (" << userInfo.m_X << ", " << userInfo.m_Y << ") to (" << x << ", " << y << ")\n";
        }

        // V2: 직업·포인트 복원 후 능력치를 다시 계산.
        // 인벤토리를 만지므로 Zone 등록(EnterWorld) 뒤에 한다 — 소유 Zone 검사(ASSERT_OWNED_BY_CURRENT_ZONE).
        // V2 기록이 없는 기존 캐릭터는 이번 접속에서 고른 직업으로 시작한다(한 번만).
        if (!player->DeserializeProgress(progress))
        {
            player->m_eClassId = static_cast<ClassId>(ClassTable::IsValid(session->GetNewClassId()) ? session->GetNewClassId() : 0);
            if (player->m_eClassId != ClassId::Knight)
            {
                // 새 직업의 초보자 무기를 쥐여 준다(기사는 가진 검을 그대로 쓴다).
                std::vector<UInt16> touched;
                const ItemTableId weapon = ClassTable::Get(player->m_eClassId).starterWeapon;
                const ItemTableRow* row = ItemTable::Find(weapon);
                if (player->GetInventory()->TryAddItem(weapon, 1, &touched, 0, row != nullptr ? ItemRules::OnAcquire(*row, 0) : 0) && !touched.empty())
                    (void)player->GetInventory()->TryEquip(touched.front());
            }
            SaveProgress(player);
        }
        player->RecomputeStats(true);
        // Phase 2-D 기술 레벨 · 갈래(직업이 정해진 뒤 — 다른 직업의 기록은 버린다)
        (void)SkillGrow::Load(player, skills);

        ObjID objId = player->GetObjID();
        SendUSER_LOGIN_ACK(player);
        SendSERVER_CONFIG_INF(player);
        SendITEM_LIST_ACK(player);
        SendUSER_INFO_INF(player);
        SendADENA_INF(player, 0, AdenaReason::Init);
        CCombatHelper::SendStats(player);
        CCombatHelper::SendResource(player);
        CQuestHelper::SendList(player);
        SkillGrow::SendTree(player);
        CChatHelper::OnEnterWorld(player);   // Phase 3-B: 이름 색인(귓속말) · 최근 대화
        CGuildHelper::OnEnterWorld(player);  // Phase 5: 결사 정보 · 접속 알림 · 머리 위 표시
        CAuctionHelper::OnEnterWorld(player); // Phase 5: 정산함 수(메뉴 막대 점)
        CPartyHelper::OnEnterWorld(player);   // 파티: 구성(없으면 빈 창) · 다시 들어온 파티원이면 접속 알림
        CAchieveHelper::OnEnterWorld(player); // 개인 업적: 소급(레벨 · 최고 강화 · 지금 지역) → Summary
        GTimerThread->ScheduleAfter(objId, 5s, TIMER_EVENT_TYPE::EV_HEAL);
        CSectorHelper::NotifyPlayerEnteredWorld(objId, false);
    }

    void HandleAddUserInfo(const SessionSharedPtr& session, const DB_USER_INFO& userInfo)
    {
        if (session == nullptr || session->m_eState != SOCKET_STATE::ST_LOGIN)
            return;
        ASSERT_CRASH(GSector != nullptr);

        auto player = std::make_shared<CUser>();
        player->SetObjID(EnumCategory::eUser, static_cast<UInt64>(session->m_ObjectId));
        player->InitInstance();
        player->SetName(userInfo.m_Name);
        // 계정 INSERT는 이미 DB 스레드에서 동기적으로 끝난 뒤(playerId 확보 후)
        // 이 함수가 호출된다 — 여기서 다시 저장을 요청할 필요가 없다.
        player->SetPlayerId(userInfo.m_PlayerId);
        player->RecomputeStats(true);

        if (!GGameObjectManager->Insert(player->GetObjID(), player))
        {
            HandleLoginFail(session);
            return;
        }
        player->SetGameSession(session);

        // 신규 캐릭터는 마을 광장에서 시작한다(CDBThread가 마을 Zone 스레드로 보냈다 — 부하 시험이면 아무 Zone에 흩어 세운다).
        const auto [x, y] = PickLoginPosition(-1, -1, !GLoadTestMode);
        if (!EnterWorld(session, player, x, y))
            return;
        // 첫 위치를 바로 저장한다 — 첫 로그아웃 전에 서버가 죽어도 (0, 0)에서 시작하지 않게.
        QueueUserSave(player, x, y);

        ObjID objId = player->GetObjID();
        // 로그인 화면에서 고른 직업으로 시작한다.
        player->m_eClassId = static_cast<ClassId>(ClassTable::IsValid(session->GetNewClassId()) ? session->GetNewClassId() : 0);
        player->RecomputeStats(true);
        SaveProgress(player);

        SendUSER_LOGIN_ACK(player);
        SendSERVER_CONFIG_INF(player);
        SendITEM_LIST_ACK(player);
        // 리니지의 "초보자 지원" — 물약/두루마리/무기/그로시를 쥐여 준다.
        CContentHelper::GiveStarterKit(player);
        player->RecomputeStats(true);
        SendUSER_INFO_INF(player);
        CCombatHelper::SendStats(player);
        CCombatHelper::SendResource(player);
        CQuestHelper::SendList(player);
        SkillGrow::SendTree(player);
        CChatHelper::OnEnterWorld(player);   // Phase 3-B: 이름 색인(귓속말) · 최근 대화
        CGuildHelper::OnEnterWorld(player);  // Phase 5: 결사 정보 · 접속 알림 · 머리 위 표시
        CAuctionHelper::OnEnterWorld(player); // Phase 5: 정산함 수(메뉴 막대 점)
        CPartyHelper::OnEnterWorld(player);   // 파티: 구성(없으면 빈 창) · 다시 들어온 파티원이면 접속 알림
        CAchieveHelper::OnEnterWorld(player); // 개인 업적: 새 캐릭터 — 마을 지역 · Summary

        GTimerThread->ScheduleAfter(objId, 5s, TIMER_EVENT_TYPE::EV_HEAL);
        CSectorHelper::NotifyPlayerEnteredWorld(objId, false);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 리니지풍 컨텐츠 송신
    // ─────────────────────────────────────────────────────────────────────────
    void SendUSER_INFO_INF(const UserSharedPtr& user)
    {
        if (user == nullptr)
            return;
        auto session = user->GetGameSession();
        if (!session)
            return;

        const UInt32 now = GetNowTime();
        USER_INFO_INF_PACKET packet;
        InitializePacket(packet, PacketType::USER_INFO_INF);
        packet.level     = user->GetStat()->GetLevel();
        packet.exp       = user->GetStat()->GetExp();
        packet.expToNext = LevelTable::ExpToNext(packet.level);
        packet.hp        = user->GetStat()->GetHp();
        packet.maxhp     = user->GetStat()->GetMaxHp();
        packet.attack    = user->GetStat()->GetOffensive();
        packet.adena     = user->GetInventory()->GetAdena();
        packet.alignment = user->GetAlignment();
        packet.pkCount   = user->GetPkCount();
        packet.flags     = BuildSubjectFlags(user);
        packet.hasteRemainMs = user->IsHasted(now) ? (user->m_HasteUntil - now) : 0;

        session->PostSend(packet);
    }

    void SendSUBJECT_INFO_NFY(CSubject::SharedPtr viewer, CSubject::SharedPtr target)
    {
        if (target == nullptr || !IsInGameViewer(viewer))
            return;
        auto session = GetSession(viewer);
        if (!session)
            return;

        SUBJECT_INFO_NFY_PACKET packet;
        InitializePacket(packet, PacketType::SUBJECT_INFO_NFY);
        packet.id = target->GetObjID();

        const EnumCategory category = target->GetObjID().GetCategory<EnumCategory>();
        if (category == EnumCategory::eUser)
        {
            const auto user = static_pointer_cast<CUser>(target);
            packet.level     = user->GetStat()->GetLevel();
            packet.alignment = user->GetAlignment();
            packet.flags     = BuildSubjectFlags(user);
            packet.enchant = EnchantRule::LegacyLevel(user->GetInventory()->GetEquippedEnchant());
            packet.hp        = user->GetStat()->GetHp();
            packet.maxhp     = user->GetStat()->GetMaxHp();
            packet.classId   = static_cast<UInt8>(user->m_eClassId);
            // 장착 무기의 희귀도(무기 빛 색)
            const auto inventory = user->GetInventory();
            const UInt16 weaponSlot = inventory->FindEquipped(EquipSlot::Weapon);
            if (weaponSlot < MAX_INVENTORY_SLOTS)
            {
                if (const ItemTableRow* row = ItemTableData::Find(inventory->GetEquippedItemId()))
                    packet.weaponGrade = static_cast<UInt8>(ItemRules::EffectiveRarity(*row, inventory->GetSlotExtra(weaponSlot)));
            }
        }
        else if (category == EnumCategory::eMonster)
        {
            // CItem에는 CStat이 없으므로(Item.h 주석) 몬스터/유저일 때만 GetStat()을 부른다.
            packet.level = target->GetStat()->GetLevel();
            packet.hp    = target->GetStat()->GetHp();
            packet.maxhp = target->GetStat()->GetMaxHp();
            packet.flags = static_pointer_cast<CMonster>(target)->IsElite() ? SUBJECT_FLAG_ELITE : 0;
        }
        else if (category == EnumCategory::eItem)
        {
            if (const CEquipmentItem* equipment = AsEquipment(static_cast<CItem*>(target.get())))
            {
                packet.enchant = EnchantRule::LegacyLevel(equipment->GetEnchant());
                // 필드 장비의 희귀도(빛기둥 색). 옵션은 주워야 보인다.
                if (const ItemTableRow* row = ItemTableData::Find(equipment->GetItemTableId()))
                    packet.weaponGrade = static_cast<UInt8>(ItemRules::EffectiveRarity(*row, equipment->GetExtra()));
            }
        }

        session->PostSend(packet);
        SUBJECT_ENCHANT_LEVEL_INF_PACKET level{};
        InitializePacket(level, PacketType::SUBJECT_ENCHANT_LEVEL_INF);
        level.id = target->GetObjID();
        if (category == EnumCategory::eUser) level.level = static_pointer_cast<CUser>(target)->GetInventory()->GetEquippedEnchant();
        else if (category == EnumCategory::eItem)
            if (const CEquipmentItem* gear = AsEquipment(static_cast<CItem*>(target.get()))) level.level = gear->GetEnchant();
        session->PostSend(level);
    }

    void BroadcastInfo(const UserSharedPtr& player)
    {
        if (player == nullptr)
            return;

        SendSUBJECT_INFO_NFY(player, player);

        // 죽어 있으면 섹터에 없으므로(CSector에서 제거됨) 주변 뷰어도 없다.
        if (player->GetSectorX() < 0)
            return;

        GSector->ForEachNeighborObject(player->GetSectorX(), player->GetSectorY(), EnumCategory::eUser, [&](const SubjectSharedPtr& object)
        {
            if (object->GetObjID() == player->GetObjID())
                return;
            if (!IsInGameViewer(object) || !CSubjectHelper::CanSee(object, player))
                return;
            SendSUBJECT_INFO_NFY(object, player);
        });
    }

    void SendITEM_LIST_ACK(const UserSharedPtr& user)
    {
        if (user == nullptr)
            return;
        auto session = user->GetGameSession();
        if (!session)
            return;

        // 경제 1차: 가방 50칸은 한 패킷(255바이트)에 들어가지 않는다 — kItemListPageSlots칸씩 쪽으로 나눠(0~29 · 30~49) 보낸다.
        // 쪽마다 그 범위의 모든 칸을 칸 번호 순으로 담는다(빈 칸은 itemId 0 — 클라는 항목마다 그 칸을 덮어쓴다).
        for (int first = 0; first < MAX_INVENTORY_SLOTS; first += kItemListPageSlots)
        {
            ITEM_LIST_ACK_PACKET packet;
            InitializePacket(packet, PacketType::ITEM_LIST_ACK);
            UInt8 slotCount = 0;
            for (int slot = first; slot < MAX_INVENTORY_SLOTS && slotCount < kItemListPageSlots; ++slot)
                user->GetInventory()->FillSlotState(static_cast<UInt16>(slot), packet.items[slotCount++]);
            packet.slotCount = slotCount;
            session->PostSend(packet);
        }

        // V2: 희귀도·옵션이 있는 슬롯만 이어서 보낸다(클라는 스냅샷을 받을 때 전부 0으로 비운다).
        for (const auto& [slotIndex, item] : user->GetInventory()->GetSlots())
            if (user->GetInventory()->GetSlotExtra(slotIndex) != 0)
                SendITEM_DETAIL_INF(user, slotIndex);
        for (UInt16 i = 0; i < MAX_INVENTORY_SLOTS; ++i) SendITEM_ENCHANT_LEVEL_INF(user, i);
        for (const auto& [key, grace] : user->GetInventory()->GetEnchantGrace())
            SendENCHANT_GRACE_INF(user, grace.itemId, grace.currentLevel);
    }

    void SendITEM_DETAIL_INF(const UserSharedPtr& user, UInt16 slotIndex)
    {
        if (user == nullptr)
            return;
        auto session = user->GetGameSession();
        if (!session)
            return;

        ITEM_DETAIL_INF_PACKET packet;
        InitializePacket(packet, PacketType::ITEM_DETAIL_INF);
        packet.slotIndex = slotIndex;
        const auto& slots = user->GetInventory()->GetSlots();
        const auto it = slots.find(slotIndex);
        packet.itemId = it != slots.end() ? it->second->GetItemTableId() : 0;
        packet.extra = user->GetInventory()->GetSlotExtra(slotIndex);
        session->PostSend(packet);
    }

    void HandleItemSort(const UserSharedPtr& player)
    {
        if (player == nullptr || player->IsTransferring())
            return;
        const UInt32 now = GetNowTime();
        if (now - player->m_LastSortAt < 800)
            return;   // 도배 방지(정리는 수십 개의 슬롯 저장을 만든다)
        player->m_LastSortAt = now;
        (void)player->GetInventory()->TrySort();
    }

    void SendITEM_SLOT_INF(const UserSharedPtr& user, UInt16 slotIndex)
    {
        if (user == nullptr)
            return;
        auto session = user->GetGameSession();
        if (!session)
            return;

        ITEM_SLOT_INF_PACKET packet;
        InitializePacket(packet, PacketType::ITEM_SLOT_INF);
        user->GetInventory()->FillSlotState(slotIndex, packet.slot);

        session->PostSend(packet);
        SendITEM_ENCHANT_LEVEL_INF(user, slotIndex);
    }

    void SendITEM_ENCHANT_LEVEL_INF(const UserSharedPtr& user, UInt16 slotIndex)
    {
        if (!user) return;
        const auto session = user->GetGameSession();
        if (!session) return;
        ITEM_ENCHANT_LEVEL_INF_PACKET packet{};
        InitializePacket(packet, PacketType::ITEM_ENCHANT_LEVEL_INF);
        packet.slotIndex = slotIndex;
        packet.level = user->GetInventory()->GetSlotEnchant(slotIndex);
        session->PostSend(packet);
        const auto& slots = user->GetInventory()->GetSlots();
        if (const auto it = slots.find(slotIndex); it != slots.end())
            SendENCHANT_GRACE_INF(user, it->second->GetItemTableId(), packet.level);
    }

    void SendENCHANT_GRACE_INF(const UserSharedPtr& user, UInt16 itemId, UInt32 currentLevel)
    {
        if (!user || EnchantRule::GraceAttempts(currentLevel) == 0) return;
        const auto session = user->GetGameSession();
        if (!session) return;
        ENCHANT_GRACE_INF_PACKET packet{};
        InitializePacket(packet, PacketType::ENCHANT_GRACE_INF);
        packet.itemId = itemId;
        packet.currentLevel = currentLevel;
        packet.failures = user->GetInventory()->GetGraceFailures(itemId, currentLevel);
        packet.guaranteedAttempt = EnchantRule::GraceAttempts(currentLevel);
        packet.successTickets = EnchantRule::GraceSuccessTickets(currentLevel, packet.failures);
        session->PostSend(packet);
    }

    void SendADENA_INF(const UserSharedPtr& user, Int32 delta, AdenaReason reason)
    {
        if (user == nullptr)
            return;
        auto session = user->GetGameSession();
        if (!session)
            return;

        ADENA_INF_PACKET packet;
        InitializePacket(packet, PacketType::ADENA_INF);
        packet.adena  = user->GetInventory()->GetAdena();
        packet.delta  = delta;
        packet.reason = static_cast<UInt8>(reason);

        session->PostSend(packet);
    }

    void SendSHOP_RESULT_ACK(const UserSharedPtr& user, ShopKind kind, ShopResult result, UInt32 seq, UInt16 itemId, UInt16 count, Int32 adenaDelta)
    {
        if (user == nullptr)
            return;
        auto session = user->GetGameSession();
        if (!session)
            return;

        SHOP_RESULT_ACK_PACKET packet;
        InitializePacket(packet, PacketType::SHOP_RESULT_ACK);
        packet.kind       = static_cast<UInt8>(kind);
        packet.result     = static_cast<UInt8>(result);
        packet.seq        = seq;
        packet.itemId     = itemId;
        packet.count      = count;
        packet.adenaDelta = adenaDelta;

        session->PostSend(packet);
    }

    void SendSystem(const UserSharedPtr& user, SystemMessageCode code, Int32 param1, Int32 param2)
    {
        if (user == nullptr)
            return;
        SendSYSTEM_MESSAGE_INF(user, code, param1, param2);
    }

    void BroadcastDamage(const CSubject::SharedPtr& victim, const ObjID& attackerId, Int32 damage, UInt8 flags,
        UInt16 skillId, UInt8 comboId, UInt8 flags2, UInt8 hitIndex)
    {
        if (victim == nullptr || victim->GetSectorX() < 0)
            return;

        SUBJECT_DAMAGE_NFY_PACKET packet;
        InitializePacket(packet, PacketType::SUBJECT_DAMAGE_NFY);
        packet.victimId   = victim->GetObjID();
        packet.attackerId = attackerId;
        packet.hp         = victim->GetStat()->GetHp();
        packet.maxhp      = victim->GetStat()->GetMaxHp();
        packet.damage     = damage;
        packet.flags      = flags;
        packet.skillId    = skillId;
        packet.comboId    = comboId;
        packet.flags2     = flags2;
        packet.hitIndex   = hitIndex;

        // 시야(CanSee — 체비셰프 GViewRange) 안만 훑는다: 칸에 적어 둔 좌표로 걸러 범위 밖 플레이어는 약한 참조를 풀지 않는다
        GSector->ForEachObjectWithin(victim->GetSectorX(), victim->GetSectorY(), victim->GetX(), victim->GetY(), GViewRange, EnumCategory::eUser,
            [&](const SubjectSharedPtr& object)
        {
            if (!IsInGameViewer(object) || !CSubjectHelper::CanSee(object, victim))
                return;
            if (auto session = GetSession(object))
                session->PostSend(packet);
        });
    }

    void BroadcastEffect(const CSubject::SharedPtr& center, const ObjID& subjectId, EffectId effect, short x, short y, Int32 param)
    {
        if (center == nullptr)
            return;

        EFFECT_NFY_PACKET packet;
        InitializePacket(packet, PacketType::EFFECT_NFY);
        packet.subjectId = subjectId;
        packet.effectId  = static_cast<UInt16>(effect);
        packet.x         = x;
        packet.y         = y;
        packet.param     = param;

        // 섹터에서 빠져 있는(죽은/이동 중) 대상이면 자기 자신에게만 보낸다.
        if (center->GetSectorX() < 0)
        {
            if (IsInGameViewer(center))
                if (auto session = GetSession(center))
                    session->PostSend(packet);
            return;
        }

        const short sectorX = static_cast<short>(x / SECTOR_RANGE);
        const short sectorY = static_cast<short>(y / SECTOR_RANGE);
        const bool inThisZone = GSector->IsValidSector(sectorX, sectorY);
        // (x, y)에서 체비셰프 GViewRange 안만 훑는다 — 칸에 적어 둔 좌표로 걸러 범위 밖 플레이어는 약한 참조를 풀지 않는다
        GSector->ForEachObjectWithin(inThisZone ? sectorX : center->GetSectorX(), inThisZone ? sectorY : center->GetSectorY(), x, y, GViewRange, EnumCategory::eUser,
            [&](const SubjectSharedPtr& object)
        {
            if (!IsInGameViewer(object))
                return;
            if (abs(object->GetX() - x) > GViewRange || abs(object->GetY() - y) > GViewRange)
                return;
            if (auto session = GetSession(object))
                session->PostSend(packet);
        });
    }

    void BroadcastWorldNotice(WorldNoticeCode code, Int32 param1, Int32 param2, const std::string& name)
    {
        WORLD_NOTICE_INF_PACKET packet;
        InitializePacket(packet, PacketType::WORLD_NOTICE_INF);
        packet.code   = static_cast<UInt16>(code);
        packet.param1 = param1;
        packet.param2 = param2;
        ::strncpy_s(packet.name, NAME_SIZE, name.c_str(), _TRUNCATE);

        for (const auto& session : GSessionManager->SnapshotSessions())
        {
            if (session == nullptr || session->m_eState != SOCKET_STATE::ST_INGAME)
                continue;
            session->PostSend(packet);
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // V2 이동 — 순번 이동 + 토큰 버킷 검증(Data/MoveRule.h)
    // ─────────────────────────────────────────────────────────────────────────
    // 좌표를 받아 보낸다 — Zone을 넘긴 뒤에는 CUser 좌표를 읽지 않는다(새 Zone 스레드가 쓰는 중)
    static void SendMoveAck(const SessionSharedPtr& session, UInt16 seq, MoveResult result, UInt32 stepMs, short x, short y)
    {
        if (!session)
            return;
        USER_MOVE_ACK_PACKET packet;
        InitializePacket(packet, PacketType::USER_MOVE_ACK);
        packet.seq = seq;
        packet.x = x;
        packet.y = y;
        packet.result = static_cast<UInt8>(result);
        packet.stepMs = static_cast<UInt16>(stepMs > 65535u ? 65535u : stepMs);
        session->PostSend(packet);
    }

    void SendUSER_MOVE_ACK(const UserSharedPtr& user, UInt16 seq, MoveResult result, UInt32 stepMs)
    {
        if (user == nullptr)
            return;
        SendMoveAck(user->GetGameSession(), seq, result, stepMs, user->GetX(), user->GetY());
    }

    void SendSERVER_CONFIG_INF(const UserSharedPtr& user)
    {
        if (user == nullptr)
            return;
        auto session = user->GetGameSession();
        if (!session)
            return;
        SERVER_CONFIG_INF_PACKET packet;
        InitializePacket(packet, PacketType::SERVER_CONFIG_INF);
        packet.protocolVersion = PROTOCOL_VERSION;
        packet.viewRange = static_cast<UInt8>(GViewRange);
        packet.baseStepMs = static_cast<UInt16>(MoveRule::kBaseStepMs);
        packet.diagPermille = static_cast<UInt16>(MoveRule::kDiagPermille);
        packet.moveSpeedPercent = static_cast<UInt16>(user->GetMoveSpeedPercent(GetNowTime()));
        session->PostSend(packet);
    }

    void HandleMove(const UserSharedPtr& player, UInt8 dir, UInt16 seq)
    {
        if (player == nullptr)
            return;
        if (!MoveRule::IsValidDir(dir) || player->GetStat()->IsDead() || player->IsTransferring() || player->GetInventory()->IsTransferCommitting())
        {
            SendUSER_MOVE_ACK(player, seq, MoveResult::Busy, 0);
            return;
        }

        const UInt32 now = GetNowTime();
        const UInt32 cost = MoveRule::StepMs(dir, player->GetMoveSpeedPercent(now));

        // 토큰 버킷: 늦게 온 패킷이 잃은 시간은 kCatchupMs까지 되찾고, kSlackMs까지는
        // 일찍 와도 받아 준다. 그보다 빠르면(속도 핵/시계 조작) 거절하고 좌표를 되돌린다.
        const UInt32 earliest = now > MoveRule::kCatchupMs ? now - MoveRule::kCatchupMs : 0;
        const UInt32 start = (std::max)(player->m_MoveClock, earliest);
        if (start > now + MoveRule::kSlackMs)
        {
            SendUSER_MOVE_ACK(player, seq, MoveResult::TooFast, cost);
            return;
        }
        player->m_MoveClock = start + cost;

        short x = player->GetX();
        short y = player->GetY();
        CSubjectHelper::MovePositionByDirection(x, y, static_cast<char>(dir));
        GuardTownMove("step", player->GetX(), player->GetY(), x, y);   // Phase 4-A 층 규칙 덫(마을 밖이면 바로 통과)
        if (dir == 2 || dir == 4 || dir == 6) player->SetFacingLeft(true);
        else if (dir == 3 || dir == 5 || dir == 7) player->SetFacingLeft(false);

        player->SetLastStepMs(cost);
        if (CSectorHelper::CrossesZone(player, x, y))
        {
            // Zone을 넘는 걸음 — HandlePlayerMove가 CUser를 새 Zone 스레드에 넘긴다. 넘긴 뒤에는 이 스레드가 CUser를 만지지 않는다:
            // 캐스팅 끊기는 넘기기 전에, 응답 좌표는 여기서 계산한 값으로, 탐험 목표는 새 Zone에서 다음 걸음에.
            // (예전엔 넘긴 뒤 응답 · 끊기를 이어서 해 새 Zone 스레드의 입장 처리와 같은 CUser를 동시에 만졌다 — TSan)
            const SessionSharedPtr session = player->GetGameSession();
            CUserSkillHelper::OnMoved(player);
            CSectorHelper::HandlePlayerMove(player, x, y, false);
            SendMoveAck(session, seq, MoveResult::Ok, cost, x, y);
            return;
        }
        CSectorHelper::HandlePlayerMove(player, x, y, false);
        SendUSER_MOVE_ACK(player, seq, MoveResult::Ok, cost);
        CUserSkillHelper::OnMoved(player);   // Phase 2-B: 걸으면 캐스팅 · 차지 · (움직일 수 없는) 홀딩이 끊긴다
        // 탐험 목표(존을 넘어가는 걸음이면 새 Zone 스레드가 소유 — 다음 걸음에서 본다)
        if (!player->IsTransferring())
            CQuestHelper::OnMoved(player);
    }

    void SaveProgress(const UserSharedPtr& user)
    {
        if (user == nullptr || user->GetPlayerId() == 0)
            return;
        GDBThread->RequestSaveProgress(user->GetPlayerId(), user->SerializeProgress());
        user->m_Achieve.dirty = false;   // 업적 진척도 이 기록에 담겼다(60초 저장 예약은 할 일 없이 끝난다)
    }
}
