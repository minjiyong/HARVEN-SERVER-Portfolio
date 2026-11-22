#pragma once

#include "Content/Inventory/Item/Item.h"

// 소비 아이템 — 현재는 CItem 대비 추가 필드/동작이 없다. equipped 같은 장비
// 전용 상태를 갖지 않는다는 것 자체가 CEquipmentItem과 타입으로 구분되는 지점.
// 실제 소비 효과(회복량 등)는 나중에 필요해지면 이 클래스에 추가한다.
class CConsumableItem : public CItem
{
public:
    using SharedPtr = shared_ptr<CConsumableItem>;
    using WeakPtr   = weak_ptr<CConsumableItem>;

    CConsumableItem() noexcept : CItem(false) {}
};

// CItem → 소비 아이템(아니면 nullptr). 맨 CItem은 만들 수 없으므로(Item.h) 장비가 아니면 소비 아이템이다.
[[nodiscard]] inline CConsumableItem* AsConsumable(const CItem::SharedPtr& item) noexcept
{
    return (item != nullptr && !item->IsEquipment()) ? static_cast<CConsumableItem*>(item.get()) : nullptr;
}
