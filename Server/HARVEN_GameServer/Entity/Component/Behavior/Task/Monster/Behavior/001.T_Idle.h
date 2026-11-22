#pragma once

#include "Base/BehaviorTree/BT_Task.h"

struct CMonsterAgent;

namespace Behavior::Monster
{
    // 대상이 없을 때(REIW Idle_Aggressive · Idle_Passive의 T_Idle).
    //  · 선공 · 보스 · 고정 몬스터: 곧바로 잠든다(ForceShutdown) — 다가오는 플레이어가 Wake로 다시 깨운다(예전 선공 몬스터와 같다).
    //  · 비선공: 보는 플레이어가 있으면 이동 간격마다 한 걸음 배회하고, 아무도 안 보면 잠든다(예전 EV_RANOM_MOVE 루프).
    struct T_Idle
    {
        static constexpr const char* kName = "T_Idle";
        static EnumTaskStatus OnUpdate(CMonsterAgent& a, BT_Frame& f);
    };
}
