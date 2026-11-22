// 처치 보상 나눔 시험 — Database/GameData/RewardRules.h(RewardRule::SplitByWeight) · PartyRules.h(SplitExp · SplitKillExp).
//   cmake --build <폴더> --target PartyRuleTests && <폴더>/Server/HARVEN_GameServer/tests/PartyRuleTests
// 1) SplitExp — 무작위 파티(1~8명 · 피해 · 레벨 · 보장 범위 · %)를 많이 굴려 늘 지켜야 하는 성질을 본다:
//   ① 합 = total ② 보장받는 사람(레벨 차 ≤ 범위)은 floor 이상 ③ 보장이 없는 사람 · 넘게 받은 사람은 비례 몫보다 늘지 않는다
//   ④ 피해가 더 큰 사람이 덜 받지 않는다(같은 보장 조건끼리) ⑤ 보장을 끄면 비례 몫과 1 이내
// 2) SplitByWeight(그로시 · 경험치 1단계) — 1~17명: 합 = total · 저마다 비례 몫 ± 1 · 몫이 크면 덜 받지 않는다
// 3) SplitKillExp(모르는 사람 + 파티 섞임) — ① 합 = total ② 파티가 아니거나 혼자 받는 사람은 1단계 몫 그대로(쩔 보장이 남의 몫을 건드리지 않는다)
//   ③ 파티마다 받은 합 = 그 파티의 1단계 몫의 합 ④ 보장 표시는 둘 이상 받는 파티의 사람만
#include <cstdio>
#include <random>
#include <vector>

#include "Database/GameData/PartyRules.h"

ObjID ObjID::npos;

namespace
{
    int g_failures = 0;
    void Check(bool ok, const char* what, int seed)
    {
        if (!ok)
        {
            ++g_failures;
            if (g_failures < 20)
                std::printf("FAIL: %s (seed %d)\n", what, seed);
        }
    }
}

int main()
{
    using namespace PartyRule;
    std::mt19937 rng(20261011);
    constexpr int kRounds = 200000;
    for (int round = 0; round < kRounds; ++round)
    {
        const int n = 1 + static_cast<int>(rng() % kMaxMembers);
        const uint32_t total = (round % 7 == 0) ? static_cast<uint32_t>(rng() % 20) : static_cast<uint32_t>(rng() % 200000);
        const uint8_t range = static_cast<uint8_t>(rng() % 50);
        const uint8_t pct = static_cast<uint8_t>(rng() % 3 == 0 ? 0 : rng() % 101);
        std::vector<ShareIn> in(static_cast<size_t>(n));
        for (auto& x : in)
        {
            x.damage = (rng() % 4 == 0) ? 1 + rng() % 10 : 1 + rng() % 5000000;
            x.level = static_cast<uint8_t>(1 + rng() % 50);
        }
        std::vector<ShareOut> out(static_cast<size_t>(n));
        SplitExp(total, in, range, pct, out);

        uint64_t sum = 0, sumDamage = 0;
        uint8_t maxLevel = 0;
        for (int i = 0; i < n; ++i)
        {
            sum += out[static_cast<size_t>(i)].exp;
            sumDamage += in[static_cast<size_t>(i)].damage;
            maxLevel = std::max(maxLevel, in[static_cast<size_t>(i)].level);
        }
        Check(sum == total, "합 = total", round);

        int eligible = 0;
        for (int i = 0; i < n; ++i)
            eligible += (maxLevel - in[static_cast<size_t>(i)].level <= range) ? 1 : 0;
        uint64_t floor = static_cast<uint64_t>(total) * pct / 100;
        if (eligible > 0 && floor * static_cast<uint64_t>(eligible) > total)
            floor = total / static_cast<uint64_t>(eligible);
        for (int i = 0; i < n; ++i)
        {
            const auto& o = out[static_cast<size_t>(i)];
            const bool elig = pct > 0 && maxLevel - in[static_cast<size_t>(i)].level <= range;
            const double fair = static_cast<double>(total) * static_cast<double>(in[static_cast<size_t>(i)].damage) / static_cast<double>(sumDamage);
            if (elig)
                Check(o.exp >= floor, "보장받는 사람 ≥ floor", round);
            if (!o.guaranteed)
                Check(static_cast<double>(o.exp) <= fair + 1.0, "보장이 없으면 비례 몫보다 늘지 않는다", round);
            if (pct == 0)
                Check(static_cast<double>(o.exp) >= fair - 1.0 && static_cast<double>(o.exp) <= fair + 1.0, "보장을 끄면 비례 ± 1", round);
            Check(!o.guaranteed || elig, "보장 표시는 보장 조건인 사람만", round);
            for (int j = 0; j < n; ++j)
            {
                const bool eligJ = pct > 0 && maxLevel - in[static_cast<size_t>(j)].level <= range;
                if (elig == eligJ && in[static_cast<size_t>(i)].damage > in[static_cast<size_t>(j)].damage)
                    Check(o.exp + 1 >= out[static_cast<size_t>(j)].exp, "피해가 크면 덜 받지 않는다(±1)", round);
            }
        }
    }
    const int afterSplitExp = g_failures;

    // 2) SplitByWeight
    for (int round = 0; round < kRounds; ++round)
    {
        const size_t n = 1 + rng() % RewardRule::kMaxShares;
        const uint32_t total = (round % 5 == 0) ? static_cast<uint32_t>(rng() % 30) : static_cast<uint32_t>(rng());
        std::vector<uint32_t> w(n), out(n);
        uint64_t sumW = 0;
        for (auto& x : w)
        {
            x = (rng() % 6 == 0) ? 0u : ((rng() % 3 == 0) ? 1 + rng() % 10 : static_cast<uint32_t>(rng()));
            sumW += x > 0 ? x : 1;
        }
        RewardRule::SplitByWeight(total, w, out);
        uint64_t sum = 0;
        for (size_t i = 0; i < n; ++i)
        {
            sum += out[i];
            const long double fair = static_cast<long double>(total) * (w[i] > 0 ? w[i] : 1) / static_cast<long double>(sumW);
            Check(static_cast<long double>(out[i]) >= fair - 1.0L && static_cast<long double>(out[i]) <= fair + 1.0L, "SplitByWeight 비례 ± 1", round);
            for (size_t j = 0; j < n; ++j)
                if ((w[i] > 0 ? w[i] : 1u) > (w[j] > 0 ? w[j] : 1u))
                    Check(out[i] + 1 >= out[j] && out[i] >= out[j], "SplitByWeight 몫이 크면 덜 받지 않는다", round);
        }
        Check(sum == total, "SplitByWeight 합 = total", round);
    }
    const int afterWeight = g_failures;

    // 3) SplitKillExp
    for (int round = 0; round < kRounds; ++round)
    {
        const size_t n = 1 + rng() % RewardRule::kMaxShares;
        const uint32_t total = (round % 7 == 0) ? static_cast<uint32_t>(rng() % 20) : static_cast<uint32_t>(rng() % 500000);
        const uint8_t range = static_cast<uint8_t>(rng() % 50);
        const uint8_t pct = static_cast<uint8_t>(rng() % 3 == 0 ? 0 : rng() % 101);
        std::vector<KillShareIn> in(n);
        int perParty[4] = {};
        for (auto& x : in)
        {
            x.damage = (rng() % 4 == 0) ? 1 + rng() % 10 : 1 + rng() % 5000000;
            x.level = static_cast<uint8_t>(1 + rng() % 50);
            uint32_t party = rng() % 4;   // 0 = 파티 없음 · 1~3
            if (party != 0 && perParty[party] >= kMaxMembers)
                party = 0;                // 파티 정원
            if (party != 0)
                ++perParty[party];
            x.partyId = party;
        }
        std::vector<ShareOut> out(n);
        SplitKillExp(total, in, range, pct, out);

        std::vector<uint32_t> dmg(n), base(n);
        for (size_t i = 0; i < n; ++i)
            dmg[i] = in[i].damage;
        RewardRule::SplitByWeight(total, dmg, base);

        uint64_t sum = 0;
        uint64_t got[4] = {}, pool[4] = {};
        for (size_t i = 0; i < n; ++i)
        {
            sum += out[i].exp;
            const uint32_t p = in[i].partyId;
            const bool pooled = p != 0 && perParty[p] >= 2;
            if (pooled)
            {
                got[p] += out[i].exp;
                pool[p] += base[i];
            }
            else
            {
                Check(out[i].exp == base[i], "파티가 아니거나 혼자 받으면 1단계 몫 그대로", round);
                Check(!out[i].guaranteed, "보장 표시는 둘 이상 받는 파티만", round);
            }
        }
        Check(sum == total, "SplitKillExp 합 = total", round);
        for (int p = 1; p < 4; ++p)
            Check(got[p] == pool[p], "파티가 받은 합 = 파티의 1단계 몫의 합", round);
    }

    std::printf("%s: SplitExp %d회 실패 %d · SplitByWeight %d회 실패 %d · SplitKillExp %d회 실패 %d\n", g_failures == 0 ? "PASS" : "FAIL",
        kRounds, afterSplitExp, kRounds, afterWeight - afterSplitExp, kRounds, g_failures - afterWeight);
    return g_failures == 0 ? 0 : 1;
}
