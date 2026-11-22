#include "pch.h"
#include "Content/Transfer/TransferHelper.h"
#include "Content/Combat/CombatHelper.h"
#include "Content/Skill/UserSkillHelper.h"
#include "Worker/TimerThread.h"
#include "Base/GameObjectManager.h"
#include "Entity/Subject/Monster/MonsterHelper.h"
#include "Entity/Component/Behavior/EntityBehaviorHelper.h"
#include "Entity/Subject/Skill/AreaOfEffect.h"
#include "User/UserHelper.h"
#include "Content/Inventory/ItemHelper.h"
#include "Content/Field/ZoneManager.h"
#include "Content/ContentHelper.h"
#include "Content/Npc/NpcHelper.h"
#include "Content/Party/PartyHelper.h"
#include "Content/Achieve/AchieveHelper.h"
#include "Worker/GameLogicThread.h"

bool CTimerThread::TimerEventCompare::operator()(const TIMER_EVENT& lhs, const TIMER_EVENT& rhs) const noexcept
{
    if (lhs.wakeupTime != rhs.wakeupTime)
        return lhs.wakeupTime > rhs.wakeupTime;

    return lhs.sequence > rhs.sequence;
}

void CTimerThread::Schedule(TIMER_EVENT timerEvent)
{
    // 이 이벤트를 나중에 어느 Zone 큐로 보낼지 여기서 미리 찍어둔다.
    // 예약을 거는 쪽은 사실상 언제나 그 오브젝트를 소유한 Zone 스레드다 —
    // 몬스터 예약은 전부 해당 몬스터의 Zone 핸들러 안에서 일어나고, 필드 아이템
    // 예약도 아이템을 떨어뜨린 그 Zone 스레드에서 일어난다. 그래서 thread_local
    // LCurrentZoneId를 읽는 것만으로 목적지가 정해지고, Dispatch가 전역 조회표를
    // 뒤질 필요가 없어진다(동기화 비용 0).
    //
    // Zone 스레드가 아닌 곳에서 예약하면(로그인 처리 등) InvalidZoneId가 찍히고,
    // 그 경우 Dispatch가 기존처럼 조회 경로로 되돌아간다.
    timerEvent.zoneId = LCurrentZoneId;

    // 자기 Zone 객체(몬스터 · 필드 아이템 · NPC)의 예약은 그 Zone 스레드가 직접 돌린다 — 타이머 스레드를 깨우고
    // Zone 큐에 다시 넣는 왕복이 없다. 플레이어는 기다리는 사이 Zone을 넘을 수 있어(아래 Dispatch 주석)
    // 예전처럼 타이머 스레드가 실행 때 소유 Zone을 찾아 보낸다.
    if (CZoneLayout::IsValidZoneId(timerEvent.zoneId) && timerEvent.subjectId.GetCategory<EnumCategory>() != EnumCategory::eUser)
    {
        if (CGameLogicThread* logic = CGameLogicThread::Current())
        {
            logic->ScheduleLocal(std::move(timerEvent));
            return;
        }
    }

    const Clock::rep wakeup = timerEvent.wakeupTime.time_since_epoch().count();
    // "넣은 수"를 먼저 올리고 넣은 뒤 "자는 중인가"를 본다 — 타이머 스레드는 "잔다"를 적은 뒤 "넣은 수"를 본다(SleepUntilNext).
    // 깨기로 한 시각보다 늦은 예약이면 깨우지 않는다(그때 깨어 받은 함을 비운다).
    m_InboxPushed.fetch_add(1, std::memory_order_seq_cst);
    if (!m_Inbox.enqueue(std::move(timerEvent)))
    {
        // 블록 할당 실패(메모리 부족) — 센 것을 되돌려 타이머 스레드가 없는 예약을 기다리며 돌지 않게 한다
        m_InboxPushed.fetch_sub(1, std::memory_order_seq_cst);
        throw std::bad_alloc();
    }
    if (m_bSleeping.load(std::memory_order_seq_cst) &&
        wakeup < m_PlannedWakeup.load(std::memory_order_seq_cst) &&
        m_bSleeping.exchange(false, std::memory_order_seq_cst))
    {
        m_Wake.signal();
    }
}

void CTimerThread::ScheduleNow(const ObjID& subjectId, TIMER_EVENT_TYPE eventType)
{
    ScheduleAfter(subjectId, Duration::zero(), eventType);
}

void CTimerThread::ScheduleNow(const ObjID& subjectId, TIMER_EVENT_TYPE eventType, const ObjID& targetId)
{
    ScheduleAfter(subjectId, Duration::zero(), eventType, targetId);
}

void CTimerThread::ScheduleAfter(const ObjID& subjectId, Duration delay, TIMER_EVENT_TYPE eventType)
{
    TIMER_EVENT timerEvent{};
    timerEvent.subjectId = subjectId;
    timerEvent.wakeupTime = Now() + delay;
    timerEvent.event = eventType;

    Schedule(std::move(timerEvent));
}

void CTimerThread::ScheduleAfter(const ObjID& subjectId, Duration delay, TIMER_EVENT_TYPE eventType, const ObjID& targetId)
{
    TIMER_EVENT timerEvent{};
    timerEvent.subjectId = subjectId;
    timerEvent.wakeupTime = Now() + delay;
    timerEvent.event = eventType;
    timerEvent.targetId = targetId;

    Schedule(std::move(timerEvent));
}

void CTimerThread::Dispatch(const TIMER_EVENT& timerEvent)
{
    const ObjID subjectId = timerEvent.subjectId;

    // 예약 시점에 찍어둔 Zone으로 곧장 넣는다. EnqueueByObject는 전역 조회표의
    // 배타 뮤텍스를 잡는데, 깨어 있는 몬스터가 저마다 0.5초에 한 번씩 이벤트를
    // 걸기 때문에 이 함수 하나 때문에 그 락이 서버에서 가장 자주 잡히는 락이 된다.
    //
    // 힌트를 믿어도 되는 범위 — 몬스터는 Zone을 벗어날 수 없고(RandomMove가 존
    // 밖 걸음을 거부하고, 어그로 추격은 ZONE_BOUNDARY_MARGIN 안에서 되돌아간다),
    // 필드 아이템은 떨어진 자리에서 움직이지 않는다. 그래서 이 둘은 예약 시점의
    // Zone이 곧 실행 시점의 Zone이다.
    //
    // 반면 플레이어는 회복(5초)이나 부활(30초)을 기다리는 사이에 Zone 경계를
    // 넘을 수 있어 힌트가 낡을 수 있다. 그때만 기존대로 조회해서 보낸다 —
    // 플레이어 이벤트는 몬스터 이벤트에 비해 빈도가 훨씬 낮아 이득의 대부분은
    // 그대로 남는다.
    const ZoneId hintedZone =
        (subjectId.GetCategory<EnumCategory>() == EnumCategory::eUser)
            ? InvalidZoneId
            : timerEvent.zoneId;

    auto enqueue = [&](CZoneManager::Task task)
    {
        if (!CZoneLayout::IsValidZoneId(hintedZone))
        {
            GZoneManager->EnqueueByObject(subjectId, std::move(task));
            return;
        }

#ifdef _DEBUG
        // 위 전제가 훗날 깨지면(예: 몬스터가 Zone을 넘게 바뀌면) 남의 Zone
        // 스레드에서 조용히 실행되는 대신 도착 즉시 크래시로 드러나게 한다.
        // Release에서는 이 래핑 자체가 사라져 조회 없는 경로만 남는다.
        //
        // 대상이 이미 사라진 경우는 정상이다 — 필드 아이템이 소멸 타이머보다
        // 먼저 습득되면 핸들러가 "대상 없음"을 확인하고 조용히 넘어가는 설계라,
        // 살아 있을 때만 소유 Zone이 힌트와 같은지 따진다.
        GZoneManager->EnqueueByZone(hintedZone, [subjectId, inner = std::move(task)]()
        {
            const auto subject = ::GetGameObject<CSubject>(subjectId);
            ASSERT_CRASH(subject == nullptr || subject->GetZoneId() == LCurrentZoneId);
            inner();
        });
#else
        GZoneManager->EnqueueByZone(hintedZone, std::move(task));
#endif
    };

    enqueue([timerEvent]()
    {
        Execute(timerEvent);
    });
}

void CTimerThread::Execute(const TIMER_EVENT& timerEvent)
{
    const ObjID subjectId = timerEvent.subjectId;
    const ObjID targetId  = timerEvent.targetId;

    switch (timerEvent.event)
    {
    case TIMER_EVENT_TYPE::EV_TRANSFER_TICK:
        CTransferHelper::Tick();
        break;
    // 몬스터 · 장판은 Zone을 넘지 않으므로 Zone 힌트를 믿어도 된다(자기 Zone 스레드가 ScheduleLocal로 직접 돈다).
    case TIMER_EVENT_TYPE::EV_MONSTER_THINK:
        CEntityBehaviorHelper::HandleThink(subjectId, static_cast<UInt32>(targetId.GetDatabaseID()));
        break;

    case TIMER_EVENT_TYPE::EV_MONSTER_RESPAWN:
        CMonsterHelper::HandleRespawn(subjectId);
        break;

    case TIMER_EVENT_TYPE::EV_MONSTER_AOE:
        CAreaOfEffectHelper::HandleTick(subjectId, static_cast<UInt32>(targetId.GetDatabaseID()));
        break;

    case TIMER_EVENT_TYPE::EV_HEAL:
        CUserHelper::HandleHeal(subjectId);
        break;

    case TIMER_EVENT_TYPE::EV_USER_RESPAWN:
        // targetId에는 예약 시점의 사망 번호(CUser::m_DeathSeq)가 실려 있다.
        CUserHelper::HandleRespawn(subjectId, static_cast<UInt32>(targetId.GetDatabaseID()));
        break;

    case TIMER_EVENT_TYPE::EV_ITEM_DESPAWN:
        CItemHelper::HandleDespawn(subjectId);
        break;

    // 플레이어 이벤트는 위 hintedZone 계산에서 이미 조회 경로로 빠진다.
    case TIMER_EVENT_TYPE::EV_PURPLE_EXPIRE:
        CContentHelper::HandlePurpleExpire(subjectId);
        break;

    case TIMER_EVENT_TYPE::EV_HASTE_EXPIRE:
        CContentHelper::HandleHasteExpire(subjectId);
        break;

    // ── V2 전투 ──
    case TIMER_EVENT_TYPE::EV_SKILL_IMPACT:
        CUserSkillHelper::HandleWake(subjectId, static_cast<UInt32>(targetId.GetDatabaseID()));
        break;

    case TIMER_EVENT_TYPE::EV_STATUS_TICK:
        CCombatHelper::HandleStatusTick(subjectId);
        break;

    // 마을 NPC: 예약 시점의 Zone(마을)으로 곧장 간다 — NPC는 CGameObject가 아니다.
    case TIMER_EVENT_TYPE::EV_NPC_TICK:
        CNpcHelper::HandleTick();
        break;

    // 파티원 상태 모으기: 예약 시점의 Zone으로 곧장 간다(Zone마다 하나 — 그 Zone 스레드가 ScheduleLocal로 돌린다)
    case TIMER_EVENT_TYPE::EV_PARTY_TICK:
        CPartyHelper::HandleZoneTick();
        break;

    // 업적 진척 저장(플레이어 — 조회표의 Zone으로 온다, 옮기는 중이면 그 안에서 새 Zone으로 넘긴다)
    case TIMER_EVENT_TYPE::EV_ACHIEVE_SAVE:
        CAchieveHelper::HandleSaveTimer(subjectId);
        break;
    }
}

size_t CTimerThread::DrainInbox()
{
    // 같은 스레드가 넣은 것은 넣은 순서대로 나온다 — 순번을 여기서 매겨 같은 시각끼리도 그 순서를 지킨다
    size_t drained = 0;
    TIMER_EVENT timerEvent;
    while (m_Inbox.try_dequeue(timerEvent))
    {
        timerEvent.sequence = m_NextSequence++;
        m_Events.push(timerEvent);
        ++drained;
    }
    m_InboxTaken += drained;
    return drained;
}

// 다음 예약 시각(+여유)까지 잔다. 받은 함에 아직 꺼내지 않은 것이 있으면 자지 않고 false.
bool CTimerThread::SleepUntilNext()
{
    const Clock::rep planned = m_Events.empty() ? kNoWakeup : (m_Events.top().wakeupTime + kTimerSlack).time_since_epoch().count();
    m_PlannedWakeup.store(planned, std::memory_order_seq_cst);
    m_bSleeping.store(true, std::memory_order_seq_cst);

    if (m_InboxPushed.load(std::memory_order_seq_cst) != m_InboxTaken)
    {
        // 그새 들어왔다(또는 넣는 중). 넣는 쪽이 표시를 먼저 가져갔다면 그쪽이 보낼(보낸) 깨우기를 하나 받아 짝을 맞춘다
        if (!m_bSleeping.exchange(false, std::memory_order_seq_cst))
            m_Wake.wait();
        return false;
    }

    bool signaled = false;
    if (planned == kNoWakeup)
    {
        signaled = m_Wake.wait();
    }
    else
    {
        const auto now = Now().time_since_epoch().count();
        if (planned > now)
        {
            const auto waitUsecs = std::chrono::duration_cast<std::chrono::microseconds>(Duration(planned - now)).count() + 1;
            signaled = m_Wake.wait(waitUsecs);
        }
    }
    if (!signaled && !m_bSleeping.exchange(false, std::memory_order_seq_cst))
        m_Wake.wait();   // 깨는 순간 넣는 쪽이 표시를 가져갔다 — 곧 오는 깨우기를 받는다
    return true;
}

void CTimerThread::DoTimer()
{
    int spins = 0;
    while (true)
    {
        const size_t drained = DrainInbox();

        const auto now = Now();
        while (!m_Events.empty() && m_Events.top().wakeupTime <= now)
        {
            m_Drained.push_back(m_Events.top());
            m_Events.pop();
        }
        for (const TIMER_EVENT& timerEvent : m_Drained)
            Dispatch(timerEvent);
        m_Drained.clear();

        if (!m_Events.empty() && m_Events.top().wakeupTime <= Now())
            continue;   // 보내는 사이 시각이 지난 예약이 있다
        if (!SleepUntilNext() && drained == 0)
        {
            // 넣는 쪽이 "넣은 수"를 올리고 아직 넣는 중이다(그 사이 선점됐을 수도) — 곧 보인다. 길어지면 잠깐씩 잔다
            if (++spins < 64)
                std::this_thread::yield();
            else
                std::this_thread::sleep_for(std::chrono::microseconds(50));
            continue;
        }
        spins = 0;
    }
}
