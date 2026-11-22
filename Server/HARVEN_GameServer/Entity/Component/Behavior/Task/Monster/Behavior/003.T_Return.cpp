#include "pch.h"
#include "Entity/Component/Behavior/Task/Monster/Behavior/003.T_Return.h"

#include "Entity/Component/Behavior/EntityBehavior.h"
#include "Entity/Component/Behavior/Task/Monster/MonsterAgent.h"
#include "Entity/Subject/Monster/Monster.h"
#include "Entity/Subject/Monster/MonsterHelper.h"

namespace Behavior::Monster
{
    bool C_Returning::Check(CMonsterAgent& a, const BT_Param&)
    {
        return a.brain.m_State == EnumEntityBehaviorState::Return;
    }

    EnumTaskStatus T_ReturnHome::OnUpdate(CMonsterAgent& a, BT_Frame&)
    {
        const MonsterSharedPtr& m = a.monster;
        if (m->GetStat()->IsDead())
            return EnumTaskStatus::ForceShutdown;
        CEntityBehavior& b = a.brain;
        if (m->GetX() == m->GetSpawnX() && m->GetY() == m->GetSpawnY())
        {
            // 집 — 다시 처음부터(체력 · 보스 단계 · 기술 대기)
            m->GetStat()->SetHp(m->GetStat()->GetMaxHp());
            m->m_DamageLedger.Clear();   // HP를 다 채웠다 — 그 전에 깎은 몫은 남지 않는다
            b.m_State = EnumEntityBehaviorState::Idle;
            b.m_Phase = 1;
            b.m_Skills.Reset(a.now);
            return EnumTaskStatus::Success;
        }
        if (const UInt32 until = CMonsterHelper::DisabledUntil(m, a.now, true); until != 0)
        {
            a.WakeAt(until);
            return EnumTaskStatus::Running;
        }
        if (!TimeReached(a.now, b.m_NextMoveAt))
        {
            a.WakeAt(b.m_NextMoveAt);
            return EnumTaskStatus::Running;
        }
        const UInt32 step = CMonsterHelper::MoveDelayMs(m, a.now);
        if (!CMonsterHelper::StepToward(m, m->GetSpawnX(), m->GetSpawnY(), step))
        {
            b.m_State = EnumEntityBehaviorState::Idle;   // 길이 없다 — 여기서 쉰다
            return EnumTaskStatus::Failure;
        }
        b.m_NextMoveAt = a.now + step;
        a.WakeAt(b.m_NextMoveAt);
        return EnumTaskStatus::Running;
    }
}
