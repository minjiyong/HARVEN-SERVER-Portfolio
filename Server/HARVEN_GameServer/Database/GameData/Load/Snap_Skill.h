#pragma once
// 스냅샷 — 스킬 묶음(탭 Skill · SkillAction · SkillHit · Status · Combo · SkillKeyMap).
// Bind_Skill.cpp가 채우고, 파사드 SkillTable · SkillDefTable · StatusTable · ComboTable이 읽는다.
#include <array>
#include <cstdint>
#include <vector>

#include "Database/GameData/CombatData.h"
#include "Database/GameData/SkillData.h"

struct SnapSkill
{
    static constexpr int kKeyMapClasses = 3;   // SkillDefTable::Default의 직업 범위(이관 전 kMap[3][kSkillKeys])

    std::vector<SkillRow> rows;                // Skill — 행 i = id i(R003: 0부터 연속, 0 = 빈 행)
    std::vector<StatusRow> status;             // Status — 행 i = StatusId i(R001: 행 수 = StatusId::Count)
    std::vector<const char*> combo;            // Combo — 행 i = id i(0 = 빈 이름)

    std::vector<SkillDef> defs;                // SkillAction(+ SkillHit 타격) — 파일 순서(정본 = id 오름차순)
    std::vector<int32_t> defIndex;             // id → defs 행(-1 없음). 같은 id가 두 번이면 로드 실패(이관 전 순차 검색 = 처음 맞는 행)
    std::vector<std::array<int32_t, 8>> hitRow; // defs와 같은 순서 — 타격 j의 SkillHit 행 번호(검증 위치)

    std::array<std::array<uint16_t, kSkillKeys>, kKeyMapClasses> keyMap{};      // SkillKeyMap (직업, 칸) → 기술 id
    std::array<std::array<int32_t, kSkillKeys>, kKeyMapClasses> keyMapRow{};    // (직업, 칸) → SkillKeyMap 행 번호(검증 위치)
};
