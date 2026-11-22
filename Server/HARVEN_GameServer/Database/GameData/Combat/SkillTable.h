#pragma once
// 연계 이름 · 기술 표(스킬 묶음 — 탭 Combo · Skill) — 파사드. 행은 GameData에서 온다.
// CombatData.h에서 나눴다(내용 이동만 — 이관 Phase 1). 우산 헤더 Data/CombatData.h가 원래 순서로 include한다.
#include <cstddef>
#include <cstdint>
#include "Database/GameData/Combat/SkillTypes.h"
#include "Database/GameData/Combat/ClassTable.h"

// 연계 이름(클라 표시용, comboId → 이름) — 값은 GameData/Combo.json(탭 Combo, 행 순서 = id, 0 = 빈 이름).
namespace ComboTable
{
    // 행 수(이관 전 kCount — uint8_t). 구조: 255행을 넘으면 로드 실패(combo_id u8).
    [[nodiscard]] uint8_t Count() noexcept;
    [[nodiscard]] const char* Name(uint8_t id) noexcept;   // 범위 밖이면 ""
}

// 기술 표 — 파사드. 값은 GameData/Skill.json(시트 HARVEN_DATA의 Skill 탭) — 부팅 때 로더가 불변 스냅샷에 둔다(Data/Load/Bind_Skill.cpp).
// 규칙(예전 static_assert(Detail::Validate()) → 부팅 검증): id = 행 순서(0부터 연속, 0 = 빈 행 — R003) ·
// Class의 skill_0~5가 있는 기술 · 같은 직업 · slot = 열 번호(R004). id는 DB · 패킷에 저장된다 — 영구(ids.lock).
namespace SkillTable
{
    using T = SkillTarget;
    using S = SkillShape;
    using K = DamageKind;
    using St = StatusId;

    // 행 수(0번 빈 행 포함 — 이관 전 kCount)
    [[nodiscard]] uint16_t Count() noexcept;

    // id → 행(0 · 범위 밖 · id가 행 순서와 다르면 nullptr). O(1)
    [[nodiscard]] const SkillRow* Find(uint16_t id) noexcept;

    // 기본 공격 간격(ms) — 공격속도 %
    [[nodiscard]] inline uint32_t BasicIntervalMs(ClassId cls, uint16_t attackSpeed) noexcept
    {
        const uint32_t base = ClassTable::Get(cls).basicIntervalMs;
        const uint32_t spd = attackSpeed < 40 ? 40 : attackSpeed;
        return base * 100u / spd;
    }

    // 실제 쿨다운(ms) — 쿨다운 감소 %
    [[nodiscard]] constexpr uint32_t CooldownMs(const SkillRow& row, uint16_t cdr) noexcept
    {
        const uint32_t r = cdr > 50 ? 50 : cdr;
        return row.cooldownMs * (100u - r) / 100u;
    }
}
