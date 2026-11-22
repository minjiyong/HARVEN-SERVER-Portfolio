#pragma once
// 정본 표 쓰기 — 마을 묶음.
//  시트가 주인(사람이 글로 다루는 것): Town(이름) · District(구획 이름 · 설명 · 투어 자리) · Ambient(소리 구역)
//  mapgen이 주인(격자 · 배치 — HARVEN-DESIGN tools/mapgen → GameData/town_denica/):
//    TownInfo(Zone · 원점 · 크기 · 훈련장) · TownGrid(행 = y: ground · surface · level) · TownBuilding · TownSpawn ·
//    TownDummy · TownSpot(이름 있는 자리) · TownNpcHome · TownProp
// 좌표는 모두 월드 칸(런타임 구조체와 같다).
#include <algorithm>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Database/GameData/Load/Canon.h"
#include "Database/GameData/TownTypes.h"

namespace Canon
{
    struct TownInfoV
    {
        const char* name = "";
        int zoneId = 0, x0 = 0, y0 = 0, w = 0, h = 0;
        int yardX0 = 0, yardY0 = 0, yardX1 = 0, yardY1 = 0;
    };
    struct TownSpotV { const char* key = ""; TownMap::PointF p{}; };

    // ── 시트 쪽 ──
    inline void WriteTownSheet(Book& book, const TownInfoV& info, std::span<const TownMap::DistrictRow> districts, std::span<const TownMap::AmbientRow> ambient)
    {
        Table& t = book.Get("Town");
        Row w;
        w.Put("zone_id", static_cast<uint16_t>(info.zoneId)).Put("name", info.name);
        t.Add(w);

        Table& d = book.Get("District");
        uint8_t seq = 0;
        for (const auto& r : districts)
        {
            Row dw;
            dw.Put("seq", ++seq).Put("name", r.name).Put("sub", r.sub).Put("x", r.x).Put("y", r.y).Put("zoom", r.zoom);
            d.Add(dw);
        }
        Table& a = book.Get("Ambient");
        seq = 0;
        for (const auto& r : ambient)
        {
            Row aw;
            aw.Put("seq", ++seq).Put("kind", r.kind).Put("x", r.x).Put("y", r.y).Put("radius", r.radius);
            a.Add(aw);
        }
    }

    // ── mapgen 쪽 ──
    inline void WriteTownLayout(Book& book, const TownInfoV& info,
        std::span<const char* const> ground, std::span<const char* const> surface, std::span<const char* const> level,
        std::span<const TownMap::BuildingRow> buildings, std::span<const TownMap::Point16> spawns, std::span<const TownMap::Point16> dummies,
        std::span<const TownSpotV> spots, std::span<const TownMap::NpcHome> homes, std::span<const TownMap::PropRow> props)
    {
        {
            Table& t = book.Get("TownInfo");
            Row w;
            w.Put("zone_id", static_cast<uint16_t>(info.zoneId)).Put("x0", static_cast<int16_t>(info.x0)).Put("y0", static_cast<int16_t>(info.y0))
             .Put("w", static_cast<uint16_t>(info.w)).Put("h", static_cast<uint16_t>(info.h))
             .Put("yard_x0", static_cast<int16_t>(info.yardX0)).Put("yard_y0", static_cast<int16_t>(info.yardY0))
             .Put("yard_x1", static_cast<int16_t>(info.yardX1)).Put("yard_y1", static_cast<int16_t>(info.yardY1));
            t.Add(w);
        }
        {
            if (ground.size() != static_cast<size_t>(info.h) || surface.size() != ground.size() || level.size() != ground.size())
                throw Error("TownGrid: 행 수가 h와 다르다");
            Table& t = book.Get("TownGrid");
            for (size_t y = 0; y < ground.size(); ++y)
            {
                for (const char* s : { ground[y], surface[y], level[y] })
                    if (std::string_view(s).size() != static_cast<size_t>(info.w))
                        throw Error("TownGrid: " + std::to_string(y) + "행 길이가 w와 다르다");
                Row w;
                w.Put("y", static_cast<uint16_t>(y)).Put("ground", ground[y]).Put("surface", surface[y]).Put("level", level[y]);
                t.Add(w);
            }
        }
        {
            Table& t = book.Get("TownBuilding");
            uint16_t seq = 0;
            for (const auto& b : buildings)
            {
                Row w;
                w.Put("seq", ++seq).Put("kind", b.kind).Put("x0", b.x0).Put("y0", b.y0).Put("x1", b.x1).Put("y1", b.y1)
                 .Put("level", b.level).Str("door", std::string(1, b.door)).Put("variant", b.variant).Put("sign", b.sign);
                t.Add(w);
            }
        }
        auto points = [&](const char* name, std::span<const TownMap::Point16> pts)
        {
            Table& t = book.Get(name);
            uint8_t seq = 0;
            for (const auto& p : pts)
            {
                Row w;
                w.Put("seq", ++seq).Put("x", p.x).Put("y", p.y);
                t.Add(w);
            }
        };
        points("TownSpawn", spawns);
        points("TownDummy", dummies);
        {
            Table& t = book.Get("TownSpot");
            // 정본 행 순서 = key(이름) 오름차순 — 코드는 이름으로만 찾는다
            std::vector<const TownSpotV*> sorted;
            for (const auto& s : spots) sorted.push_back(&s);
            std::stable_sort(sorted.begin(), sorted.end(), [](const TownSpotV* a, const TownSpotV* b) { return std::string_view(a->key) < std::string_view(b->key); });
            for (const TownSpotV* ps : sorted)
            {
                const TownSpotV& s = *ps;
                Row w;
                w.Put("key", s.key).Put("x", s.p.x).Put("y", s.p.y);
                t.Add(w);
            }
        }
        {
            Table& t = book.Get("TownNpcHome");
            for (const auto& h : homes)
            {
                Row w;
                w.Put("npc_id", h.id).Put("x", h.x).Put("y", h.y);
                t.Add(w);
            }
        }
        {
            Table& t = book.Get("TownProp");
            uint16_t seq = 0;
            for (const auto& p : props)
            {
                Row w;
                w.Put("seq", ++seq).Put("kind", p.kind).Put("x", p.x).Put("y", p.y).Put("x2", p.x2).Put("y2", p.y2);
                t.Add(w);
            }
        }
    }
}
