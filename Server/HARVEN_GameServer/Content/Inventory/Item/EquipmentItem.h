#pragma once

#include "Content/Inventory/Item/Item.h"

// 장착 가능한 아이템. CItem 대비 추가 상태는 "지금 장착 중인가" 플래그, 강화 수치, V2 인스턴스 값이다.
class CEquipmentItem : public CItem
{
public:
    using SharedPtr = shared_ptr<CEquipmentItem>;
    using WeakPtr   = weak_ptr<CEquipmentItem>;

public:
    CEquipmentItem() noexcept : CItem(true) {}

    [[nodiscard]] bool IsEquipped() const noexcept { return m_bEquipped; }
    void Equip() noexcept   { m_bEquipped = true; }
    void Unequip() noexcept { m_bEquipped = false; }

    // 모든 착용 장비의 강화 수치. 게임상 상한이 없는 UInt32 인스턴스 상태로,
    // 아이템 자신이 들고 다닌다 — 바닥에 버렸다가 다시 주워도 유지된다.
    [[nodiscard]] UInt32 GetEnchant() const noexcept { return m_Enchant; }
    void SetEnchant(UInt32 enchant) noexcept { m_Enchant = enchant; }

    // V2 인스턴스 값(희귀도 · 옵션 · 특수 효과 — Data/ItemRules.h ItemInstance). 강화 수치처럼
    // 아이템 자신이 들고 다닌다 — 버렸다 주워도, 슬롯을 옮겨도 따라간다. 0 = 굴린 적 없음.
    [[nodiscard]] UInt64 GetExtra() const noexcept { return m_Extra; }
    void SetExtra(UInt64 extra) noexcept { m_Extra = extra; }

private:
    bool     m_bEquipped = false;
    UInt32  m_Enchant  = 0;
    UInt64 m_Extra    = 0;
};

// CItem → 장비(아니면 nullptr). dynamic_pointer_cast 대신 쓴다 — 형 태그만 보고 참조 수는 건드리지 않는다.
// 돌려준 포인터는 넘긴 아이템이 살아 있는 동안만 쓴다(인벤토리 칸 · 바닥 · 지역 변수가 붙잡고 있는 동안).
[[nodiscard]] inline CEquipmentItem* AsEquipment(CItem* item) noexcept
{
    return (item != nullptr && item->IsEquipment()) ? static_cast<CEquipmentItem*>(item) : nullptr;
}
[[nodiscard]] inline CEquipmentItem* AsEquipment(const CItem::SharedPtr& item) noexcept
{
    return AsEquipment(item.get());
}
