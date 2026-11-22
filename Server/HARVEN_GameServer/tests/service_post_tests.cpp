// PostToOwner 한 걸음 판정 시험 — Content/ServicePost.h(NextPostStep).
//   cmake --build <폴더> --target ServicePostTests && <폴더>/Server/HARVEN_GameServer/tests/ServicePostTests
// 1) 판정 표 — 나감 · 가짐 · 옮기는 중 · 남의 Zone · 넘긴 횟수
// 2) 쫓아가기 흉내 — 사람이 걸음마다 Zone을 옮기는(또는 옮기는 중인) 경우에도 fn은 정확히 한 번, user를 받는다면 늘 가진 스레드에서,
//    넘긴 횟수는 kMaxPostHops를 넘지 않는다(예전: 다 넘기면 남의 스레드에서 fn(user))
#include <cstdio>
#include <random>

#include "Content/ServicePost.h"

namespace
{
    int g_failures = 0, g_checks = 0;
    void Check(bool ok, const char* what, int seed = -1)
    {
        ++g_checks;
        if (!ok && ++g_failures < 30)
            std::printf("FAIL: %s (seed %d)\n", what, seed);
    }
    using Service::PostStep;
}

int main()
{
    using Service::NextPostStep;
    using Service::kMaxPostHops;
    for (int hops = 0; hops <= kMaxPostHops + 2; ++hops)
    {
        Check(NextPostStep(false, false, false, hops) == PostStep::RunOffline, "나감 → nullptr");
        Check(NextPostStep(true, false, true, hops) == PostStep::Run, "가진 스레드 → user");
        const bool exhausted = hops >= kMaxPostHops;
        Check(NextPostStep(true, true, false, hops) == (exhausted ? PostStep::RunOffline : PostStep::ForwardToZone), "옮기는 중");
        Check(NextPostStep(true, false, false, hops) == (exhausted ? PostStep::RunOffline : PostStep::Repost), "남의 Zone");
    }
    // 쫓아가기: 걸음마다 사람이 p 확률로 Zone을 옮긴다(옮기는 중이거나 다른 Zone이 가짐)
    for (int seed = 0; seed < 20000; ++seed)
    {
        std::mt19937 rng(static_cast<unsigned>(seed));
        const double p = (seed % 5) / 4.0;   // 0 · 0.25 · 0.5 · 0.75 · 1(늘 도망)
        const bool leaves = seed % 7 == 0;
        int hops = 0, runs = 0, offline = 0, runOnOwner = 0;
        for (int step = 0; step < 100; ++step)
        {
            const bool alive = !(leaves && step == 3);
            const bool moving = std::uniform_real_distribution<double>(0, 1)(rng) < p;
            const bool transferring = alive && moving && (rng() & 1);
            const bool ownedHere = alive && !moving;
            const PostStep s = NextPostStep(alive, transferring, ownedHere, hops);
            if (s == PostStep::Run) { ++runs; runOnOwner += ownedHere && !transferring; break; }
            if (s == PostStep::RunOffline) { ++offline; break; }
            ++hops;
        }
        Check(runs + offline == 1, "fn은 정확히 한 번", seed);
        Check(runs == runOnOwner, "user를 넘길 때는 늘 가진 스레드", seed);
        Check(hops <= kMaxPostHops, "넘긴 횟수 ≤ kMaxPostHops", seed);
        if (p == 1.0 && !leaves)
            Check(offline == 1 && hops == kMaxPostHops, "늘 도망치면 kMaxPostHops 뒤 nullptr", seed);
    }
    std::printf("ServicePostTests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
