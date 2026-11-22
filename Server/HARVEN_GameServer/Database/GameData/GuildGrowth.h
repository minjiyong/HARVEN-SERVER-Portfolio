#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "Database/GameData/GuildAuction.h"   // GuildRule::Rank · HasBannedWord

// ─────────────────────────────────────────────────────────────────────────────
// 결사 성장 — 레벨 · 경험치 · 기여도 · 주간 임무 · 금고(결사 인장) · 결사 상점 · 업적 · 칭호 · 출석 · 직위 권한 · 결사장 위임 청구.
// 서버 · 클라 공용 규칙(검사 · 표시가 같은 값을 쓴다).
//
// 튜닝 값은 시트(GuildConst · GuildLevel · GuildMission · GuildShop · GuildAchievement · GuildTitle · GuildAttend → Bin/GameData)에서 온다.
// 여기에는 구조 상수(패킷 줄 수 · 바이트 · 배열 상한), 시트 열에 쓰는 열거형, 순수 함수(주 · 날 번호 · 레벨 · 임무 고르기 · 권한)만 둔다.
// 시트 값을 읽는 함수의 구현은 Database/GameData/Load/Bind_Guild.cpp. 설계는 Shared/Document/GUILD.md.
// ─────────────────────────────────────────────────────────────────────────────

// 시트 열(enum: · flags:)에 쓰는 열거형 — 값은 영구(끝에만 추가). datatool gen이 Gen/EnumNames.h로 이름 표를 만든다.
enum class GuildMissionKind : uint8_t
{
    Kill,      // 결사원이 몬스터를 쓰러뜨린 수(한 마리 = 1 — 여럿이 함께 때려도 1). sub = 몬스터 최소 레벨(0 = 아무거나)
    Boss,      // 보스를 쓰러뜨린 수
    Collect,   // 전리품으로 얻은 개수. sub = 아이템 id(0 = 강화 재료 — 각인 · 강화 효과 아이템)
    Gear,      // 전리품으로 얻은 장비 수. sub = 최소 희귀도(Rarity 값)
    Active,    // 이번 주 sub분 이상 활동한 결사원 수(활동 = 그 1분 안에 몬스터를 함께 쓰러뜨림 — 접속만으로는 세지 않는다)
    Quest,     // 결사원이 마친 퀘스트 수
};
enum class GuildAchKind : uint8_t
{
    Kills,     // 누적 처치
    BossKills, // 누적 보스 처치
    Missions,  // 주간 임무 달성 수
    Coop,      // 협력 처치(새 결사원과 함께 쓰러뜨림)
    Days,      // 세운 뒤 지난 날
    Attend,    // 출석 1단계 이상을 채운 날 수
    Level,     // 결사 레벨
    Members,   // 결사원 수(가장 많았던 때)
};
// 직위 권한 비트 — 결사장은 늘 전부. 나머지 직위는 결사마다 결사장이 정한다(기본값 시트 GuildConst perm_*).
enum class GuildPerm : uint8_t
{
    Invite  = 1 << 0,   // 초대
    Approve = 1 << 1,   // 가입 신청 승인 · 거절
    Kick    = 1 << 2,   // 추방(자기보다 낮은 직위만)
    Notice  = 1 << 3,   // 공지 · 가입 안내 · 모집 설정
    Board   = 1 << 4,   // 남의 글 내리기 · 고정
    Rank    = 1 << 5,   // 직위 바꾸기(자기보다 낮은 직위 사이만)
    Shop    = 1 << 6,   // 결사 상점(금고의 인장으로 사기)
};

namespace GuildGrowth
{
    // ── 구조 상수(패킷 · 배열) ──
    inline constexpr int      kMaxLevel          = 20;    // GuildLevel 행 수 위(R117)
    inline constexpr int      kMaxMembersCap     = 60;    // GuildLevel.max_members 위(R117 — 구성원 페이지 · 바이트 칸)
    inline constexpr int      kMissionSlotsMax   = 3;     // GUILD_EX_MISSIONS_INF_PACKET::rows(R118)
    inline constexpr int      kTitleIdMax        = 31;    // 칭호 id 1~31(열린 칭호를 u32 비트로 보낸다 — R119)
    inline constexpr int      kAttendTiersMax    = 5;     // GuildAttend 행 수 위(R120)
    inline constexpr int      kCatalogMax        = 64;    // 임무 · 상점 · 업적 표 행 수 위(R121 — 진행 저장 칸)
    inline constexpr int      kTitleBytes        = 25;    // 칭호 이름(한글 8자 + 널)
    inline constexpr int      kNameBytes         = 31;    // 임무 · 업적 이름(한글 10자 + 널)
    inline constexpr int      kPostTitleBytes    = 31;    // 글 제목(한글 10자 + 널)
    inline constexpr int      kPostBodyBytes     = 151;   // 글 본문(한글 50자 + 널)
    inline constexpr int      kIntroBytes        = 121;   // 가입 안내(한글 40자 + 널)
    inline constexpr int      kLogWhatBytes      = 31;    // 기록 한 줄의 덧말(아이템 · 임무 이름)
    inline constexpr int      kApplicantsMax     = 8;     // GUILD_EX_APPLICANTS_INF_PACKET::rows
    inline constexpr int      kPostMaxCap        = 200;   // GuildConst.post_max 위(R122)
    inline constexpr int      kLogKeepCap        = 500;   // GuildConst.log_keep 위(R122)
    // 한 패킷의 줄 수
    inline constexpr int      kStatsPerPage      = 7;
    inline constexpr int      kLogPerPage        = 4;
    inline constexpr int      kBoardPerPage      = 4;
    inline constexpr int      kRecruitPerPage    = 6;
    inline constexpr int      kShopPerPage       = 10;
    inline constexpr int      kAchPerPage        = 4;
    inline constexpr int      kTitlesPerPage     = 8;
    // ── 요청 간격(사람마다, 소유 Zone이 거른다 — 넘치면 Busy) ──
    inline constexpr uint32_t kQueryMs           = 250;   // 목록 · 정보 묻기
    inline constexpr uint32_t kPostMs            = 5000;  // 글 쓰기
    inline constexpr uint8_t  kAllPerms          = 0x7F;

    // ── 시트 행(파사드가 돌려준다 — 문자열은 스냅샷 소유) ──
    struct ConstRow
    {
        uint32_t weekPeriodSec = 604800;      // 한 주(초) — 시험 데이터는 짧게
        uint32_t weekOffsetSec = 334800;      // 주의 시작(유닉스 초 기준 어긋남 — 기본 월요일 06:00 KST)
        uint32_t dayPeriodSec = 86400;
        uint32_t dayOffsetSec = 75600;        // 날의 시작(기본 06:00 KST)
        uint8_t  killExpPct = 0;              // 처치 → 결사 경험치(몬스터 경험치의 %)
        uint32_t memberDailyExpCap = 0;       // 한 사람이 하루에 사냥으로 올려 줄 수 있는 결사 경험치
        uint16_t killContrib = 0;             // 처치 한 번의 기여도
        uint16_t bossContrib = 0;             // 보스 처치의 기여도
        uint8_t  coopNewDays = 0;             // 들어온 지 이 날 수 안이면 "새 결사원"
        uint16_t coopContribBonus = 0;        // 새 결사원과 함께 쓰러뜨리면 덧붙는 기여도
        uint16_t attendActiveMin = 0;         // 하루 이만큼(분) 활동하면 출석
        uint8_t  missionSlots = 0;            // 한 주의 임무 수(1~kMissionSlotsMax)
        uint8_t  missionBonusMembers = 0;     // 임무에 이만큼 이상(서로 다른 결사원) 참여하면
        uint8_t  missionBonusPct = 0;         // 보상에 이 %를 더 준다
        uint32_t leaderAbsentSec = 0;         // 결사장이 이만큼 들어오지 않으면 위임 청구 가능
        uint32_t claimGraceSec = 0;           // 청구 뒤 이만큼 기다린다(그 사이 결사장이 들어오면 취소)
        uint16_t postMax = 0;                 // 게시판 글 수(넘으면 고정하지 않은 가장 오래된 글부터 내린다)
        uint16_t logKeep = 0;                 // 기록 줄 수(메모리 · 불러오기)
        uint32_t applyExpireSec = 0;          // 가입 신청이 남아 있는 시간
        uint8_t  applyMax = 0;                // 결사 하나의 대기 신청(1~kApplicantsMax)
        uint8_t  permOfficer = 0;             // 직위 권한 기본값(GuildPerm 비트)
        uint8_t  permElite = 0;
        uint8_t  permMember = 0;
    };
    struct LevelRow
    {
        uint8_t  level = 0;
        uint32_t exp = 0;                     // 이 레벨이 되는 누적 경험치(1레벨 = 0)
        uint8_t  maxMembers = 0;
        uint8_t  frame = 0;                   // 문장 테두리 단계(0~3 — 표시)
        uint8_t  titleId = 0;                 // 이 레벨이 되면 여는 칭호(0 = 없음)
    };
    struct MissionRow
    {
        uint16_t         id = 0;
        const char*      name = "";
        GuildMissionKind kind = GuildMissionKind::Kill;
        uint32_t         target = 0;
        uint32_t         sub = 0;             // 종류마다 뜻이 다르다(GuildMissionKind 주석)
        uint8_t          minLevel = 1;        // 이 결사 레벨부터 뽑힌다
        uint32_t         exp = 0;
        uint32_t         seals = 0;
        uint16_t         contrib = 0;         // 달성하면 참여한 결사원마다
        uint16_t         weight = 0;          // 뽑힐 무게(0 = 뽑지 않음)
    };
    struct ShopRow
    {
        uint16_t id = 0;
        uint16_t itemId = 0;
        uint16_t count = 0;
        uint32_t price = 0;                   // 결사 인장
        uint8_t  minLevel = 1;
        uint8_t  weeklyLimit = 0;             // 한 사람이 한 주에(0 = 제한 없음)
    };
    struct AchRow
    {
        uint16_t     id = 0;
        const char*  name = "";
        GuildAchKind kind = GuildAchKind::Kills;
        uint32_t     target = 0;
        uint32_t     seals = 0;
        uint32_t     exp = 0;
        uint8_t      titleId = 0;
    };
    struct TitleRow
    {
        uint8_t     id = 0;
        const char* name = "";
    };
    struct AttendRow
    {
        uint8_t  tier = 0;                    // 1부터
        uint8_t  members = 0;                 // 하루 출석한 결사원 수
        uint32_t seals = 0;
        uint32_t exp = 0;
        uint16_t contrib = 0;                 // 그날 출석한 사람마다
    };

    // ── 시트 파사드(Bind_Guild.cpp) ──
    [[nodiscard]] const ConstRow&            Const() noexcept;
    [[nodiscard]] std::span<const LevelRow>  Levels() noexcept;          // level 순
    [[nodiscard]] std::span<const MissionRow> Missions() noexcept;       // id 순
    [[nodiscard]] std::span<const ShopRow>   Shop() noexcept;            // id 순
    [[nodiscard]] std::span<const AchRow>    Achievements() noexcept;    // id 순
    [[nodiscard]] std::span<const TitleRow>  Titles() noexcept;          // id 순
    [[nodiscard]] std::span<const AttendRow> AttendTiers() noexcept;     // tier 순
    [[nodiscard]] const MissionRow* FindMission(uint16_t id) noexcept;
    [[nodiscard]] const ShopRow*    FindShop(uint16_t id) noexcept;
    [[nodiscard]] const AchRow*     FindAch(uint16_t id) noexcept;
    [[nodiscard]] const TitleRow*   FindTitle(uint8_t id) noexcept;

    // ── 순수 함수 ──
    // 주 · 날 번호(유닉스 초 → 번호). now가 offset보다 작으면 0.
    [[nodiscard]] constexpr uint32_t PeriodId(int64_t now, uint32_t period, uint32_t offset) noexcept
    {
        if (period == 0 || now < static_cast<int64_t>(offset)) return 0;
        return static_cast<uint32_t>((now - offset) / period);
    }
    // 다음 번호까지 남은 초
    [[nodiscard]] constexpr uint32_t PeriodLeft(int64_t now, uint32_t period, uint32_t offset) noexcept
    {
        if (period == 0) return 0;
        if (now < static_cast<int64_t>(offset)) return static_cast<uint32_t>(offset - now);
        return period - static_cast<uint32_t>((now - offset) % period);
    }
    static_assert(PeriodId(334800, 604800, 334800) == 0 && PeriodId(334800 + 604800, 604800, 334800) == 1);
    static_assert(PeriodLeft(334800 + 10, 604800, 334800) == 604790);

    // 누적 경험치 → 레벨(levels는 level 순 · exp가 늘어난다 — R117). 표가 비면 1.
    [[nodiscard]] constexpr uint8_t LevelForExp(std::span<const LevelRow> levels, uint32_t exp) noexcept
    {
        uint8_t lv = 1;
        for (const LevelRow& r : levels)
            if (exp >= r.exp) lv = r.level;
            else break;
        return lv;
    }
    [[nodiscard]] constexpr const LevelRow* LevelRowOf(std::span<const LevelRow> levels, uint8_t level) noexcept
    {
        for (const LevelRow& r : levels)
            if (r.level == level) return &r;
        return nullptr;
    }

    // 권한: 결사장은 늘 전부. masks[rank]는 결사마다 정한 값(Rank 차례 — [0]은 쓰지 않는다)
    [[nodiscard]] constexpr bool HasPerm(GuildRule::Rank rank, const uint8_t (&masks)[4], GuildPerm perm) noexcept
    {
        if (rank == GuildRule::Rank::Leader) return true;
        const auto r = static_cast<uint8_t>(rank);
        return r < 4 && (masks[r] & static_cast<uint8_t>(perm)) != 0;
    }
    // 사람을 다루는 일(추방 · 직위)은 권한에 더해 자기보다 낮은 직위에게만
    [[nodiscard]] constexpr bool Outranks(GuildRule::Rank actor, GuildRule::Rank target) noexcept { return actor < target; }
    // 직위 바꾸기: Rank 권한 + 대상 · 바꿀 직위 모두 자기보다 낮다(결사장으로는 넘기기만)
    [[nodiscard]] constexpr bool CanSetRank(GuildRule::Rank actor, GuildRule::Rank target, GuildRule::Rank to, const uint8_t (&masks)[4]) noexcept
    {
        if (to == GuildRule::Rank::Leader || target == GuildRule::Rank::Leader || to >= GuildRule::Rank::Count) return false;
        if (!HasPerm(actor, masks, GuildPerm::Rank)) return false;
        return actor == GuildRule::Rank::Leader || (Outranks(actor, target) && Outranks(actor, to));
    }
    // 예전 고정 규칙(GuildAuction.h — CanInvite · CanKick · CanNotice · CanSetRank)과 같은 기본값
    inline constexpr uint8_t kLegacyOfficer = static_cast<uint8_t>(GuildPerm::Invite) | static_cast<uint8_t>(GuildPerm::Approve) | static_cast<uint8_t>(GuildPerm::Kick)
        | static_cast<uint8_t>(GuildPerm::Notice) | static_cast<uint8_t>(GuildPerm::Board) | static_cast<uint8_t>(GuildPerm::Rank) | static_cast<uint8_t>(GuildPerm::Shop);
    inline constexpr uint8_t kLegacyElite = static_cast<uint8_t>(GuildPerm::Invite) | static_cast<uint8_t>(GuildPerm::Shop);
    inline constexpr uint8_t kLegacyMember = static_cast<uint8_t>(GuildPerm::Shop);
    static_assert([]
    {
        using GuildRule::Rank;
        const uint8_t m[4] = { 0, kLegacyOfficer, kLegacyElite, kLegacyMember };
        for (int a = 0; a < 4; ++a)
        {
            const auto ra = static_cast<Rank>(a);
            if (HasPerm(ra, m, GuildPerm::Invite) != GuildRule::CanInvite(ra)) return false;
            if (HasPerm(ra, m, GuildPerm::Notice) != GuildRule::CanNotice(ra)) return false;
            for (int t = 0; t < 4; ++t)
            {
                const auto rt = static_cast<Rank>(t);
                if ((HasPerm(ra, m, GuildPerm::Kick) && Outranks(ra, rt)) != GuildRule::CanKick(ra, rt)) return false;
                for (int to = 0; to < 4; ++to)
                    if (CanSetRank(ra, rt, static_cast<Rank>(to), m) != GuildRule::CanSetRank(ra, rt, static_cast<Rank>(to))) return false;
            }
        }
        return true;
    }(), "기본 권한은 예전 고정 규칙과 같다");

    // 위임 청구: 결사장이 접속해 있지 않고 leaderAbsentSec 넘게 들어오지 않았다. 청구하는 사람은 결사장을 뺀 가장 높은 직위(같으면 누구나).
    [[nodiscard]] constexpr bool LeaderAbsent(bool leaderOnline, int64_t leaderLastSeen, int64_t now, uint32_t absentSec) noexcept
    {
        return !leaderOnline && leaderLastSeen > 0 && now - leaderLastSeen >= static_cast<int64_t>(absentSec);
    }

    // 새 결사원: 들어온 지 days 날 안(협력 처치)
    [[nodiscard]] constexpr bool IsNewMember(int64_t joinedAt, int64_t now, uint8_t days) noexcept
    {
        return joinedAt > 0 && now >= joinedAt && now - joinedAt < static_cast<int64_t>(days) * 86400;
    }

    // 출석 단계: 출석 수 → 채운 가장 높은 단계(tiers는 tier 순 · members가 늘어난다 — R120). 못 채우면 0.
    [[nodiscard]] constexpr uint8_t AttendTierFor(std::span<const AttendRow> tiers, uint32_t attended) noexcept
    {
        uint8_t t = 0;
        for (const AttendRow& r : tiers)
            if (attended >= r.members) t = r.tier;
        return t;
    }

    // 임무 보상(참여 덧보상 포함)
    [[nodiscard]] constexpr uint32_t WithBonus(uint32_t base, uint32_t contributors, uint8_t bonusMembers, uint8_t bonusPct) noexcept
    {
        if (bonusMembers == 0 || contributors < bonusMembers) return base;
        return base + static_cast<uint32_t>(static_cast<uint64_t>(base) * bonusPct / 100);
    }

    // 한 주의 임무 고르기 — 결사 번호 · 주 번호로 정해진다(서버를 다시 켜도 같은 임무). minLevel ≤ level · weight > 0인 것 중
    // 무게대로 겹치지 않게 slots개(모자라면 있는 만큼). out에 id, 돌려주는 값 = 고른 수.
    [[nodiscard]] constexpr uint64_t SplitMix(uint64_t& s) noexcept
    {
        uint64_t z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    [[nodiscard]] constexpr size_t PickMissions(std::span<const MissionRow> pool, uint8_t level, uint32_t guildId, uint32_t weekId,
        size_t slots, std::span<uint16_t> out) noexcept
    {
        bool used[kCatalogMax]{};
        uint64_t seed = (static_cast<uint64_t>(guildId) << 32) ^ weekId ^ 0x6A09E667F3BCC909ull;
        size_t n = 0;
        const size_t cap = pool.size() < static_cast<size_t>(kCatalogMax) ? pool.size() : static_cast<size_t>(kCatalogMax);
        while (n < slots && n < out.size())
        {
            uint64_t total = 0;
            for (size_t i = 0; i < cap; ++i)
                if (!used[i] && pool[i].minLevel <= level) total += pool[i].weight;
            if (total == 0) break;
            uint64_t roll = SplitMix(seed) % total;
            for (size_t i = 0; i < cap; ++i)
            {
                if (used[i] || pool[i].minLevel > level || pool[i].weight == 0) continue;
                if (roll < pool[i].weight)
                {
                    used[i] = true;
                    out[n++] = pool[i].id;
                    break;
                }
                roll -= pool[i].weight;
            }
        }
        return n;
    }

    // 글 검사(서버가 판정 — 클라는 입력 중 안내에 같은 함수를 쓴다). UTF-8 바이트 검사(제어 문자 · 깨진 글자)는 따로(CChatHelper::IsValidUtf8).
    // 글자 수 minChars~maxChars · 공백만은 안 됨 · 금칙어(결사 이름과 같은 표).
    enum class TextCheck : uint8_t { Ok, TooShort, TooLong, Blank, Banned };   // 끝에만 추가
    [[nodiscard]] constexpr int CountChars(const char* s) noexcept
    {
        int n = 0;
        for (size_t i = 0; s[i] != '\0'; ++i)
            if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) ++n;
        return n;
    }
    [[nodiscard]] constexpr TextCheck CheckText(const char* s, int minChars, int maxChars) noexcept
    {
        const int n = CountChars(s);
        if (n < minChars) return TextCheck::TooShort;
        if (n > maxChars) return TextCheck::TooLong;
        bool blank = true;
        for (size_t i = 0; s[i] != '\0'; ++i)
            if (s[i] != ' ') { blank = false; break; }
        if (blank && n > 0) return TextCheck::Blank;
        return GuildRule::HasBannedWord(s) ? TextCheck::Banned : TextCheck::Ok;
    }
    inline constexpr int kPostTitleMinChars = 2, kPostTitleMaxChars = 10;
    inline constexpr int kPostBodyMinChars = 1, kPostBodyMaxChars = 50;
    inline constexpr int kIntroMaxChars = 40;
    static_assert(CheckText("사냥 같이해요", 2, 10) == TextCheck::Ok && CheckText("가", 2, 10) == TextCheck::TooShort);
    static_assert(CheckText("운영자 공지", 2, 10) == TextCheck::Banned && CheckText("   ", 1, 10) == TextCheck::Blank);
}
