#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "Database/GameData/GuildGrowth.h"
#include "Protocol/Protocol.h"   // GuildLogKind

// ─────────────────────────────────────────────────────────────────────────────
// 결사 성장 진행 — 결사 하나의 레벨 · 경험치 · 기여도 · 주간 임무 · 업적 · 출석 · 권한 · 위임 청구(Shared/Document/GUILD.md)
//
//  · GuildProgress = DB GuildProgress.Data 한 덩어리(버전 있는 바이너리 — Serialize · Deserialize). 금고(Seals)는 따로(±로만).
//  · GuildModel::* = 순수 규칙 적용(네트워크 · DB · 시각 읽기 없음 — 시각은 인자로). 서비스(CGuildService)가 결과(Effects)를 보고
//    금고 DB ± · 기록 · 알림을 한다. 단위 시험(tests/guild_rule_tests.cpp)이 이 함수들을 바로 부른다.
//  · 서비스 스레드 하나만 만진다(락 없음).
// ─────────────────────────────────────────────────────────────────────────────
struct GuildMemberProgress
{
    int32_t  playerId = 0;
    uint32_t joinedAt = 0;          // 유닉스 초(예전 결사원은 처음 불러온 때)
    uint32_t contribWeek = 0;
    uint32_t contribTotal = 0;
    uint16_t activeWeek = 0;        // 이번 주 활동 분
    uint16_t activeToday = 0;       // 오늘 활동 분
    uint32_t lastActiveMin = 0;     // 마지막으로 센 활동 분(유닉스 분) — 같은 분은 한 번만
    uint32_t expToday = 0;          // 오늘 사냥으로 올려 준 결사 경험치(한도 member_daily_exp_cap)
    uint8_t  attendedToday = 0;
    std::vector<std::pair<uint16_t, uint8_t>> bought;   // 이번 주 상점 구매(상점 id → 수)
};
struct GuildMissionProgress
{
    uint16_t id = 0;
    uint32_t progress = 0;
    uint8_t  done = 0;
    std::vector<std::pair<int32_t, uint32_t>> who;      // 참여한 결사원(playerId → 몫)
};
struct GuildProgress
{
    uint8_t  level = 1;
    uint32_t exp = 0;
    uint8_t  title = 0;
    uint8_t  recruiting = 0;
    uint8_t  recruitMinLevel = 1;
    std::string intro;
    uint8_t  permsSet = 0;          // 0 = 시트 기본값(GuildConst perm_*)을 쓴다
    uint8_t  perms[4]{};            // [1] 부관 · [2] 정예 · [3] 결사원
    uint32_t kills = 0;
    uint32_t bossKills = 0;
    uint32_t coopKills = 0;
    uint32_t missionsDone = 0;
    uint32_t attendDays = 0;
    uint8_t  peakMembers = 0;
    std::vector<std::pair<uint16_t, uint32_t>> achievements;   // 이룬 업적(id → 유닉스 초)
    uint32_t weekId = 0;
    std::vector<GuildMissionProgress> missions;
    uint32_t dayId = 0;
    uint8_t  attendCount = 0;
    uint8_t  attendTier = 0;
    int32_t  claimantId = 0;        // 결사장 위임 청구(0 = 없음)
    uint32_t claimEndsAt = 0;       // 유닉스 초
    std::vector<GuildMemberProgress> members;
};

namespace GuildModel
{
    inline constexpr uint8_t kVersion = 1;
    inline constexpr size_t  kMaxBytes = 16000;   // DB VARBINARY(16000)

    [[nodiscard]] std::vector<uint8_t> Serialize(const GuildProgress& p);
    [[nodiscard]] bool Deserialize(std::span<const uint8_t> bytes, GuildProgress& out);   // 깨졌거나 모르는 판이면 false(out은 그대로)

    // 시트 표 묶음(서비스는 파사드에서, 시험은 직접 만든 행으로)
    struct Tables
    {
        GuildGrowth::ConstRow                c{};
        std::span<const GuildGrowth::LevelRow>   levels;
        std::span<const GuildGrowth::MissionRow> missions;
        std::span<const GuildGrowth::AchRow>     achs;
        std::span<const GuildGrowth::AttendRow>  attend;
    };
    [[nodiscard]] Tables FromSheet();

    // 규칙 적용의 결과 — 서비스가 금고 DB ± · 기록 · 알림으로 옮긴다(진행 덩어리에는 이미 반영했다)
    struct Seal
    {
        int32_t      amount = 0;
        GuildLogKind kind = GuildLogKind::SealsIn;
        int32_t      value = 0;
        std::string  what;
    };
    struct Effects
    {
        uint32_t                                exp = 0;           // 더한 결사 경험치
        uint8_t                                 levelBefore = 0;
        uint8_t                                 levelAfter = 0;
        std::vector<Seal>                       seals;             // 금고에 넣을 것(임무 · 업적 · 출석)
        std::vector<uint16_t>                   missionsDone;
        std::vector<uint16_t>                   achievementsDone;
        std::vector<uint8_t>                    attendTiers;       // 오늘 새로 채운 출석 단계
        std::vector<uint8_t>                    titlesUnlocked;
        std::vector<std::pair<int32_t, uint32_t>> contrib;         // 기여가 오른 결사원(playerId → 오른 만큼)
        bool                                    missionProgress = false;
        [[nodiscard]] bool Leveled() const noexcept { return levelAfter > levelBefore; }
    };

    [[nodiscard]] GuildMemberProgress* Member(GuildProgress& p, int32_t playerId);
    [[nodiscard]] const GuildMemberProgress* Member(const GuildProgress& p, int32_t playerId);
    GuildMemberProgress& EnsureMember(GuildProgress& p, int32_t playerId, uint32_t now);
    void RemoveMember(GuildProgress& p, int32_t playerId);

    // 나간 결사원의 한도 몫 — 나갔다 다시 들어와도 오늘 · 이번 주 한도(하루 경험치 member_daily_exp_cap · 오늘 활동 · 출석 ·
    // 주 상점 구매 weekly_limit)가 새로 시작하지 않게. 기여도 · 가입일은 잇지 않는다(나가면 사라지는 결사의 것). 서비스가 메모리에만 둔다.
    struct Departed
    {
        uint32_t            dayId = 0;      // 나간 날 · 주(GuildProgress::dayId · weekId)
        uint32_t            weekId = 0;
        GuildMemberProgress m;              // playerId 0 = 진행이 없던 사람
    };
    [[nodiscard]] Departed Depart(const GuildProgress& p, int32_t playerId);   // RemoveMember 앞에서
    // 다시 들어온 m(EnsureMember)에 잇는다 — 같은 날이면 오늘 몫, 같은 주면 상점 구매
    void Rejoin(const GuildProgress& p, GuildMemberProgress& m, const Departed& d);
    // 불러온 결사원 목록에 맞춘다(진행에만 있는 사람은 빼고, 없는 사람은 joinedAt = now로 넣는다)
    void SyncMembers(GuildProgress& p, std::span<const int32_t> playerIds, uint32_t now);

    // 직위 권한(결사장 = 전부)
    [[nodiscard]] uint8_t PermsOf(const GuildProgress& p, const GuildGrowth::ConstRow& c, uint8_t rank) noexcept;
    void PermsArray(const GuildProgress& p, const GuildGrowth::ConstRow& c, uint8_t (&out)[4]) noexcept;

    // 주 · 날이 바뀌었으면 넘긴다 — 주: 이번 주 기여 · 활동 · 상점 구매를 비우고 임무를 새로 뽑는다, 날: 오늘 활동 · 경험치 · 출석을 비운다.
    // 돌려주는 값: 1 = 날, 2 = 주(둘 다면 3)
    int Roll(GuildProgress& p, uint32_t guildId, int64_t now, const Tables& t);

    // 결사 경험치 더하기 — 레벨이 오르면 레벨 업적 · 레벨 칭호까지
    void AddExp(GuildProgress& p, uint32_t exp, int64_t now, const Tables& t, Effects& fx);

    struct KillIn
    {
        std::vector<int32_t> players;    // 함께 쓰러뜨린 이 결사의 결사원(같은 Zone · 피해를 줌)
        uint32_t             monsterExp = 0;
        uint8_t              monsterLevel = 0;
        bool                 boss = false;
    };
    void OnKill(GuildProgress& p, const KillIn& k, int64_t now, const Tables& t, Effects& fx);
    // 전리품: enchantMaterial = 강화 재료(각인 · 강화 효과), equipment = 장비면 rarity
    void OnLoot(GuildProgress& p, int32_t playerId, uint16_t itemId, uint16_t count, bool enchantMaterial, bool equipment, uint8_t rarity,
        int64_t now, const Tables& t, Effects& fx);
    void OnQuest(GuildProgress& p, int32_t playerId, int64_t now, const Tables& t, Effects& fx);
    // 결사원 수가 바뀜(들어옴) · 날이 바뀜 · 접속 — 인원 · 날 업적
    void CheckAchievements(GuildProgress& p, int64_t now, int64_t createdAt, uint8_t memberCount, const Tables& t, Effects& fx);

    [[nodiscard]] uint32_t TitlesUnlocked(const GuildProgress& p, const Tables& t) noexcept;
    [[nodiscard]] uint32_t AchProgress(const GuildProgress& p, GuildAchKind kind, int64_t now, int64_t createdAt) noexcept;
    [[nodiscard]] uint8_t  BoughtThisWeek(const GuildMemberProgress& m, uint16_t shopId) noexcept;
    void AddBought(GuildMemberProgress& m, uint16_t shopId);
}
