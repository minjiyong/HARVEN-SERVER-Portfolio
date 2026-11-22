#include "pch.h"
#include "Worker/GameLogicThread.h"
#include "Worker/TimerThread.h"
#include "Base/GameObjectManager.h"
#include "Database/GameData/Load/GameData.h"
#include "Network/GameSession.h"

namespace
{
    thread_local CGameLogicThread* LCurrentLogicThread = nullptr;
}

void CGameLogicThread::Enqueue(Task task)
{
    m_Queue.Push(std::move(task));
}

void CGameLogicThread::BindToCurrentThread() noexcept
{
    LCurrentLogicThread = this;
}

CGameLogicThread* CGameLogicThread::Current() noexcept
{
    return LCurrentLogicThread;
}

void CGameLogicThread::ScheduleLocal(TIMER_EVENT timerEvent)
{
    ASSERT_CRASH(LCurrentLogicThread == this);
    timerEvent.sequence = m_LocalSequence++;
    m_LocalTimers.push(std::move(timerEvent));
}

// 때가 된 예약만 먼저 꺼내 실행한다 — 실행하면서 새로 거는 예약(0ms 포함)은 다음 바퀴에 돈다
void CGameLogicThread::RunDueTimers()
{
    if (m_LocalTimers.empty())
        return;

    const auto now = CTimerThread::Now();
    m_DueTimers.clear();
    while (!m_LocalTimers.empty() && m_LocalTimers.top().wakeupTime <= now)
    {
        m_DueTimers.push_back(m_LocalTimers.top());
        m_LocalTimers.pop();
    }

    size_t processed = 0;
    for (const TIMER_EVENT& timerEvent : m_DueTimers)
    {
#ifdef _DEBUG
        // 자기 Zone 객체만 여기로 온다(CTimerThread::Schedule) — 전제가 깨지면 바로 드러나게
        const auto subject = ::GetGameObject<CSubject>(timerEvent.subjectId);
        ASSERT_CRASH(subject == nullptr || subject->GetZoneId() == LCurrentZoneId);
#endif
        CTimerThread::Execute(timerEvent);
        if (++processed % kFlushEveryTasks == 0)
            CGameSession::FlushDeferredSends();
    }
}

void CGameLogicThread::Run()
{
    BindToCurrentThread();
    // 이 스레드에서 만든 패킷은 묶음이 끝날 때 세션마다 한 번에 보낸다(CGameSession::FlushDeferredSends)
    CGameSession::BeginDeferredSends();

    Task task;
    while (true)
    {
        // 할 일이 없으면 다음 예약 시각까지만 기다린다(예약이 없으면 일이 올 때까지). kTimerSlack만큼 늦게 깨어
        // 그 사이 때가 된 예약을 한 번에 돌린다 — 몬스터 수백 마리가 저마다 깨우지 않게(깨어남 · 보내기 횟수가 준다).
        if (m_Queue.Empty())
        {
            if (m_LocalTimers.empty())
            {
                m_Queue.Wait();
            }
            else
            {
                const auto deadline = m_LocalTimers.top().wakeupTime + kTimerSlack;
                const auto now = CTimerThread::Now();
                if (deadline > now)
                    m_Queue.Wait(std::chrono::duration_cast<std::chrono::microseconds>(deadline - now).count() + 1);
            }
        }

        // 기획 데이터 세대를 이 묶음(틱) 동안 고정한다 — 핫 리로드(--dev)가 새 판을 올려도 한 틱 안에서는 한 판만 보인다.
        GameData::PinThread();

        // 큐에 든 일을 넣은 순서대로 처리한다(한 바퀴에 kMaxTasksPerRound까지)
        size_t processed = 0;
        while (processed < kMaxTasksPerRound && m_Queue.TryPop(task))
        {
            if (task)
                task();
            task = nullptr;   // 붙잡은 것(세션 · 객체)을 바로 놓는다

            // 묶음이 길면 중간에도 내보낸다 — 앞 일의 응답이 뒤 일을 다 기다리지 않게
            if (++processed % kFlushEveryTasks == 0)
                CGameSession::FlushDeferredSends();
        }

        RunDueTimers();
        CGameSession::FlushDeferredSends();
    }
}
