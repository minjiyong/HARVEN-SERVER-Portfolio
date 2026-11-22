#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// Service::PostToOwner의 한 걸음 판정(순수 — 시험 ServicePostTests). ServiceCommon.h가 쓴다.
//  · 사람이 없다(나갔다)                       → RunOffline: fn(nullptr)
//  · 이 스레드가 가졌고 옮기는 중이 아니다       → Run: fn(user)
//  · 옮기는 중 · 다른 Zone이 가졌다             → ForwardToZone(옮기는 중 — 새 Zone 큐 뒤) · Repost(지금 소유 Zone으로)
//  · 넘긴 횟수가 kMaxPostHops에 닿았다          → RunOffline: fn(nullptr) — 가진 스레드가 아닌 곳에서 살아 있는 CUser를 넘기지 않는다
//    (부르는 쪽은 모두 nullptr = 나간 사람 길을 갖고 있다 — 되돌림 · 표시처럼 할 일을 버리는 쪽으로 틀린다)
// ─────────────────────────────────────────────────────────────────────────────
namespace Service
{
    inline constexpr int kMaxPostHops = 8;

    enum class PostStep : unsigned char { Run, RunOffline, ForwardToZone, Repost };

    [[nodiscard]] constexpr PostStep NextPostStep(bool alive, bool transferring, bool ownedHere, int hops) noexcept
    {
        if (!alive)
            return PostStep::RunOffline;
        if (!transferring && ownedHere)
            return PostStep::Run;
        if (hops >= kMaxPostHops)
            return PostStep::RunOffline;
        return transferring ? PostStep::ForwardToZone : PostStep::Repost;
    }

    static_assert(NextPostStep(false, false, true, 0) == PostStep::RunOffline);
    static_assert(NextPostStep(true, false, true, 0) == PostStep::Run);
    static_assert(NextPostStep(true, false, true, kMaxPostHops + 3) == PostStep::Run);       // 가진 스레드라면 넘긴 횟수와 상관없다
    static_assert(NextPostStep(true, true, true, 0) == PostStep::ForwardToZone);
    static_assert(NextPostStep(true, false, false, 0) == PostStep::Repost);
    static_assert(NextPostStep(true, true, false, kMaxPostHops) == PostStep::RunOffline);   // 다 넘겼다 — 남의 스레드에서 user를 넘기지 않는다
    static_assert(NextPostStep(true, false, false, kMaxPostHops) == PostStep::RunOffline);
}
