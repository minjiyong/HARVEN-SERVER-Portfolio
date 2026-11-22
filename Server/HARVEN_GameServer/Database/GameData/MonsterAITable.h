#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "Database/GameData/MonsterAITypes.h"

// ─────────────────────────────────────────────────────────────────────────────
// 몬스터 AI · 기술 · 장판 표 — 파사드. 값은 GameData/MonsterAI.json(+ MonsterAISkill) · MonsterSkill.json(+ MonsterSkillHit) ·
// MonsterAoE.json(시트 HARVEN_DATA의 같은 이름 표)에서 온다. 부팅 때 GameData::LoadFromDir가 불변 스냅샷에 둔다(Data/Load/Bind_MonsterAI.cpp).
//
// 규칙(부팅 검증 R105~R112 — Shared/Document/DATA_RULES.md): MonsterSkill · MonsterAoE id는 1부터 연속 · 거리 · 단계 범위 ·
// 참조(기술 · 장판 · 종족) · 장판 틱 바닥 · 오라는 따라다니는 장판 · 강화 세기 상한.
// 핫 리로드(--dev)는 값만 바뀐 판을 받는다 — 행동 트리는 종류(ai)마다 코드에 있고 기술 고르기는 매번 표를 읽어 새 값이 바로 쓰인다.
// ─────────────────────────────────────────────────────────────────────────────
namespace MonsterSkillTable
{
    [[nodiscard]] std::span<const MonsterSkillRow> Rows() noexcept;
    // id → 행(없으면 nullptr). id가 1부터 연속이라 인덱스로 바로 찾는다(O(1)).
    [[nodiscard]] const MonsterSkillRow* Find(uint16_t id) noexcept;
}

namespace MonsterAoeTable
{
    // 틱 간격의 바닥(ms) — 표는 R109가 막고, 틱을 거는 곳(AreaOfEffect.cpp)도 이 값으로 자른다
    inline constexpr uint16_t kMinIntervalMs = 200;
    // 굳건(받는 피해 감소)의 상한(%) — 100이면 죽지 않는다
    inline constexpr uint16_t kMaxFortifyPower = 90;

    [[nodiscard]] std::span<const MonsterAoeRow> Rows() noexcept;
    [[nodiscard]] const MonsterAoeRow* Find(uint16_t id) noexcept;
}

namespace MonsterAiTable
{
    [[nodiscard]] std::span<const MonsterAiRow> Rows() noexcept;
    // 종족 id → 행. 행이 없는 종족은 nullptr — 근접 · 기술 없음 · 선공이면 감지 3칸(예전 WAKE_RANGE)으로 돈다.
    [[nodiscard]] const MonsterAiRow* Find(uint16_t monsterId) noexcept;
    // 모든 종족의 assist_range 중 가장 큰 값 — 도움 요청이 이 반경만 훑는다(0이면 훑지 않는다)
    [[nodiscard]] uint8_t MaxAssistRange() noexcept;
}
