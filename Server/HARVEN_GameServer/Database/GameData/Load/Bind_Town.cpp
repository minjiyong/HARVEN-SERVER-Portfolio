#include "pch.h"
// ─────────────────────────────────────────────────────────────────────────────
// 마을 묶음 — 스냅샷 → 파사드 TownMap::* · WorldData::(TownCell · TownLevel · IsTrainingYard · DummySpots …).
//   mapgen이 주인(HARVEN-DESIGN tools/mapgen/gen_town.py가 쓴다): GameData/TownInfo · TownGrid · TownBuilding · TownSpawn ·
//     TownDummy · TownSpot · TownNpcHome · TownProp
//   시트 HARVEN_DATA가 주인(datatool export): GameData/Town · District · Ambient
// 모양은 Bind_Monster.cpp(모범 표)와 같다:
//   1) 파사드 — GD()의 스냅샷을 읽는다(격자 칸은 평면 배열을 바로 색인 — O(1))
//   2) Bind    — 열마다 RowReader + 구조 검사(구조 상수 TownMap::kZoneId · kX0 · kY0 · kW · kH와 TownInfo가 같은지 · seq 연속 · 한 글자 열)
//   3) Validate — 옮겨 온 규칙(RULES.md R064 · R072~R075, 예전 static_assert ValidateYard · ValidateTown = R065 · R080) + 새 규칙(R104)
//   4) Dump    — Canon_Town.h(datatool export와 같은 바이트)로 정본 표
// ─────────────────────────────────────────────────────────────────────────────
#include <algorithm>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "Database/GameData/Load/Bundles.h"
#include "Database/GameData/Load/Canon_Town.h"
#include "Database/GameData/Load/JsonRead.h"
#include "Database/GameData/Load/Snapshot.h"
#include "Database/GameData/Load/Validate.h"
#include "Database/GameData/TownMap.h"
#include "Database/GameData/WorldData.h"

// ── 1) 파사드 ────────────────────────────────────────────────────────────────
namespace TownMap
{
    const char* Name() noexcept { return GD().town.name; }

    std::span<const char* const> GroundRows() noexcept { return GD().town.groundRows; }
    std::span<const char* const> SurfaceRows() noexcept { return GD().town.surfaceRows; }
    std::span<const char* const> LevelRows() noexcept { return GD().town.levelRows; }

    std::span<const BuildingRow> Buildings() noexcept { return GD().town.buildings; }
    std::span<const Point16> Spawns() noexcept { return GD().town.spawns; }
    std::span<const Point16> Dummies() noexcept { return GD().town.dummies; }
    Rect16 Yard() noexcept { return GD().town.yard; }

    std::span<const SpotRow> Spots() noexcept { return GD().town.spots; }

    const PointF* FindSpot(std::string_view key) noexcept
    {
        for (const SpotRow& s : GD().town.spots)
            if (key == s.key)
                return &s.p;
        return nullptr;
    }

    std::span<const NpcHome> NpcHomes() noexcept { return GD().town.npcHomes; }
    const NpcHome* FindNpcHome(uint16_t npcId) noexcept { return ::FindNpcHome(GD().town, npcId); }

    std::span<const DistrictRow> Districts() noexcept { return GD().town.districts; }
    std::span<const AmbientRow> Ambient() noexcept { return GD().town.ambient; }
    std::span<const PropRow> Props() noexcept { return GD().town.props; }
}

namespace WorldData
{
    namespace Detail
    {
        // 호출하는 쪽(TownCell · TownLevel)이 IsInTown으로 범위를 이미 확인했다 — 평면 격자는 늘 kW × kH
        char TownGroundAt(int lx, int ly) noexcept
        {
            return GD().town.ground[static_cast<size_t>(ly) * TownMap::kW + static_cast<size_t>(lx)];
        }
        char TownLevelAt(int lx, int ly) noexcept
        {
            return GD().town.level[static_cast<size_t>(ly) * TownMap::kW + static_cast<size_t>(lx)];
        }

    }

    bool IsTrainingYard(int x, int y) noexcept
    {
        const TownMap::Rect16& r = GD().town.yard;
        return x >= r.x0 && x <= r.x1 && y >= r.y0 && y <= r.y1;
    }

    std::span<const DummySpot> DummySpots() noexcept { return GD().town.dummies; }

}

namespace GameDataLoad
{
    namespace
    {
        // 한 행짜리 표(Town · TownInfo) — 행이 하나가 아니면 로드 실패(구조)
        const Json* OneRow(LoadContext& ctx, const char* table)
        {
            const Json* rows = ctx.Rows(table);
            if (rows == nullptr)
                return nullptr;
            if (rows->size() != 1)
            {
                ctx.Error(std::string(table) + ".json", "행은 1개여야 한다(마을 하나) — " + std::to_string(rows->size()) + "개");
                return nullptr;
            }
            return rows;
        }

        // 구조 상수와 같아야 하는 값 — 다르면 로드 실패(조용히 맞추지 않는다)
        void MustEqual(LoadContext& ctx, const char* table, const char* key, long long got, int want, const char* name)
        {
            if (got != want)
                ctx.Error(ctx.Where(table, 0, key), std::to_string(got) + " ≠ 구조 상수 " + name + "(" + std::to_string(want) +
                    ") — 바꾸려면 서버 코드(Data/TownMap.h)와 mapgen town.txt를 함께");
        }

        // seq = 행 순서(1부터 연속) — 덤프가 행 순서로 다시 매기므로 어긋나면 GameData와 메모리가 달라진다
        void SeqMustFollow(LoadContext& ctx, const char* table, size_t index, unsigned seq)
        {
            if (seq != index + 1)
                ctx.Error(ctx.Where(table, index, "seq"), "seq는 1부터 연속(행 순서) — " + std::to_string(seq) + " ≠ " + std::to_string(index + 1));
        }

        // 평면 격자에 한 줄 복사(넘치는 글자는 버리고 모자란 칸은 채운 값 그대로 — 길이 오류는 R072가 위치와 함께 남긴다)
        void CopyRow(std::vector<char>& grid, size_t y, const char* row)
        {
            if (row == nullptr || y >= static_cast<size_t>(TownMap::kH))
                return;
            const size_t n = std::min(std::strlen(row), static_cast<size_t>(TownMap::kW));
            std::memcpy(grid.data() + y * TownMap::kW, row, n);
        }

        std::string Cell(int x, int y) { return "(" + std::to_string(x) + ", " + std::to_string(y) + ")"; }
    }

    // ── 2) Bind ──────────────────────────────────────────────────────────────
    void BindTown(LoadContext& ctx, GameDataSnapshot& snap)
    {
        SnapTown& t = snap.town;
        t.ground.assign(static_cast<size_t>(TownMap::kW) * TownMap::kH, '#');
        t.level.assign(static_cast<size_t>(TownMap::kW) * TownMap::kH, '0');

        // Town(시트) · TownInfo(mapgen)
        bool sheetOk = false, infoOk = false;
        if (const Json* rows = OneRow(ctx, "Town"))
        {
            const size_t before = ctx.ErrorCount();
            RowReader r(ctx, "Town", 0, (*rows)[0]);
            r.Int("zone_id", t.sheetZoneId)
             .Str("name", t.name);
            r.Finish();
            sheetOk = ctx.ErrorCount() == before;
        }
        if (const Json* rows = OneRow(ctx, "TownInfo"))
        {
            const size_t before = ctx.ErrorCount();
            RowReader r(ctx, "TownInfo", 0, (*rows)[0]);
            r.Int("zone_id", t.zoneId)
             .Int("x0", t.x0)
             .Int("y0", t.y0)
             .Int("w", t.w)
             .Int("h", t.h)
             .Int("yard_x0", t.yard.x0)
             .Int("yard_y0", t.yard.y0)
             .Int("yard_x1", t.yard.x1)
             .Int("yard_y1", t.yard.y1);
            r.Finish();
            infoOk = ctx.ErrorCount() == before;
            if (infoOk)
            {
                MustEqual(ctx, "TownInfo", "zone_id", t.zoneId, TownMap::kZoneId, "TownMap::kZoneId");
                MustEqual(ctx, "TownInfo", "x0", t.x0, TownMap::kX0, "TownMap::kX0");
                MustEqual(ctx, "TownInfo", "y0", t.y0, TownMap::kY0, "TownMap::kY0");
                MustEqual(ctx, "TownInfo", "w", t.w, TownMap::kW, "TownMap::kW");
                MustEqual(ctx, "TownInfo", "h", t.h, TownMap::kH, "TownMap::kH");
            }
        }
        if (sheetOk && infoOk && t.sheetZoneId != t.zoneId)
            ctx.Error(ctx.Where("Town", 0, "zone_id"), std::to_string(t.sheetZoneId) + " ≠ TownInfo.zone_id " + std::to_string(t.zoneId) + "(mapgen town.txt)");

        // TownGrid — 행 = y
        if (const Json* rows = ctx.Rows("TownGrid"))
        {
            t.groundRows.reserve(rows->size());
            t.surfaceRows.reserve(rows->size());
            t.levelRows.reserve(rows->size());
            for (size_t i = 0; i < rows->size(); ++i)
            {
                RowReader r(ctx, "TownGrid", i, (*rows)[i]);
                uint16_t y = 0;
                const char* ground = nullptr;
                const char* surface = nullptr;
                const char* level = nullptr;
                r.Int("y", y)
                 .Str("ground", ground)
                 .Str("surface", surface)
                 .Str("level", level);
                r.Finish();
                if (r.Ok() && y != i)
                    ctx.Error(r.Where("y"), "y는 0부터 연속(행 = y) — " + std::to_string(y) + " ≠ " + std::to_string(i));
                t.groundRows.push_back(ground != nullptr ? ground : "");
                t.surfaceRows.push_back(surface != nullptr ? surface : "");
                t.levelRows.push_back(level != nullptr ? level : "");
                CopyRow(t.ground, i, ground);
                CopyRow(t.level, i, level);
            }
        }

        if (const Json* rows = ctx.Rows("TownBuilding"))
        {
            t.buildings.reserve(rows->size());
            for (size_t i = 0; i < rows->size(); ++i)
            {
                RowReader r(ctx, "TownBuilding", i, (*rows)[i]);
                TownMap::BuildingRow b{};
                b.sign = "";
                uint16_t seq = 0;
                char door[2] = {};
                const size_t before = ctx.ErrorCount();
                r.Int("seq", seq)
                 .Enum("kind", b.kind)
                 .Int("x0", b.x0)
                 .Int("y0", b.y0)
                 .Int("x1", b.x1)
                 .Int("y1", b.y1)
                 .Int("level", b.level)
                 .Chars("door", door)
                 .Int("variant", b.variant)
                 .Str("sign", b.sign);
                r.Finish();
                if (ctx.ErrorCount() == before)
                {
                    SeqMustFollow(ctx, "TownBuilding", i, seq);
                    if (door[0] == '\0')
                        ctx.Error(r.Where("door"), "문이 난 면 한 글자(S · E · N · W)여야 한다 — 비었다");
                }
                b.door = door[0];
                t.buildings.push_back(b);
            }
        }

        auto points = [&ctx](const char* table, std::vector<TownMap::Point16>& out)
        {
            const Json* rows = ctx.Rows(table);
            if (rows == nullptr)
                return;
            out.reserve(rows->size());
            for (size_t i = 0; i < rows->size(); ++i)
            {
                RowReader r(ctx, table, i, (*rows)[i]);
                TownMap::Point16 p{};
                uint8_t seq = 0;
                const size_t before = ctx.ErrorCount();
                r.Int("seq", seq)
                 .Int("x", p.x)
                 .Int("y", p.y);
                r.Finish();
                if (ctx.ErrorCount() == before)
                    SeqMustFollow(ctx, table, i, seq);
                out.push_back(p);
            }
        };
        points("TownSpawn", t.spawns);
        points("TownDummy", t.dummies);

        // TownSpot — 정본 행 순서 = key 오름차순(코드는 이름으로만 찾는다 — 이름이 겹치면 안 된다)
        if (const Json* rows = ctx.Rows("TownSpot"))
        {
            t.spots.reserve(rows->size());
            for (size_t i = 0; i < rows->size(); ++i)
            {
                RowReader r(ctx, "TownSpot", i, (*rows)[i]);
                TownMap::SpotRow s{ "", {} };
                const size_t before = ctx.ErrorCount();
                r.Str("key", s.key)
                 .F32("x", s.p.x)
                 .F32("y", s.p.y);
                r.Finish();
                if (ctx.ErrorCount() == before)
                {
                    if (*s.key == '\0')
                        ctx.Error(r.Where("key"), "이름이 비었다");
                    else if (!t.spots.empty() && !(std::string_view(t.spots.back().key) < std::string_view(s.key)))
                        ctx.Error(r.Where("key"), std::string("key는 오름차순 · 겹치지 않게 — '") + s.key + "'가 '" + t.spots.back().key + "' 뒤");
                }
                t.spots.push_back(s);
            }
        }

        if (const Json* rows = ctx.Rows("TownNpcHome"))
        {
            t.npcHomes.reserve(rows->size());
            for (size_t i = 0; i < rows->size(); ++i)
            {
                RowReader r(ctx, "TownNpcHome", i, (*rows)[i]);
                TownMap::NpcHome h{};
                r.Int("npc_id", h.id)
                 .Int("x", h.x)
                 .Int("y", h.y);
                r.Finish();
                t.npcHomes.push_back(h);
            }
        }

        if (const Json* rows = ctx.Rows("TownProp"))
        {
            t.props.reserve(rows->size());
            for (size_t i = 0; i < rows->size(); ++i)
            {
                RowReader r(ctx, "TownProp", i, (*rows)[i]);
                TownMap::PropRow p{};
                uint16_t seq = 0;
                const size_t before = ctx.ErrorCount();
                r.Int("seq", seq)
                 .Enum("kind", p.kind)
                 .Int("x", p.x)
                 .Int("y", p.y)
                 .Int("x2", p.x2)
                 .Int("y2", p.y2);
                r.Finish();
                if (ctx.ErrorCount() == before)
                    SeqMustFollow(ctx, "TownProp", i, seq);
                t.props.push_back(p);
            }
        }

        // District · Ambient(시트)
        if (const Json* rows = ctx.Rows("District"))
        {
            t.districts.reserve(rows->size());
            for (size_t i = 0; i < rows->size(); ++i)
            {
                RowReader r(ctx, "District", i, (*rows)[i]);
                TownMap::DistrictRow d{ "", "", 0.f, 0.f, 0.f };
                uint8_t seq = 0;
                const size_t before = ctx.ErrorCount();
                r.Int("seq", seq)
                 .Str("name", d.name)
                 .Str("sub", d.sub)
                 .F32("x", d.x)
                 .F32("y", d.y)
                 .F32("zoom", d.zoom);
                r.Finish();
                if (ctx.ErrorCount() == before)
                    SeqMustFollow(ctx, "District", i, seq);
                t.districts.push_back(d);
            }
        }
        if (const Json* rows = ctx.Rows("Ambient"))
        {
            t.ambient.reserve(rows->size());
            for (size_t i = 0; i < rows->size(); ++i)
            {
                RowReader r(ctx, "Ambient", i, (*rows)[i]);
                TownMap::AmbientRow a{};
                uint8_t seq = 0;
                const size_t before = ctx.ErrorCount();
                r.Int("seq", seq)
                 .Enum("kind", a.kind)
                 .F32("x", a.x)
                 .F32("y", a.y)
                 .F32("radius", a.radius);
                r.Finish();
                if (ctx.ErrorCount() == before)
                    SeqMustFollow(ctx, "Ambient", i, seq);
                t.ambient.push_back(a);
            }
        }
    }

    // ── 3) Validate — 예전 WorldData::Detail::ValidateYard(R064 · static_assert R065) · ValidateTown(R072~R075 · static_assert R080) ──
    //    생성기(gen_town.py)도 같은 규칙을 생성 때 검사한다. 칸 판정은 파사드(WorldData::IsBlocked …)로 — 새 판을 읽는다.
    void ValidateTown(ValidateContext& v, const GameDataSnapshot& snap)
    {
        const SnapTown& t = snap.town;
        const std::string town = "마을(" + std::to_string(WorldData::kTownX0) + "~" + std::to_string(WorldData::kTownX0 + WorldData::kTownW - 1) + ", " +
            std::to_string(WorldData::kTownY0) + "~" + std::to_string(WorldData::kTownY0 + WorldData::kTownH - 1) + ")";

        // R104 — 부활 · 귀환 · 첫 접속 자리가 하나 이상(ContentHelper::PickTownSpawn이 행 수로 나눈다 — 비면 0으로 나누기)
        if (t.spawns.empty())
            v.Fail("R104", "TownSpawn", "행이 없다 — 부활 · 귀환 · 첫 접속 자리가 하나 이상 있어야 한다(mapgen이 쓰는 표)");

        // R072 — 격자 kH줄 · 줄마다 kW글자(ground · surface · level). 틀리면 칸을 읽는 규칙(R074 · R075 · R064)은 건너뛴다
        bool gridOk = true;
        const size_t n = t.groundRows.size();
        if (n != static_cast<size_t>(TownMap::kH))
        {
            gridOk = false;
            v.Fail("R072", v.Where("TownGrid", std::min(n, static_cast<size_t>(TownMap::kH)), "y"),
                "격자 줄 수 " + std::to_string(n) + " ≠ h " + std::to_string(TownMap::kH) + "(TownMap::kH)");
        }
        for (size_t i = 0; i < n; ++i)
        {
            const std::pair<const char*, const char*> cols[] = { { "ground", t.groundRows[i] }, { "surface", t.surfaceRows[i] }, { "level", t.levelRows[i] } };
            for (const auto& [key, row] : cols)
            {
                const size_t len = std::strlen(row);
                if (len != static_cast<size_t>(TownMap::kW))
                {
                    gridOk = false;
                    v.Fail("R072", v.Where("TownGrid", i, key), "줄 길이 " + std::to_string(len) + " ≠ w " + std::to_string(TownMap::kW) + "(TownMap::kW)");
                }
            }
        }

        // R073 · R074 — 건물 양 끝이 마을 안, 발자국 칸은 모두 막힌 'B'(충돌은 칸 문자가 맡는다)
        for (size_t i = 0; i < t.buildings.size(); ++i)
        {
            const TownMap::BuildingRow& b = t.buildings[i];
            const bool in0 = WorldData::IsInTown(b.x0, b.y0);
            const bool in1 = WorldData::IsInTown(b.x1, b.y1);
            if (!in0 || !in1)
            {
                v.Fail("R073", v.Where("TownBuilding", i, !in0 ? "x0" : "x1"),
                    "건물 끝 " + (!in0 ? Cell(b.x0, b.y0) : Cell(b.x1, b.y1)) + "이 " + town + " 밖");
                continue;
            }
            if (!gridOk)
                continue;
            bool footprintOk = true;
            for (int y = b.y0; y <= b.y1 && footprintOk; ++y)
                for (int x = b.x0; x <= b.x1 && footprintOk; ++x)
                {
                    const char c = WorldData::TownCell(x, y);
                    if (c != 'B')
                    {
                        footprintOk = false;
                        v.Fail("R074", v.Where("TownBuilding", i, "x0"),
                            "발자국 칸 " + Cell(x, y) + "이 '" + std::string(1, c) + "' — 건물 발자국은 모두 'B'(TownGrid ground)");
                    }
                }
        }
        if (!gridOk)
            return;

        // R075 — 부활 · 귀환 자리는 걸을 수 있는 마을 칸
        for (size_t i = 0; i < t.spawns.size(); ++i)
        {
            const TownMap::Point16& p = t.spawns[i];
            if (WorldData::IsBlocked(p.x, p.y) || !WorldData::IsInTown(p.x, p.y))
                v.Fail("R075", v.Where("TownSpawn", i, "x"),
                    Cell(p.x, p.y) + "은 걸을 수 있는 " + town + " 칸이어야 한다(부활 · 귀환 자리)" +
                    (WorldData::IsInTown(p.x, p.y) ? " — 칸 '" + std::string(1, WorldData::TownCell(p.x, p.y)) + "'" : std::string()));
        }

        // R064 — 허수아비 자리는 훈련장 안 · 걸을 수 있음 · 안전지대
        const TownMap::Rect16 yard = t.yard;
        for (size_t i = 0; i < t.dummies.size(); ++i)
        {
            const TownMap::Point16& d = t.dummies[i];
            const bool inYard = WorldData::IsTrainingYard(d.x, d.y);
            if (!inYard || WorldData::IsBlocked(d.x, d.y) || !WorldData::IsSafeZone(d.x, d.y))
            {
                const char* key = (!inYard && d.x >= yard.x0 && d.x <= yard.x1) ? "y" : "x";   // 훈련장 밖이면 벗어난 축
                std::string why = !inYard ? "훈련장(" + Cell(yard.x0, yard.y0) + "~" + Cell(yard.x1, yard.y1) + ") 밖"
                    : !WorldData::IsSafeZone(d.x, d.y) ? "안전지대(마을) 밖" : "막힌 칸 '" + std::string(1, WorldData::TownCell(d.x, d.y)) + "'";
                v.Fail("R064", v.Where("TownDummy", i, key), "허수아비 자리 " + Cell(d.x, d.y) + " — " + why + "(훈련장 안의 걸을 수 있는 칸이어야 한다)");
            }
        }
    }

    // ── 4) Dump ──────────────────────────────────────────────────────────────
    void DumpTown(Canon::Book& book, const GameDataSnapshot& snap)
    {
        const SnapTown& t = snap.town;
        Canon::TownInfoV info;
        info.name = t.name;
        info.zoneId = t.zoneId; info.x0 = t.x0; info.y0 = t.y0; info.w = t.w; info.h = t.h;
        info.yardX0 = t.yard.x0; info.yardY0 = t.yard.y0; info.yardX1 = t.yard.x1; info.yardY1 = t.yard.y1;
        Canon::WriteTownSheet(book, info, t.districts, t.ambient);
        std::vector<Canon::TownSpotV> spots;
        spots.reserve(t.spots.size());
        for (const TownMap::SpotRow& s : t.spots)
            spots.push_back({ s.key, s.p });
        Canon::WriteTownLayout(book, info, t.groundRows, t.surfaceRows, t.levelRows, t.buildings, t.spawns, t.dummies, spots, t.npcHomes, t.props);
    }
}
