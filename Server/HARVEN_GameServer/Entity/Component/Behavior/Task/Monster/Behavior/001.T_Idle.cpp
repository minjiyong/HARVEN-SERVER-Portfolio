#include "pch.h"
#include "Entity/Component/Behavior/Task/Monster/Behavior/001.T_Idle.h"

#include "Content/Field/Sector.h"
#include "Entity/Component/Behavior/EntityBehavior.h"
#include "Entity/Component/Behavior/EntityBehaviorHelper.h"
#include "Entity/Component/Behavior/Task/Monster/MonsterAgent.h"
#include "Entity/Subject/Monster/Monster.h"
#include "Entity/Subject/Monster/MonsterHelper.h"
#include "Entity/Subject/SubjectHelper.h"
#include "User/User.h"

namespace
{
    // 보는 사람을 다시 훑는 간격 — 그 사이에 모두 떠나도 이만큼 더 배회하다 잠든다(걸음마다 시야 9칸을 훑던 비용을 줄인다)
    constexpr UInt32 kObserverCheckMs = 2000;

    // 이 몬스터를 볼 수 있는 게임 중 플레이어가 있는가
    [[nodiscard]] bool AnyObserver(const MonsterSharedPtr& m)
    {
        bool seen = false;
        GSector->ForEachObjectWithin(m->GetSectorX(), m->GetSectorY(), m->GetX(), m->GetY(), GViewRange, EnumCategory::eUser, [&](const SubjectSharedPtr& object)
        {
            if (seen)
                return;
            const auto user = static_pointer_cast<CUser>(object);
            const auto session = user->GetGameSession();
            if (!session || session->m_eState != SOCKET_STATE::ST_INGAME)
                return;
            seen = CSubjectHelper::CanSee(m, user);
        });
        return seen;
    }
}

namespace Behavior::Monster
{
    EnumTaskStatus T_Idle::OnUpdate(CMonsterAgent& a, BT_Frame&)
    {
        const MonsterSharedPtr& m = a.monster;
        if (m->GetStat()->IsDead())
            return EnumTaskStatus::ForceShutdown;
        const MonsterAiType type = a.Type();
        if (CEntityBehaviorHelper::AggroRange(a) > 0 || type == MonsterAiType::Boss || type == MonsterAiType::Stationary)
            return EnumTaskStatus::ForceShutdown;
        CEntityBehavior& b = a.brain;
        if (TimeReached(a.now, b.m_ObservedUntil))
        {
            if (!AnyObserver(m))
                return EnumTaskStatus::ForceShutdown;
            b.m_ObservedUntil = a.now + kObserverCheckMs;
        }
        if (const UInt32 until = CMonsterHelper::DisabledUntil(m, a.now, true); until != 0)
        {
            a.WakeAt(until);
            return EnumTaskStatus::Running;
        }
        if (TimeReached(a.now, b.m_NextMoveAt))
        {
            const UInt32 step = CMonsterHelper::MoveDelayMs(m, a.now);
            CMonsterHelper::RandomStep(m, step);
            b.m_NextMoveAt = a.now + step;
        }
        a.WakeAt(b.m_NextMoveAt);
        return EnumTaskStatus::Running;
    }
}
