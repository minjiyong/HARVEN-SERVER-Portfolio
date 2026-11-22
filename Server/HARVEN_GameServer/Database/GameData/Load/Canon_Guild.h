#pragma once
// 정본 표 쓰기 — 결사 성장 묶음(탭 GuildConst · GuildLevel · GuildMission · GuildShop · GuildAchievement · GuildTitle · GuildAttend).
// 열 순서 = Tools/DataTool/schema/guild.yaml.
#include <cstdint>
#include <span>

#include "Database/GameData/GuildGrowth.h"
#include "Database/GameData/Load/Canon.h"

namespace Canon
{
    inline void WriteGuildConst(Book& book, const GuildGrowth::ConstRow& c)
    {
        Table& t = book.Get("GuildConst");
        Row w;
        w.Put("week_period_sec", c.weekPeriodSec).Put("week_offset_sec", c.weekOffsetSec).Put("day_period_sec", c.dayPeriodSec)
         .Put("day_offset_sec", c.dayOffsetSec).Put("kill_exp_pct", c.killExpPct).Put("member_daily_exp_cap", c.memberDailyExpCap)
         .Put("kill_contrib", c.killContrib).Put("boss_contrib", c.bossContrib).Put("coop_new_days", c.coopNewDays)
         .Put("coop_contrib_bonus", c.coopContribBonus).Put("attend_active_min", c.attendActiveMin).Put("mission_slots", c.missionSlots)
         .Put("mission_bonus_members", c.missionBonusMembers).Put("mission_bonus_pct", c.missionBonusPct)
         .Put("leader_absent_sec", c.leaderAbsentSec).Put("claim_grace_sec", c.claimGraceSec).Put("post_max", c.postMax)
         .Put("log_keep", c.logKeep).Put("apply_expire_sec", c.applyExpireSec).Put("apply_max", c.applyMax)
         .Flags<GuildPerm>("perm_officer", c.permOfficer).Flags<GuildPerm>("perm_elite", c.permElite).Flags<GuildPerm>("perm_member", c.permMember);
        t.Add(w);
    }

    inline void WriteGuildLevels(Book& book, std::span<const GuildGrowth::LevelRow> rows)
    {
        Table& t = book.Get("GuildLevel");
        for (const auto& r : rows)
        {
            Row w;
            w.Put("level", r.level).Put("exp", r.exp).Put("max_members", r.maxMembers).Put("frame", r.frame).Put("title_id", r.titleId);
            t.Add(w);
        }
    }

    inline void WriteGuildMissions(Book& book, std::span<const GuildGrowth::MissionRow> rows)
    {
        Table& t = book.Get("GuildMission");
        for (const auto& r : rows)
        {
            Row w;
            w.Put("id", r.id).Put("name", r.name).Put("kind", r.kind).Put("target", r.target).Put("sub", r.sub).Put("min_level", r.minLevel)
             .Put("exp", r.exp).Put("seals", r.seals).Put("contrib", r.contrib).Put("weight", r.weight);
            t.Add(w);
        }
    }

    inline void WriteGuildShop(Book& book, std::span<const GuildGrowth::ShopRow> rows)
    {
        Table& t = book.Get("GuildShop");
        for (const auto& r : rows)
        {
            Row w;
            w.Put("id", r.id).Put("item_id", r.itemId).Put("count", r.count).Put("price", r.price).Put("min_level", r.minLevel)
             .Put("weekly_limit", r.weeklyLimit);
            t.Add(w);
        }
    }

    inline void WriteGuildAchievements(Book& book, std::span<const GuildGrowth::AchRow> rows)
    {
        Table& t = book.Get("GuildAchievement");
        for (const auto& r : rows)
        {
            Row w;
            w.Put("id", r.id).Put("name", r.name).Put("kind", r.kind).Put("target", r.target).Put("seals", r.seals).Put("exp", r.exp)
             .Put("title_id", r.titleId);
            t.Add(w);
        }
    }

    inline void WriteGuildTitles(Book& book, std::span<const GuildGrowth::TitleRow> rows)
    {
        Table& t = book.Get("GuildTitle");
        for (const auto& r : rows)
        {
            Row w;
            w.Put("id", r.id).Put("name", r.name);
            t.Add(w);
        }
    }

    inline void WriteGuildAttend(Book& book, std::span<const GuildGrowth::AttendRow> rows)
    {
        Table& t = book.Get("GuildAttend");
        for (const auto& r : rows)
        {
            Row w;
            w.Put("tier", r.tier).Put("members", r.members).Put("seals", r.seals).Put("exp", r.exp).Put("contrib", r.contrib);
            t.Add(w);
        }
    }
}
