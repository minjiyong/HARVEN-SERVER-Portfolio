#pragma once
// 스냅샷 — 몬스터 AI 묶음(탭 MonsterSkill · MonsterSkillHit · MonsterAoE · MonsterAI · MonsterAISkill).
// Bind_MonsterAI.cpp가 채우고, 파사드 MonsterSkillTable · MonsterAoeTable · MonsterAiTable이 읽는다.
#include <array>
#include <cstdint>
#include <vector>

#include "Database/GameData/MonsterAITypes.h"

struct SnapMonsterAI
{
    std::vector<MonsterSkillRow> skills;      // 행 i = id i + 1(R105: 1부터 연속) — hits[]는 자식 탭 MonsterSkillHit
    std::vector<MonsterAoeRow> aoes;          // 행 i = id i + 1(R105)
    std::vector<MonsterAiRow> ais;            // MonsterAI 행(종족 id 순) — skills[]는 자식 탭 MonsterAISkill
    std::vector<int32_t> aiIndex;             // 종족 id → ais 칸(-1 = 행 없음) — 핫 경로에 해시맵 없이 바로 색인
    uint8_t maxAssistRange = 0;               // 모든 종족의 assist_range 중 가장 큰 값(0 = 돕는 종족이 없다 — 도움 요청이 주변을 훑지 않는다)
    // 검증 · 덤프가 오류 위치를 시트 행으로 찍게 — 자식 탭의 행 번호
    std::vector<std::array<int32_t, kMonsterSkillHits>> hitRow;   // skills와 같은 순서
    std::vector<std::array<int32_t, kMonsterAiSkills>> slotRow;   // ais와 같은 순서
};
