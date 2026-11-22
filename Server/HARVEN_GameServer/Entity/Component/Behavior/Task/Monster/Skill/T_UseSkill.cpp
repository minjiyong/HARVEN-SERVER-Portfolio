#include "pch.h"
#include "Entity/Component/Behavior/Task/Monster/Skill/T_UseSkill.h"

#include "Content/Skill/MonsterSkillHelper.h"
#include "Entity/Component/Behavior/EntityBehavior.h"
#include "Entity/Component/Behavior/Task/Monster/MonsterAgent.h"
#include "Entity/Subject/Monster/Monster.h"

namespace Behavior::Monster
{
    EnumTaskStatus T_UseSkill::OnEnter(CMonsterAgent& a, BT_Frame&)
    {
        if (a.monster->GetStat()->IsDead() || a.brain.m_Skills.IsCasting())
            return EnumTaskStatus::Failure;
        return CMonsterSkillHelper::TryBegin(a) ? EnumTaskStatus::Success : EnumTaskStatus::Failure;
    }

    EnumTaskStatus T_UseSkill::OnUpdate(CMonsterAgent& a, BT_Frame&)
    {
        const MonsterCast& c = a.brain.m_Skills.cast;
        if (!c.Active() || c.slot == MonsterCast::kPhaseSlot)
            return EnumTaskStatus::Failure;
        return CMonsterSkillHelper::Advance(a);
    }

    void T_UseSkill::OnLeave(CMonsterAgent& a, BT_Frame&, EnumTaskStatus result)
    {
        const MonsterCast& c = a.brain.m_Skills.cast;
        if (result == EnumTaskStatus::Invalid && c.Active() && c.slot != MonsterCast::kPhaseSlot)
            CMonsterSkillHelper::Cancel(a);
    }
}
