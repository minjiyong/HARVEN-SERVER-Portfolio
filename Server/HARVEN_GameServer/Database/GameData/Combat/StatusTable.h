#pragma once
// 상태이상 표(스킬 묶음 — 탭 Status) — 파사드. 행은 GameData에서 온다.
// CombatData.h에서 나눴다(내용 이동만 — 이관 Phase 1). 우산 헤더 Data/CombatData.h가 원래 순서로 include한다.
#include <cstddef>
#include <cstdint>
#include "Database/GameData/Combat/CombatIds.h"

struct StatusRow
{
    StatusId    id;
    const char* name;
    bool        debuff;
    bool        blocksMove;
    bool        blocksAttack;
    uint8_t     maxStacks;
    uint16_t    tickMs;        // 0 = 지속 피해 없음
    uint16_t    tickPercent;   // 틱 피해 = 거는 사람 공격력 × % (중첩마다)
    uint16_t    slowPercent;   // 이동속도 감소 %
    uint32_t    color;         // 클라 표시
};

namespace StatusTable
{
    // 값은 GameData/Status.json(시트 HARVEN_DATA의 Status 탭) — 부팅 때 로더가 불변 스냅샷에 둔다(Data/Load/Bind_Skill.cpp).
    // 규칙(예전 static_assert → 부팅 검증 R001): 행 수 = StatusId::Count, 행 순서 = id.

    // id → 행. 범위 밖이면 None 행(이관 전과 같다).
    [[nodiscard]] const StatusRow& Get(StatusId id) noexcept;
}
