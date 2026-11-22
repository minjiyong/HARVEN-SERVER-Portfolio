#include "pch.h"
#include "Entity/Component/Behavior/Task/Monster/Behavior/002.T_Combat.h"

#include "Content/Combat/CombatHelper.h"
#include "Content/ContentHelper.h"
#include "Content/Field/Sector.h"
#include "Content/Field/ZoneLayout.h"
#include "Content/Navigator/Collision.h"
#include "Content/Skill/MonsterSkillHelper.h"
#include "Database/GameData/MoveRule.h"
#include "Database/GameData/WorldData.h"
#include "Entity/Component/Behavior/EntityBehavior.h"
#include "Entity/Component/Behavior/EntityBehaviorHelper.h"
#include "Entity/Component/Behavior/Task/Monster/MonsterAgent.h"
#include "Entity/Subject/Monster/Monster.h"
#include "Entity/Subject/Monster/MonsterHelper.h"
#include "User/User.h"

namespace
{
    [[nodiscard]] int Cheb(int ax, int ay, int bx, int by) noexcept { return std::max(std::abs(ax - bx), std::abs(ay - by)); }
    [[nodiscard]] int Sign(int v) noexcept { return (v > 0) - (v < 0); }

    // 쫓고 칠 수 있는 대상인가
    [[nodiscard]] bool ValidTarget(const CMonsterAgent& a, const UserSharedPtr& u)
    {
        if (u == nullptr || u->GetStat()->IsDead() || u->IsTransferring() || u->GetZoneId() != a.monster->GetZoneId())
            return false;
        const auto session = u->GetGameSession();
        if (!session || session->m_eState != SOCKET_STATE::ST_INGAME)
            return false;
        if (WorldData::IsSafeZone(u->GetX(), u->GetY()) || u->Status().Has(StatusId::Veil, a.now))
            return false;
        return Cheb(u->GetX(), u->GetY(), a.monster->GetX(), a.monster->GetY()) <= GViewRange;
    }

    // 도발 → 위협 표 → 감지
    [[nodiscard]] ObjID PickTarget(CMonsterAgent& a)
    {
        const MonsterSharedPtr& m = a.monster;
        CEntityBehavior& b = a.brain;
        if (const StatusSlot* taunt = m->Status().Get(StatusId::Taunt, a.now))
            if (ValidTarget(a, ::GetGameObject<CUser>(taunt->source)))
            {
                b.m_Aggro.Add(taunt->source, 1);
                return taunt->source;
            }

        // 위협이 큰 순서로 — 단 지금 대상보다 10% 넘게 앞서야 바꾼다(두 사람이 번갈아 때릴 때 이리저리 돌지 않게)
        const ObjID picked = b.m_Aggro.PickValid([&](const ObjID& id)
        {
            return ValidTarget(a, ::GetGameObject<CUser>(id));
        });
        if (picked && b.m_TargetId && picked != b.m_TargetId)
        {
            const UInt64 current = b.m_Aggro.Threat(b.m_TargetId);
            if (current > 0 && static_cast<UInt64>(b.m_Aggro.Threat(picked)) * 10u < current * 11u
                && ValidTarget(a, ::GetGameObject<CUser>(b.m_TargetId)))
                return b.m_TargetId;
        }
        if (picked)
            return picked;

        const UInt8 range = CEntityBehaviorHelper::AggroRange(a);
        if (range == 0)
            return ObjID{};
        ObjID nearest{};
        int best = range + 1;
        GSector->ForEachObjectWithin(m->GetSectorX(), m->GetSectorY(), m->GetX(), m->GetY(), range, EnumCategory::eUser, [&](const SubjectSharedPtr& object)
        {
            const int d = Cheb(object->GetX(), object->GetY(), m->GetX(), m->GetY());
            if (d >= best)
                return;
            const auto u = static_pointer_cast<CUser>(object);
            if (!ValidTarget(a, u))
                return;
            best = d;
            nearest = u->GetObjID();
        });
        if (nearest)
            b.m_Aggro.Add(nearest, 1);
        return nearest;
    }

    [[nodiscard]] UserSharedPtr Target(const CMonsterAgent& a)
    {
        return a.brain.m_TargetId ? ::GetGameObject<CUser>(a.brain.m_TargetId) : nullptr;
    }

    // 이동 간격이 되었으면 목표 쪽으로 한 걸음. 막혔거나 기다려야 하면 그 시각을 남긴다.
    EnumTaskStatus StepToward(CMonsterAgent& a, short goalX, short goalY)
    {
        const MonsterSharedPtr& m = a.monster;
        CEntityBehavior& b = a.brain;
        if (const UInt32 until = CMonsterHelper::DisabledUntil(m, a.now, true); until != 0)
        {
            a.WakeAt(until);   // 빙결 · 기절 — 서서 기다린다
            return EnumTaskStatus::Running;
        }
        if (!TimeReached(a.now, b.m_NextMoveAt))
        {
            a.WakeAt(b.m_NextMoveAt);
            return EnumTaskStatus::Running;
        }
        const UInt32 step = CMonsterHelper::MoveDelayMs(m, a.now);
        CMonsterHelper::StepToward(m, goalX, goalY, step);   // 길이 없으면 서 있다가 다음 간격에 다시(대상이 움직이면 길이 생긴다)
        b.m_NextMoveAt = a.now + step;
        a.WakeAt(b.m_NextMoveAt);
        return EnumTaskStatus::Running;
    }
}

namespace Behavior::Monster
{
    bool C_InCombat::Check(CMonsterAgent& a, const BT_Param&)
    {
        CEntityBehavior& b = a.brain;
        const MonsterSharedPtr& m = a.monster;
        if (b.m_State == EnumEntityBehaviorState::Return || m->GetStat()->IsDead())
            return false;

        if (b.m_State == EnumEntityBehaviorState::Combat)
        {
            // 리쉬: 스폰에서 종족별 한계(기본 7칸, 보스 12칸)를 넘으면 귀환.
            // Zone 경계: ZONE_BOUNDARY_MARGIN 안에 들면 귀환(몬스터는 Zone을 넘지 않는다) — 스폰 자리 그대로면 서서 싸운다.
            const bool atSpawn = m->GetX() == m->GetSpawnX() && m->GetY() == m->GetSpawnY();
            if (Cheb(m->GetX(), m->GetY(), m->GetSpawnX(), m->GetSpawnY()) > m->GetLeash()
                || (!atSpawn && CMonsterHelper::IsNearZoneBoundary(m->GetX(), m->GetY())))
            {
                CEntityBehaviorHelper::BeginReturn(a);
                return false;
            }
        }

        const ObjID target = PickTarget(a);
        if (!target)
        {
            if (b.m_State == EnumEntityBehaviorState::Combat)
                CEntityBehaviorHelper::EndCombat(a);
            return false;
        }
        if (target != b.m_TargetId)
        {
            b.m_TargetId = target;
            m->ClearPath();
        }
        if (b.m_State != EnumEntityBehaviorState::Combat)
            CEntityBehaviorHelper::EnterCombat(a);
        return true;
    }

    EnumTaskStatus T_Attack::OnUpdate(CMonsterAgent& a, BT_Frame&)
    {
        const MonsterSharedPtr& m = a.monster;
        if (m->GetStat()->IsDead())
            return EnumTaskStatus::ForceShutdown;
        const UserSharedPtr target = Target(a);
        if (target == nullptr || !CCombatHelper::MonsterCanReach(m, target))
            return EnumTaskStatus::Failure;
        m->UpdateFacing(target->GetX() - m->GetX());
        CEntityBehavior& b = a.brain;
        if (const UInt32 until = CMonsterHelper::DisabledUntil(m, a.now, false); until != 0)
        {
            a.WakeAt(until);   // 기절 · 무력화 — 붙은 채 풀리기를 기다린다
            return EnumTaskStatus::Running;
        }
        if (TimeReached(a.now, b.m_NextAttackAt))
        {
            // 회피 · 방어 · 철벽 태세 · 반격까지 CCombatHelper가 처리
            const bool died = CCombatHelper::HitPlayerFromMonster(m, target, CMonsterSkillHelper::RollDamage(m, 100), 0);
            if (m->GetStat()->IsDead())
                return EnumTaskStatus::ForceShutdown;   // 반격에 쓰러졌다
            if (died)
            {
                b.m_Aggro.Remove(target->GetObjID());
                CContentHelper::HandlePlayerDeath(target, m->GetObjID(), false);
                return EnumTaskStatus::Success;   // 다음 생각에 다른 대상 · 귀환
            }
            b.m_NextAttackAt = a.now + CMonsterHelper::AttackDelayMs(m, a.now);
        }
        a.WakeAt(b.m_NextAttackAt);
        a.WakeIn(CMonsterHelper::MoveDelayMs(m, a.now));   // 대상이 떨어지면 걸음 간격 안에 알아챈다
        return EnumTaskStatus::Running;
    }

    EnumTaskStatus T_Chase::OnUpdate(CMonsterAgent& a, BT_Frame&)
    {
        const MonsterSharedPtr& m = a.monster;
        if (m->GetStat()->IsDead())
            return EnumTaskStatus::ForceShutdown;
        const UserSharedPtr target = Target(a);
        if (target == nullptr || CCombatHelper::MonsterCanReach(m, target))
            return EnumTaskStatus::Failure;
        return StepToward(a, target->GetX(), target->GetY());
    }

    EnumTaskStatus T_ChaseToRange::OnUpdate(CMonsterAgent& a, BT_Frame&)
    {
        const MonsterSharedPtr& m = a.monster;
        if (m->GetStat()->IsDead())
            return EnumTaskStatus::ForceShutdown;
        const UserSharedPtr target = Target(a);
        if (target == nullptr)
            return EnumTaskStatus::Failure;
        const int keep = std::max<int>(1, a.KeepRange());
        if (Cheb(m->GetX(), m->GetY(), target->GetX(), target->GetY()) <= keep)
        {
            // 사거리 안 — 서서 기술을 기다린다(기술 잎이 풀리는 시각에 깨워 달라고 남긴다). 대상이 움직이면 걸음 간격마다 다시 본다.
            m->UpdateFacing(target->GetX() - m->GetX());
            a.WakeIn(CMonsterHelper::MoveDelayMs(m, a.now));
            return EnumTaskStatus::Running;
        }
        return StepToward(a, target->GetX(), target->GetY());
    }

    EnumTaskStatus T_KeepDistance::OnUpdate(CMonsterAgent& a, BT_Frame&)
    {
        const MonsterSharedPtr& m = a.monster;
        const UserSharedPtr target = Target(a);
        if (target == nullptr || m->GetStat()->IsDead() || !CCombatHelper::MonsterCanReach(m, target))
            return EnumTaskStatus::Failure;
        if (CMonsterHelper::DisabledUntil(m, a.now, true) != 0)
            return EnumTaskStatus::Failure;
        int dx = Sign(m->GetX() - target->GetX()), dy = Sign(m->GetY() - target->GetY());
        int x = m->GetX(), y = m->GetY();
        for (int i = 0; i < 2; ++i)
        {
            const int nx = x + dx, ny = y + dy;
            if (!CZoneLayout::IsValidWorldPosition(static_cast<short>(nx), static_cast<short>(ny)) || !MoveRule::CleanStep(x, y, dx, dy, &CollisionAt)
                || WorldData::IsSafeZone(nx, ny) || CZoneLayout::GetZoneIdByWorld(static_cast<short>(nx), static_cast<short>(ny)) != m->GetZoneId())
                break;
            x = nx;
            y = ny;
        }
        if (x == m->GetX() && y == m->GetY())
            return EnumTaskStatus::Failure;   // 등 뒤가 막혔다 — 붙어서 싸운다
        constexpr UInt32 kHopMs = 130;
        CMonsterSkillHelper::ForcedMove(m, static_cast<short>(x), static_cast<short>(y), kHopMs * static_cast<UInt32>(Cheb(x, y, m->GetX(), m->GetY())), ForcedMoveKind::Roll);
        m->UpdateFacing(target->GetX() - m->GetX());
        a.brain.m_NextMoveAt = a.now + CMonsterHelper::MoveDelayMs(m, a.now);
        return EnumTaskStatus::Success;
    }

    EnumTaskStatus T_Hold::OnUpdate(CMonsterAgent& a, BT_Frame&)
    {
        if (a.monster->GetStat()->IsDead())
            return EnumTaskStatus::ForceShutdown;
        a.WakeIn(500);
        return EnumTaskStatus::Running;
    }

    EnumTaskStatus T_PhaseShift::OnEnter(CMonsterAgent& a, BT_Frame&)
    {
        const MonsterAiRow* ai = a.ai;
        if (ai == nullptr || ai->phase2Hp == 0)
            return EnumTaskStatus::Failure;
        const MonsterSharedPtr& m = a.monster;
        const UInt32 maxHp = std::max<UInt32>(1, m->GetStat()->GetMaxHp());
        const UInt32 hp = static_cast<UInt32>(m->GetStat()->GetHp()) * 100u / maxHp;
        UInt8 phase = 1;
        if (ai->phase3Hp > 0 && hp <= ai->phase3Hp)
            phase = 3;
        else if (hp <= ai->phase2Hp)
            phase = 2;
        CEntityBehavior& b = a.brain;
        if (phase <= b.m_Phase)
            return EnumTaskStatus::Failure;
        b.m_Phase = phase;
        const UInt16 skill = phase == 3 ? ai->phase3Skill : ai->phase2Skill;
        if (skill == 0)
            return EnumTaskStatus::Failure;   // 단계만 오른다(기술 칸이 단계로 바뀐다)
        // 진행 중인 기술을 끊고 단계 기술을 곧바로(우선 선택이 뒤 가지를 끊기 전에 — 그 가지는 자기 시전만 정리한다)
        CMonsterSkillHelper::Cancel(a);
        return CMonsterSkillHelper::Begin(a, skill, MonsterCast::kPhaseSlot) ? EnumTaskStatus::Success : EnumTaskStatus::Failure;
    }

    EnumTaskStatus T_PhaseShift::OnUpdate(CMonsterAgent& a, BT_Frame&)
    {
        if (a.brain.m_Skills.cast.slot != MonsterCast::kPhaseSlot)
            return EnumTaskStatus::Failure;
        return CMonsterSkillHelper::Advance(a);
    }

    void T_PhaseShift::OnLeave(CMonsterAgent& a, BT_Frame&, EnumTaskStatus result)
    {
        if (result == EnumTaskStatus::Invalid && a.brain.m_Skills.IsCasting() && a.brain.m_Skills.cast.slot == MonsterCast::kPhaseSlot)
            CMonsterSkillHelper::Cancel(a);
    }
}
