#pragma once
// 스냅샷 — 결사 성장 묶음(탭 GuildConst · GuildLevel · GuildMission · GuildShop · GuildAchievement · GuildTitle · GuildAttend).
// Bind_Guild.cpp가 채우고, 파사드 GuildGrowth::Const · Levels · Missions …가 읽는다. 문자열은 스냅샷 문자열 영역 소유.
#include <vector>

#include "Database/GameData/GuildGrowth.h"

struct SnapGuild
{
    GuildGrowth::ConstRow                c{};
    std::vector<GuildGrowth::LevelRow>   levels;        // level 순
    std::vector<GuildGrowth::MissionRow> missions;      // id 순
    std::vector<GuildGrowth::ShopRow>    shop;          // id 순
    std::vector<GuildGrowth::AchRow>     achievements;  // id 순
    std::vector<GuildGrowth::TitleRow>   titles;        // id 순
    std::vector<GuildGrowth::AttendRow>  attend;        // tier 순
};
