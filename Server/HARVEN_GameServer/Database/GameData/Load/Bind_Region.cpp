#include "pch.h"
// ─────────────────────────────────────────────────────────────────────────────
// 지형 묶음 — 탭 Region · RegionSpawn(GameData/Region.json · RegionSpawn.json) → 스냅샷 → 파사드 WorldData::FindRegion.
// Bind_Monster.cpp(Phase 1 모범 표)의 모양을 따른다.
//   1) 파사드  — WorldRegion.h에 선언된 FindRegion을 GD()의 스냅샷으로
//   2) Bind    — 열마다 RowReader. RegionSpawn(zone_id, seq)은 부모 행의 spawns[seq - 1]로(구조 상수 spawns[4])
//   3) Validate — 옮겨 온 규칙(RULES.md R076~R079 — 예전 static_assert(Detail::ValidateRegions()) R081) + 종족 참조
//   4) Dump    — Canon_Region.h(datatool export와 같은 바이트)로 정본 표
// 월드 · Zone 배치(kWorldWidth · kRegionSize · kRegionCount = 16)는 구조 상수 — 코드에 남는다.
// ─────────────────────────────────────────────────────────────────────────────
#include "Database/GameData/Load/Bundles.h"
#include "Database/GameData/Load/Canon_Region.h"
#include "Database/GameData/Load/JsonRead.h"
#include "Database/GameData/Load/Snapshot.h"
#include "Database/GameData/Load/Validate.h"
#include "Database/GameData/MonsterTable.h"
#include "Database/GameData/WorldData.h"

// ── 1) 파사드 ────────────────────────────────────────────────────────────────
namespace WorldData
{
    const RegionRow* FindRegion(int zoneId) noexcept
    {
        // 이관 전: (0 ≤ zoneId < kRegionCount) ? &kRegions[zoneId] : nullptr. 행 순서 = zone_id(R076)라 같은 행이다.
        const std::vector<RegionRow>& rows = GD().region.rows;
        if (zoneId < 0 || zoneId >= kRegionCount || static_cast<size_t>(zoneId) >= rows.size())
            return nullptr;
        return &rows[static_cast<size_t>(zoneId)];
    }

    std::span<const RegionRow> Regions() noexcept { return GD().region.rows; }
}

namespace GameDataLoad
{
    namespace
    {
        constexpr size_t kNoRow = static_cast<size_t>(-1);

        // 좌표 (x, y)가 지역 zone 밖일 때 어느 열이 틀렸는지 — x가 월드 밖이거나 다른 지역 열이면 x, 아니면 y
        const char* OffAxis(int x, int zone, const char* keyX, const char* keyY) noexcept
        {
            const bool xOk = x >= 0 && x < WorldData::kWorldWidth && x / WorldData::kRegionSize == zone % WorldData::kRegionCountX;
            return xOk ? keyY : keyX;
        }

        std::string Point(int x, int y) { return "(" + std::to_string(x) + ", " + std::to_string(y) + ")"; }
    }

    // ── 2) Bind ──────────────────────────────────────────────────────────────
    void BindRegion(LoadContext& ctx, GameDataSnapshot& snap)
    {
        SnapRegion& g = snap.region;
        if (const Json* rows = ctx.Rows("Region"))
        {
            g.rows.reserve(rows->size());
            g.spawnSrc.reserve(rows->size());
            for (size_t i = 0; i < rows->size(); ++i)
            {
                RowReader r(ctx, "Region", i, (*rows)[i]);
                WorldData::RegionRow row{};
                r.Int("zone_id", row.zoneId)
                 .Str("name", row.name)
                 .Enum("biome", row.biome)
                 .Int("tier", row.tier)
                 .Int("boss_species", row.bossSpecies)
                 .Int("boss_x", row.bossX)
                 .Int("boss_y", row.bossY)
                 .Int("land_x", row.landX)
                 .Int("land_y", row.landY)
                 .Int("teleport_fee", row.teleportFee);
                r.Finish();
                g.rows.push_back(row);
                g.spawnSrc.push_back({ -1, -1, -1, -1 });
            }
        }

        const Json* spawns = ctx.Rows("RegionSpawn");
        if (spawns == nullptr)
            return;
        for (size_t i = 0; i < spawns->size(); ++i)
        {
            RowReader r(ctx, "RegionSpawn", i, (*spawns)[i]);
            uint16_t zone = 0;
            uint8_t seq = 0;
            WorldData::RegionSpawn sp{};
            const size_t before = ctx.ErrorCount();
            r.Int("zone_id", zone)
             .Int("seq", seq)
             .Int("species", sp.species)
             .Int("weight", sp.weight);
            r.Finish();
            if (ctx.ErrorCount() != before)
                continue;
            // 부모 행은 zone_id로 찾는다(행 순서가 아니라 — 행 순서 = zone_id는 R076이 따로 본다)
            size_t parent = kNoRow;
            for (size_t k = 0; k < g.rows.size(); ++k)
                if (g.rows[k].zoneId == zone) { parent = k; break; }
            if (parent == kNoRow)
            {
                ctx.Error(r.Where("zone_id"), "Region에 없는 zone_id " + std::to_string(zone));
                continue;
            }
            if (seq < 1 || seq > WorldData::kRegionSpawnSlots)
            {
                ctx.Error(r.Where("seq"), "seq " + std::to_string(seq) + " — 1~" + std::to_string(WorldData::kRegionSpawnSlots) + "(구조 상수 spawns[" +
                    std::to_string(WorldData::kRegionSpawnSlots) + "] — 지역마다 스폰은 최대 " + std::to_string(WorldData::kRegionSpawnSlots) + "개)");
                continue;
            }
            int32_t& src = g.spawnSrc[parent][seq - 1u];
            if (src >= 0)
            {
                ctx.Error(r.Where("seq"), "zone_id " + std::to_string(zone) + " · seq " + std::to_string(seq) + " 중복(" +
                    ctx.Where("RegionSpawn", static_cast<size_t>(src)) + ")");
                continue;
            }
            src = static_cast<int32_t>(i);
            g.rows[parent].spawns[seq - 1u] = sp;
        }
        // seq는 지역마다 1부터 연속 — spawns[4]의 빈 칸(species 0)은 뒤쪽에만 있다(정본 · 스폰 고르기가 그렇게 읽는다)
        for (size_t k = 0; k < g.rows.size(); ++k)
        {
            bool hole = false;
            for (int32_t src : g.spawnSrc[k])
            {
                if (src < 0)
                    hole = true;
                else if (hole)
                {
                    ctx.Error(ctx.Where("RegionSpawn", static_cast<size_t>(src), "seq"),
                        "zone_id " + std::to_string(g.rows[k].zoneId) + "의 seq가 1부터 연속이 아니다");
                    break;
                }
            }
        }
    }

    // ── 3) Validate — 예전 WorldData::Detail::ValidateRegions()(static_assert R081 → 과도기 부팅 검증)의 규칙 ──
    // 걷기 판정은 마을 파사드 WorldData::IsBlocked(마을 격자 · 옛 패턴)를 부른다 — 마을 묶음이 먼저 묶인다.
    void ValidateRegion(ValidateContext& v, const GameDataSnapshot& snap)
    {
        using namespace WorldData;
        const SnapRegion& g = snap.region;
        const auto& rows = g.rows;

        // R076: 행 수 = kRegionCount(Zone 배치 구조 상수), zone_id = 행 순서
        if (rows.size() < static_cast<size_t>(kRegionCount))
            v.Fail("R076", "Region", "행 수 " + std::to_string(rows.size()) + " < " + std::to_string(kRegionCount) + "(kRegionCount — Zone 배치 구조 상수)");
        for (size_t i = static_cast<size_t>(kRegionCount); i < rows.size(); ++i)
            v.Fail("R076", v.Where("Region", i, "zone_id"), "행 수 " + std::to_string(rows.size()) + " > " + std::to_string(kRegionCount) + "(kRegionCount — Zone 배치 구조 상수)");

        for (size_t i = 0; i < rows.size(); ++i)
        {
            const RegionRow& r = rows[i];
            const int zone = static_cast<int>(i);
            if (r.zoneId != i)
                v.Fail("R076", v.Where("Region", i, "zone_id"), "zone_id는 행 순서여야 한다(행 " + std::to_string(i) + " → zone_id " + std::to_string(r.zoneId) + ")");

            // R077: 길잡이 도착 지점 — 그 지역 안 · 걸을 수 있음
            if (RegionIdAt(r.landX, r.landY) != zone)
                v.Fail("R077", v.Where("Region", i, OffAxis(r.landX, zone, "land_x", "land_y")),
                    "길잡이 도착 지점 " + Point(r.landX, r.landY) + "이 지역 " + std::to_string(zone) + " 밖이다(지역 " + std::to_string(RegionIdAt(r.landX, r.landY)) + ")");
            else if (IsBlocked(r.landX, r.landY))
                v.Fail("R077", v.Where("Region", i, "land_x"), "길잡이 도착 지점 " + Point(r.landX, r.landY) + "이 막힌 칸이다");

            // R078: 필드 보스가 있으면 보스 자리 — 그 지역 안 · 걸을 수 있음
            if (r.bossSpecies != 0)
            {
                if (RegionIdAt(r.bossX, r.bossY) != zone)
                    v.Fail("R078", v.Where("Region", i, OffAxis(r.bossX, zone, "boss_x", "boss_y")),
                        "보스 자리 " + Point(r.bossX, r.bossY) + "가 지역 " + std::to_string(zone) + " 밖이다(지역 " + std::to_string(RegionIdAt(r.bossX, r.bossY)) + ")");
                else if (IsBlocked(r.bossX, r.bossY))
                    v.Fail("R078", v.Where("Region", i, "boss_x"), "보스 자리 " + Point(r.bossX, r.bossY) + "가 막힌 칸이다");
                // 참조(시트 스키마 ref: Monster와 같은 검사 — 서버 쪽에서도)
                if (MonsterTable::Find(r.bossSpecies) == nullptr)
                    v.Fail("", v.Where("Region", i, "boss_species"), "Monster에 없는 종족 id " + std::to_string(r.bossSpecies));
            }

            // R079: 스폰 가중치 합 > 0(RegionSpawn — 지역마다)
            int total = 0;
            for (const auto& s : r.spawns)
                total += s.weight;
            const auto& src = g.spawnSrc[i];
            if (total == 0)
            {
                if (src[0] >= 0)
                    v.Fail("R079", v.Where("RegionSpawn", static_cast<size_t>(src[0]), "weight"),
                        "zone_id " + std::to_string(r.zoneId) + "의 스폰 가중치 합이 0이다");
                else
                    v.Fail("R079", v.Where("Region", i, "zone_id"),
                        "zone_id " + std::to_string(r.zoneId) + "의 RegionSpawn 행이 없다(스폰 가중치 합 0)");
            }

            // 스폰 종족 참조 — MonsterTable 파사드(새 판)로
            for (size_t k = 0; k < src.size(); ++k)
                if (src[k] >= 0 && MonsterTable::Find(r.spawns[k].species) == nullptr)
                    v.Fail("", v.Where("RegionSpawn", static_cast<size_t>(src[k]), "species"),
                        "Monster에 없는 종족 id " + std::to_string(r.spawns[k].species));
        }
    }

    // ── 4) Dump ──────────────────────────────────────────────────────────────
    void DumpRegion(Canon::Book& book, const GameDataSnapshot& snap)
    {
        Canon::WriteRegion(book, snap.region.rows);
    }
}
