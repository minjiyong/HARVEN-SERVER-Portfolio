#pragma once

#include "WorkerThread/SafeMessageQueue.h"

// 로직 스레드 하나(Zone · 서비스 · 기본) — 작업 큐(메일함) 하나를 혼자 처리한다.
// 큐는 CSafeMessageQueue(넣기 잠금 없음 · 넣은 순서 그대로 · 주인이 잘 때만 깨움 — Libs/WorkerThread/SafeMessageQueue.h).
class CGameLogicThread
{
public:
    using Task = std::function<void()>;

public:
    CGameLogicThread() = default;
    ~CGameLogicThread() = default;

    void Enqueue(Task task);
    void Run();

    // 지금 스레드를 이 로직 스레드로 적는다(Zone::Run이 몬스터를 넣기 전에 — 그때 거는 예약도 이 스레드 것)
    void BindToCurrentThread() noexcept;
    // 지금 스레드가 돌리는 로직 스레드(로직 스레드가 아니면 nullptr)
    [[nodiscard]] static CGameLogicThread* Current() noexcept;

    // 이 Zone 객체의 예약 — 이 스레드에서만 부른다(CTimerThread::Schedule). 일 묶음 사이에 이 스레드가 직접 실행한다.
    void ScheduleLocal(TIMER_EVENT timerEvent);

private:
    struct TimerCompare
    {
        bool operator()(const TIMER_EVENT& lhs, const TIMER_EVENT& rhs) const noexcept
        {
            if (lhs.wakeupTime != rhs.wakeupTime)
                return lhs.wakeupTime > rhs.wakeupTime;
            return lhs.sequence > rhs.sequence;
        }
    };

    void RunDueTimers();

    static constexpr size_t kFlushEveryTasks = 64;
    // 한 바퀴에 처리하는 작업 수의 위 — 일이 계속 들어와도 사이사이 예약 실행 · 보내기를 한다
    static constexpr size_t kMaxTasksPerRound = 1024;
    // 예약 실행이 늦어도 되는 폭(몬스터 걸음 · 공격 간격은 수백 ms라 몇 ms는 보이지 않는다)
    static constexpr std::chrono::milliseconds kTimerSlack{ 2 };

    CSafeMessageQueue<Task> m_Queue;

    // 이 스레드만 만지는 예약(잠금 없음)
    std::priority_queue<TIMER_EVENT, std::vector<TIMER_EVENT>, TimerCompare> m_LocalTimers;
    std::vector<TIMER_EVENT> m_DueTimers;
    UInt64                   m_LocalSequence = 0;
};

extern std::shared_ptr<CGameLogicThread> GGameLogicThread;
