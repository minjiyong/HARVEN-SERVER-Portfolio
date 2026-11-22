#pragma once
// 상점 표(아이템 묶음 — 탭 Shop).
// WorldData.h에서 나눴다(내용 이동만 — 이관 Phase 1). 우산 헤더 Data/WorldData.h가 원래 순서로 include한다.
#include <cstddef>
#include <cstdint>
#include <span>
#include "Database/GameData/World/WorldNpc.h"

namespace WorldData
{
    // ── 상점: NPC별 판매 목록(아이템 id). 매입은 상인(eMerchant/eWeaponSmith) 누구나 한다.
    struct ShopRow
    {
        uint16_t npcId;
        uint16_t items[16];  // 0 = 빈 칸
    };

    // 값은 GameData/Shop.json(시트 Shop 탭 — npc_id · seq · item_id, NPC마다 최대 16칸: items[16]이 구조 상한 — 넘으면 로드 실패).
    // 구현은 Data/Load/Bind_Item.cpp(스냅샷이 NPC마다 ShopRow 하나로 묶어 둔다 — 빈 칸은 0, 뒤에만).
    [[nodiscard]] bool ShopSells(uint16_t npcId, uint16_t itemId) noexcept;

    // 상점 표 전체(NPC마다 한 줄, npc_id 순) — 예전 kShops 배열 대신(클라 상점 창)
    [[nodiscard]] std::span<const ShopRow> Shops() noexcept;

    [[nodiscard]] constexpr bool NpcBuysItems(const NpcRow& npc) noexcept
    {
        return npc.kind == NpcKind::eMerchant || npc.kind == NpcKind::eWeaponSmith;
    }
}
