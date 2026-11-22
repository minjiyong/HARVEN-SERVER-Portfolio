#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "Database/GameData/CombatData.h"          // ClassId
#include "Database/GameData/Item/ItemTableData.h"  // SlotOf · ClassCanUse

// ─────────────────────────────────────────────────────────────────────────────
// 경제 1차 규칙 — 장비 굴림(필드 · 정예 · 보스) · 제작 · 분해의 시트 값. 서버 · 클라 공용.
//
// 값은 시트 econ 묶음(LootConst · TierLoot · EliteGear · BossLoot · CraftRecipe · Dismantle → Bin/GameData)과
// Item 표의 rank · route · bind 열에서 온다. 구현(GD()로 읽기)은 Database/GameData/Load/Bind_Econ.cpp,
// 라인업 색인(등급 · 단 · 경로 → 장비 목록)은 로드 때 미리 만든다 — 핫 경로에 해시맵 · 순회가 없다.
// 굴림 · 지급 흐름은 Content 쪽(CRewardHelper::HandleKill · 제작)이 이 함수들로 짠다. 설계: Shared/Document/ECONOMY.md.
// ─────────────────────────────────────────────────────────────────────────────
namespace EconRule
{
    // ── 구조 상수 ──
    inline constexpr int kTierCount   = 5;   // 지역 단계 1~5(Region.tier)
    inline constexpr int kRankCount   = 3;   // 단 1 하 · 2 중 · 3 상
    inline constexpr int kCraftInputs = 4;   // 레시피 재료 칸
    inline constexpr int kRouteBits   = 6;   // ItemRoute 비트 수(Shop ~ Quest)

    // ── LootConst(한 행) ──
    [[nodiscard]] uint16_t RarePity() noexcept;               // 필드 장비 N개 동안 희귀가 없으면 다음은 희귀
    [[nodiscard]] uint16_t HeroicPity() noexcept;             // 정예 · 보스 장비 N개 동안 영웅이 없으면 다음은 영웅
    [[nodiscard]] uint8_t  ClassSwapPercent() noexcept;       // 남의 직업 무기 → 자기 직업 무기로 바꿀 확률(%)
    [[nodiscard]] uint8_t  LevelGap() noexcept;               // 이 레벨 차 이상 낮은 몬스터는 정수 · 결정 확률 절반
    [[nodiscard]] uint16_t ElitePermille() noexcept;          // 스폰 · 재등장마다 정예 확률(‰)
    [[nodiscard]] uint8_t  EliteMinTier() noexcept;           // 이 단계 이상 지역에서만 정예
    [[nodiscard]] uint16_t EliteHpPercent() noexcept;
    [[nodiscard]] uint16_t EliteAttackPercent() noexcept;
    [[nodiscard]] uint8_t  EliteLevelBonus() noexcept;        // 정예 방어 · 회피 · 경직 계산 레벨 가산
    [[nodiscard]] uint16_t EliteExpPercent() noexcept;
    [[nodiscard]] uint16_t EliteAdenaPercent() noexcept;
    [[nodiscard]] uint8_t  EliteSharePercent() noexcept;      // 정예 보상 자격: 피해 N% 이상
    [[nodiscard]] uint8_t  BossSharePercent() noexcept;       // 보스 개인 굴림 자격: 피해 N% 이상(또는 그런 사람의 파티원)
    [[nodiscard]] ItemTableId ShardItem() noexcept;           // 장비 파편
    [[nodiscard]] uint8_t  TradedDismantlePercent() noexcept; // 거래 이력 장비 분해량(%)

    // ── TierLoot(단계 1~5) ──
    struct TierLootRow
    {
        uint8_t     tier = 0;
        uint16_t    commonBp = 0, uncommonBp = 0, rareBp = 0;   // 만분율(일반 몬스터 처치당)
        ItemTableId essenceItem = ITEM_TABLE_ID_NONE;
        uint16_t    essencePermille = 0;
        ItemTableId crystalItem = ITEM_TABLE_ID_NONE;           // 0 = 없음
        uint16_t    crystalPermille = 0;
        ItemTableId eliteCrystalItem = ITEM_TABLE_ID_NONE;      // 0 = 없음
        uint8_t     eliteCrystalCount = 0;
        uint8_t     eliteEssenceCount = 0;
        uint8_t     eliteGearPercent = 0;
    };
    [[nodiscard]] const TierLootRow& Tier(int tier) noexcept;   // 1..5로 자른다

    // ── EliteGear(단계마다 등급 · 단 가중치) ──
    struct EliteGearRow
    {
        uint8_t   tier = 0, seq = 0;
        ItemGrade grade = ItemGrade::eNone;
        uint8_t   rank = 0;
        uint16_t  weight = 0;
    };
    [[nodiscard]] std::span<const EliteGearRow> EliteGear(int tier) noexcept;   // 그 단계 행(없으면 빈 것)

    // ── BossLoot(보스 개인 굴림) ──
    struct BossLootRow
    {
        uint16_t    species = 0;
        uint8_t     seq = 0;
        ItemTableId itemId = ITEM_TABLE_ID_NONE;   // 0 = 없음
        uint16_t    weight = 0;
    };
    [[nodiscard]] std::span<const BossLootRow> BossLoot(uint16_t species) noexcept;   // 없으면 빈 것

    // ── 제작(CraftRecipe) ──
    // 부위 묶음: 무기(equip_slot Weapon 또는 None + 장비) · 방어구(Helmet ~ Cloak) · 장신구(Necklace · Ring). 장비가 아니면 CraftPart::Count.
    [[nodiscard]] constexpr CraftPart PartOf(const ItemTableRow& row) noexcept
    {
        switch (ItemTableData::SlotOf(row))
        {
        case EquipSlot::Weapon:   return CraftPart::eWeapon;
        case EquipSlot::Helmet:
        case EquipSlot::Armor:
        case EquipSlot::Gloves:
        case EquipSlot::Boots:
        case EquipSlot::Cloak:    return CraftPart::eArmor;
        case EquipSlot::Necklace:
        case EquipSlot::Ring:     return CraftPart::eAccessory;
        default:                  return CraftPart::Count;
        }
    }
    struct CraftInput
    {
        ItemTableId item = ITEM_TABLE_ID_NONE;   // 0 = 빈 칸
        uint16_t    count = 0;
    };
    struct CraftRecipeRow
    {
        uint16_t  id = 0;
        ItemGrade grade = ItemGrade::eNone;
        uint8_t   rank = 0;
        CraftPart part = CraftPart::eWeapon;
        uint32_t  fee = 0;   // 그로시
        std::array<CraftInput, kCraftInputs> in{};
    };
    // route에 Craft가 있고 (등급, 단, 부위 묶음) 레시피가 있을 때만(아니면 nullptr)
    [[nodiscard]] const CraftRecipeRow* RecipeFor(const ItemTableRow& row) noexcept;
    // (등급, 단, 부위 묶음)의 레시피(route와 상관없이 — 검증 · 표시용, 없으면 nullptr)
    [[nodiscard]] const CraftRecipeRow* RecipeAt(ItemGrade grade, uint8_t rank, CraftPart part) noexcept;

    // ── 분해(Dismantle — 등급마다 한 행) ──
    struct DismantleRow
    {
        ItemGrade   grade = ItemGrade::eNone;
        uint16_t    shardCount = 0;
        ItemTableId itemId = ITEM_TABLE_ID_NONE;   // 0 = 없음
        uint16_t    itemCount = 0;
    };
    // 분해 불가(행의 파편 · 재료가 모두 0 — 신화)거나 없는 등급이면 nullptr
    [[nodiscard]] const DismantleRow* DismantleFor(ItemGrade grade) noexcept;
    // 거래 이력 장비의 분해량 — (n × traded_dismantle_percent + 99) / 100(올림)
    [[nodiscard]] inline uint16_t TradedAmount(uint16_t n) noexcept
    {
        return static_cast<uint16_t>((static_cast<uint32_t>(n) * TradedDismantlePercent() + 99u) / 100u);
    }

    // ── 라인업 색인(로드 때 미리 만든다 — Item 행 id 순) ──
    [[nodiscard]] inline bool HasRoute(const ItemTableRow& r, ItemRoute x) noexcept
    {
        return (r.route & static_cast<uint8_t>(x)) != 0;
    }
    // 장비 중 등급 · 단(0 = 모든 단) · route에 그 비트가 있는 것(route는 비트 하나 — 여럿이거나 0이면 빈 것)
    [[nodiscard]] std::span<const ItemTableId> Items(ItemGrade grade, uint8_t rank, ItemRoute route) noexcept;
    // 위 중 그 직업이 쓸 수 있는 무기(class_mask가 그 직업 — 0 = 모두 포함)
    [[nodiscard]] std::span<const ItemTableId> ClassWeapons(ClassId cls, ItemGrade grade, uint8_t rank, ItemRoute route) noexcept;

    // 비트 하나짜리 route → 0..5(아니면 -1)
    [[nodiscard]] constexpr int RouteIndex(ItemRoute route) noexcept
    {
        const unsigned v = static_cast<uint8_t>(route);
        if (v == 0 || (v & (v - 1)) != 0)
            return -1;
        int i = 0;
        while (((v >> i) & 1u) == 0) ++i;
        return i < kRouteBits ? i : -1;
    }
    static_assert(RouteIndex(ItemRoute::Shop) == 0 && RouteIndex(ItemRoute::Quest) == 5 && RouteIndex(static_cast<ItemRoute>(3)) == -1);
}
