#include "pch.h"
#include "Entity/Component/Behavior/Task/Monster/MonsterBehaviorHelper.h"

#include "Entity/Component/Behavior/Task/Monster/MonsterAgent.h"
#include "Entity/Component/Behavior/Task/Monster/Behavior/001.T_Idle.h"
#include "Entity/Component/Behavior/Task/Monster/Behavior/002.T_Combat.h"
#include "Entity/Component/Behavior/Task/Monster/Behavior/003.T_Return.h"
#include "Entity/Component/Behavior/Task/Monster/Skill/T_UseSkill.h"

using namespace Behavior::Monster;

namespace
{
    using Builder = BT_Builder<CMonsterAgent>;
    using TreePtr = std::shared_ptr<const CEntityBehavior::Tree>;

    // 물러서기는 몇 초에 한 번만(계속 뒷걸음질치며 도망 다니지 않게)
    constexpr UInt32 kRetreatCooldownMs = 4000;

    TreePtr CreateMeleeBT()
    {
        return Builder("Melee", CEntityBehavior::kMaxNodes)
            .ReactiveSelector()
                .Condition(&C_InCombat::Check)
                    .ReactiveSelector()
                        .Do<T_UseSkill>()
                        .Do<T_Attack>()
                        .Do<T_Chase>()
                    .End()
                .End()
                .Condition(&C_Returning::Check)
                    .Do<T_ReturnHome>()
                .End()
                .Do<T_Idle>()
            .End()
            .Build();
    }

    // 원거리 · 지원: 사거리를 지키며 기술, 붙으면 한 번 물러서고 그래도 붙어 있으면 평타
    TreePtr CreateRangedBT(const char* name)
    {
        return Builder(name, CEntityBehavior::kMaxNodes)
            .ReactiveSelector()
                .Condition(&C_InCombat::Check)
                    .ReactiveSelector()
                        .Do<T_UseSkill>()
                        .Cooldown(kRetreatCooldownMs)
                            .Do<T_KeepDistance>()
                        .End()
                        .Do<T_Attack>()
                        .Do<T_ChaseToRange>()
                    .End()
                .End()
                .Condition(&C_Returning::Check)
                    .Do<T_ReturnHome>()
                .End()
                .Do<T_Idle>()
            .End()
            .Build();
    }

    TreePtr CreateBossBT()
    {
        return Builder("Boss", CEntityBehavior::kMaxNodes)
            .ReactiveSelector()
                .Condition(&C_InCombat::Check)
                    .ReactiveSelector()
                        .Do<T_PhaseShift>()
                        .Do<T_UseSkill>()
                        .Do<T_Attack>()
                        .Do<T_Chase>()
                    .End()
                .End()
                .Condition(&C_Returning::Check)
                    .Do<T_ReturnHome>()
                .End()
                .Do<T_Idle>()
            .End()
            .Build();
    }

    TreePtr CreateStationaryBT()
    {
        return Builder("Stationary", CEntityBehavior::kMaxNodes)
            .ReactiveSelector()
                .Condition(&C_InCombat::Check)
                    .ReactiveSelector()
                        .Do<T_UseSkill>()
                        .Do<T_Attack>()
                        .Do<T_Hold>()
                    .End()
                .End()
                .Do<T_Idle>()
            .End()
            .Build();
    }
}

namespace MonsterBehaviorHelper
{
    const CEntityBehavior::Tree* TreeOf(MonsterAiType type)
    {
        // 처음 부르는 순간 만든다(C++11 이후 함수 지역 static 초기화는 스레드 안전) — 그 뒤로는 불변이라 모든 Zone 스레드가 함께 읽는다
        static const TreePtr melee = CreateMeleeBT();
        static const TreePtr ranged = CreateRangedBT("Ranged");
        static const TreePtr support = CreateRangedBT("Support");
        static const TreePtr boss = CreateBossBT();
        static const TreePtr stationary = CreateStationaryBT();
        switch (type)
        {
        case MonsterAiType::Melee:      return melee.get();
        case MonsterAiType::Ranged:     return ranged.get();
        case MonsterAiType::Support:    return support.get();
        case MonsterAiType::Boss:       return boss.get();
        case MonsterAiType::Stationary: return stationary.get();
        case MonsterAiType::Dummy:      return nullptr;
        }
        return nullptr;
    }
}
