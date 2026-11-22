#pragma once
// 월드 크기 · 지역(Zone) 표(지형 묶음 — 탭 Region · RegionSpawn).
// WorldData.h에서 나눴다(내용 이동만 — 이관 Phase 1). 우산 헤더 Data/WorldData.h가 원래 순서로 include한다.
#include <cstddef>
#include <cstdint>
#include <span>

namespace WorldData
{
    // Protocol.h의 W_WIDTH/W_HEIGHT, ZoneLayout의 Zone 크기(50섹터 x 10칸)와 같아야 한다.
    // 서버 쪽 Collision.cpp에서 static_assert로 일치를 확인한다.
    inline constexpr int kWorldWidth  = 2000;
    inline constexpr int kWorldHeight = 2000;
    inline constexpr int kRegionSize  = 500;
    inline constexpr int kRegionCountX = kWorldWidth / kRegionSize;   // 4
    inline constexpr int kRegionCount  = kRegionCountX * (kWorldHeight / kRegionSize);  // 16

    enum class Biome : uint8_t
    {
        eMeadow, eForest, eSnow, eCanyon, eVolcanic, eFairy, eRuins, eOrcFort,
        eSwamp, eDesert, eWasteland, eGraveyard, eBeach, eAntHill, eHell,
    };

    inline constexpr int kRegionSpawnSlots = 4;   // 지역마다 스폰 칸 수(구조 상수 — RegionSpawn.seq 1~4)

    struct RegionSpawn
    {
        uint16_t species = 0;  // MonsterTable id
        uint8_t  weight  = 0;
    };

    struct RegionRow
    {
        uint16_t    zoneId       = 0;
        const char* name         = "";
        Biome       biome        = Biome::eMeadow;
        uint8_t     tier         = 1;      // 1~5 (★ 개수)
        RegionSpawn spawns[kRegionSpawnSlots] = {};   // 빈 칸(species 0)은 뒤쪽에만
        uint16_t    bossSpecies  = 0;      // 0 = 필드 보스 없음
        int16_t     bossX        = 0;
        int16_t     bossY        = 0;
        int16_t     landX        = 0;      // 길잡이 도착 지점
        int16_t     landY        = 0;
        uint32_t    teleportFee  = 0;      // 0 = 텔레포트 목적지가 아님(마을 지역)
    };

    // zoneId = zoneY * 4 + zoneX (ZoneLayout::GetZoneId와 동일한 배치)
    //   [ 0 벨고라 설원 ][ 1 올벤 숲     ][ 2 크라스나 협곡][ 3 바트라 화산 ]
    //   [ 4 이레린 숲   ][ 5 데니차 들녘 ][ 6 캄파나 폐허 ][ 7 그락 요새   ]
    //   [ 8 모크라 늪지 ][ 9 야스나 초원 ][10 아스린 사막 ][11 가르 황무지 ]
    //   [12 서약의 언덕 ][13 베레그 해안 ][14 굴개미 둔덕 ][15 칼둔 균열   ]
    //   이름의 유래와 언어 규칙은 HARVEN-DESIGN/WORLD.md — 새 지역도 그 규칙으로 짓는다.
    //
    // 지역 표 — 값은 GameData/Region.json · RegionSpawn.json(시트 HARVEN_DATA의 Region · RegionSpawn 탭)에서 온다.
    // 서버 부팅 때 GameData::LoadFromDir가 읽어 불변 스냅샷에 둔다(Data/Load/Bind_Region.cpp).
    // 행 수 kRegionCount(16)와 spawns[4]는 Zone 배치 · 구조 상수 — 시트가 넘으면 로드 실패(자르지 않는다).
    // 규칙(예전 static_assert(Detail::ValidateRegions()) → 부팅 검증 R076~R079, 오류에 시트 행 위치):
    //   zone_id = 행 순서 · 행 수 16 / 길잡이 도착 지점은 그 지역 안의 걸을 수 있는 칸 /
    //   필드 보스가 있으면 보스 자리도 그 지역 안의 걸을 수 있는 칸 / 지역마다 스폰 가중치 합 > 0.
    //   스폰 · 보스 종족은 Monster 탭에 있어야 한다.

    [[nodiscard]] constexpr int RegionIdAt(int x, int y) noexcept
    {
        if (x < 0 || y < 0 || x >= kWorldWidth || y >= kWorldHeight)
            return -1;
        return (y / kRegionSize) * kRegionCountX + (x / kRegionSize);
    }

    // zoneId → 지역 행(0 ≤ zoneId < kRegionCount가 아니면 nullptr). 행 순서 = zone_id라 바로 찾는다(O(1)).
    [[nodiscard]] const RegionRow* FindRegion(int zoneId) noexcept;

    // 지역 표 전체(행 순서 = zone_id, kRegionCount행) — 예전 kRegions 배열 대신(클라 지도 · 텔레포트 창이 돈다)
    [[nodiscard]] std::span<const RegionRow> Regions() noexcept;
}
