#pragma once

#include "Base/Core/CoreTLS.h"
#include "Database/GameData/MonsterAITypes.h"

class CEntityBehavior;

// ─────────────────────────────────────────────────────────────────────────────
// CMonsterAgent — 행동 트리를 한 번 돌리는 동안의 문맥(BT_Tree<CMonsterAgent>의 Agent).
// 잎(T_)은 이것으로 몬스터 · 행동 · AI 표를 보고, 다음에 깨워 달라는 시각을 남긴다(가장 이른 것 하나).
// 한 번 돌리고 버린다(스택) — 오래 들고 있지 않는다.
// ─────────────────────────────────────────────────────────────────────────────
struct CMonsterAgent
{
    const MonsterSharedPtr& monster;
    CEntityBehavior&        brain;
    const MonsterAiRow*     ai;          // nullptr = 표에 없는 종족(근접 · 기술 없음)
    UInt32                  now;
    UInt32                  wakeAt = 0;
    bool                    hasWake = false;

    [[nodiscard]] UInt32 Now() const noexcept { return now; }

    void WakeAt(UInt32 at) noexcept
    {
        if (!hasWake || static_cast<Int32>(at - wakeAt) < 0)
        {
            wakeAt = at;
            hasWake = true;
        }
    }
    void WakeIn(UInt32 ms) noexcept { WakeAt(now + ms); }

    [[nodiscard]] UInt32 Random(UInt32 bound) const { return bound == 0 ? 0 : static_cast<UInt32>(LRng() % bound); }

    [[nodiscard]] MonsterAiType Type() const noexcept { return ai != nullptr ? ai->ai : MonsterAiType::Melee; }
    [[nodiscard]] UInt8 KeepRange() const noexcept { return ai != nullptr ? ai->keepRange : 0; }
};

// 시각 비교(GetNowTime은 49.7일마다 한 바퀴 — 차이로 본다)
[[nodiscard]] inline bool TimeReached(UInt32 now, UInt32 at) noexcept { return static_cast<Int32>(now - at) >= 0; }
