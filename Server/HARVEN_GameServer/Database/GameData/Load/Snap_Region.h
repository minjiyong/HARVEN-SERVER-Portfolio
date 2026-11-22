#pragma once
// 스냅샷 — 지형 묶음(탭 Region · RegionSpawn). Bind_Region.cpp가 채우고, 파사드 WorldData::FindRegion이 읽는다.
#include <array>
#include <cstdint>
#include <vector>

#include "Database/GameData/World/WorldRegion.h"

struct SnapRegion
{
    // 행 i = zone_id i(R076: 행 순서 = zone_id, 행 수 = kRegionCount). spawns[4]는 RegionSpawn(seq 1~4)에서 채운다.
    std::vector<WorldData::RegionRow> rows;
    // 행과 같은 순서 — spawns[k]를 채운 RegionSpawn JSON 행 번호(-1 = 빈 칸). 검증 오류의 위치에만 쓴다.
    std::vector<std::array<int32_t, 4>> spawnSrc;
};
