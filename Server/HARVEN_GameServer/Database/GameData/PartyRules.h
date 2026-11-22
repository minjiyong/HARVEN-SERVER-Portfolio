#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "Database/GameData/GuildAuction.h"   // GuildRule::HasBannedWord — 모집 글 제목도 같은 금칙어
#include "Database/GameData/LevelTable.h"
#include "Database/GameData/RewardRules.h"   // RewardRule::SplitByWeight — 비례 나눔
#include "Protocol/Protocol.h"                // PartyPurpose · PartyInvitePolicy

// ─────────────────────────────────────────────────────────────────────────────
// 파티 · 모집 게시판 규칙 — 서버 · 클라 공용(검사 · 표시가 같은 값을 쓴다).
//
// 튜닝 값(인원 · 초대 시간 · 처치 보상 나눔 거리 · 쩔 보장 · 넘친 전리품 줍기 우선권 · 모집 글 만료)은 시트 PartyConst 표(한 행 → Bin/GameData)에서 온다 —
// 여기에는 구조 상수(패킷 줄 수 · 배열 크기 · 제목 바이트)와 요청 간격(도배 막기)만 둔다. 시트 값을 읽는 함수의 구현은
// Database/GameData/Load/Bind_Party.cpp. 설계는 Shared/Document/PARTY.md.
// ─────────────────────────────────────────────────────────────────────────────
namespace PartyRule
{
    // ── 구조 상수(패킷 · 배열) ──
    inline constexpr int      kMaxMembers         = 8;     // 파티 인원의 위 — 시트 max_members는 2~8(R114)
    inline constexpr int      kRosterRowsPerPacket = 4;    // PARTY_ROSTER_INF_PACKET::rows
    inline constexpr int      kRecruitRowsPerPage = 3;     // PARTY_RECRUIT_LIST_INF_PACKET::rows
    inline constexpr int      kMaxApplicants      = 8;     // 글 하나의 승인 대기 신청자(PARTY_APPLICANTS_INF_PACKET::rows)
    inline constexpr int      kTitleBytes         = 46;    // 한글 15자(45B) + 널
    inline constexpr int      kTitleMinChars      = 2;
    inline constexpr int      kTitleMaxChars      = 15;
    inline constexpr uint8_t  kAnyPurpose         = 0xFF;  // RecruitList 거르기 없음
    inline constexpr uint32_t kCombatWindowMs     = 5000;  // 이 안에 때렸거나 맞았으면 "전투 중"(PARTY_MEMBER_COMBAT — 기술 성장의 전투 판정과 같은 5초)
    // ── 요청 간격(사람마다, 소유 Zone이 거른다 — 넘치면 TooFast) ──
    inline constexpr uint32_t kQueryMs            = 500;   // 목록 · 구성 · 신청자 묻기
    inline constexpr uint32_t kTargetShareMs      = 150;   // 대상 공유(클라는 대상이 바뀔 때만 보낸다)

    // ── 시트 PartyConst(한 행) ──
    [[nodiscard]] uint8_t  MaxMembers() noexcept;          // 파티 인원(파티장 포함)
    [[nodiscard]] uint32_t InviteExpireMs() noexcept;      // 초대에 답할 시간
    [[nodiscard]] uint32_t OfflineKeepSec() noexcept;      // 접속이 끊긴 파티원을 남겨 두는 시간(다시 들어오면 그대로 파티원)
    [[nodiscard]] uint16_t SyncIntervalMs() noexcept;      // 파티원 HP · MP · 위치를 모아 보내는 간격
    [[nodiscard]] uint8_t  ExpShareRange() noexcept;       // 그로시 · 경험치를 나눠 받는 거리(칸, 처치 자리에서 체비쇼프 거리 — 파티가 아니어도)
    [[nodiscard]] uint8_t  CarryLevelRange() noexcept;     // 쩔 보장: 함께 나눠 받는 파티원 중 최고 레벨과의 차이가 이 안이면
    [[nodiscard]] uint8_t  CarryMinExpPct() noexcept;      // 쩔 보장: 피해를 조금만 줘도 파티가 받은 경험치의 이 %는 받는다
    [[nodiscard]] uint32_t LootOwnerMs() noexcept;         // 가방이 차 넘친 전리품: 이 시간까지 주인(막타)만
    [[nodiscard]] uint32_t LootPartyMs() noexcept;         // 넘친 전리품: 이 시간까지 주인 + 주인의 파티원(그 뒤 누구나, 30초에 사라진다)
    [[nodiscard]] uint32_t RecruitIdleSec() noexcept;      // 모집 글: 이 시간 동안 움직임이 없으면 닫는다

    [[nodiscard]] constexpr bool ValidPurpose(uint8_t p) noexcept { return p < static_cast<uint8_t>(PartyPurpose::Count); }
    [[nodiscard]] constexpr bool ValidPolicy(uint8_t p) noexcept { return p <= static_cast<uint8_t>(PartyInvitePolicy::Anyone); }
    [[nodiscard]] constexpr uint8_t ChebyshevDistance(int x0, int y0, int x1, int y1) noexcept
    {
        const int dx = x0 > x1 ? x0 - x1 : x1 - x0;
        const int dy = y0 > y1 ? y0 - y1 : y1 - y0;
        const int d = dx > dy ? dx : dy;
        return static_cast<uint8_t>(d > 255 ? 255 : d);
    }

    // 모집 글 제목 검사(서버가 판정 — 클라는 입력 중 안내에 같은 함수를 쓴다). UTF-8 바이트 검사(제어 문자 · 깨진 글자)는 따로(CChatHelper::IsValidUtf8).
    // 글자 수 2~15 · 앞뒤 공백 없음 · 금칙어(결사 이름과 같은 표 — 운영 사칭 · 욕설) 없음.
    enum class TitleCheck : uint8_t { Ok, TooShort, TooLong, Blank, Banned };   // 끝에만 추가
    [[nodiscard]] constexpr TitleCheck CheckTitle(const char* s) noexcept
    {
        int chars = 0;
        size_t n = 0;
        for (; s[n] != '\0'; ++n)
            if ((static_cast<unsigned char>(s[n]) & 0xC0) != 0x80)   // 이음 바이트가 아니면 글자 하나
                ++chars;
        if (n == 0) return TitleCheck::TooShort;
        if (s[0] == ' ' || s[n - 1] == ' ') return TitleCheck::Blank;
        if (chars < kTitleMinChars) return TitleCheck::TooShort;
        if (chars > kTitleMaxChars || n >= static_cast<size_t>(kTitleBytes)) return TitleCheck::TooLong;
        return GuildRule::HasBannedWord(s) ? TitleCheck::Banned : TitleCheck::Ok;
    }
    static_assert(CheckTitle("모로즈 잡으실 분") == TitleCheck::Ok && CheckTitle("Lv20 hunt") == TitleCheck::Ok);
    static_assert(CheckTitle("가") == TitleCheck::TooShort && CheckTitle(" 사냥") == TitleCheck::Blank && CheckTitle("운영자 파티") == TitleCheck::Banned);
    static_assert(CheckTitle("가나다라마바사아자차카타파하가나") == TitleCheck::TooLong);

    // ─────────────────────────────────────────────────────────────────────────
    // 파티 몫 나누기 — 파티가 받은 경험치 total을 그 파티원(피해를 준 사람만)에게 준 피해에 비례해 나눈다(SplitKillExp가 부른다). 합은 언제나 total.
    //  1) 비례: total × 내 피해 / 모두의 피해 — 나머지는 소수 부분이 큰 순으로 1씩(같으면 피해가 큰 · 앞 사람).
    //  2) 쩔 보장: 나눠 받는 사람 중 최고 레벨과의 차이가 carryLevelRange 이하인 사람은 적어도 floor = total × carryMinPct / 100을 받는다.
    //     모자란 몫은 floor를 넘게 받은 사람들에게서 넘는 만큼에 비례해 가져온다(그들도 floor 아래로는 내려가지 않는다).
    //     보장받는 사람이 많아 floor × 사람 수 > total이면 floor = total / 사람 수.
    //  피해 0인 사람은 넣지 않는다(호출하는 쪽이 거른다 — "아주 조금이라도 때려야" 나눠 받는다).
    // ─────────────────────────────────────────────────────────────────────────
    struct ShareIn
    {
        uint32_t damage = 0;      // > 0(피해 기록이 32비트에서 멈춘다 — 곱이 64비트 안에 든다)
        uint8_t  level  = 1;
    };
    struct ShareOut
    {
        uint32_t exp        = 0;
        bool     guaranteed = false;   // 쩔 보장으로 몫이 올라갔다
    };

    // out.size() == in.size(). 사람은 kMaxMembers 이하(넘는 줄은 0).
    constexpr void SplitExp(uint32_t total, std::span<const ShareIn> in, uint8_t carryLevelRange, uint8_t carryMinPct, std::span<ShareOut> out) noexcept
    {
        const size_t n = in.size() < out.size() ? in.size() : out.size();
        for (size_t i = 0; i < out.size(); ++i)
            out[i] = ShareOut{};
        if (n == 0 || total == 0)
            return;
        const size_t m = n < static_cast<size_t>(kMaxMembers) ? n : static_cast<size_t>(kMaxMembers);

        uint8_t maxLevel = 0;
        uint32_t damage[kMaxMembers] = {};
        for (size_t i = 0; i < m; ++i)
        {
            damage[i] = in[i].damage;
            if (in[i].level > maxLevel) maxLevel = in[i].level;
        }

        // 1) 비례 — 몫 · 나머지(나머지가 큰 순 · 피해가 큰 순 · 앞 사람 순으로 1씩)
        uint32_t share[kMaxMembers] = {};
        RewardRule::SplitByWeight(total, std::span<const uint32_t>(damage, m), std::span<uint32_t>(share, m));
        uint64_t base[kMaxMembers] = {};
        for (size_t i = 0; i < m; ++i)
            base[i] = share[i];

        // 2) 쩔 보장
        if (carryMinPct > 0)
        {
            bool eligible[kMaxMembers] = {};
            uint64_t k = 0;
            for (size_t i = 0; i < m; ++i)
                if (static_cast<int>(maxLevel) - static_cast<int>(in[i].level) <= static_cast<int>(carryLevelRange))
                {
                    eligible[i] = true;
                    ++k;
                }
            uint64_t floor = static_cast<uint64_t>(total) * (carryMinPct > 100 ? 100 : carryMinPct) / 100;
            if (k > 0 && floor * k > total)
                floor = total / k;

            uint64_t need[kMaxMembers] = {};
            uint64_t excess[kMaxMembers] = {};
            uint64_t sumNeed = 0, sumExcess = 0;
            for (size_t i = 0; i < m; ++i)
            {
                if (eligible[i] && base[i] < floor)
                {
                    need[i] = floor - base[i];
                    sumNeed += need[i];
                    continue;
                }
                const uint64_t keep = eligible[i] ? floor : 0;
                excess[i] = base[i] > keep ? base[i] - keep : 0;
                sumExcess += excess[i];
            }
            if (sumNeed > 0 && sumExcess >= sumNeed)   // 증명: sumExcess − sumNeed = total − k × floor ≥ 0
            {
                uint64_t take[kMaxMembers] = {};
                uint64_t frac[kMaxMembers] = {};
                uint64_t taken = 0;
                for (size_t i = 0; i < m; ++i)
                {
                    if (excess[i] == 0) continue;
                    const uint64_t q = sumNeed * excess[i];   // 둘 다 ≤ total(32비트)
                    take[i] = static_cast<uint64_t>(q / sumExcess);
                    frac[i] = static_cast<uint64_t>(q % sumExcess);
                    taken += take[i];
                }
                for (uint64_t left = sumNeed - taken; left > 0; --left)
                {
                    size_t best = m;
                    for (size_t i = 0; i < m; ++i)
                        if (frac[i] > 0 && (best == m || frac[i] > frac[best]))
                            best = i;
                    if (best == m) break;   // 일어나지 않는다(소수 부분의 합 = left)
                    ++take[best];
                    frac[best] = 0;
                }
                for (size_t i = 0; i < m; ++i)
                {
                    base[i] -= take[i];
                    if (need[i] > 0)
                    {
                        base[i] += need[i];
                        out[i].guaranteed = true;
                    }
                }
            }
        }
        for (size_t i = 0; i < m; ++i)
            out[i].exp = static_cast<uint32_t>(base[i]);
    }

    namespace Detail
    {
        // 컴파일 시험 — 합 보존 · 비례 · 쩔 보장
        constexpr bool SplitCase(uint32_t total, uint32_t d0, uint8_t l0, uint32_t d1, uint8_t l1, uint8_t range, uint8_t pct,
            uint32_t want0, uint32_t want1, bool g1)
        {
            const ShareIn in[2] = { { d0, l0 }, { d1, l1 } };
            ShareOut out[2] = {};
            SplitExp(total, in, range, pct, out);
            return out[0].exp == want0 && out[1].exp == want1 && out[1].guaranteed == g1 && out[0].exp + out[1].exp == total;
        }
        static_assert(SplitCase(100, 3, 10, 1, 10, 10, 0, 75, 25, false));    // 3:1
        static_assert(SplitCase(100, 99, 40, 1, 12, 30, 20, 80, 20, true));   // 쩔: 1% 피해 → 20% 보장
        static_assert(SplitCase(100, 99, 40, 1, 5, 30, 20, 99, 1, false));    // 레벨 차 35 > 30 — 보장 없음(비례만)
        static_assert(SplitCase(7, 1, 1, 1, 1, 0, 0, 4, 3, false));           // 나머지 1은 앞 사람
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 처치 경험치 — 피해를 준 사람 모두(파티가 아니어도, 모르는 사람끼리도)에게 준 피해에 비례(RewardRule::SplitByWeight)로 나눈 뒤,
    // 같은 파티(partyId ≠ 0)에서 둘 이상 받으면 그 파티의 몫을 모아 파티 안에서 SplitExp로 다시 나눈다.
    //  · 쩔 보장은 파티 몫 안에서만 움직인다 — 모르는 사람의 몫은 줄지도 늘지도 않는다.
    //  · 혼자인 사람(파티 없음 · 파티원이 혼자 받음)은 비례 몫 그대로. 합은 언제나 total.
    //  · 한 파티의 사람은 kMaxMembers 이하(파티 정원) — 넘으면(일어나지 않는다) 그 파티는 비례 몫 그대로 둔다.
    // ─────────────────────────────────────────────────────────────────────────
    struct KillShareIn
    {
        uint32_t damage  = 0;
        uint8_t  level   = 1;
        uint32_t partyId = 0;     // 0 = 파티 없음
    };

    // out.size() ≥ in.size(). 사람은 RewardRule::kMaxShares까지(넘는 줄은 0).
    constexpr void SplitKillExp(uint32_t total, std::span<const KillShareIn> in, uint8_t carryLevelRange, uint8_t carryMinPct, std::span<ShareOut> out) noexcept
    {
        for (size_t i = 0; i < out.size(); ++i)
            out[i] = ShareOut{};
        size_t n = in.size() < out.size() ? in.size() : out.size();
        if (n > RewardRule::kMaxShares)
            n = RewardRule::kMaxShares;
        if (n == 0 || total == 0)
            return;

        // 1) 모두에게 피해 비례
        uint32_t damage[RewardRule::kMaxShares] = {};
        uint32_t base[RewardRule::kMaxShares] = {};
        for (size_t i = 0; i < n; ++i)
            damage[i] = in[i].damage;
        RewardRule::SplitByWeight(total, std::span<const uint32_t>(damage, n), std::span<uint32_t>(base, n));
        for (size_t i = 0; i < n; ++i)
            out[i].exp = base[i];

        // 2) 파티마다 몫을 모아 다시 나눈다(쩔 보장)
        bool done[RewardRule::kMaxShares] = {};
        for (size_t i = 0; i < n; ++i)
        {
            if (done[i] || in[i].partyId == 0)
                continue;
            size_t who[kMaxMembers] = {};
            ShareIn sin[kMaxMembers] = {};
            size_t k = 0;
            uint64_t pool = 0;
            bool over = false;
            for (size_t j = i; j < n; ++j)
            {
                if (in[j].partyId != in[i].partyId)
                    continue;
                done[j] = true;
                if (k == static_cast<size_t>(kMaxMembers))
                {
                    over = true;
                    continue;
                }
                who[k] = j;
                sin[k] = ShareIn{ in[j].damage, in[j].level };
                pool += base[j];
                ++k;
            }
            if (k < 2 || over)
                continue;
            ShareOut sout[kMaxMembers] = {};
            SplitExp(static_cast<uint32_t>(pool), std::span<const ShareIn>(sin, k), carryLevelRange, carryMinPct, std::span<ShareOut>(sout, k));
            for (size_t t = 0; t < k; ++t)
                out[who[t]] = sout[t];
        }
    }

    namespace Detail
    {
        constexpr bool KillCase()
        {
            // 모르는 사람 A(50) · 파티 1의 B(49, 레벨 40) · C(1, 레벨 12) — 범위 30 · 보장 20%
            //  비례 50 · 49 · 1 → 파티 몫 50을 다시: 49:1 → C는 50의 20% = 10 보장(B에게서) → A 50 · B 40 · C 10
            const KillShareIn in[3] = { { 50, 40, 0 }, { 49, 40, 1 }, { 1, 12, 1 } };
            ShareOut out[3] = {};
            SplitKillExp(100, in, 30, 20, out);
            return out[0].exp == 50 && !out[0].guaranteed && out[1].exp == 40 && out[2].exp == 10 && out[2].guaranteed;
        }
        constexpr bool StrangersCase()
        {
            // 파티가 없는 둘 — 비례만(3:1), 쩔 보장 없음
            const KillShareIn in[2] = { { 3, 40, 0 }, { 1, 12, 0 } };
            ShareOut out[2] = {};
            SplitKillExp(100, in, 30, 20, out);
            return out[0].exp == 75 && out[1].exp == 25 && !out[1].guaranteed;
        }
        constexpr bool TwoPartiesCase()
        {
            // 파티 1(60 · 20) · 파티 2(19 · 1) — 각 파티 몫 80 · 20 안에서만 보장(20%: 16 · 4)
            const KillShareIn in[4] = { { 60, 30, 1 }, { 20, 30, 1 }, { 19, 30, 2 }, { 1, 30, 2 } };
            ShareOut out[4] = {};
            SplitKillExp(100, in, 30, 20, out);
            return out[0].exp == 60 && out[1].exp == 20 && out[2].exp == 16 && out[3].exp == 4 && out[3].guaranteed
                && out[0].exp + out[1].exp + out[2].exp + out[3].exp == 100;
        }
        static_assert(KillCase());
        static_assert(StrangersCase());
        static_assert(TwoPartiesCase());
    }
}
