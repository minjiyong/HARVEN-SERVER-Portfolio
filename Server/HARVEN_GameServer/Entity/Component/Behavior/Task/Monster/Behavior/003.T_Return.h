#pragma once

#include "Base/BehaviorTree/BT_Task.h"

struct CMonsterAgent;

namespace Behavior::Monster
{
    // 귀환 중인가(조건)
    struct C_Returning
    {
        static bool Check(CMonsterAgent& a, const BT_Param& p);
    };

    // 귀환 — 스폰 지점까지 이동 간격마다 한 걸음. 닿으면 체력을 채우고(멀리 끌고 가서 조금씩 깎는 리쉬 악용 막기) 쉰다.
    // 길이 막혀 닿지 못하면 그 자리에서 쉰다(예전 Deactivate와 같다).
    struct T_ReturnHome
    {
        static constexpr const char* kName = "T_ReturnHome";
        static EnumTaskStatus OnUpdate(CMonsterAgent& a, BT_Frame& f);
    };
}
