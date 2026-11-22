#pragma once
// 마을 칸 · 층 · 안전지대 · 훈련장 · 충돌 · 건물 · 부활 지점(마을 묶음 — mapgen).
// WorldData.h에서 나눴다(내용 이동만 — 이관 Phase 1). 우산 헤더 Data/WorldData.h가 원래 순서로 include한다.
// 값은 GameData/에서(Phase 2 — Data/Load/Bind_Town.cpp). 마을의 자리 · 크기(kTown*)는 구조 상수라 컴파일 때 그대로다.
#include <cstddef>
#include <cstdint>
#include <span>
#include "Database/GameData/TownMap.h"
#include "Database/GameData/World/WorldRegion.h"

namespace WorldData
{
    // ─────────────────────────────────────────────────────────────────────────
    // 마을(안전지대) — 데니차(변경말로 '샛별'). 칸 · 층 · 건물은 GameData/Town*.json(생성물 —
    // 원본 HARVEN-DESIGN/tools/mapgen/denica/*.txt, 생성기 HARVEN-DESIGN/tools/mapgen/gen_town.py). Phase 4-A.
    // 자리 · 크기 · Zone은 구조 상수(TownMap.h — TownInfo가 다르면 로드 실패)라 아래 판정은 컴파일 때도 쓸 수 있다.
    // ─────────────────────────────────────────────────────────────────────────
    inline constexpr int kTownZoneId = TownMap::kZoneId;
    inline constexpr int kTownX0 = TownMap::kX0;
    inline constexpr int kTownY0 = TownMap::kY0;
    inline constexpr int kTownW = TownMap::kW;
    inline constexpr int kTownH = TownMap::kH;
    // 마을 이름(시트 Town.name — 예전 kTownName)
    [[nodiscard]] inline const char* TownName() noexcept { return TownMap::Name(); }

    namespace Detail
    {
        // 마을 기준 칸(0 ≤ lx < kTownW, 0 ≤ ly < kTownH)의 격자 문자 — 스냅샷을 바로 색인(O(1)). Data/Load/Bind_Town.cpp
        [[nodiscard]] char TownGroundAt(int lx, int ly) noexcept;
        [[nodiscard]] char TownLevelAt(int lx, int ly) noexcept;
    }

    [[nodiscard]] constexpr bool IsInTown(int x, int y) noexcept
    {
        return x >= kTownX0 && x < kTownX0 + kTownW && y >= kTownY0 && y < kTownY0 + kTownH;
    }

    // 마을 칸 문자(마을 밖이면 '\0') — 글자 뜻은 HARVEN-DESIGN/tools/mapgen/gen_town.py의 LEGEND.
    // constexpr로 남긴다: 마을 밖 칸은 컴파일 때도 답이 나온다(이 함수를 부르는 constexpr 함수 — MoveRule · QuestData — 가 그대로 상수 함수).
    // 마을 안 칸은 스냅샷을 읽는다(GameData).
    [[nodiscard]] constexpr char TownCell(int x, int y) noexcept
    {
        return IsInTown(x, y) ? Detail::TownGroundAt(x - kTownX0, y - kTownY0) : '\0';
    }

    // 마을 칸의 층(0~4, 계단은 낮은 쪽 층). 마을 밖은 0(들판)
    [[nodiscard]] constexpr int TownLevel(int x, int y) noexcept
    {
        return IsInTown(x, y) ? Detail::TownLevelAt(x - kTownX0, y - kTownY0) - '0' : 0;
    }

    // 계단 문자 → 오르는 방향(없으면 false)
    [[nodiscard]] constexpr bool StairsDir(char c, int& dx, int& dy) noexcept
    {
        switch (c)
        {
        case '^': dx = 0; dy = -1; return true;
        case 'v': dx = 0; dy = 1; return true;
        case '<': dx = -1; dy = 0; return true;
        case '>': dx = 1; dy = 0; return true;
        default: return false;
        }
    }

    [[nodiscard]] constexpr bool IsTownCellBlocked(char c) noexcept
    {
        switch (c)
        {
        case '.': case '=': case ',': case ':': case 's': case 'G': case 'b': case 'g': case 'N':
        case '^': case 'v': case '<': case '>':
            return false;   // 풀밭 · 길 · 광장 · 흙길 · 모래 · 성문 · 다리 · 묘지 풀 · 계단
        default:
            return true;    // 성벽 · 망루 · 건물 · 분수 · 나무 · 가로등 · 꽃밭 · 가판대 · 가장자리(R) · 물 · 바위 …
        }
    }

    // 안전지대: 마을 사각형 전체(성벽 포함). PvP 불가, 몬스터 진입/피해 불가, 회복 2배.
    [[nodiscard]] constexpr bool IsSafeZone(int x, int y) noexcept
    {
        return IsInTown(x, y);
    }

    // ── Phase 2-E 훈련장(마을 동쪽 풀밭) — 안전지대 안이지만 여기서는 기술을 쓸 수 있다(맞는 것은 허수아비뿐:
    //    몬스터는 마을에 들어오지 못하고, 안전지대의 사람은 강제 공격으로도 맞지 않는다). Phase 4에서 훈련장 건물로 옮긴다.
    //    사각형(양끝 포함)은 TownInfo.yard_*(anchors.txt yard) — 예전 kYardX0 … kYardY1. Data/Load/Bind_Town.cpp
    [[nodiscard]] bool IsTrainingYard(int x, int y) noexcept;
    using DummySpot = TownMap::Point16;
    // 허수아비 자리(TownDummy) — 예전 kDummySpots · kDummyCount(= 스팬 크기)
    [[nodiscard]] std::span<const DummySpot> DummySpots() noexcept;

    // Continuous road lattice between all 16 existing regions and town exits.
    [[nodiscard]] constexpr bool IsRoad(int x, int y) noexcept
    {
        if (x < 3 || y < 3 || x >= 1997 || y >= 1997 || IsInTown(x, y)) return false;
        return (x % 500 >= 248 && x % 500 <= 252) || (y % 500 >= 248 && y % 500 <= 252)
            || (x >= 686 && x <= 707 && y >= 764 && y <= 768) || (x >= 686 && x <= 690 && y >= 748 && y <= 768)
            || (x >= 828 && x <= 846 && y >= 764 && y <= 768) || (x >= 842 && x <= 846 && y >= 748 && y <= 768)
            || (x >= 743 && x <= 751 && y >= 815);
    }

    // 기존 서버의 장애물 패턴(Collision.cpp의 checkCollision과 동일한 식).
    // 마을 밖 월드는 이 패턴을 그대로 유지한다 — 기존 A*/몬스터 이동/저장 좌표와의 호환.
    [[nodiscard]] constexpr bool LegacyPatternBlocked(int x, int y) noexcept
    {
        if ((x / 3 + y / 3) % 3 == 0) return false;
        if ((x / 3 + y / 3) % 3 == 1) return false;
        return (x / 2 + y / 2) % 3 == 0;
    }

    // 서버/클라 공용 충돌 판정. (x, y) = (열, 행) — 월드 밖은 막힘.
    [[nodiscard]] constexpr bool IsBlocked(int x, int y) noexcept
    {
        if (x < 0 || y < 0 || x >= kWorldWidth || y >= kWorldHeight)
            return true;
        if (IsInTown(x, y))
            return IsTownCellBlocked(TownCell(x, y));
        return !IsRoad(x, y) && LegacyPatternBlocked(x, y);
    }

    // 예전 static_assert(Detail::ValidateYard(), "허수아비 자리는 훈련장 안의 걸을 수 있는 칸이어야 합니다.")
    // → 부팅 검증 GameDataLoad::ValidateTown R064(Data/Load/Bind_Town.cpp — 행 위치를 남긴다).

    // ── 마을 건물(클라이언트가 입체로 그리기 위한 사각형. 충돌은 칸 문자 'B'가 담당) ──
    using BuildingKind = TownMap::BuildingKind;
    using BuildingRow = TownMap::BuildingRow;
    // 예전 kBuildings(TownBuilding)
    [[nodiscard]] inline std::span<const BuildingRow> Buildings() noexcept { return TownMap::Buildings(); }

    // ── 마을 부활/귀환 지점 — 여러 곳 중 무작위로 고른다(anchors.txt의 spawn → TownSpawn).
    using Point16 = TownMap::Point16;
    // 예전 kTownSpawnPoints · kTownSpawnPointCount(= 스팬 크기)
    [[nodiscard]] inline std::span<const Point16> TownSpawnPoints() noexcept { return TownMap::Spawns(); }
}
