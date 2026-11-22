#include "pch.h"
#include "Content/Inventory/Inventory.h"

#include <algorithm>
#include <vector>

#include "Content/Inventory/Item/ConsumableItem.h"
#include "Content/Inventory/Item/EquipmentItem.h"
#include "Database/GameData/Item/ItemTable.h"
#include "Database/GameData/Item/ItemTableData.h"
#include "Database/DBThread.h"
#include "User/User.h"
#include "User/UserHelper.h"
#include "Content/Field/ZoneManager.h"
#include "Database/GameData/LevelTable.h"
#include "Database/GameData/ItemRules.h"
#include "Database/GameData/GuildAuction.h"
#include "Content/Economy/EconomyLog.h"
#include "Content/Achieve/AchieveHelper.h"

void CInventory::Attach(const UserSharedPtr& owner) noexcept
{
    m_wpOwner = owner;
}

bool CInventory::BeginTransfer() noexcept
{
    AssertOwnedByCurrentZone();
    if (IsBusy()) return false;
    m_PendingTx = PendingTx::Transfer;
    return true;
}

void CInventory::SetTransferSlots(const std::vector<UInt16>& slots) noexcept
{
    AssertOwnedByCurrentZone();
    ASSERT_CRASH(m_PendingTx == PendingTx::Transfer && !m_TransferCommitting);
    m_LockedSlots.reset();
    for (auto slot : slots) { ASSERT_CRASH(slot < MAX_INVENTORY_SLOTS); m_LockedSlots.set(slot); }
}

void CInventory::FreezeTransfer(UInt32 adenaAfter) noexcept
{
    AssertOwnedByCurrentZone();
    ASSERT_CRASH(m_PendingTx == PendingTx::Transfer);
    m_TransferCommitting = true;
    m_LockedSlots.set();
    m_TransferWalletCeiling = std::max(m_Adena, adenaAfter);
}

void CInventory::FinishTransfer(const DB_ITEM_BATCH_SAVE* committed) noexcept
{
    AssertOwnedByCurrentZone();
    ASSERT_CRASH(m_PendingTx == PendingTx::Transfer);
    const auto owner = m_wpOwner.lock();
    const UInt32 oldAdena = m_Adena;
    if (committed)
    {
        for (const auto& a : committed->after)
        {
            if (!a.hasItem) { m_Slots.erase(a.slotIndex); continue; }
            auto item = MakeNewItem(a.itemId, a.count);
            ASSERT_CRASH(item != nullptr); // 핫 리로드는 기존 아이템 ID 제거를 허용하지 않는다.
            item->SetOwnerID(owner->GetObjID());
            if (auto* gear = AsEquipment(item))
            { gear->SetEnchant(a.enchant); gear->SetExtra(a.extra); if (a.equipped) gear->Equip(); }
            m_Slots[a.slotIndex] = std::move(item);
        }
        m_Adena = committed->adenaAfter;
        RefreshEquipped();
    }
    m_TransferCommitting = false; UnlockSlots();
    if (committed)
    {
        for (const auto& a : committed->after) NotifySlotChanged(a.slotIndex);
        const auto delta = static_cast<Int32>(Int64(m_Adena) - oldAdena);
        if (delta) { CEconomyLog::OnAdena(AdenaReason::DirectTrade, delta); CUserHelper::SendADENA_INF(owner, delta, AdenaReason::DirectTrade); }
    }
    auto credits = std::move(m_DeferredCredits); m_DeferredCredits.clear(); m_DeferredAdena = 0;
    for (const auto& [amount, reason] : credits) (void)AddAdena(amount, reason);
}

void CInventory::AssertOwnedByCurrentZone() const noexcept
{
    auto owner = m_wpOwner.lock();
    // 소유자가 이미 사라졌다면(CUser 소멸 이후에도 이 CInventory를 들고 있었다는 뜻)
    // 그 자체로 호출자 쪽 수명 관리가 잘못된 것이므로, Release에서도 항상 크래시한다.
    ASSERT_CRASH(owner != nullptr);
    ASSERT_OWNED_BY_CURRENT_ZONE(owner->GetObjID());
}

void CInventory::SaveSlotToDB(UInt16 slotIndex) const
{
    auto owner = m_wpOwner.lock();
    ASSERT_CRASH(owner != nullptr);

    const auto it = m_Slots.find(slotIndex);
    if (it == m_Slots.end())
    {
        GDBThread->RequestDeleteItem(owner->GetPlayerId(), slotIndex);
        NotifySlotChanged(slotIndex);
        return;
    }

    const CItem::SharedPtr& item = it->second;
    const CEquipmentItem* equipment = AsEquipment(item);
    const bool equipped = (equipment != nullptr) && equipment->IsEquipped();
    const UInt32 enchant = (equipment != nullptr) ? equipment->GetEnchant() : 0;
    const UInt64 extra = (equipment != nullptr) ? equipment->GetExtra() : 0;

    GDBThread->RequestSaveItem(owner->GetPlayerId(), slotIndex, item->GetItemTableId(), item->GetCount(), equipped, enchant, extra);
    NotifySlotChanged(slotIndex);
}

DB_ITEM_SLOT_SAVE CInventory::DescribeSlot(UInt16 slotIndex) const
{
    DB_ITEM_SLOT_SAVE save{};
    save.slotIndex = slotIndex;

    const auto it = m_Slots.find(slotIndex);
    if (it == m_Slots.end())
    {
        save.hasItem = false;
        return save;
    }

    const CItem::SharedPtr& item = it->second;
    const CEquipmentItem* equipment = AsEquipment(item);

    save.hasItem  = true;
    save.itemId   = item->GetItemTableId();
    save.count    = item->GetCount();
    save.equipped = (equipment != nullptr) && equipment->IsEquipped();
    save.enchant  = (equipment != nullptr) ? equipment->GetEnchant() : 0;
    save.extra    = (equipment != nullptr) ? equipment->GetExtra() : 0;
    return save;
}

void CInventory::SaveTwoSlotsToDB(UInt16 slotIndexA, UInt16 slotIndexB) const
{
    auto owner = m_wpOwner.lock();
    ASSERT_CRASH(owner != nullptr);

    GDBThread->RequestSaveTwoItems(owner->GetPlayerId(), DescribeSlot(slotIndexA), DescribeSlot(slotIndexB));
    NotifySlotChanged(slotIndexA);
    NotifySlotChanged(slotIndexB);
}

void CInventory::NotifySlotChanged(UInt16 slotIndex) const
{
    auto owner = m_wpOwner.lock();
    if (owner == nullptr)
        return;
    CUserHelper::SendITEM_SLOT_INF(owner, slotIndex);
    // V2: 희귀도·옵션은 바로 뒤에 따로 보낸다(슬롯 포맷 ITEM_SLOT_STATE_DATA의 크기는 그대로 둔다).
    CUserHelper::SendITEM_DETAIL_INF(owner, slotIndex);
}

UInt64 CInventory::GetSlotExtra(UInt16 slotIndex) const noexcept
{
    const auto it = m_Slots.find(slotIndex);
    if (it == m_Slots.end())
        return 0;
    const CEquipmentItem* equipment = AsEquipment(it->second);
    return equipment != nullptr ? equipment->GetExtra() : 0;
}

void CInventory::RefreshEquipped() noexcept
{
    m_EquippedCount = 0;
    for (const auto& [slotIndex, item] : m_Slots)
    {
        const CEquipmentItem* equipment = AsEquipment(item);
        if (equipment == nullptr || !equipment->IsEquipped() || m_EquippedCount >= m_Equipped.size())
            continue;
        m_Equipped[m_EquippedCount++] = EquippedRef{ slotIndex, item->GetItemTableId() };
    }
}

void CInventory::VerifyEquipped() const noexcept
{
#ifdef _DEBUG
    // 칸 전체를 훑은 결과(예전 FindEquipped · CollectGearBonus가 보던 것)와 목록이 순서까지 같아야 한다 —
    // 다르면 칸이나 착용 여부를 바꾸고 RefreshEquipped()를 빠뜨린 경로가 있다는 뜻이다.
    UInt8 n = 0;
    for (const auto& [slotIndex, item] : m_Slots)
    {
        const CEquipmentItem* equipment = AsEquipment(item);
        if (equipment == nullptr || !equipment->IsEquipped())
            continue;
        ASSERT_CRASH(n < m_EquippedCount);
        ASSERT_CRASH(m_Equipped[n].slotIndex == slotIndex && m_Equipped[n].itemId == item->GetItemTableId());
        ++n;
    }
    ASSERT_CRASH(n == m_EquippedCount);
#endif
}

UInt16 CInventory::FindEquipped(EquipSlot slot) const noexcept
{
    VerifyEquipped();
    for (UInt8 i = 0; i < m_EquippedCount; ++i)
    {
        if (ItemTableData::SlotOf(m_Equipped[i].itemId) == slot)
            return m_Equipped[i].slotIndex;
    }
    return MAX_INVENTORY_SLOTS;
}

CInventory::EquippedParts CInventory::GetEquippedParts() const noexcept
{
    VerifyEquipped();
    EquippedParts parts;
    parts.fill(MAX_INVENTORY_SLOTS);
    // 같은 부위에 둘이 걸려 있으면(DB가 깨진 경우) 앞엣것 — FindEquipped와 같은 답이다.
    for (UInt8 i = 0; i < m_EquippedCount; ++i)
    {
        const auto part = static_cast<size_t>(ItemTableData::SlotOf(m_Equipped[i].itemId));
        if (part < parts.size() && parts[part] == MAX_INVENTORY_SLOTS)
            parts[part] = m_Equipped[i].slotIndex;
    }
    return parts;
}

void CInventory::FillSlotState(UInt16 slotIndex, ITEM_SLOT_STATE_DATA& out) const noexcept
{
    out = {};
    out.slotIndex = slotIndex;
    const auto it = m_Slots.find(slotIndex);
    if (it == m_Slots.end())
        return;

    const CItem::SharedPtr& item = it->second;
    const CEquipmentItem* equipment = AsEquipment(item);
    out.itemId   = item->GetItemTableId();
    out.count    = item->GetCount();
    out.equipped = (equipment != nullptr && equipment->IsEquipped()) ? 1 : 0;
    out.enchant = EnchantRule::LegacyLevel((equipment != nullptr) ? equipment->GetEnchant() : 0);
}

void CInventory::CollectGearBonus(GearBonus& out, GearExtras* outExtras) const
{
    // 착용 중인 장비 전부(무기 · 방어구 · 장신구) → 공용 규칙(Data/ItemRules.h)으로 합산한다.
    // 클라이언트의 비교 툴팁도 같은 함수를 부르므로 "보이는 숫자 = 서버 숫자"가 된다.
    // 착용 칸 목록은 칸 순회 순서 그대로라 합산 순서도 예전(칸 전체 훑기)과 같다.
    // 부위마다 하나만 센다 — 외형(GetEquippedParts) · FindEquipped와 같은 칸(목록의 앞엣것). 2026-09-02 전 서버는 무기를 여러 개
    // 장착할 수 있어 옛 DB에 같은 부위가 둘 이상 장착된 채 남아 있을 수 있다 — 예전엔 그 공격력 · 세트 수를 모두 더했다.
    VerifyEquipped();
    EquippedPiece pieces[static_cast<size_t>(EquipSlot::Count)];
    std::bitset<static_cast<size_t>(EquipSlot::Count)> partTaken;
    int count = 0;
    for (UInt8 i = 0; i < m_EquippedCount; ++i)
    {
        const CEquipmentItem* equipment = AsEquipment(m_Slots.at(m_Equipped[i].slotIndex));
        const ItemTableRow* row = ItemTable::Find(m_Equipped[i].itemId);
        if (equipment == nullptr || row == nullptr || count >= static_cast<int>(EquipSlot::Count))
            continue;
        const auto part = static_cast<size_t>(ItemTableData::SlotOf(*row));
        if (part >= partTaken.size() || partTaken.test(part))
            continue;
        partTaken.set(part);
        pieces[count++] = EquippedPiece{ row, equipment->GetExtra(), equipment->GetEnchant() };
    }
    GearExtras extras{};
    ItemRules::Accumulate(pieces, count, out, extras);
    if (outExtras != nullptr)
        *outExtras = extras;
}

void CInventory::RecomputeOwnerStats() const
{
    auto owner = m_wpOwner.lock();
    ASSERT_CRASH(owner != nullptr);

    // 직업 · 능력치 · 장비를 모두 반영한 계산(CUser::RecomputeStats)으로 넘긴다.
    owner->RecomputeStats(false);
}

void CInventory::LoadFromDB(const std::vector<DB_ITEM_INFO>& items) noexcept
{
    // Attach()가 InitInstance() 안에서 이미 호출된 뒤라 m_wpOwner는 항상 유효하다.
    // (이 시점엔 아직 Zone에 등록 전이라 AssertOwnedByCurrentZone은 쓸 수 없지만,
    // owner 자체의 생존은 별개 문제이므로 그냥 크래시로 불변조건을 지킨다.)
    const auto owner = m_wpOwner.lock();
    ASSERT_CRASH(owner != nullptr);

    for (const auto& info : items)
    {
        // 범위 밖 슬롯이 DB에 남아있는 경우(과거 버전이 저장해둔 잘못된 행 등)도
        // 방어적으로 무시한다 — 메모리에 올려봐야 클라이언트에 보이지도 않고
        // 꺼낼 수도 없는 슬롯이라, 들고 있어봐야 혼란만 만든다.
        if (!IsValidSlotIndex(info.m_SlotIndex))
            continue;

        CItem::SharedPtr item = MakeNewItem(info.m_ItemId, info.m_Count);
        if (item == nullptr)
            continue;  // ItemTable에서 사라진 id — 게임 데이터가 DB보다 최신인 경우, 방어적으로 무시

        item->SetOwnerID(owner->GetObjID());

        if (CEquipmentItem* equipment = AsEquipment(item))
        {
            if (info.m_bEquipped)
                equipment->Equip();
            // 게임상 최대 강화 제한이 없으므로 저장된 값을 그대로 복원한다.
            equipment->SetEnchant(info.m_Enchant);
            equipment->SetExtra(info.m_Extra);
        }

        m_Slots[info.m_SlotIndex] = std::move(item);
    }
    RefreshEquipped();

    // 로그인 시 이미 장비를 장착한 채로 복원됐을 수 있으므로, CUser::InitInstance()가
    // 세팅한 기본 공격력을 여기서 곧바로 실제 상태로 덮어쓴다.
    RecomputeOwnerStats();
}

bool CInventory::CanAddItem(ItemTableId itemId, UInt16 count) const noexcept
{
    if (count == 0)
        return false;
    const ItemTableRow* row = ItemTable::Find(itemId);
    if (row == nullptr)
        return false;
    // 그로시는 슬롯이 아니라 지갑으로 간다(AddAdena) — 여기로 오면 호출자 실수다.
    if (row->type == ItemType::eCurrency)
        return false;

    // 드라이런: 맵을 하나도 건드리지 않고, count 전부를 수용할 수 있는지만 확인한다 —
    // "일부만 추가된 채로 실패" 상태가 생기지 않도록 반영 전에 반드시 먼저 확인한다(TryAddItem 1단계).
    UInt16 remaining = count;
    if (row->maxStack > 1)
    {
        for (const auto& [slotIndex, item] : m_Slots)
        {
            if (remaining == 0)
                break;
            if (!IsSlotLocked(slotIndex) && item->GetItemTableId() == itemId && item->GetCount() < row->maxStack)
                remaining -= std::min<UInt16>(row->maxStack - item->GetCount(), remaining);
        }
    }
    for (UInt16 i = 0; i < MAX_INVENTORY_SLOTS && remaining > 0; ++i)
    {
        if (!IsSlotLocked(i) && m_Slots.find(i) == m_Slots.end())
            remaining -= std::min<UInt16>(row->maxStack, remaining);
    }
    return remaining == 0;
}

bool CInventory::TryAddItem(ItemTableId itemId, UInt16 count, std::vector<UInt16>* outTouchedSlots, UInt32 enchant, UInt64 extra) noexcept
{
    AssertOwnedByCurrentZone();

    // 1단계(드라이런 — CanAddItem): 부족하면 아무 것도 바꾸지 않고 바로 false(없는 아이템 · 그로시 · count 0도 false).
    if (!CanAddItem(itemId, count))
        return false;

    // AssertOwnedByCurrentZone()이 이미 owner != nullptr을 보장했으므로 여기서
    // 다시 null 체크할 필요는 없다 — 새로 만드는 CItem에 OwnerID를 찍어주기 위해
    // 필요한 값만 미리 꺼내둔다.
    const auto owner = m_wpOwner.lock();
    const ItemTableRow* row = ItemTable::Find(itemId);   // CanAddItem이 이미 찾았다(널 아님)
    UInt16 remaining = count;

    // 2단계(반영): 1단계와 완전히 같은 순회 순서를 사용하므로, 이미 자리가
    // 충분함을 확인했기에 여기서는 실패할 수 없다. 건드린 슬롯은 모아뒀다가
    // 전부 반영이 끝난 뒤 한 번에 DB로 즉시 저장한다.
    std::vector<UInt16> touchedSlots;
    if (row->maxStack > 1)
    {
        for (auto& [slotIndex, item] : m_Slots)
        {
            if (remaining == 0)
                break;
            if (!IsSlotLocked(slotIndex) && item->GetItemTableId() == itemId && item->GetCount() < row->maxStack)
            {
                const UInt16 added = std::min<UInt16>(row->maxStack - item->GetCount(), remaining);
                item->AddCount(added);
                remaining -= added;
                touchedSlots.push_back(slotIndex);
            }
        }
    }
    for (UInt16 i = 0; i < MAX_INVENTORY_SLOTS && remaining > 0; ++i)
    {
        if (IsSlotLocked(i) || m_Slots.find(i) != m_Slots.end())
            continue;

        const UInt16 added = std::min<UInt16>(row->maxStack, remaining);
        CItem::SharedPtr newItem = MakeNewItem(itemId, added);
        // ItemTable::Find(itemId)가 이미 위에서 성공했으므로 MakeNewItem은 실패할
        // 수 없다 — nullptr이 나온다면 그 자체가 불변 조건 위반이라 여기서 멈춘다.
        // (여기서 조용히 false를 반환하면 앞 슬롯들은 이미 채워진 채로 남아
        // "일부만 반영된 실패"가 되어버리므로, 절대 그렇게 하지 않는다.)
        ASSERT_CRASH(newItem != nullptr);
        newItem->SetOwnerID(owner->GetObjID());
        if (CEquipmentItem* equipment = AsEquipment(newItem))
        {
            equipment->SetEnchant(enchant);
            equipment->SetExtra(extra);
        }
        m_Slots.emplace(i, std::move(newItem));
        touchedSlots.push_back(i);
        remaining -= added;
    }
    RefreshEquipped();   // 새 칸은 착용 전이지만, 넣다 보면 맵 순회 순서가 바뀔 수 있다

    for (UInt16 slotIndex : touchedSlots)
        SaveSlotToDB(slotIndex);

    if (outTouchedSlots != nullptr)
        *outTouchedSlots = std::move(touchedSlots);

    return true;
}

bool CInventory::TryRemoveItem(UInt16 slotIndex, UInt16 count) noexcept
{
    AssertOwnedByCurrentZone();
    if (IsSlotLocked(slotIndex)) return false;

    if (count == 0 || !IsValidSlotIndex(slotIndex))
        return false;

    const auto it = m_Slots.find(slotIndex);
    if (it == m_Slots.end() || it->second->GetCount() < count)
        return false;

    it->second->RemoveCount(count);
    if (it->second->GetCount() == 0)
    {
        m_Slots.erase(it);
        RefreshEquipped();
    }

    SaveSlotToDB(slotIndex);
    return true;
}

CItem::SharedPtr CInventory::TryExtractItem(UInt16 slotIndex, UInt16 count) noexcept
{
    AssertOwnedByCurrentZone();
    if (IsSlotLocked(slotIndex)) return nullptr;

    if (count == 0 || !IsValidSlotIndex(slotIndex))
        return nullptr;

    const auto it = m_Slots.find(slotIndex);
    if (it == m_Slots.end() || it->second->GetCount() < count)
        return nullptr;

    CItem::SharedPtr extracted;
    if (it->second->GetCount() == count)
    {
        // 슬롯 전량 추출 — 기존 CItem 인스턴스를 그대로 꺼내 재사용한다(새로
        // 만들 필요 없음). 장착 중이었다면 여기서 탈착한다 — 인벤토리 밖으로
        // 나가는 순간 "장착 상태"는 의미가 없어진다.
        extracted = std::move(it->second);
        m_Slots.erase(it);
        RefreshEquipped();

        if (CEquipmentItem* equipment = AsEquipment(extracted))
        {
            const bool wasEquipped = equipment->IsEquipped();
            equipment->Unequip();
            // 장착 중이던 장비를 버린 경우에만 공격력을 다시 계산한다 — 아니면
            // 불필요한 재계산(결과는 항상 같지만)을 매번 할 필요가 없다.
            if (wasEquipped)
                RecomputeOwnerStats();
        }
    }
    else
    {
        // 부분 추출 — 스택형 아이템에서만 일어난다(장비는 maxStack=1이라
        // count == 전체 수량인 위 분기로만 들어옴). 남은 수량은 슬롯에 그대로
        // 두고, 꺼내는 만큼만 새 CItem으로 만든다.
        it->second->RemoveCount(count);
        extracted = MakeNewItem(it->second->GetItemTableId(), count);
        // 이미 존재하는(같은 슬롯에 들어있던) itemId를 그대로 쓰므로 ItemTable
        // 조회는 반드시 성공한다 — 실패한다면 그 자체가 불변조건 위반이다.
        ASSERT_CRASH(extracted != nullptr);
    }

    // 지금부터 이 CItem은 어떤 CInventory에도 속하지 않는다 — 호출자(CItemHelper)가
    // 필드에 놓거나, 놓지 않고 버려서(참조 카운트가 0이 되어) 그대로 사라지게 한다.
    extracted->SetOwnerID(ObjID::npos);

    // 슬롯의 "남은" 상태를 그대로 반영한다 — 전량 추출이면 삭제, 부분 추출이면
    // 갱신. SaveSlotToDB는 이미 그 두 경우를 모두 올바르게 처리한다.
    SaveSlotToDB(slotIndex);

    return extracted;
}

ItemTableId CInventory::GetEquippedItemId() const noexcept
{
    // "장착 무기" — V2부터 방어구·장신구도 장착되므로 무기 부위만 본다
    // (외형 패킷 SUBJECT_ADD_NFY · SUBJECT_EQUIP_CHANGE_NFY · SUBJECT_INFO_NFY가 이 값을 쓴다).
    const UInt16 slotIndex = FindEquipped(EquipSlot::Weapon);
    if (slotIndex >= MAX_INVENTORY_SLOTS)
        return ITEM_TABLE_ID_NONE;
    return m_Slots.at(slotIndex)->GetItemTableId();
}

UInt32 CInventory::GetEquippedEnchant() const noexcept
{
    const UInt16 slotIndex = FindEquipped(EquipSlot::Weapon);
    if (slotIndex >= MAX_INVENTORY_SLOTS)
        return 0;
    const CEquipmentItem* equipment = AsEquipment(m_Slots.at(slotIndex));
    return equipment != nullptr ? equipment->GetEnchant() : 0;
}

bool CInventory::TryEquip(UInt16 slotIndex, UInt16* outPreviousSlot) noexcept
{
    AssertOwnedByCurrentZone();
    if (IsBusy()) return false;   // 대장간 트랜잭션 중에는 장착을 바꾸지 않는다(같은 부위의 다른 칸이 벗겨질 수 있다)
    if (IsSlotLocked(slotIndex)) return false;

    if (!IsValidSlotIndex(slotIndex))
        return false;

    const auto it = m_Slots.find(slotIndex);
    if (it == m_Slots.end())
        return false;

    CEquipmentItem* equipment = AsEquipment(it->second);
    if (equipment == nullptr || equipment->IsEquipped())
        return false;

    const ItemTableRow* row = ItemTable::Find(equipment->GetItemTableId());
    if (row == nullptr)
        return false;
    const EquipSlot part = ItemTableData::SlotOf(*row);

    // V2: 직업 전용 무기 · 착용 레벨.
    if (const auto owner = m_wpOwner.lock(); owner != nullptr)
    {
        if (!ItemTableData::ClassCanUse(*row, static_cast<UInt8>(owner->m_eClassId)))
            return false;
        if (row->reqLevel > owner->GetStat()->GetLevel())
            return false;
    }

    // 이 시점 이후로는 실패할 수 없다(단순 플래그 플립 + 비동기 DB enqueue뿐) —
    // 그래서 TryAddItem처럼 별도 드라이런 단계 없이도 "확인 -> 반영"이 이미 원자적이다.
    // 같은 부위에 이미 입고 있던 장비를 벗는다(부위마다 하나 — 불변조건). 보통은 하나지만 2026-09-02 전 서버가 남긴 옛 DB에는
    // 같은 부위가 둘 이상 장착돼 있을 수 있다 — 모두 벗겨야 새 장비 하나만 남는다(예전엔 하나만 벗겨 둘이 남았다).
    UInt16 previousSlot = 0;
    bool hadPrevious = false;
    std::vector<UInt16> extraPrevious;
    for (auto& [otherSlotIndex, otherItem] : m_Slots)
    {
        if (otherSlotIndex == slotIndex)
            continue;

        if (CEquipmentItem* otherEquipment = AsEquipment(otherItem);
            otherEquipment != nullptr && otherEquipment->IsEquipped()
            && ItemTableData::SlotOf(otherEquipment->GetItemTableId()) == part)
        {
            otherEquipment->Unequip();
            if (!hadPrevious)
            {
                previousSlot = otherSlotIndex;
                hadPrevious = true;
            }
            else
                extraPrevious.push_back(otherSlotIndex);
        }
    }

    equipment->Equip();
    RefreshEquipped();
    RecomputeOwnerStats();

    // 메모리 상태는 이미 위에서 원자적으로(한 번의 함수 호출 안에서, 다른 스레드가
    // 끼어들 수 없이) 바뀌었다. DB 반영도 이전 장비가 있었다면 SaveTwoSlotsToDB로
    // 한 트랜잭션에 묶어서, 서버가 두 저장 사이에 죽어도 "DB에 두 개 다 장착"
    // 같은 불변조건 위반 상태가 남지 않게 한다. 옛 DB의 남는 장착은 따로 푼다(이미 깨진 상태를 고치는 길이라 한 트랜잭션일 필요는 없다).
    if (hadPrevious)
        SaveTwoSlotsToDB(previousSlot, slotIndex);
    else
        SaveSlotToDB(slotIndex);
    for (const UInt16 extra : extraPrevious)
        SaveSlotToDB(extra);

    if (hadPrevious && outPreviousSlot != nullptr)
        *outPreviousSlot = previousSlot;

    return true;
}

bool CInventory::TryUnequip(UInt16 slotIndex) noexcept
{
    AssertOwnedByCurrentZone();
    if (IsSlotLocked(slotIndex)) return false;

    if (!IsValidSlotIndex(slotIndex))
        return false;

    const auto it = m_Slots.find(slotIndex);
    if (it == m_Slots.end())
        return false;

    CEquipmentItem* equipment = AsEquipment(it->second);
    if (equipment == nullptr || !equipment->IsEquipped())
        return false;

    equipment->Unequip();
    RefreshEquipped();
    RecomputeOwnerStats();
    SaveSlotToDB(slotIndex);
    return true;
}

bool CInventory::TryUseItem(UInt16 slotIndex) noexcept
{
    AssertOwnedByCurrentZone();
    if (IsSlotLocked(slotIndex)) return false;

    if (!IsValidSlotIndex(slotIndex))
        return false;

    const auto it = m_Slots.find(slotIndex);
    if (it == m_Slots.end())
        return false;

    CConsumableItem* const consumable = AsConsumable(it->second);
    if (consumable == nullptr)
        return false;

    const ItemTableRow* row = ItemTable::Find(consumable->GetItemTableId());
    // 슬롯에 실제로 들어있는 아이템이므로 ItemTable에 없을 수 없다 — 없다면
    // 그 자체가 불변조건 위반이다.
    ASSERT_CRASH(row != nullptr);

    // 이 경로는 회복 물약 전용이다. 두루마리/바람걸음 물약처럼 효과가 인벤토리 밖에서
    // 일어나는 아이템은 TryConsumeOne + CUserHelper 쪽 효과 처리로 간다 — 여기서
    // 받아버리면 효과 없이 소모만 되는 버그가 된다.
    if (row->effect != ItemEffect::eHeal || row->healAmount == 0)
        return false;

    const auto owner = m_wpOwner.lock();
    // 레벨업으로 최대 HP가 오르므로 상한은 고정 상수(PLAYER_MAX_HP)가 아니라 현재 최대 HP다.
    owner->GetStat()->HealHp(row->healAmount, owner->GetStat()->GetMaxHp());

    consumable->RemoveCount(1);
    if (consumable->GetCount() == 0)
    {
        m_Slots.erase(it);
        RefreshEquipped();
    }

    SaveSlotToDB(slotIndex);
    return true;
}

bool CInventory::TrySwapSlots(UInt16 slotIndexA, UInt16 slotIndexB) noexcept
{
    AssertOwnedByCurrentZone();
    if (IsSlotLocked(slotIndexA) || IsSlotLocked(slotIndexB)) return false;

    // 아래에서 m_Slots[...]로 없던 키를 만들 수 있으므로, 두 인덱스 모두 반드시
    // 먼저 검증한다 — 하나라도 범위 밖이면 아이템이 접근 불가능한 슬롯으로
    // 넘어가 영구히 사라진다(Inventory.h의 TrySwapSlots 주석 참고).
    if (!IsValidSlotIndex(slotIndexA) || !IsValidSlotIndex(slotIndexB))
        return false;

    if (slotIndexA == slotIndexB)
        return false;

    const auto itA = m_Slots.find(slotIndexA);
    const auto itB = m_Slots.find(slotIndexB);
    if (itA == m_Slots.end() && itB == m_Slots.end())
        return false;

    // 지운 뒤 다시 넣는 과정에서 이터레이터가 무효화될 수 있으므로, 먼저
    // 값을 복사해두고 그 다음에 맵을 수정한다.
    const CItem::SharedPtr a = (itA != m_Slots.end()) ? itA->second : nullptr;
    const CItem::SharedPtr b = (itB != m_Slots.end()) ? itB->second : nullptr;

    if (a != nullptr)
        m_Slots[slotIndexB] = a;
    else
        m_Slots.erase(slotIndexB);

    if (b != nullptr)
        m_Slots[slotIndexA] = b;
    else
        m_Slots.erase(slotIndexA);
    RefreshEquipped();

    // 두 슬롯을 한 트랜잭션으로 묶는다 — 따로 저장하면 그 사이에 서버가 죽었을
    // 때 DB에 아이템이 두 슬롯 모두에 있거나(중복) 어디에도 없는(유실) 상태가
    // 남을 수 있다.
    SaveTwoSlotsToDB(slotIndexA, slotIndexB);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// 리니지풍 컨텐츠 확장
// ─────────────────────────────────────────────────────────────────────────────

UInt32 CInventory::AddAdena(UInt32 amount, AdenaReason reason) noexcept
{
    AssertOwnedByCurrentZone();
    if (m_TransferCommitting)
    {
        const UInt32 added = std::min(amount, 2'000'000'000u - m_TransferWalletCeiling - m_DeferredAdena);
        if (added) { m_DeferredCredits.emplace_back(added, reason); m_DeferredAdena += added; }
        // 개인 업적(사냥 그로시)은 여기서 세지 않는다 — FinishTransfer가 미룬 몫을 AddAdena(같은 사유)로 다시 넣을 때 한 번 센다
        return added; // 소유권은 유지하고 절대값 저장은 거래 결과가 확정된 뒤에만 한다.
    }

    // DB 컬럼이 INT(부호 있음)라 그 범위를 넘지 않게 자른다.
    constexpr UInt32 kMaxAdena = 2'000'000'000u;
    const UInt32 room  = (m_Adena < kMaxAdena) ? (kMaxAdena - m_Adena) : 0;
    const UInt32 added = (std::min)(amount, room);
    if (added == 0)
        return 0;

    m_Adena += added;
    CEconomyLog::OnAdena(reason, added);

    auto owner = m_wpOwner.lock();
    GDBThread->RequestSaveAdena(owner->GetPlayerId(), m_Adena);
    CUserHelper::SendADENA_INF(owner, static_cast<Int32>(added), reason);
    if (reason == AdenaReason::Pickup)
        CAchieveHelper::OnAdenaPickup(owner, added);   // 개인 업적(사냥 그로시 — 처치 몫 · 떨어진 그로시 줍기)
    return added;
}

bool CInventory::TrySpendAdena(UInt32 amount, AdenaReason reason) noexcept
{
    AssertOwnedByCurrentZone();
    if (m_TransferCommitting) return false;

    if (m_Adena < amount)
        return false;
    if (amount == 0)
        return true;

    m_Adena -= amount;
    CEconomyLog::OnAdena(reason, -static_cast<int64_t>(amount));

    auto owner = m_wpOwner.lock();
    GDBThread->RequestSaveAdena(owner->GetPlayerId(), m_Adena);
    CUserHelper::SendADENA_INF(owner, -static_cast<Int32>(amount), reason);
    return true;
}

bool CInventory::TryConsumeOne(UInt16 slotIndex, ItemEffect expected, ItemTableId* outItemId) noexcept
{
    AssertOwnedByCurrentZone();
    if (IsSlotLocked(slotIndex)) return false;

    if (!IsValidSlotIndex(slotIndex))
        return false;

    const auto it = m_Slots.find(slotIndex);
    if (it == m_Slots.end())
        return false;

    CConsumableItem* const consumable = AsConsumable(it->second);
    if (consumable == nullptr)
        return false;

    const ItemTableRow* row = ItemTable::Find(consumable->GetItemTableId());
    ASSERT_CRASH(row != nullptr);
    if (row->effect != expected)
        return false;

    if (outItemId != nullptr)
        *outItemId = row->id;

    consumable->RemoveCount(1);
    if (consumable->GetCount() == 0)
    {
        m_Slots.erase(it);
        RefreshEquipped();
    }

    SaveSlotToDB(slotIndex);
    return true;
}

void CInventory::LockSlots(PendingTx kind, std::initializer_list<UInt16> slots) noexcept
{
    m_PendingTx = kind;
    m_LockedSlots.reset();
    for (const UInt16 slot : slots)
        if (IsValidSlotIndex(slot))
            m_LockedSlots.set(slot);
}

void CInventory::UnlockSlots() noexcept
{
    m_PendingTx = PendingTx::None;
    m_LockedSlots.reset();
}

UInt32 CInventory::GetSlotEnchant(UInt16 slot) const noexcept
{
    const auto it = m_Slots.find(slot);
    if (it == m_Slots.end()) return 0;
    const CEquipmentItem* gear = AsEquipment(it->second);
    return gear ? gear->GetEnchant() : 0;
}

void CInventory::LoadEnchantGrace(const std::vector<DB_ENCHANT_GRACE>& rows)
{
    // LoadFromDB와 같이 로그인 때 CUser를 월드에 공개하기 전(Zone 등록 전)에 부른다 — 이 스레드만 만지므로 소유 Zone 검사를 하지 않는다
    // (예전엔 검사해서 디버그 빌드가 기존 캐릭터 재접속마다 죽었다).
    m_EnchantGrace.clear();
    for (const auto& r : rows) m_EnchantGrace[(UInt64(r.itemId) << 32) | r.currentLevel] = r;
}

UInt16 CInventory::GetGraceFailures(UInt16 itemId, UInt32 currentLevel) const noexcept
{
    const auto it = m_EnchantGrace.find((UInt64(itemId) << 32) | currentLevel);
    return it == m_EnchantGrace.end() ? 0 : it->second.failures;
}

CInventory::EnchantPlan CInventory::PrepareEnchant(UInt16 ticketSlot, UInt16 targetSlot, UInt16 protectionSlot, UInt64 roll)
{
    AssertOwnedByCurrentZone();
    EnchantPlan plan{};
    if (IsBusy()) { plan.outcome.result = EnchantResult::Busy; return plan; }
    // 보호권 없음 = kNoInventorySlot(0xFFFF). MAX_INVENTORY_SLOTS 이상은 모두 없음으로 본다(가방 50칸 — 예전 「30 = 없음」은 이제 칸이다).
    const bool protect = protectionSlot < MAX_INVENTORY_SLOTS;
    if (!IsValidSlotIndex(ticketSlot) || !IsValidSlotIndex(targetSlot) || ticketSlot == targetSlot ||
        (protect && (protectionSlot == ticketSlot || protectionSlot == targetSlot))) return plan;
    const auto t = m_Slots.find(ticketSlot), g = m_Slots.find(targetSlot), p = m_Slots.find(protectionSlot);
    if (t == m_Slots.end() || g == m_Slots.end() || t->second->GetCount() == 0) return plan;
    const auto* ticket = ItemTable::Find(t->second->GetItemTableId());
    const CEquipmentItem* gear = AsEquipment(g->second);
    const auto* row = ItemTable::Find(g->second->GetItemTableId());
    if (!ticket || ticket->effect != ItemEffect::eEnchantWeapon || !gear || !row || ItemTableData::SlotOf(*row) == EquipSlot::None ||
        gear->GetEnchant() == EnchantRule::kMaxLevel || roll >= EnchantRule::kRollScale) return plan;
    if (protect && (p == m_Slots.end() || p->second->GetItemTableId() != ItemTableData::Id::EnhancementProtection || p->second->GetCount() == 0)) return plan;

    plan.outcome.itemId = gear->GetItemTableId();
    plan.outcome.before = gear->GetEnchant();
    plan.outcome.wasEquipped = gear->IsEquipped();
    plan.save.graceBefore = GetGraceFailures(plan.outcome.itemId, plan.outcome.before);
    const bool success = EnchantRule::SucceedsWithGrace(plan.outcome.before, plan.save.graceBefore, roll);
    plan.save.graceAfter = EnchantRule::GraceAttempts(plan.outcome.before) != 0 && !success ? plan.save.graceBefore + 1 : 0;
    plan.outcome.result = success ? EnchantResult::Success : (protect ? EnchantResult::Protected : EnchantResult::Evaporated);
    plan.outcome.after = success ? plan.outcome.before + 1 : (protect ? plan.outcome.before : 0);
    plan.save.count = protect ? 3 : 2;
    const std::array<UInt16, 3> slots{ ticketSlot, targetSlot, protect ? protectionSlot : kNoInventorySlot };
    for (size_t i = 0; i < plan.save.count; ++i)
    {
        plan.save.before[i] = DescribeSlot(slots[i]);
        plan.save.after[i] = plan.save.before[i];
        auto& after = plan.save.after[i];
        if (i == 1)
        {
            after.enchant = plan.outcome.after;
            if (!success && !protect) { after.hasItem = false; after.equipped = false; }
        }
        else { --after.count; after.hasItem = after.count > 0; }
    }
    plan.save.itemId = plan.outcome.itemId;
    plan.save.level = success && plan.outcome.after >= EnchantRule::FirstNoticeLevel() ? plan.outcome.after : 0;
    plan.save.name = m_wpOwner.lock()->GetName();
    LockSlots(PendingTx::Enchant, { ticketSlot, targetSlot, slots[2] });
    return plan;
}

void CInventory::CompleteEnchant(const EnchantPlan& plan, bool committed)
{
    AssertOwnedByCurrentZone();
    if (m_PendingTx != PendingTx::Enchant) return;
    if (committed)
    {
        if (EnchantRule::GraceAttempts(plan.outcome.before) != 0)
        {
            const DB_ENCHANT_GRACE grace{plan.save.itemId, plan.outcome.before, plan.save.graceAfter};
            m_EnchantGrace[(UInt64(grace.itemId) << 32) | grace.currentLevel] = grace;
        }
        for (size_t i = 0; i < plan.save.count; ++i)
        {
            const auto& after = plan.save.after[i];
            if (!after.hasItem)
            {
                if (CEquipmentItem* gear = AsEquipment(m_Slots.at(after.slotIndex))) gear->Unequip();
                m_Slots.erase(after.slotIndex);
            }
            else
            {
                const auto item = m_Slots.at(after.slotIndex);
                item->SetCount(after.count);
                if (CEquipmentItem* gear = AsEquipment(item)) gear->SetEnchant(after.enchant);
            }
        }
        RefreshEquipped();   // 증발한 장비가 착용 중이었을 수 있다
        if (plan.outcome.wasEquipped) RecomputeOwnerStats();
    }
    UnlockSlots();
    if (committed) for (size_t i = 0; i < plan.save.count; ++i) NotifySlotChanged(plan.save.after[i].slotIndex);
    if (committed && EnchantRule::GraceAttempts(plan.outcome.before) != 0)
        CUserHelper::SendENCHANT_GRACE_INF(m_wpOwner.lock(), plan.save.itemId, plan.outcome.before);
}

namespace
{
    // 계획의 before가 지금 칸과 같은가(빈 칸이면 둘 다 비어 있어야)
    bool SameSlot(const DB_ITEM_SLOT_SAVE& a, const DB_ITEM_SLOT_SAVE& b) noexcept
    {
        if (a.hasItem != b.hasItem) return false;
        return !a.hasItem || (a.itemId == b.itemId && a.count == b.count && a.equipped == b.equipped && a.enchant == b.enchant && a.extra == b.extra);
    }
}

bool CInventory::BeginBatch(const DB_ITEM_BATCH_SAVE& save) noexcept
{
    AssertOwnedByCurrentZone();
    // 그로시는 줄기만 한다(수수료) — 예약은 메모리에서 빼 두는 것이라 늘어나는 계획은 받지 않는다
    if (IsBusy() || save.before.empty() || save.before.size() != save.after.size() || save.before.size() > MAX_INVENTORY_SLOTS
        || save.adenaBefore != m_Adena || save.adenaAfter > save.adenaBefore)
        return false;
    std::bitset<MAX_INVENTORY_SLOTS> slots;
    for (size_t i = 0; i < save.before.size(); ++i)
    {
        const DB_ITEM_SLOT_SAVE& b = save.before[i];
        if (!IsValidSlotIndex(b.slotIndex) || save.after[i].slotIndex != b.slotIndex || slots.test(b.slotIndex) || !SameSlot(DescribeSlot(b.slotIndex), b))
            return false;
        slots.set(b.slotIndex);
    }
    m_PendingTx = PendingTx::Craft;
    m_LockedSlots = slots;
    m_Adena = save.adenaAfter;   // 예약(저장 없음 — 프로시저가 adenaAfter를 쓴다)
    return true;
}

void CInventory::CompleteBatch(const DB_ITEM_BATCH_SAVE& save, bool committed, AdenaReason reason) noexcept
{
    AssertOwnedByCurrentZone();
    if (m_PendingTx != PendingTx::Craft)
        return;
    const auto owner = m_wpOwner.lock();
    const UInt32 fee = save.adenaBefore - save.adenaAfter;
    if (!committed)
    {
        // 예약을 되돌리고 지금 값을 저장한다(그 사이 다른 절대값 저장이 예약 뒤 값을 넣었을 수 있다 — 경매 ReturnAdena와 같다).
        // 칸은 잠근 채 둔다 — 호출자가 연결을 닫고, 다시 들어오면 DB가 원본이다.
        if (fee != 0)
        {
            m_Adena += fee;
            GDBThread->RequestSaveAdena(owner->GetPlayerId(), m_Adena);
        }
        return;
    }

    bool equipChanged = false;
    for (const DB_ITEM_SLOT_SAVE& a : save.after)
    {
        const auto it = m_Slots.find(a.slotIndex);
        CEquipmentItem* old = it != m_Slots.end() ? AsEquipment(it->second) : nullptr;
        if (old != nullptr && old->IsEquipped())
            equipChanged = true;
        if (!a.hasItem || a.count == 0)
        {
            if (it != m_Slots.end())
            {
                if (old != nullptr) old->Unequip();
                m_Slots.erase(it);
            }
            continue;
        }
        if (it != m_Slots.end() && it->second->GetItemTableId() == a.itemId)
        {
            it->second->SetCount(a.count);
            if (old != nullptr)
            {
                old->SetEnchant(a.enchant);
                old->SetExtra(a.extra);
                if (a.equipped) old->Equip(); else old->Unequip();
            }
        }
        else
        {
            CItem::SharedPtr item = MakeNewItem(a.itemId, a.count);
            if (item == nullptr)
            {
                // 표에서 사라진 id(개발용 핫 리로드) — DB에는 커밋됐다. 다시 들어오면 LoadFromDB도 같은 이유로 건너뛴다
                if (it != m_Slots.end()) m_Slots.erase(it);
                continue;
            }
            item->SetOwnerID(owner->GetObjID());
            if (CEquipmentItem* gear = AsEquipment(item))
            {
                gear->SetEnchant(a.enchant);
                gear->SetExtra(a.extra);
                if (a.equipped) gear->Equip();
            }
            m_Slots[a.slotIndex] = std::move(item);
        }
        if (a.equipped)
            equipChanged = true;
    }
    RefreshEquipped();
    if (equipChanged)
        RecomputeOwnerStats();
    UnlockSlots();
    for (const DB_ITEM_SLOT_SAVE& a : save.after)
        NotifySlotChanged(a.slotIndex);
    if (fee != 0)
        CUserHelper::SendADENA_INF(owner, -static_cast<Int32>(fee), reason);
}

UInt16 CInventory::FindSingleSlotFor(const ItemTableRow& row, UInt16 count) const noexcept
{
    if (count == 0 || count > row.maxStack)
        return MAX_INVENTORY_SLOTS;

    // 같은 아이템 스택에 통째로 들어갈 자리가 있으면 거기 합친다(장비는 maxStack=1이라 해당 없음).
    if (row.maxStack > 1)
    {
        for (const auto& [slotIndex, item] : m_Slots)
        {
            if (!IsSlotLocked(slotIndex) && item->GetItemTableId() == row.id && item->GetCount() + count <= row.maxStack)
                return slotIndex;
        }
    }

    for (UInt16 i = 0; i < MAX_INVENTORY_SLOTS; ++i)
    {
        if (!IsSlotLocked(i) && m_Slots.find(i) == m_Slots.end())
            return i;
    }
    return MAX_INVENTORY_SLOTS;
}

namespace
{
    // 상점 거래(그로시 + 칸 하나) 저장 — P_SHOP_TRADE 한 트랜잭션. 그 프로시저는 칸의 Extra를 0으로 쓴다(인자에 없다) —
    // 획득 귀속 bit 8처럼 extra가 있는 칸은 같은 샤드에서 바로 이어 절대값으로 다시 쓴다(P_INVENTORY_UPSERT).
    // 예전엔 MySQL에서만 상점에서 산 귀속 장비의 bit 8이 사라졌다(인메모리 DB는 남겼다 — 이제 인메모리도 P_SHOP_TRADE처럼 0으로 쓴다).
    void SaveShopTransaction(int playerId, UInt32 adena, const DB_ITEM_SLOT_SAVE& slot)
    {
        GDBThread->RequestShopTransaction(playerId, adena, slot);
        if (slot.hasItem && slot.extra != 0)
            GDBThread->RequestSaveItem(playerId, slot.slotIndex, slot.itemId, slot.count, slot.equipped, slot.enchant, slot.extra);
    }
}

ShopResult CInventory::TryBuy(ItemTableId itemId, UInt16 count, UInt32 totalPrice, UInt16* outSlot) noexcept
{
    AssertOwnedByCurrentZone();
    if (m_TransferCommitting) return ShopResult::InvalidState;

    const ItemTableRow* row = ItemTable::Find(itemId);
    if (row == nullptr || row->type == ItemType::eCurrency)
        return ShopResult::InvalidItem;
    if (count == 0 || count > row->maxStack)
        return ShopResult::InvalidCount;
    if (m_Adena < totalPrice)
        return ShopResult::NotEnoughAdena;

    const UInt16 slotIndex = FindSingleSlotFor(*row, count);
    if (slotIndex >= MAX_INVENTORY_SLOTS)
        return ShopResult::InventoryFull;

    // ── 확인이 전부 끝났다. 여기부터 실패할 수 없다(메모리 반영 → DB 한 트랜잭션). ──
    const auto owner = m_wpOwner.lock();
    const auto it = m_Slots.find(slotIndex);
    if (it != m_Slots.end())
    {
        it->second->AddCount(count);
    }
    else
    {
        CItem::SharedPtr newItem = MakeNewItem(itemId, count);
        ASSERT_CRASH(newItem != nullptr);
        newItem->SetOwnerID(owner->GetObjID());
        if (CEquipmentItem* equipment = AsEquipment(newItem))
            equipment->SetExtra(ItemRules::OnAcquire(*row, 0));   // 획득 귀속 장비면 bit 8(지금 상점 장비는 귀속이 아니다)
        m_Slots.emplace(slotIndex, std::move(newItem));
        RefreshEquipped();
    }
    m_Adena -= totalPrice;
    CEconomyLog::OnAdena(AdenaReason::ShopBuy, -static_cast<int64_t>(totalPrice));
    if (row->type == ItemType::eEquipment)
        CEconomyLog::OnGearIn(GearSource::Shop, row->grade, count);

    SaveShopTransaction(owner->GetPlayerId(), m_Adena, DescribeSlot(slotIndex));
    NotifySlotChanged(slotIndex);
    CUserHelper::SendADENA_INF(owner, -static_cast<Int32>(totalPrice), AdenaReason::ShopBuy);

    if (outSlot != nullptr)
        *outSlot = slotIndex;
    return ShopResult::Success;
}

ShopResult CInventory::TrySell(UInt16 slotIndex, UInt16 count, UInt32* outGain) noexcept
{
    AssertOwnedByCurrentZone();
    if (IsSlotLocked(slotIndex)) return ShopResult::InvalidState;

    if (!IsValidSlotIndex(slotIndex))
        return ShopResult::InvalidItem;

    const auto it = m_Slots.find(slotIndex);
    if (it == m_Slots.end())
        return ShopResult::InvalidItem;

    const ItemTableRow* row = ItemTable::Find(it->second->GetItemTableId());
    ASSERT_CRASH(row != nullptr);
    if (row->sellPrice == 0)
        return ShopResult::InvalidItem;
    if (count == 0 || count > it->second->GetCount())
        return ShopResult::InvalidCount;

    if (const CEquipmentItem* equipment = AsEquipment(it->second); equipment != nullptr && equipment->IsEquipped())
        return ShopResult::EquippedItem;

    // 강화된 무기(+1당 10%)와 희귀도 높은 장비는 더 받는다 — 표시는 클라이언트가
    // 같은 함수(ItemRules::SellPrice)로 계산한다.
    UInt64 unit = row->sellPrice;
    if (const CEquipmentItem* equipment = AsEquipment(it->second))
        unit = ItemRules::SellPrice(*row, equipment->GetExtra(), equipment->GetEnchant());
    const UInt64 gain64 = unit * count;
    constexpr UInt32 kMaxAdena = 2'000'000'000u;
    if (gain64 > kMaxAdena - (std::min)(m_Adena, kMaxAdena))
        return ShopResult::InvalidCount;  // 지갑 상한 초과 — 거래를 거절한다(버리지 않는다)
    const UInt32 gain = static_cast<UInt32>(gain64);

    // ── 여기부터 실패할 수 없다 ──
    it->second->RemoveCount(count);
    if (it->second->GetCount() == 0)
    {
        m_Slots.erase(it);
        RefreshEquipped();
    }
    m_Adena += gain;
    CEconomyLog::OnAdena(AdenaReason::ShopSell, gain);
    if (row->type == ItemType::eEquipment)
        CEconomyLog::OnGearOut(GearSink::ShopSell, row->grade, count);
    else if (row->type == ItemType::eMaterial)
        CEconomyLog::OnMaterialOut(MaterialSink::ShopSell, row->id, count);

    const auto owner = m_wpOwner.lock();
    SaveShopTransaction(owner->GetPlayerId(), m_Adena, DescribeSlot(slotIndex));
    NotifySlotChanged(slotIndex);
    CUserHelper::SendADENA_INF(owner, static_cast<Int32>(gain), AdenaReason::ShopSell);

    if (outGain != nullptr)
        *outGain = gain;
    return ShopResult::Success;
}

int CInventory::TrySort() noexcept
{
    AssertOwnedByCurrentZone();
    if (IsBusy()) return 0;

    int changes = 0;

    // 1) 같은 물건 스택 합치기(앞 칸으로 모은다). 두 칸이 함께 바뀌므로 한 트랜잭션.
    std::vector<UInt16> order;
    order.reserve(m_Slots.size());
    for (const auto& [slotIndex, item] : m_Slots)
        order.push_back(slotIndex);
    std::sort(order.begin(), order.end());
    for (size_t a = 0; a < order.size(); ++a)
    {
        auto ita = m_Slots.find(order[a]);
        if (ita == m_Slots.end())
            continue;
        const ItemTableRow* row = ItemTable::Find(ita->second->GetItemTableId());
        if (row == nullptr || row->maxStack <= 1)
            continue;
        for (size_t b = a + 1; b < order.size(); ++b)
        {
            ita = m_Slots.find(order[a]);
            if (ita == m_Slots.end() || ita->second->GetCount() >= row->maxStack)
                break;
            const auto itb = m_Slots.find(order[b]);
            if (itb == m_Slots.end() || itb->second->GetItemTableId() != row->id)
                continue;
            const UInt16 move = (std::min)(static_cast<UInt16>(row->maxStack - ita->second->GetCount()), itb->second->GetCount());
            if (move == 0)
                continue;
            ita->second->AddCount(move);
            itb->second->RemoveCount(move);
            if (itb->second->GetCount() == 0)
            {
                m_Slots.erase(itb);
                RefreshEquipped();
            }
            SaveTwoSlotsToDB(order[a], order[b]);
            ++changes;
        }
    }

    // 2) 원하는 순서
    struct Entry { CItem::SharedPtr item; int group, part, rarity, tier, effect, id, count; UInt32 enchant; };
    std::vector<Entry> entries;
    entries.reserve(m_Slots.size());
    for (const auto& [slotIndex, item] : m_Slots)
    {
        const ItemTableRow* row = ItemTable::Find(item->GetItemTableId());
        if (row == nullptr)
            continue;
        const CEquipmentItem* eq = AsEquipment(item);
        Entry e{};
        e.item = item;
        e.group = (eq != nullptr && eq->IsEquipped()) ? 0 : (row->type == ItemType::eEquipment ? 1 : (row->type == ItemType::eConsumable ? 2 : 3));
        e.part = static_cast<int>(ItemTableData::SlotOf(*row));
        e.rarity = eq != nullptr ? static_cast<int>(ItemRules::EffectiveRarity(*row, eq->GetExtra())) : 0;
        e.tier = row->tier;
        e.effect = static_cast<int>(row->effect);
        e.id = row->id;
        e.enchant = eq != nullptr ? eq->GetEnchant() : 0;
        e.count = item->GetCount();
        entries.push_back(std::move(e));
    }
    std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b)
    {
        if (a.group != b.group) return a.group < b.group;
        if (a.part != b.part) return a.part < b.part;
        if (a.rarity != b.rarity) return a.rarity > b.rarity;
        if (a.tier != b.tier) return a.tier > b.tier;
        if (a.effect != b.effect) return a.effect < b.effect;
        if (a.id != b.id) return a.id < b.id;
        if (a.enchant != b.enchant) return a.enchant > b.enchant;
        return a.count > b.count;
    });

    // 3) 앞 칸부터 제자리로(각 교환이 한 트랜잭션)
    for (size_t t = 0; t < entries.size() && t < MAX_INVENTORY_SLOTS; ++t)
    {
        const UInt16 target = static_cast<UInt16>(t);
        const auto here = m_Slots.find(target);
        if (here != m_Slots.end() && here->second == entries[t].item)
            continue;
        UInt16 from = MAX_INVENTORY_SLOTS;
        for (const auto& [slotIndex, item] : m_Slots)
            if (item == entries[t].item) { from = slotIndex; break; }
        if (from >= MAX_INVENTORY_SLOTS)
            continue;
        if (TrySwapSlots(from, target))
            ++changes;
    }
    return changes;
}

CItem::SharedPtr CInventory::TryTakeRandomItem(UInt32 roll) noexcept
{
    AssertOwnedByCurrentZone();

    if (m_Slots.empty())
        return nullptr;

    // unordered_map 순회 순서는 의미가 없으므로 슬롯 번호로 정렬된 후보에서 고른다.
    std::vector<UInt16> candidates;
    candidates.reserve(m_Slots.size());
    for (const auto& [slotIndex, item] : m_Slots)
    {
        if (IsSlotLocked(slotIndex))
            continue;
        // 귀속된 물건은 떨어뜨리지 않는다(경제 1차 명세 §1 — 획득 귀속 행 · 인스턴스 bit 8)
        const ItemTableRow* row = ItemTable::Find(item->GetItemTableId());
        const CEquipmentItem* equipment = AsEquipment(item);
        if (row != nullptr && ItemRules::IsBound(*row, equipment != nullptr ? equipment->GetExtra() : 0))
            continue;
        candidates.push_back(slotIndex);
    }
    std::sort(candidates.begin(), candidates.end());

    if (candidates.empty()) return nullptr;
    const UInt16 slotIndex = candidates[roll % candidates.size()];
    const UInt16 count = m_Slots[slotIndex]->GetCount();
    // TryExtractItem이 장착 해제/공격력 재계산/DB 저장/알림을 전부 처리한다.
    return TryExtractItem(slotIndex, count);
}

// ─────────────────────────────────────────────────────────────────────────────
// Phase 5 결사 · 경매장 — 메모리만 바꾼다(프로시저 하나가 그로시 · 슬롯을 함께 커밋한다).
// ─────────────────────────────────────────────────────────────────────────────
bool CInventory::ReserveAdena(UInt32 amount, AdenaReason reason) noexcept
{
    AssertOwnedByCurrentZone();
    if (m_PendingTx == PendingTx::Transfer) return false;
    if (m_Adena < amount)
        return false;
    if (amount == 0)
        return true;
    m_Adena -= amount;
    CEconomyLog::OnAdena(reason, -static_cast<int64_t>(amount));
    auto owner = m_wpOwner.lock();
    CUserHelper::SendADENA_INF(owner, -static_cast<Int32>(amount), reason);
    return true;
}

void CInventory::ReturnAdena(UInt32 amount, AdenaReason reason) noexcept
{
    AssertOwnedByCurrentZone();
    if (amount == 0)
        return;
    m_Adena += amount;
    CEconomyLog::OnAdena(reason, amount);
    auto owner = m_wpOwner.lock();
    // 트랜잭션이 실패했다 — 그 사이 다른 절대값 저장이 예약분이 빠진 값을 넣었을 수 있으니 지금 값을 저장한다
    GDBThread->RequestSaveAdena(owner->GetPlayerId(), m_Adena);
    CUserHelper::SendADENA_INF(owner, static_cast<Int32>(amount), reason);
}

void CInventory::CreditAdenaNoSave(UInt32 amount, AdenaReason reason) noexcept
{
    AssertOwnedByCurrentZone();
    if (amount == 0)
        return;
    m_Adena += amount;
    CEconomyLog::OnAdena(reason, amount);
    auto owner = m_wpOwner.lock();
    CUserHelper::SendADENA_INF(owner, static_cast<Int32>(amount), reason);
}

CInventory::TakeResult CInventory::TakeForListing(UInt16 slotIndex, UInt16 count, TakenItem& out, DB_ITEM_SLOT_SAVE& remain) noexcept
{
    AssertOwnedByCurrentZone();
    if (IsSlotLocked(slotIndex)) return TakeResult::NoItem;
    if (!IsValidSlotIndex(slotIndex))
        return TakeResult::NoItem;
    const auto it = m_Slots.find(slotIndex);
    if (it == m_Slots.end())
        return TakeResult::NoItem;
    const ItemTableRow* row = ItemTable::Find(it->second->GetItemTableId());
    if (row == nullptr || !AuctionRule::Listable(*row))
        return TakeResult::NotListable;
    const CEquipmentItem* equipment = AsEquipment(it->second);
    if (ItemRules::IsBound(*row, equipment != nullptr ? equipment->GetExtra() : 0))
        return TakeResult::Bound;   // 귀속(획득 귀속 행 · 인스턴스 bit 8) — 경매에 올릴 수 없다
    if (equipment != nullptr && equipment->IsEquipped())
        return TakeResult::Equipped;
    if (count == 0 || count > it->second->GetCount())
        return TakeResult::BadCount;

    out.itemId = it->second->GetItemTableId();
    out.count = count;
    out.enchant = equipment != nullptr ? equipment->GetEnchant() : 0;
    out.extra = equipment != nullptr ? equipment->GetExtra() : 0;
    it->second->RemoveCount(count);
    if (it->second->GetCount() == 0)
    {
        m_Slots.erase(it);
        RefreshEquipped();
    }
    remain = DescribeSlot(slotIndex);
    NotifySlotChanged(slotIndex);
    return TakeResult::Ok;
}

void CInventory::RestoreTaken(UInt16 slotIndex, const TakenItem& item) noexcept
{
    AssertOwnedByCurrentZone();
    const ItemTableRow* row = ItemTable::Find(item.itemId);
    if (row == nullptr)
        return;
    const auto it = m_Slots.find(slotIndex);
    if (it == m_Slots.end())
    {
        // 원래 칸이 비어 있으면 그대로 되돌린다
        CItem::SharedPtr newItem = MakeNewItem(item.itemId, item.count);
        if (newItem == nullptr) return;
        newItem->SetOwnerID(m_wpOwner.lock()->GetObjID());
        if (CEquipmentItem* equipment = AsEquipment(newItem))
        {
            equipment->SetEnchant(item.enchant);
            equipment->SetExtra(item.extra);
        }
        m_Slots.emplace(slotIndex, std::move(newItem));
        RefreshEquipped();
        SaveSlotToDB(slotIndex);
        return;
    }
    if (it->second->GetItemTableId() == item.itemId && row->maxStack > 1 && it->second->GetCount() + item.count <= row->maxStack)
    {
        it->second->AddCount(item.count);
        SaveSlotToDB(slotIndex);
        return;
    }
    // 그 사이 칸이 다른 것으로 찼다 — 빈 곳 어디든(자리가 없으면 잃는다: 등록 트랜잭션 실패 + 가득 찬 가방이 겹칠 때만)
    (void)TryAddItem(item.itemId, item.count, nullptr, item.enchant, item.extra);
}

UInt16 CInventory::FindSlotForIncoming(UInt16 itemId, UInt16 count) const noexcept
{
    const ItemTableRow* row = ItemTable::Find(itemId);
    if (row == nullptr || row->type == ItemType::eCurrency)
        return MAX_INVENTORY_SLOTS;
    return FindSingleSlotFor(*row, count);
}

bool CInventory::PlaceIncoming(UInt16 slotIndex, const TakenItem& item, DB_ITEM_SLOT_SAVE& out) noexcept
{
    AssertOwnedByCurrentZone();
    if (IsSlotLocked(slotIndex)) return false;
    if (!IsValidSlotIndex(slotIndex))
        return false;
    const ItemTableRow* row = ItemTable::Find(item.itemId);
    if (row == nullptr)
        return false;
    const auto it = m_Slots.find(slotIndex);
    if (it != m_Slots.end())
    {
        if (it->second->GetItemTableId() != item.itemId || row->maxStack <= 1 || it->second->GetCount() + item.count > row->maxStack)
            return false;
        it->second->AddCount(item.count);
    }
    else
    {
        CItem::SharedPtr newItem = MakeNewItem(item.itemId, item.count);
        if (newItem == nullptr) return false;
        newItem->SetOwnerID(m_wpOwner.lock()->GetObjID());
        if (CEquipmentItem* equipment = AsEquipment(newItem))
        {
            equipment->SetEnchant(item.enchant);
            equipment->SetExtra(item.extra);
        }
        m_Slots.emplace(slotIndex, std::move(newItem));
        RefreshEquipped();
    }
    out = DescribeSlot(slotIndex);
    NotifySlotChanged(slotIndex);
    return true;
}

void CInventory::UndoIncoming(UInt16 slotIndex, const TakenItem& item) noexcept
{
    AssertOwnedByCurrentZone();
    const auto it = m_Slots.find(slotIndex);
    if (it == m_Slots.end() || it->second->GetItemTableId() != item.itemId)
        return;
    const UInt16 n = std::min<UInt16>(item.count, it->second->GetCount());
    it->second->RemoveCount(n);
    if (it->second->GetCount() == 0)
    {
        m_Slots.erase(it);
        RefreshEquipped();
    }
    SaveSlotToDB(slotIndex);
}
