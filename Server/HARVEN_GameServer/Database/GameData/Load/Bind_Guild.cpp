#include "pch.h"
// ─────────────────────────────────────────────────────────────────────────────
// 결사 성장 묶음 — 탭 GuildConst(한 행) · GuildLevel · GuildMission · GuildShop · GuildAchievement · GuildTitle · GuildAttend → 스냅샷 → 파사드 GuildGrowth::*.
// 모양은 Bind_Party.cpp와 같다.
//   1) 파사드 — 시트 값을 읽는 함수(구조 상수 · 순수 함수는 헤더 GuildGrowth.h)
//   2) Bind    — 열마다 RowReader
//   3) Validate — R117(레벨) · R118(임무 수) · R119(칭호 id · 참조) · R120(출석 단계) · R121(표 크기 · 임무 조건) · R122(게시판 · 기록 · 신청 상한) · R123(상점 · 업적 조건)
//   4) Dump    — Canon_Guild.h(datatool export와 같은 바이트)
// ─────────────────────────────────────────────────────────────────────────────
#include <algorithm>
#include <string>

#include "Database/GameData/GuildGrowth.h"
#include "Database/GameData/Item/ItemTableData.h"
#include "Database/GameData/ItemRules.h"
#include "Database/GameData/Load/Bundles.h"
#include "Database/GameData/Load/Canon_Guild.h"
#include "Database/GameData/Load/JsonRead.h"
#include "Database/GameData/Load/Snapshot.h"
#include "Database/GameData/Load/Validate.h"

// ── 1) 파사드 ────────────────────────────────────────────────────────────────
namespace GuildGrowth
{
    const ConstRow& Const() noexcept { return GD().guild.c; }
    std::span<const LevelRow> Levels() noexcept { return GD().guild.levels; }
    std::span<const MissionRow> Missions() noexcept { return GD().guild.missions; }
    std::span<const ShopRow> Shop() noexcept { return GD().guild.shop; }
    std::span<const AchRow> Achievements() noexcept { return GD().guild.achievements; }
    std::span<const TitleRow> Titles() noexcept { return GD().guild.titles; }
    std::span<const AttendRow> AttendTiers() noexcept { return GD().guild.attend; }

    namespace
    {
        template <class Row, class Id>
        const Row* FindById(std::span<const Row> rows, Id id) noexcept
        {
            const auto it = std::lower_bound(rows.begin(), rows.end(), id, [](const Row& r, Id v) { return r.id < v; });
            return (it != rows.end() && it->id == id) ? &*it : nullptr;
        }
    }
    const MissionRow* FindMission(uint16_t id) noexcept { return FindById(Missions(), id); }
    const ShopRow* FindShop(uint16_t id) noexcept { return FindById(Shop(), id); }
    const AchRow* FindAch(uint16_t id) noexcept { return FindById(Achievements(), id); }
    const TitleRow* FindTitle(uint8_t id) noexcept { return FindById(Titles(), id); }
}

namespace GameDataLoad
{
    namespace
    {
        std::string N(uint64_t v) { return std::to_string(v); }

        template <class Row, class Fn>
        void BindRows(LoadContext& ctx, const char* tab, std::vector<Row>& out, Fn&& read)
        {
            const Json* rows = ctx.Rows(tab);
            if (rows == nullptr)
                return;
            out.reserve(rows->size());
            for (size_t i = 0; i < rows->size(); ++i)
            {
                RowReader r(ctx, tab, i, (*rows)[i]);
                Row row{};
                read(r, row);
                r.Finish();
                out.push_back(row);
            }
        }
    }

    // ── 2) Bind ──────────────────────────────────────────────────────────────
    void BindGuild(LoadContext& ctx, GameDataSnapshot& snap)
    {
        SnapGuild& g = snap.guild;
        if (const Json* rows = ctx.Rows("GuildConst"))
        {
            if (rows->size() != 1)
                ctx.Error("GuildConst", "한 행이어야 한다(" + N(rows->size()) + "행)");
            for (size_t i = 0; i < rows->size(); ++i)
            {
                RowReader r(ctx, "GuildConst", i, (*rows)[i]);
                GuildGrowth::ConstRow c;
                r.Int("week_period_sec", c.weekPeriodSec).Int("week_offset_sec", c.weekOffsetSec).Int("day_period_sec", c.dayPeriodSec)
                 .Int("day_offset_sec", c.dayOffsetSec).Int("kill_exp_pct", c.killExpPct).Int("member_daily_exp_cap", c.memberDailyExpCap)
                 .Int("kill_contrib", c.killContrib).Int("boss_contrib", c.bossContrib).Int("coop_new_days", c.coopNewDays)
                 .Int("coop_contrib_bonus", c.coopContribBonus).Int("attend_active_min", c.attendActiveMin).Int("mission_slots", c.missionSlots)
                 .Int("mission_bonus_members", c.missionBonusMembers).Int("mission_bonus_pct", c.missionBonusPct)
                 .Int("leader_absent_sec", c.leaderAbsentSec).Int("claim_grace_sec", c.claimGraceSec).Int("post_max", c.postMax)
                 .Int("log_keep", c.logKeep).Int("apply_expire_sec", c.applyExpireSec).Int("apply_max", c.applyMax)
                 .Flags<GuildPerm>("perm_officer", c.permOfficer).Flags<GuildPerm>("perm_elite", c.permElite).Flags<GuildPerm>("perm_member", c.permMember);
                r.Finish();
                if (i == 0)
                    g.c = c;
            }
        }
        BindRows(ctx, "GuildLevel", g.levels, [](RowReader& r, GuildGrowth::LevelRow& x)
        {
            r.Int("level", x.level).Int("exp", x.exp).Int("max_members", x.maxMembers).Int("frame", x.frame).Int("title_id", x.titleId);
        });
        BindRows(ctx, "GuildMission", g.missions, [](RowReader& r, GuildGrowth::MissionRow& x)
        {
            r.Int("id", x.id).Str("name", x.name).Enum("kind", x.kind).Int("target", x.target).Int("sub", x.sub).Int("min_level", x.minLevel)
             .Int("exp", x.exp).Int("seals", x.seals).Int("contrib", x.contrib).Int("weight", x.weight);
        });
        BindRows(ctx, "GuildShop", g.shop, [](RowReader& r, GuildGrowth::ShopRow& x)
        {
            r.Int("id", x.id).Int("item_id", x.itemId).Int("count", x.count).Int("price", x.price).Int("min_level", x.minLevel)
             .Int("weekly_limit", x.weeklyLimit);
        });
        BindRows(ctx, "GuildAchievement", g.achievements, [](RowReader& r, GuildGrowth::AchRow& x)
        {
            r.Int("id", x.id).Str("name", x.name).Enum("kind", x.kind).Int("target", x.target).Int("seals", x.seals).Int("exp", x.exp)
             .Int("title_id", x.titleId);
        });
        BindRows(ctx, "GuildTitle", g.titles, [](RowReader& r, GuildGrowth::TitleRow& x)
        {
            r.Int("id", x.id).Str("name", x.name);
        });
        BindRows(ctx, "GuildAttend", g.attend, [](RowReader& r, GuildGrowth::AttendRow& x)
        {
            r.Int("tier", x.tier).Int("members", x.members).Int("seals", x.seals).Int("exp", x.exp).Int("contrib", x.contrib);
        });
    }

    // ── 3) Validate ──────────────────────────────────────────────────────────
    void ValidateGuild(ValidateContext& v, const GameDataSnapshot& snap)
    {
        const SnapGuild& g = snap.guild;
        const GuildGrowth::ConstRow& c = g.c;

        // R117 — 레벨: 1부터 연속 · kMaxLevel 이하 · 1레벨 경험치 0 · 경험치는 늘고 인원은 줄지 않는다 · 인원 2 ~ kMaxMembersCap
        if (g.levels.empty() || g.levels.size() > static_cast<size_t>(GuildGrowth::kMaxLevel))
            v.Fail("R117", "GuildLevel", "레벨 행은 1 ~ " + N(GuildGrowth::kMaxLevel) + "개(GuildGrowth::kMaxLevel) — 지금 " + N(g.levels.size()));
        for (size_t i = 0; i < g.levels.size(); ++i)
        {
            const auto& r = g.levels[i];
            if (r.level != i + 1)
                v.Fail("R117", v.Where("GuildLevel", i, "level"), "레벨은 1부터 빈틈없이 — " + N(i + 1) + "이어야 한다(지금 " + N(r.level) + ")");
            if (i == 0 && r.exp != 0)
                v.Fail("R117", v.Where("GuildLevel", i, "exp"), "1레벨 누적 경험치는 0");
            if (i > 0 && r.exp <= g.levels[i - 1].exp)
                v.Fail("R117", v.Where("GuildLevel", i, "exp"), "누적 경험치는 앞 레벨(" + N(g.levels[i - 1].exp) + ")보다 커야 한다");
            if (r.maxMembers < 2 || r.maxMembers > GuildGrowth::kMaxMembersCap)
                v.Fail("R117", v.Where("GuildLevel", i, "max_members"), "인원 한도는 2 ~ " + N(GuildGrowth::kMaxMembersCap) + " — 지금 " + N(r.maxMembers));
            if (i > 0 && r.maxMembers < g.levels[i - 1].maxMembers)
                v.Fail("R117", v.Where("GuildLevel", i, "max_members"), "인원 한도가 앞 레벨(" + N(g.levels[i - 1].maxMembers) + ")보다 줄었다 — 레벨이 올라 쫓겨나는 사람이 생긴다");
        }

        // R119 — 칭호 id 1 ~ kTitleIdMax · 겹치지 않음(id 순) · 레벨 · 업적의 칭호 참조가 있다
        for (size_t i = 0; i < g.titles.size(); ++i)
        {
            const auto& r = g.titles[i];
            if (r.id < 1 || r.id > GuildGrowth::kTitleIdMax)
                v.Fail("R119", v.Where("GuildTitle", i, "id"), "칭호 id는 1 ~ " + N(GuildGrowth::kTitleIdMax) + "(열린 칭호를 u32 비트로 보낸다) — 지금 " + N(r.id));
            if (i > 0 && r.id <= g.titles[i - 1].id)
                v.Fail("R119", v.Where("GuildTitle", i, "id"), "id가 겹치거나 차례가 아니다");
            if (Utf8Bytes(r.name) + 1 > static_cast<size_t>(GuildGrowth::kTitleBytes))
                v.Fail("R119", v.Where("GuildTitle", i, "name"), "이름 " + N(Utf8Bytes(r.name)) + "바이트 > " + N(GuildGrowth::kTitleBytes - 1));
        }
        auto titleExists = [&](uint8_t id) { return id == 0 || std::any_of(g.titles.begin(), g.titles.end(), [&](const auto& t) { return t.id == id; }); };
        for (size_t i = 0; i < g.levels.size(); ++i)
            if (!titleExists(g.levels[i].titleId))
                v.Fail("R119", v.Where("GuildLevel", i, "title_id"), "없는 칭호 " + N(g.levels[i].titleId));
        for (size_t i = 0; i < g.achievements.size(); ++i)
            if (!titleExists(g.achievements[i].titleId))
                v.Fail("R119", v.Where("GuildAchievement", i, "title_id"), "없는 칭호 " + N(g.achievements[i].titleId));

        // R120 — 출석 단계: 1부터 연속 · kAttendTiersMax 이하 · 필요한 수가 는다
        if (g.attend.size() > static_cast<size_t>(GuildGrowth::kAttendTiersMax))
            v.Fail("R120", "GuildAttend", "출석 단계는 " + N(GuildGrowth::kAttendTiersMax) + "개까지 — 지금 " + N(g.attend.size()));
        for (size_t i = 0; i < g.attend.size(); ++i)
        {
            const auto& r = g.attend[i];
            if (r.tier != i + 1)
                v.Fail("R120", v.Where("GuildAttend", i, "tier"), "단계는 1부터 빈틈없이 — " + N(i + 1) + "이어야 한다");
            if (i > 0 && r.members <= g.attend[i - 1].members)
                v.Fail("R120", v.Where("GuildAttend", i, "members"), "필요한 출석 수는 앞 단계(" + N(g.attend[i - 1].members) + ")보다 커야 한다");
        }

        // R121 — 임무 · 상점 · 업적 표: kCatalogMax행 이하 · id 겹침 없음 · 임무 조건
        auto catalogSize = [&](const char* tab, size_t n)
        {
            if (n > static_cast<size_t>(GuildGrowth::kCatalogMax))
                v.Fail("R121", tab, N(n) + "행 > " + N(GuildGrowth::kCatalogMax) + "(GuildGrowth::kCatalogMax — 결사 진행 저장 칸)");
        };
        catalogSize("GuildMission", g.missions.size());
        catalogSize("GuildShop", g.shop.size());
        catalogSize("GuildAchievement", g.achievements.size());
        const uint8_t maxLevel = g.levels.empty() ? 1 : g.levels.back().level;
        for (size_t i = 0; i < g.missions.size(); ++i)
        {
            const auto& m = g.missions[i];
            if (i > 0 && m.id <= g.missions[i - 1].id)
                v.Fail("R121", v.Where("GuildMission", i, "id"), "id가 겹치거나 차례가 아니다");
            if (Utf8Bytes(m.name) + 1 > static_cast<size_t>(GuildGrowth::kNameBytes))
                v.Fail("R121", v.Where("GuildMission", i, "name"), "이름 " + N(Utf8Bytes(m.name)) + "바이트 > " + N(GuildGrowth::kNameBytes - 1));
            if (m.minLevel > maxLevel)
                v.Fail("R121", v.Where("GuildMission", i, "min_level"), "결사 최고 레벨(" + N(maxLevel) + ")보다 높다 — 뽑히지 않는다");
            switch (m.kind)
            {
            case GuildMissionKind::Collect:
                if (m.sub != 0 && ItemTableData::Find(static_cast<ItemTableId>(m.sub)) == nullptr)
                    v.Fail("R121", v.Where("GuildMission", i, "sub"), "Collect의 sub는 아이템 id(0 = 강화 재료) — 없는 아이템 " + N(m.sub));
                break;
            case GuildMissionKind::Gear:
                if (m.sub >= static_cast<uint32_t>(Rarity::Count))
                    v.Fail("R121", v.Where("GuildMission", i, "sub"), "Gear의 sub는 최소 희귀도(0 ~ " + N(static_cast<int>(Rarity::Count) - 1) + ")");
                break;
            case GuildMissionKind::Active:
                if (m.sub == 0 || m.sub > 10080)
                    v.Fail("R121", v.Where("GuildMission", i, "sub"), "Active의 sub는 한 사람의 이번 주 활동 분(1 ~ 10080)");
                if (m.target > GuildGrowth::kMaxMembersCap)
                    v.Fail("R121", v.Where("GuildMission", i, "target"), "Active의 target은 결사원 수(" + N(GuildGrowth::kMaxMembersCap) + " 이하)");
                break;
            default:
                break;
            }
        }

        // R118 — 한 주의 임무 수 1 ~ kMissionSlotsMax, 그리고 1레벨 결사가 뽑을 임무가 그만큼 있다
        if (c.missionSlots < 1 || c.missionSlots > GuildGrowth::kMissionSlotsMax)
            v.Fail("R118", v.Where("GuildConst", 0, "mission_slots"), "한 주의 임무 수는 1 ~ " + N(GuildGrowth::kMissionSlotsMax) + " — 지금 " + N(c.missionSlots));
        else
        {
            const auto lv1 = std::count_if(g.missions.begin(), g.missions.end(), [](const auto& m) { return m.minLevel <= 1 && m.weight > 0; });
            if (lv1 < c.missionSlots)
                v.Fail("R118", v.Where("GuildConst", 0, "mission_slots"), "1레벨 결사가 뽑을 수 있는 임무(min_level 1 · weight > 0)가 " + N(static_cast<uint64_t>(lv1)) + "개뿐 — "
                    + N(c.missionSlots) + "개가 필요하다");
        }

        // R122 — 게시판 · 기록 · 가입 신청 상한(패킷 · 메모리)
        if (c.postMax < 1 || c.postMax > GuildGrowth::kPostMaxCap)
            v.Fail("R122", v.Where("GuildConst", 0, "post_max"), "게시판 글 수 1 ~ " + N(GuildGrowth::kPostMaxCap));
        if (c.logKeep < 1 || c.logKeep > GuildGrowth::kLogKeepCap)
            v.Fail("R122", v.Where("GuildConst", 0, "log_keep"), "기록 줄 수 1 ~ " + N(GuildGrowth::kLogKeepCap));
        if (c.applyMax < 1 || c.applyMax > GuildGrowth::kApplicantsMax)
            v.Fail("R122", v.Where("GuildConst", 0, "apply_max"), "대기 신청 수 1 ~ " + N(GuildGrowth::kApplicantsMax) + "(신청자 패킷 줄 수)");

        // R123 — 상점 · 업적: 아이템이 있다 · 레벨 조건이 닿는다 · 레벨 업적 목표 ≤ 최고 레벨 · id 겹침 없음
        for (size_t i = 0; i < g.shop.size(); ++i)
        {
            const auto& s = g.shop[i];
            if (i > 0 && s.id <= g.shop[i - 1].id)
                v.Fail("R123", v.Where("GuildShop", i, "id"), "id가 겹치거나 차례가 아니다");
            const ItemTableRow* item = ItemTableData::Find(static_cast<ItemTableId>(s.itemId));
            if (item == nullptr)
                v.Fail("R123", v.Where("GuildShop", i, "item_id"), "없는 아이템 " + N(s.itemId));
            else if (s.count > 1 && item->type == ItemType::eEquipment)
                v.Fail("R123", v.Where("GuildShop", i, "count"), "장비는 한 번에 하나(겹쳐지지 않는다)");
            if (s.minLevel > maxLevel)
                v.Fail("R123", v.Where("GuildShop", i, "min_level"), "결사 최고 레벨(" + N(maxLevel) + ")보다 높다 — 영영 보이지 않는다");
        }
        for (size_t i = 0; i < g.achievements.size(); ++i)
        {
            const auto& a = g.achievements[i];
            if (i > 0 && a.id <= g.achievements[i - 1].id)
                v.Fail("R123", v.Where("GuildAchievement", i, "id"), "id가 겹치거나 차례가 아니다");
            if (Utf8Bytes(a.name) + 1 > static_cast<size_t>(GuildGrowth::kNameBytes))
                v.Fail("R123", v.Where("GuildAchievement", i, "name"), "이름 " + N(Utf8Bytes(a.name)) + "바이트 > " + N(GuildGrowth::kNameBytes - 1));
            if (a.kind == GuildAchKind::Level && a.target > maxLevel)
                v.Fail("R123", v.Where("GuildAchievement", i, "target"), "레벨 업적 목표가 결사 최고 레벨(" + N(maxLevel) + ")보다 높다");
            if (a.kind == GuildAchKind::Members && a.target > GuildGrowth::kMaxMembersCap)
                v.Fail("R123", v.Where("GuildAchievement", i, "target"), "인원 업적 목표가 " + N(GuildGrowth::kMaxMembersCap) + "보다 크다");
        }
    }

    // ── 4) Dump ──────────────────────────────────────────────────────────────
    void DumpGuild(Canon::Book& book, const GameDataSnapshot& snap)
    {
        const SnapGuild& g = snap.guild;
        Canon::WriteGuildConst(book, g.c);
        Canon::WriteGuildLevels(book, g.levels);
        Canon::WriteGuildMissions(book, g.missions);
        Canon::WriteGuildShop(book, g.shop);
        Canon::WriteGuildAchievements(book, g.achievements);
        Canon::WriteGuildTitles(book, g.titles);
        Canon::WriteGuildAttend(book, g.attend);
    }
}
