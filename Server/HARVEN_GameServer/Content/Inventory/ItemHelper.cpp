#include "pch.h"
#include "Content/Inventory/ItemHelper.h"

#include "Base/GameObjectManager.h"
#include "Content/Field/Sector.h"
#include "Content/Field/SectorHelper.h"
#include "Entity/Subject/SubjectHelper.h"
#include "Worker/TimerThread.h"
#include "User/User.h"
#include "User/UserHelper.h"
#include "Content/Field/ZoneManager.h"
#include "Database/GameData/Item/ItemTable.h"
#include "Database/GameData/ItemRules.h"
#include "Content/Inventory/Item/EquipmentItem.h"
#include "Database/GameData/PartyRules.h"
#include "Content/Reward/RewardHelper.h"
#include "Database/GameData/WorldData.h"
#include "Content/Field/ZoneLayout.h"
#include "Content/Navigator/Collision.h"

namespace
{
    // 필드 아이템 인스턴스에만 쓰이는 카운터. eItem 카테고리 안에서만 유일하면
    // 되므로(ObjID는 카테고리+이 값의 조합), CUser/CMonster의 ID 범위와 겹칠 걱정이
    // 없다 — 절대 재사용하지 않고 계속 증가만 하므로, "이미 지운 아이템의 ID를
    // 다른 아이템이 물려받아 헷갈리는" 일도 없다.
    std::atomic<UInt64> GNextFieldItemId{ 1 };

    // 줍기 우선권 시간(주인만 · 주인 + 파티)은 시트 PartyConst(loot_owner_ms · loot_party_ms). 전체 소멸 시간(30초)보다
    // 짧아야 주인이 안 줍고 자리를 뜨면 다른 사람도 주울 기회가 남는다(R115).

    // CGameObjectManager/CSector/CZoneManager 등록을 지우고 주변에 사라졌다고
    // 알린다 - 습득 성공 시와 자동 소멸 시 둘 다에서 쓰는 공통 정리 로직이다.
    void RemoveFieldItem(const CItem::SharedPtr& item)
    {
        ObjID itemId = item->GetObjID();

        GSector->ForEachNeighborObject(item->GetSectorX(), item->GetSectorY(), EnumCategory::eUser, [&](const SubjectSharedPtr& viewer)
        {
            if (viewer->GetObjID().GetCategory<EnumCategory>() != EnumCategory::eUser)
                return;

            auto user = static_pointer_cast<CUser>(viewer);
            auto session = user->GetGameSession();
            if (!session || session->m_eState != SOCKET_STATE::ST_INGAME)
                return;
            if (!CSubjectHelper::CanSee(viewer, item))
                return;

            CUserHelper::SendSUBJECT_REMOVE_NFY(user, itemId);
        });

        GSector->RemoveObject(itemId, item->RefSectorX(), item->RefSectorY());
        GZoneManager->RemoveObject(item->GetObjID());
        GGameObjectManager->Delete(item->GetObjID());
    }
}

namespace CItemHelper
{
    // 몬스터 처치 드롭을 놓을 칸 — 죽은 칸부터 시작해 주변으로 퍼뜨린다(한 칸에 여러 개가
    // 겹치면 줍기 대상이 가려지므로). 같은 Zone, 막히지 않은 칸, 안전지대 밖만 쓴다.
    bool PickDropTile(short originX, short originY, int index, short& outX, short& outY)
    {
        static constexpr int kOffsets[][2] = {
            { 0, 0 }, { 1, 0 }, { 0, 1 }, { -1, 0 }, { 0, -1 }, { 1, 1 }, { -1, 1 }, { 1, -1 }, { -1, -1 },
            { 2, 0 }, { 0, 2 }, { -2, 0 }, { 0, -2 }, { 2, 1 }, { 1, 2 }, { -2, -1 }, { -1, -2 },
        };
        const ZoneId originZone = CZoneLayout::GetZoneIdByWorld(originX, originY);
        int found = 0;
        for (const auto& off : kOffsets)
        {
            const int x = originX + off[0];
            const int y = originY + off[1];
            if (!CZoneLayout::IsValidWorldPosition(x, y) || isCollision(static_cast<short>(x), static_cast<short>(y)))
                continue;
            if (CZoneLayout::GetZoneIdByWorld(x, y) != originZone || WorldData::IsSafeZone(x, y))
                continue;
            if (found++ == index)
            {
                outX = static_cast<short>(x);
                outY = static_cast<short>(y);
                return true;
            }
        }
        return false;
    }

    // 귀속 아이템(표 bind 또는 인스턴스 bit 8)인가 — 가방이 차서 땅에 넘친 보스 · 전설 장비, 장비 파편
    bool IsBoundItem(const CItem& item) noexcept
    {
        const ItemTableRow* row = ItemTable::Find(item.GetItemTableId());
        if (row == nullptr)
            return false;
        const UInt64 extra = item.IsEquipment() ? static_cast<const CEquipmentItem&>(item).GetExtra() : 0;
        return ItemRules::IsBound(*row, extra);
    }

    bool CanLoot(const CItem& item, const CUser& player, UInt32 now) noexcept
    {
        const ObjID& owner = item.GetLootPriorityOwner();
        if (owner == ObjID::npos || owner == player.GetObjID())
            return true;                                   // 우선권 없음 · 내 것
        // 주인이 다시 접속했다(ObjID는 접속마다 새로 받는다) — 계정으로 알아본다. 예전엔 귀속 넘침을 주인도 못 주워 2분 뒤 사라졌다
        if (const Int32 ownerPid = item.GetLootOwnerPlayerId(); ownerPid > 0 && ownerPid == player.GetPlayerId())
            return true;
        if (IsBoundItem(item))
            return false;                                  // 귀속 — 주인 말고는 끝까지 못 줍는다(파티 · 누구나 단계 없음)
        if (static_cast<Int32>(now - item.GetLootOwnerUntil()) < 0)
            return false;                                  // 1단계: 주인만
        if (static_cast<Int32>(now - item.GetLootPartyUntil()) < 0)
        {
            const UInt32 party = item.GetLootPartyId();    // 2단계: 주인의 파티원(떨어질 때의 파티)
            return party != 0 && player.m_PartyId.load(std::memory_order_relaxed) == party;
        }
        return true;                                       // 3단계: 누구나
    }

    void SpawnFieldItem(const CItem::SharedPtr& item, short x, short y, const ObjID& lootPriorityOwner, UInt32 lootPartyId, Int32 lootOwnerPlayerId)
    {
        ASSERT_CRASH(item != nullptr);
        ASSERT_CRASH(item->IsOnGround());  // 호출 전에 CInventory에서 이미 빠져나와 있어야 한다

        const UInt64 instanceId = GNextFieldItemId.fetch_add(1, std::memory_order_relaxed);
        item->SetObjID(EnumCategory::eItem, instanceId);
        if (lootPriorityOwner != ObjID::npos)
        {
            // 파티가 없으면 2단계가 없다 — 주인 시간이 끝나면 바로 누구나(예전 처치자 우선권 10초와 같다)
            const UInt32 now = GetNowTime();
            const UInt32 ownerUntil = now + PartyRule::LootOwnerMs();
            const UInt32 partyUntil = lootPartyId != 0 ? now + PartyRule::LootPartyMs() : ownerUntil;
            item->SetLootPriority(lootPriorityOwner, lootPartyId, ownerUntil, partyUntil, lootOwnerPlayerId);
        }
        else
            item->SetLootPriority(ObjID::npos, 0, 0, 0);

        ASSERT_CRASH(GGameObjectManager->Insert(item->GetObjID(), item));

        ObjID itemId = item->GetObjID();
        CSectorHelper::UpdatePosition(itemId, x, y);

        GSector->ForEachNeighborObject(item->GetSectorX(), item->GetSectorY(), EnumCategory::eUser, [&](const SubjectSharedPtr& viewer)
        {
            if (viewer->GetObjID().GetCategory<EnumCategory>() != EnumCategory::eUser)
                return;

            auto user = static_pointer_cast<CUser>(viewer);
            auto session = user->GetGameSession();
            if (!session || session->m_eState != SOCKET_STATE::ST_INGAME)
                return;
            if (!CSubjectHelper::CanSee(viewer, item))
                return;

            CUserHelper::SendSUBJECT_ADD_NFY(user, item);
        });

        // 귀속 아이템은 주인만 주울 수 있으니 가방을 비울 시간을 더 준다(2분) — 그 밖은 30초
        const bool boundOverflow = lootPriorityOwner != ObjID::npos && IsBoundItem(*item);
        GTimerThread->ScheduleAfter(item->GetObjID(), boundOverflow ? std::chrono::seconds(120) : std::chrono::seconds(30),
            TIMER_EVENT_TYPE::EV_ITEM_DESPAWN);
    }

    void TryPickupAt(const UserSharedPtr& player)
    {
        if (player == nullptr)
            return;

        CItem::SharedPtr target;
        const UInt32 now = GetNowTime();
        GSector->ForEachNeighborObject(player->GetSectorX(), player->GetSectorY(), EnumCategory::eItem, [&](const SubjectSharedPtr& object)
        {
            if (target != nullptr)
                return;  // 이미 하나 찾음 - 한 번의 습득 요청은 아이템 하나만 대상으로 한다
            if (object->GetObjID().GetCategory<EnumCategory>() != EnumCategory::eItem)
                return;
            if (object->GetX() != player->GetX() || object->GetY() != player->GetY())
                return;

            auto candidate = static_pointer_cast<CItem>(object);

            // 아직 줍기 우선권이 걸려 있고 내가 주울 수 있는 단계가 아니면(주인만 · 주인의 파티만) "여기 아무 것도
            // 없다"와 똑같이 취급한다 — 남의 우선권인지 원래 빈 칸인지는 클라이언트가
            // 구분할 필요가 없다고 보고 단순하게 갔다. (같은 칸에 주울 수 있는 다른
            // 아이템이 더 있다면 계속 찾도록 여기서 return만 하고 target은 안 채운다.)
            if (!CanLoot(*candidate, *player, now))
                return;

            target = std::move(candidate);
        });

        if (target == nullptr)
        {
            CUserHelper::SendITEM_PICKUP_ACK(player, false);
            return;
        }

        // 그로시는 슬롯이 아니라 지갑으로 간다. 지갑은 가득 찰 일이 사실상 없으므로
        // (상한 20억) 실패 경로 없이 필드에서 지우고 끝낸다.
        if (const ItemTableRow* row = ItemTable::Find(target->GetItemTableId()); row != nullptr && row->type == ItemType::eCurrency)
        {
            player->GetInventory()->AddAdena(target->GetCount(), AdenaReason::Pickup);
            RemoveFieldItem(target);
            CUserHelper::SendITEM_PICKUP_ACK(player, true);
            return;
        }

        // 여기서 target은 아직 필드에 그대로 남아있는 상태다 - TryAddItem이
        // 실패해도(인벤토리가 꽉 참) 손댄 게 없으니 되돌릴 필요조차 없다.
        // 성공했을 때만, 바로 아래에서 필드에서 지운다. 이 함수 전체가 한
        // Zone 스레드 위에서 끊김 없이 실행되므로, "찾았는데 다른 요청이 먼저
        // 가져갔다" 같은 중간 상태가 존재하지 않는다.
        // 강화된 무기를 버렸다가 다시 주우면 강화 수치가 그대로 따라온다.
        const CEquipmentItem* equipment = AsEquipment(target);
        const UInt32 enchant = equipment != nullptr ? equipment->GetEnchant() : 0;
        // V2 희귀도·옵션도 그대로 따라온다(버렸다 주워도 같은 물건)
        const UInt64 extra = equipment != nullptr ? equipment->GetExtra() : 0;
        std::vector<UInt16> touchedSlots;
        if (!player->GetInventory()->TryAddItem(target->GetItemTableId(), target->GetCount(), &touchedSlots, enchant, extra))
        {
            CUserHelper::SendITEM_PICKUP_ACK(player, false);
            CUserHelper::SendSYSTEM_MESSAGE_INF(player, SystemMessageCode::InventoryFull,
                static_cast<Int32>(target->GetItemTableId()), static_cast<Int32>(target->GetCount()));
            return;
        }

        RemoveFieldItem(target);

        CUserHelper::SendITEM_PICKUP_ACK(player, true);

        // 몬스터 전리품(가방이 차 떨어뜨린 것 — 줍기 우선권이 걸려 있던 물건)이면 희귀도 알림(시트 Rarity.loot_notice) · 신화 월드 공지.
        // 플레이어가 버린 물건(우선권 없음)은 알리지 않는다 — 신화를 버렸다 주워 공지를 되풀이하던 구멍을 막는다.
        if (target->GetLootPriorityOwner() != ObjID::npos)
            CRewardHelper::AnnounceLoot(player, target->GetItemTableId(), extra);
    }

    void HandleDespawn(const ObjID& itemId)
    {
        auto item = ::GetGameObject<CItem>(itemId);
        if (item == nullptr)
            return;  // 이미 습득되었음 - 정상 경로, 아무 것도 하지 않는다

        RemoveFieldItem(item);
    }
}
