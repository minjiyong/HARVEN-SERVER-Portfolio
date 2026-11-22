#pragma once
// 스냅샷 — 경제 묶음(탭 LootConst · TierLoot · EliteGear · BossLoot · CraftRecipe · Dismantle + Item의 라인업 색인).
// Bind_Econ.cpp가 채우고, 파사드 EconRule::*가 읽는다. 표마다 id · 배열로 바로 찾는다(핫 경로에 해시맵 없음).
#include <array>
#include <cstdint>
#include <vector>

#include "Database/GameData/EconRules.h"
#include "Database/GameData/Load/Canon_Econ.h"

struct SnapEcon
{
    Canon::LootConstV c{};

    // TierLoot — [1..5] = 단계(0은 빈 행: 범위 밖 자르기의 기준)
    std::array<EconRule::TierLootRow, EconRule::kTierCount + 1> tiers{};
    std::vector<uint32_t> tierSrc;                         // TierLoot 탭 행 번호(행 순서 = 단계 순) — 검증 위치

    // EliteGear — (tier, seq) 순. eliteStart[t] ~ eliteStart[t + 1] = 단계 t의 행
    std::vector<EconRule::EliteGearRow> elite;
    std::vector<uint32_t> eliteSrc;                        // elite와 같은 순서 — EliteGear 탭 행 번호(검증 위치)
    std::array<uint32_t, EconRule::kTierCount + 2> eliteStart{};

    // BossLoot — (species, seq) 순. bossStart[species] ~ bossStart[species + 1](species < bossStart.size() - 1)
    std::vector<EconRule::BossLootRow> boss;
    std::vector<uint32_t> bossSrc;                         // boss와 같은 순서 — BossLoot 탭 행 번호
    std::vector<uint32_t> bossStart;

    // CraftRecipe — 탭 행 순서. recipeAt[등급][단][부위] = recipes 위치 + 1(0 = 없음)
    std::vector<EconRule::CraftRecipeRow> recipes;
    std::array<std::array<std::array<uint16_t, static_cast<size_t>(CraftPart::Count)>, EconRule::kRankCount + 1>, kItemGradeCount> recipeAt{};

    // Dismantle — 행 i = 등급 i(구조: 행 수 = 등급 수)
    std::array<EconRule::DismantleRow, kItemGradeCount> dismantle{};
    size_t dismantleRows = 0;

    // 라인업 색인 — 등급 × 단(0 = 모두) × 경로 비트 → 장비 id(id 순). 칸마다 [start, end)를 pool에.
    static constexpr size_t kCells = static_cast<size_t>(kItemGradeCount) * (EconRule::kRankCount + 1) * EconRule::kRouteBits;
    static constexpr size_t kClasses = static_cast<size_t>(ClassId::Count);
    std::vector<ItemTableId> lineupPool;
    std::array<uint32_t, kCells + 1> lineupStart{};
    std::vector<ItemTableId> weaponPool;                   // 직업 × 칸
    std::array<uint32_t, kClasses * kCells + 1> weaponStart{};

    [[nodiscard]] static constexpr size_t Cell(ItemGrade g, uint8_t rank, int routeIndex) noexcept
    {
        return (static_cast<size_t>(g) * (EconRule::kRankCount + 1) + rank) * EconRule::kRouteBits + static_cast<size_t>(routeIndex);
    }
};
