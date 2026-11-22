#pragma once
// 스냅샷 — 기술성장 묶음(탭 GrowthTree · GrowthVariant · GrowthConst).
// Bind_Growth.cpp가 채우고, 파사드 SkillGrowth::FindTree · TierLevel · DamagePerLevel이 읽는다.
#include <array>
#include <cstdint>
#include <vector>

#include "Database/GameData/SkillGrowth.h"

struct SnapGrowth
{
    struct VariantRow
    {
        uint16_t id = 0;
        SkillGrowth::Variant v{};
    };

    // GrowthVariant — 파일 순서(정본 = id 오름차순). 시트의 행 그대로(덤프 · 검증 위치)
    std::vector<VariantRow> variants;
    std::vector<int32_t> variantIndex;              // 갈래 id → variants 행(-1 없음)

    // GrowthTree — 파일 순서(정본 = skill_id 오름차순). Tree는 이관 전과 같은 모양(갈래 값 복사)
    std::vector<SkillGrowth::Tree> trees;
    std::vector<std::array<uint16_t, 4>> treeRefs;  // trees와 같은 순서 — a1 · a2 · b1 · b2의 갈래 id(덤프)
    std::vector<int32_t> treeIndex;                 // skill_id → trees 행(-1 없음, 처음 나온 행 = 이관 전 순차 검색)

    // GrowthConst(한 행)
    uint8_t tierLevel[2] = { 0, 0 };
    uint32_t damagePerLevel = 0;
};
