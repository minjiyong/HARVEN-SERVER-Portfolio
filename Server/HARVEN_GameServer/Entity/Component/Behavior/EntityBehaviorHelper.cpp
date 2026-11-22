#include "pch.h"
#include "Entity/Component/Behavior/EntityBehaviorHelper.h"

#include <atomic>

#include "Base/Core/ObjectPool.h"
#include "Content/Field/Sector.h"
#include "Content/Skill/MonsterSkillHelper.h"
#include "Database/GameData/MonsterAITable.h"
#include "Database/GameData/WorldData.h"
#include "Entity/Component/Behavior/EntityBehavior.h"
#include "Entity/Component/Behavior/Task/Monster/MonsterAgent.h"
#include "Entity/Component/Behavior/Task/Monster/MonsterBehaviorHelper.h"
#include "Entity/Subject/Monster/Monster.h"
#include "Entity/Subject/Skill/AreaOfEffect.h"
#include "User/User.h"
#include "Worker/TimerThread.h"

namespace
{
    // 트리가 끝(성공 · 실패)을 냈는데 깨울 시각을 남기지 않았을 때 다시 생각할 때까지
    constexpr UInt32 kRethinkMs = 100;
    // 생각 간격의 바닥 — 잎이 "지금"을 거듭 남겨도 한 틱에 같은 몬스터를 되풀이해 돌리지 않게
    constexpr UInt32 kMinThinkMs = 20;

    std::atomic<Int64> GAwake{ 0 };

    [[nodiscard]] int Cheb(int ax, int ay, int bx, int by) noexcept { return std::max(std::abs(ax - bx), std::abs(ay - by)); }

    [[nodiscard]] const MonsterAiRow* AiOf(const MonsterSharedPtr& m)
    {
        return m->GetSpecies() != nullptr ? MonsterAiTable::Find(m->GetSpecies()->id) : nullptr;
    }

    [[nodiscard]] UInt8 AggroRangeOf(const MonsterSharedPtr& m, const MonsterAiRow* ai)
    {
        if (ai != nullptr)
            return ai->aggroRange;
        return m->GetType() == MONSTER_TYPE::AGGRO ? static_cast<UInt8>(WAKE_RANGE) : 0;
    }

    // 깨운다 — 행동을 풀에서 꺼내 붙인다(이미 깨어 있으면 그것). 허수아비 · 행동 트리가 없는 종류는 nullptr.
    CEntityBehavior* Activate(const MonsterSharedPtr& m)
    {
        if (CEntityBehavior* b = m->GetBehavior())
            return b;
        if (m->IsDummy() || m->GetStat()->IsDead() || m->GetSectorX() < 0)
            return nullptr;
        const MonsterAiRow* ai = AiOf(m);
        const MonsterAiType type = ai != nullptr ? ai->ai : MonsterAiType::Melee;
        const CEntityBehavior::Tree* tree = MonsterBehaviorHelper::TreeOf(type);
        if (tree == nullptr)
            return nullptr;
        CEntityBehavior* b = CObjectPool<CEntityBehavior>::Acquire(tree, GetNowTime());
        m->SetBehavior(b);
        GAwake.fetch_add(1, std::memory_order_relaxed);
        // 오라 — 깨어 있는 동안 몸에 두른다(잠들면 Sleep이 거둔다)
        if (ai != nullptr && ai->aura != 0)
            b->m_AuraHandle = CAreaOfEffectHelper::Spawn(m, ai->aura, m->GetX(), m->GetY());
        return b;
    }

    void Think(const MonsterSharedPtr& m)
    {
        CEntityBehavior* b = m->GetBehavior();
        if (b == nullptr)
            return;
        if (m->GetStat()->IsDead() || m->GetSectorX() < 0)
        {
            CEntityBehaviorHelper::Sleep(m);
            return;
        }
        const UInt32 now = GetNowTime();
        CMonsterAgent a{ m, *b, AiOf(m), now };
        m->m_bThinking = true;
        const EnumTaskStatus result = b->m_pTree->Tick(a, b->m_Run);
        m->m_bThinking = false;
        if (result == EnumTaskStatus::ForceShutdown || m->m_bSleepAfterThink || m->GetStat()->IsDead())
        {
            m->m_bSleepAfterThink = false;
            CEntityBehaviorHelper::Sleep(m);
            return;
        }
        UInt32 next = a.hasWake ? a.wakeAt : now + kRethinkMs;
        if (static_cast<Int32>(next - now) < static_cast<Int32>(kMinThinkMs))
            next = now + kMinThinkMs;
        CEntityBehaviorHelper::ScheduleThink(m, next);
    }

    // 동료에게 도움 요청 — assist_range 안의 쉬고 있는 동료가 같은 대상과 싸운다(도우러 온 동료는 다시 부르지 않는다 — 연쇄 방지).
    // 표에서 가장 큰 assist_range만큼만 훑는다(돕는 종족이 없으면 훑지 않는다 — 전투가 시작될 때마다 주변 몬스터를 다 보던 비용).
    void CallForHelp(const MonsterSharedPtr& m, const ObjID& targetId)
    {
        const int reach = MonsterAiTable::MaxAssistRange();
        if (reach == 0)
            return;
        const auto target = ::GetGameObject<CUser>(targetId);
        if (target == nullptr)
            return;
        GSector->ForEachObjectWithin(m->GetSectorX(), m->GetSectorY(), m->GetX(), m->GetY(), reach, EnumCategory::eMonster,
            [&](const SubjectSharedPtr& object)
            {
                if (object.get() == m.get() || object->GetStat()->IsDead())
                    return;
                const auto ally = static_pointer_cast<CMonster>(object);
                const MonsterAiRow* ai = AiOf(ally);
                if (ai == nullptr || ai->assistRange == 0 || Cheb(ally->GetX(), ally->GetY(), m->GetX(), m->GetY()) > ai->assistRange)
                    return;
                if (const CEntityBehavior* busy = ally->GetBehavior(); busy != nullptr && busy->m_State != EnumEntityBehaviorState::Idle)
                    return;
                CEntityBehavior* b = Activate(ally);
                if (b == nullptr)
                    return;
                b->m_bAssisting = true;
                b->m_Aggro.Add(targetId, 1);
                CEntityBehaviorHelper::Poke(ally);
            });
    }
}

namespace CEntityBehaviorHelper
{
    UInt8 AggroRange(const CMonsterAgent& a)
    {
        return AggroRangeOf(a.monster, a.ai);
    }

    Int64 AwakeCount()
    {
        return GAwake.load(std::memory_order_relaxed);
    }

    void Wake(const MonsterSharedPtr& monster, const UserSharedPtr& waker, bool force)
    {
        if (monster == nullptr || waker == nullptr || monster->GetStat()->IsDead() || monster->IsDummy())
            return;
        CEntityBehavior* b = monster->GetBehavior();
        // 걸음마다 시야의 몬스터마다 불리는 길 — 이미 이 사람을 알거나 집으로 가는 중이면 표를 보기 전에 끝낸다
        if (b != nullptr && (b->m_State == EnumEntityBehaviorState::Return || b->m_Aggro.Has(waker->GetObjID())))
            return;
        const MonsterAiRow* ai = AiOf(monster);
        if (ai != nullptr && ai->ai == MonsterAiType::Dummy)
            return;
        const UInt8 range = AggroRangeOf(monster, ai);
        if (range > 0)
        {
            // 선공 — 감지 거리 안 · 마을 밖 · 은신하지 않은 사람만(예전 WAKE_RANGE 검사)
            if (!force && Cheb(monster->GetX(), monster->GetY(), waker->GetX(), waker->GetY()) > range)
                return;
            if (WorldData::IsSafeZone(waker->GetX(), waker->GetY()) || waker->GetStat()->IsDead() || waker->Status().Has(StatusId::Veil, GetNowTime()))
                return;
            if (b == nullptr && (b = Activate(monster)) == nullptr)
                return;
            b->m_Aggro.Add(waker->GetObjID(), 1);
            Poke(monster);   // 새로 감지했다 — 곧바로(이미 아는 사람이면 위에서 끝났다 · 집으로 가는 중이면 닿은 뒤 다시 감지한다)
            return;
        }
        // 비선공 — 보이면 깨어나 배회한다(보는 사람이 없으면 T_Idle이 다시 재운다)
        if (b == nullptr && Activate(monster) != nullptr)
            Poke(monster);
    }

    void OnDamaged(const MonsterSharedPtr& monster, const UserSharedPtr& attacker, UInt32 damage)
    {
        if (monster == nullptr || attacker == nullptr || monster->GetStat()->IsDead() || monster->IsDummy())
            return;
        CEntityBehavior* b = Activate(monster);
        if (b == nullptr || b->m_State == EnumEntityBehaviorState::Return)
            return;   // 집으로 가는 중엔 반격하지 않는다(멀리 끌고 가서 조금씩 깎는 리쉬 악용 막기 — 집에 닿으면 다시)
        b->m_Aggro.Add(attacker->GetObjID(), damage);
        if (b->m_State != EnumEntityBehaviorState::Combat)
            Poke(monster);   // 싸우고 있지 않았다 — 곧바로 반격
    }

    void OnTaunted(const MonsterSharedPtr& monster, const UserSharedPtr& taunter)
    {
        if (monster == nullptr || taunter == nullptr || monster->GetStat()->IsDead() || monster->IsDummy())
            return;
        CEntityBehavior* b = Activate(monster);
        if (b == nullptr || b->m_State == EnumEntityBehaviorState::Return)
            return;
        b->m_Aggro.Add(taunter->GetObjID(), 1);
        Poke(monster);
    }

    void Forget(const MonsterSharedPtr& monster, const ObjID& userId)
    {
        if (monster == nullptr)
            return;
        CEntityBehavior* b = monster->GetBehavior();
        if (b == nullptr || !b->m_Aggro.Has(userId))
            return;
        b->m_Aggro.Remove(userId);
        if (b->m_TargetId == userId)
            b->m_TargetId = ObjID{};
        Poke(monster);
    }

    void OnCountered(const MonsterSharedPtr& monster)
    {
        if (monster == nullptr)
            return;
        CEntityBehavior* b = monster->GetBehavior();
        if (b == nullptr || !b->m_Skills.IsWindingUp())
            return;
        b->m_Skills.cast.countered = true;
        Poke(monster);
    }

    void OnDisabled(const MonsterSharedPtr& monster)
    {
        if (monster == nullptr)
            return;
        CEntityBehavior* b = monster->GetBehavior();
        if (b != nullptr && b->m_Skills.IsCasting())
            Poke(monster);
    }

    void OnDeath(const MonsterSharedPtr& monster)
    {
        if (monster != nullptr)
            Sleep(monster);
    }

    bool IsWindingUp(const MonsterSharedPtr& monster)
    {
        const CEntityBehavior* b = monster != nullptr ? monster->GetBehavior() : nullptr;
        return b != nullptr && b->m_Skills.IsWindingUp();
    }

    void HandleThink(const ObjID& monsterId, UInt32 epoch)
    {
        const auto monster = ::GetGameObject<CMonster>(monsterId);
        if (monster == nullptr || !monster->m_bThinkPending || monster->m_ThinkEpoch != epoch)
            return;   // 낡은 예약(더 이른 예약으로 바뀌었거나 잠들었다)
        monster->m_bThinkPending = false;
        Think(monster);
    }

    void ScheduleThink(const MonsterSharedPtr& monster, UInt32 at)
    {
        if (monster == nullptr || monster->GetBehavior() == nullptr)
            return;
        if (monster->m_bThinkPending && static_cast<Int32>(monster->m_ThinkAt - at) <= 0)
            return;   // 같거나 더 이른 예약이 있다 — 그때 트리가 다시 시각을 정한다
        const UInt32 now = GetNowTime();
        ++monster->m_ThinkEpoch;
        monster->m_ThinkAt = at;
        monster->m_bThinkPending = true;
        const UInt32 delay = static_cast<Int32>(at - now) > 0 ? at - now : 0;
        GTimerThread->ScheduleAfter(monster->GetObjID(), std::chrono::milliseconds(delay), TIMER_EVENT_TYPE::EV_MONSTER_THINK,
            ObjID(EnumCategory::eNone, static_cast<UInt64>(monster->m_ThinkEpoch)));
    }

    void Poke(const MonsterSharedPtr& monster)
    {
        ScheduleThink(monster, GetNowTime());
    }

    void Sleep(const MonsterSharedPtr& monster)
    {
        if (monster == nullptr)
            return;
        CEntityBehavior* b = monster->GetBehavior();
        if (b == nullptr)
            return;
        if (monster->m_bThinking)
        {
            // 트리를 도는 중에 죽었다(반격) — 도는 중인 행동을 돌려주지 않고, 생각이 끝나면 재운다
            monster->m_bSleepAfterThink = true;
            return;
        }
        // 달리던 가지를 끊는다 — 시전 중이면 예고 끊김을 알리고 대기 시간을 남긴다
        {
            CMonsterAgent a{ monster, *b, AiOf(monster), GetNowTime() };
            b->m_pTree->Abort(a, b->m_Run);
            CMonsterSkillHelper::Cancel(a);
        }
        if (b->m_bOwnsAoe)
            CAreaOfEffectHelper::RemoveByOwner(monster->GetObjID());
        monster->SetBehavior(nullptr);
        CObjectPool<CEntityBehavior>::Release(b);
        GAwake.fetch_sub(1, std::memory_order_relaxed);
        // 걸어 둔 생각 예약을 낡게 한다(다시 깨면 새 번호로)
        ++monster->m_ThinkEpoch;
        monster->m_ThinkAt = 0;
        monster->m_bThinkPending = false;
        monster->ClearPath();
    }

    void EnterCombat(CMonsterAgent& a)
    {
        CEntityBehavior& b = a.brain;
        b.m_State = EnumEntityBehaviorState::Combat;
        b.m_CombatSince = a.now;
        if (!b.m_bAssisting && b.m_TargetId)
            CallForHelp(a.monster, b.m_TargetId);
    }

    void EndCombat(CMonsterAgent& a)
    {
        CEntityBehavior& b = a.brain;
        b.m_TargetId = ObjID{};
        b.m_Aggro.Clear();
        b.m_bAssisting = false;
        const MonsterSharedPtr& m = a.monster;
        if (Cheb(m->GetX(), m->GetY(), m->GetSpawnX(), m->GetSpawnY()) > 1)
            BeginReturn(a);
        else
            b.m_State = EnumEntityBehaviorState::Idle;
    }

    void BeginReturn(CMonsterAgent& a)
    {
        CEntityBehavior& b = a.brain;
        b.m_State = EnumEntityBehaviorState::Return;
        b.m_TargetId = ObjID{};
        b.m_Aggro.Clear();
        b.m_bAssisting = false;
        a.monster->ClearPath();
    }
}
