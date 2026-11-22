#include "pch.h"
#include "Content/Inventory/Item/Item.h"

#include "Content/Inventory/Item/ConsumableItem.h"
#include "Content/Inventory/Item/EquipmentItem.h"
#include "Database/GameData/Item/ItemTable.h"

void CItem::InitInstance()
{
    CGameObject::InitInstance();
}

CItem::SharedPtr MakeNewItem(ItemTableId itemId, UInt16 count)
{
    const ItemTableRow* row = ItemTable::Find(itemId);
    if (row == nullptr)
        return nullptr;

    CItem::SharedPtr item = (row->type == ItemType::eEquipment)
        ? static_pointer_cast<CItem>(std::make_shared<CEquipmentItem>())
        : static_pointer_cast<CItem>(std::make_shared<CConsumableItem>());

    item->InitInstance();
    item->SetItemTableId(itemId);
    item->SetCount(count);
    // SUBJECT_ADD_NFY 등 일반 CSubject 표시 경로가 이름을 그대로 쓸 수 있도록
    // 항상 채워둔다(인벤토리 안에 있을 때는 안 쓰이지만, 필드에 떨어지면 바로 필요해진다).
    item->SetName(row->name);

    return item;
}
