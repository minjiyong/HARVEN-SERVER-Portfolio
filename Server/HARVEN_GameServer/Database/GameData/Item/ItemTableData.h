#pragma once

#include <cstddef>
#include <span>

#include "Database/GameData/Item/ItemTableRow.h"
#include "Database/GameData/Gen/ItemIds.h"   // ItemTableData::Id::* — Item.code_name에서 생성(datatool export)

// ─────────────────────────────────────────────────────────────────────────────
// 아이템 테이블 — 파사드. 값은 GameData/Item.json(시트 HARVEN_DATA의 Item 탭)에서 온다.
// 서버 부팅 때 GameData::LoadFromDir가 읽어 불변 스냅샷에 둔다(Data/Load/Bind_Item.cpp).
//
// 서버 코드는 ItemTable::Find()로 조회한다(이 표를 직접 순회/수정하는 서버 코드를 만들지 않는다 — 조회 지점을 하나로).
// 규칙(예전 static_assert → 부팅 검증): 장비 V2 필드(R082~R085) · id ≠ 0 · 표시 이름 · 장비 겹치기 1 · 매입가 ≤ 판매가 · id 중복 없음(R087~R091).
// 새 아이템은 끝에만 추가하고, 이미 등록된 id는 절대 바꾸거나 재사용하지 않는다 — Inventory DB가 id를 영구 식별자로 저장한다(ids.lock).
//
// attackBonus는 무기 공격력 — 희귀도 배율 · 강화 보너스와 함께 전투 능력치에 더해진다(Data/ItemRules.h Accumulate).
// grade(예전 등급)는 V2에서 "이 물건이 적어도 이 희귀도로 나온다"는 바닥이 된다
// (Data/ItemRules.h FloorRarity) — 없음=일반, 레어=고급, 에픽=희귀, 유니크=영웅, 전설=전설.
// 지팡이의 attackBonus는 마법 공격력으로 들어간다(WeaponKind::Staff).
// ─────────────────────────────────────────────────────────────────────────────
namespace ItemTableData
{
    // 직업 비트(classMask)
    inline constexpr uint8_t kMaskKnight = 1 << 0;
    inline constexpr uint8_t kMaskRune   = 1 << 1;
    inline constexpr uint8_t kMaskHunter = 1 << 2;

    // 모든 행(id 순서). 값은 GameData에서.
    [[nodiscard]] std::span<const ItemTableRow> Rows() noexcept;
    [[nodiscard]] size_t RowCount() noexcept;

    // id → 행(없으면 nullptr). 스냅샷의 id 색인으로 바로 찾는다(O(1)).
    [[nodiscard]] const ItemTableRow* Find(ItemTableId id) noexcept;

    // 장비 부위. 예전 행(equipSlot을 적지 않은 장비)은 무기다.
    [[nodiscard]] constexpr EquipSlot SlotOf(const ItemTableRow& row) noexcept
    {
        if (row.type != ItemType::eEquipment)
            return EquipSlot::None;
        return row.equipSlot == EquipSlot::None ? EquipSlot::Weapon : row.equipSlot;
    }

    [[nodiscard]] inline EquipSlot SlotOf(ItemTableId id) noexcept
    {
        const ItemTableRow* row = Find(id);
        return row != nullptr ? SlotOf(*row) : EquipSlot::None;
    }

    // 무기(각인 두루마리 대상 · 손에 드는 외형)
    [[nodiscard]] inline bool IsWeapon(ItemTableId id) noexcept
    {
        return SlotOf(id) == EquipSlot::Weapon;
    }

    // 지팡이(마법 무기) — 공격력 보너스가 마법 공격력으로 들어간다.
    [[nodiscard]] inline bool IsStaff(ItemTableId id) noexcept
    {
        const ItemTableRow* row = Find(id);
        return row != nullptr && row->weaponKind == WeaponKind::Staff;
    }

    // 직업 제한. mask 0 = 모두.
    [[nodiscard]] constexpr bool ClassCanUse(const ItemTableRow& row, uint8_t classId) noexcept
    {
        return row.classMask == 0 || (classId < 8 && (row.classMask & (1u << classId)) != 0);
    }
}
