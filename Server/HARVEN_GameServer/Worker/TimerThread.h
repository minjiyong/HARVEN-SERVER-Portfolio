#pragma once

#include <atomic>
#include <limits>

#include "ThirdParty/moodycamel/concurrentqueue.h"
#include "ThirdParty/moodycamel/lightweightsemaphore.h"
#ifndef MOODYCAMEL_LIGHTWEIGHTSEMAPHORE_MONOTONIC
#error "LightweightSemaphore가 벽시계로 기다린다 — glibc 2.30 이상 · _GNU_SOURCE가 필요하다(SafeMessageQueue.h)"
#endif

// ─────────────────────────────────────────────────────────────────────────────
// CTimerThread — Zone 밖에서 거는 예약(플레이어 회복 · 부활 · 버프 만료 등, Zone 스레드가 아닌 곳의 예약)을 시간에 맞춰
// 소유 Zone 큐로 보낸다. Zone 객체(몬스터 · 필드 아이템 · NPC) 예약은 그 Zone 스레드가 직접 돌린다(ScheduleLocal).
//
// · 넣기: moodycamel ConcurrentQueue(잠금 없음). 꺼낸 뒤 시간 힙이 순서를 정하므로 넣는 스레드 사이의 순서는 필요 없다.
//   같은 스레드가 넣은 것끼리는 넣은 순서를 지킨다(같은 시각이면 먼저 넣은 것 먼저).
// · 타이머 스레드만 시간 힙을 만진다. 잘 때는 "언제 깰지"를 적어 두고, 그보다 이른 예약이 들어올 때만 깨운다.
//   넣는 쪽은 "넣은 수"를 먼저 올린 뒤 넣고 "자는 중"을 본다. 자는 쪽은 "자는 중"을 적은 뒤 "넣은 수"를 본다 — 둘 다 seq_cst라
//   적어도 한쪽은 상대를 본다(데커 방식, "넣었는데 아무도 안 깨움"이 없다). 장벽(fence) 없이 원자 연산만 써서 TSan도 이 약속을 본다.
// · 깨는 시각에 kTimerSlack을 더해 그 사이 예약을 한 번에 처리한다(Zone 타이머와 같은 2ms).
// ─────────────────────────────────────────────────────────────────────────────
class CTimerThread
{
public:
    using Clock = std::chrono::steady_clock;
    using Duration = Clock::duration;

    CTimerThread() = default;
    ~CTimerThread() = default;

    void DoTimer();
    void Schedule(TIMER_EVENT timerEvent);
    void ScheduleNow(const ObjID& subjectId, TIMER_EVENT_TYPE eventType);
    void ScheduleNow(const ObjID& subjectId, TIMER_EVENT_TYPE eventType, const ObjID& targetId);
    void ScheduleAfter(const ObjID& subjectId, Duration delay, TIMER_EVENT_TYPE eventType);
    void ScheduleAfter(const ObjID& subjectId, Duration delay, TIMER_EVENT_TYPE eventType, const ObjID& targetId);

    // 예약 하나를 지금 스레드에서 실행한다(그 객체의 Zone 스레드에서 부른다)
    static void Execute(const TIMER_EVENT& timerEvent);

    [[nodiscard]] static Clock::time_point Now() noexcept
    {
        return Clock::now();
    }

private:
    struct TimerEventCompare
    {
        bool operator()(const TIMER_EVENT& lhs, const TIMER_EVENT& rhs) const noexcept;
    };

    static constexpr Duration kTimerSlack = std::chrono::milliseconds(2);
    static constexpr Clock::rep kNoWakeup = std::numeric_limits<Clock::rep>::max();

    size_t DrainInbox();
    bool SleepUntilNext();
    void Dispatch(const TIMER_EVENT& timerEvent);

private:
    // ── 넣는 쪽(여러 스레드) → 타이머 스레드 ──
    moodycamel::ConcurrentQueue<TIMER_EVENT> m_Inbox;
    moodycamel::LightweightSemaphore         m_Wake{ 0, 64 };
    alignas(64) std::atomic<UInt64>          m_InboxPushed{ 0 };              // 넣기 전에 올린다
    alignas(64) std::atomic<bool>            m_bSleeping{ false };
    std::atomic<Clock::rep>                  m_PlannedWakeup{ kNoWakeup };   // 자는 동안 깨기로 한 시각(time_since_epoch)

    // ── 타이머 스레드만 ──
    std::priority_queue<TIMER_EVENT, std::vector<TIMER_EVENT>, TimerEventCompare> m_Events;
    std::vector<TIMER_EVENT> m_Drained;
    UInt64 m_InboxTaken = 0;                                                  // 받은 함에서 꺼낸 수
    UInt64 m_NextSequence = 0;
};

