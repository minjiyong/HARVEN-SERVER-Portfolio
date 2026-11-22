#pragma once
// 스냅샷 — 마을 묶음. Bind_Town.cpp가 채우고, 파사드 TownMap::* · WorldData::(TownCell · TownLevel · IsBlocked …)가 읽는다.
//   mapgen이 주인: TownInfo · TownGrid · TownBuilding · TownSpawn · TownDummy · TownSpot · TownNpcHome · TownProp
//   시트가 주인: Town · District · Ambient
// 자리 · 크기 · Zone은 구조 상수(TownMap::kZoneId · kX0 · kY0 · kW · kH) — TownInfo가 다르면 로드 실패.
#include <cstdint>
#include <vector>

#include "Database/GameData/TownMap.h"

struct SnapTown
{
    // Town(시트) — 마을 이름. zone_id는 TownInfo와 같아야 한다(로드 검사)
    const char* name = "";
    uint16_t sheetZoneId = 0;

    // TownInfo(mapgen)
    uint16_t zoneId = 0;
    int16_t x0 = 0, y0 = 0;
    uint16_t w = 0, h = 0;
    TownMap::Rect16 yard{};

    // TownGrid — 줄 글자(행 = y, 스냅샷 문자열 영역 — 덤프 · 줄 파사드)와 찾기용 평면 격자.
    // 평면 격자는 늘 kW × kH(구조 상수) — 칸 (lx, ly) = [ly * kW + lx], O(1). 줄이 모자라거나 길면 R072로 로드가 실패하고,
    // 그 사이(검증 중)에도 넘치는 읽기가 없게 모자란 칸은 막힘('#') · 층 '0'으로 채워 둔다.
    std::vector<const char*> groundRows, surfaceRows, levelRows;
    std::vector<char> ground, level;

    std::vector<TownMap::BuildingRow> buildings;   // 행 i = seq i + 1
    std::vector<TownMap::Point16> spawns;          // 행 i = seq i + 1
    std::vector<TownMap::Point16> dummies;         // 행 i = seq i + 1
    std::vector<TownMap::SpotRow> spots;           // key 오름차순
    std::vector<TownMap::NpcHome> npcHomes;        // npc_id 오름차순
    std::vector<TownMap::PropRow> props;           // 행 i = seq i + 1
    std::vector<TownMap::DistrictRow> districts;   // 행 i = seq i + 1(투어 순서)
    std::vector<TownMap::AmbientRow> ambient;      // 행 i = seq i + 1
};

// NPC 집 — 마을이 다른 묶음보다 먼저 Bind되므로, Bind_Npc가 만들고 있는 스냅샷(snap.town)에서 바로 읽는다(GD()를 부르지 않는다).
// 없으면 nullptr. npc_id가 1부터 연속이면 인덱스로 바로(O(1)), 아니면 순차 검색.
[[nodiscard]] inline const TownMap::NpcHome* FindNpcHome(const SnapTown& town, uint16_t npcId) noexcept
{
    const std::vector<TownMap::NpcHome>& h = town.npcHomes;
    if (npcId >= 1 && npcId <= h.size() && h[npcId - 1].id == npcId)
        return &h[npcId - 1];
    for (const auto& home : h)
        if (home.id == npcId)
            return &home;
    return nullptr;
}
