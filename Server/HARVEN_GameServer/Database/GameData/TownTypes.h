#pragma once

#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// 마을 데이터 형(손으로 쓰는 쪽). 값은 GameData/에서 온다 — 격자 · 배치는 생성기(HARVEN-DESIGN/tools/mapgen/gen_town.py)가 쓰는
// GameData/Town*.json, 이름 · 구획 · 소리 구역은 시트 HARVEN_DATA(Town · District · Ambient 탭). 파사드는 TownMap.h.
// 서버와 클라가 같이 include한다 — 열거형은 끝에만 추가한다.
// ─────────────────────────────────────────────────────────────────────────────
namespace TownMap
{
    struct Point16 { int16_t x, y; };
    struct PointF { float x, y; };
    struct NpcHome { uint16_t id; int16_t x, y; };   // 마을 NPC 집(월드 칸) — anchors.txt의 npc(TownNpcHome)
    struct Rect16 { int16_t x0, y0, x1, y1; };       // 월드 칸 사각형, 양끝 포함(훈련장 — TownInfo.yard_*)
    struct SpotRow { const char* key; PointF p; };   // 이름 있는 자리(TownSpot — anchors.txt의 이름, 예: "south_gate_out")

    // 건물 종류 — 클라이언트 그리기(props::DrawBuilding)가 실루엣 · 재질을 고른다(서버는 쓰지 않는다)
    enum class BuildingKind : uint8_t
    {
        eStore, eWeaponShop, eTemple, eInn, eHouse,   // Phase 4 전부터
        // Phase 4-B · 4-C: 방어구점 · 선술집 · 샛별 탑 · 경매장 · 결사 회관 · 창고 · 마구간 · 물레방아간 · 풍차 · 경비 초소 ·
        // 필경소 · 약초 온실 · 공방 · 전망대
        eArmorer, eTavern, eStarTower, eAuctionHall, eGuildHall, eWarehouse, eStable, eWaterMill, eWindMill, eGuardPost,
        eScriptorium, eGreenhouse, eWorkshop, eLookout,
    };

    struct BuildingRow
    {
        BuildingKind kind;
        int16_t x0, y0, x1, y1;   // 월드 칸, 양끝 포함 — 발자국 칸은 모두 막힘('B')
        uint8_t level;            // 발자국의 층(0~4)
        char    door;             // 문이 난 면 'S' 'E' 'N' 'W'
        uint8_t variant;          // 같은 종류의 모양 변형
        const char* sign;         // 간판 글(없으면 "")
    };

    struct DistrictRow
    {
        const char* name;
        const char* sub;          // 한 줄 설명(투어 자막)
        float x, y;               // 투어 · 지도 이름 자리(월드 칸)
        float zoom;               // 투어 줌
    };

    // 소리 구역(클라이언트 — audio/Ambient)
    enum class AmbientKind : uint8_t { eFountain, eMarket, eSmithy, eBell, eRiver, eWind };
    struct AmbientRow
    {
        AmbientKind kind;
        float x, y;               // 월드 칸
        float radius;             // 이 거리 안에서 들린다(칸)
    };

    // 소품(props.txt — Phase 4-C). 막히는 소품은 격자 ground의 'P' 칸(서버 충돌), 줄 소품은 두 끝 사이에 매단다(클라 그림만)
    enum class PropKind : uint8_t
    {
        eGrave, eBoard, eBench, eStatue, eFlagpole, eWell, eCart, eHay, eBarrels, eCrates, eSignpost, eTarget, eRack,
        eLaundry, eBunting, eLogs,
    };
    struct PropRow
    {
        PropKind kind;
        int16_t x, y;             // 월드 칸
        int16_t x2, y2;           // 줄 소품의 다른 끝(나머지는 x, y와 같다)
    };
}
