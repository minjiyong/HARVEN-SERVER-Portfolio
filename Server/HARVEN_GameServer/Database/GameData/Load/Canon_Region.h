#pragma once
// 정본 표 쓰기 — 지형 묶음(탭 Region · RegionSpawn). 월드 · Zone 배치 상수(kWorldWidth · kRegionSize …)는 코드에 남는다.
// RegionSpawn: spawns[4]에서 species 0(빈 칸)은 뒤쪽에만 있어야 하고 표에 적지 않는다. seq는 1부터.
#include <span>

#include "Database/GameData/Load/Canon.h"
#include "Database/GameData/WorldData.h"

namespace Canon
{
    inline void WriteRegion(Book& book, std::span<const WorldData::RegionRow> rows)
    {
        Table& t = book.Get("Region");
        Table& s = book.Get("RegionSpawn");
        for (const auto& r : rows)
        {
            Row w;
            w.Put("zone_id", r.zoneId).Put("name", r.name).Put("biome", r.biome).Put("tier", r.tier)
             .Put("boss_species", r.bossSpecies).Put("boss_x", r.bossX).Put("boss_y", r.bossY)
             .Put("land_x", r.landX).Put("land_y", r.landY).Put("teleport_fee", r.teleportFee);
            t.Add(w);
            bool ended = false;
            uint8_t seq = 0;
            for (const auto& sp : r.spawns)
            {
                if (sp.species == 0)
                {
                    if (sp.weight != 0) throw Error("RegionSpawn " + std::to_string(r.zoneId) + ": species 0에 weight가 있다");
                    ended = true;
                    continue;
                }
                if (ended) throw Error("RegionSpawn " + std::to_string(r.zoneId) + ": 빈 칸 뒤에 스폰이 있다");
                Row sw;
                sw.Put("zone_id", r.zoneId).Put("seq", ++seq).Put("species", sp.species).Put("weight", sp.weight);
                s.Add(sw);
            }
        }
    }
}
