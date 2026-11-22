#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

// ─────────────────────────────────────────────────────────────────────────────
// 처치 보상 나눔 — 순수 함수(constexpr · 서버 · 시험 공용). 쓰는 곳: Content/Reward/RewardHelper.cpp, 설계: Shared/Document/PARTY.md 3.
//
//  · 아이템 = 막타를 친 사람(나누지 않는다). 그로시 · 경험치 = 몬스터에게 피해를 준 사람 모두(파티가 아니어도)가 준 피해에 비례.
//  · 나눔은 최대 나머지 방식 — 몫(total × 내 몫 / 모두의 몫)을 먼저 주고, 남은 1씩은 소수 부분이 큰 순(같으면 몫이 큰 · 앞 사람)으로.
//    합은 언제나 total(늘지도 줄지도 않는다). 경험치는 여기서 나눈 뒤 파티 몫을 다시 나눈다(PartyRule::SplitKillExp — 쩔 보장).
// ─────────────────────────────────────────────────────────────────────────────
namespace RewardRule
{
    // 한 번에 나누는 사람의 위 — 몬스터 피해 기록 16칸 + 막타(CDamageLedger::kMax + 1, Content/Reward/DamageLedger.h가 맞춘다)
    inline constexpr size_t kMaxShares = 17;

    // total을 weight에 비례해 out에 나눈다(weight 0은 1로 본다 — 막타처럼 기록이 비어도 몫이 있다).
    // out.size() ≥ weight.size(). 사람은 kMaxShares까지(넘는 줄은 0).
    // weight · total은 32비트 — 곱이 64비트 안에 든다(피해 기록이 32비트에서 멈춘다).
    constexpr void SplitByWeight(uint32_t total, std::span<const uint32_t> weight, std::span<uint32_t> out) noexcept
    {
        for (size_t i = 0; i < out.size(); ++i)
            out[i] = 0;
        size_t m = weight.size() < out.size() ? weight.size() : out.size();
        if (m > kMaxShares)
            m = kMaxShares;
        if (m == 0 || total == 0)
            return;

        uint64_t sum = 0;
        for (size_t i = 0; i < m; ++i)
            sum += weight[i] > 0 ? weight[i] : 1;

        uint64_t rem[kMaxShares] = {};
        uint64_t given = 0;
        for (size_t i = 0; i < m; ++i)
        {
            const uint64_t q = static_cast<uint64_t>(total) * (weight[i] > 0 ? weight[i] : 1u);
            out[i] = static_cast<uint32_t>(q / sum);
            rem[i] = q % sum;
            given += out[i];
        }
        for (uint64_t left = total - given; left > 0; --left)   // left < m(소수 부분의 합 = left)
        {
            size_t best = m;
            for (size_t i = 0; i < m; ++i)
            {
                if (rem[i] == 0 && best != m) continue;
                if (best == m || rem[i] > rem[best] || (rem[i] == rem[best] && weight[i] > weight[best]))
                    best = i;
            }
            ++out[best];
            rem[best] = 0;   // 한 사람에게 두 번 주지 않는다
        }
    }

    namespace Detail
    {
        // 컴파일 시험 — 합 보존 · 비례 · 나머지 순서
        template<size_t N>
        constexpr bool WeightCase(uint32_t total, const uint32_t (&w)[N], const uint32_t (&want)[N])
        {
            uint32_t out[N] = {};
            SplitByWeight(total, w, out);
            uint64_t sum = 0;
            for (size_t i = 0; i < N; ++i)
            {
                if (out[i] != want[i]) return false;
                sum += out[i];
            }
            return sum == total;
        }
        static_assert(WeightCase(100, { 3u, 1u }, { 75u, 25u }));
        static_assert(WeightCase(7, { 1u, 1u }, { 4u, 3u }));              // 같으면 앞 사람
        static_assert(WeightCase(10, { 1u, 1u, 1u }, { 4u, 3u, 3u }));
        static_assert(WeightCase(10, { 1u, 2u, 1u }, { 3u, 5u, 2u }));     // 2.5 · 5 · 2.5 → 나머지가 같으면 앞 사람
        static_assert(WeightCase(5, { 0u, 0u }, { 3u, 2u }));              // 0은 1로 본다
        static_assert(WeightCase(1, { 1u, 9u }, { 0u, 1u }));              // 나머지가 큰 쪽
        static_assert(WeightCase(0, { 5u, 5u }, { 0u, 0u }));
    }
}
