#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// 마을 — 파사드. 값은 GameData/에서 온다(서버 부팅 때 GameData::LoadFromDir → 불변 스냅샷, Data/Load/Bind_Town.cpp).
//   mapgen이 주인(HARVEN-DESIGN tools/mapgen/gen_town.py → GameData/Town*.json — 직접 고치지 않는다):
//     TownInfo(Zone · 원점 · 크기 · 훈련장) · TownGrid(칸 · 겉면 · 층) · TownBuilding · TownSpawn · TownDummy ·
//     TownSpot(이름 있는 자리) · TownNpcHome · TownProp
//   시트 HARVEN_DATA가 주인: Town(이름) · District(구획) · Ambient(소리 구역)
// 예전에는 이 파일이 생성물(constexpr 표)이었다 — 이제는 손으로 쓰는 파사드다. 서버(충돌 · NPC · 부활)와 클라(지형 · 건물 · 미니맵)가 같이 쓴다.
// 규칙(예전 static_assert → 부팅 검증 R064 · R072~R075): 격자 kH줄 × kW글자 · 건물 발자국은 모두 'B' · 부활 자리는 걸을 수 있는 마을 칸 ·
// 허수아비는 훈련장 안 걸을 수 있는 칸. 생성기도 같은 규칙을 검사한다.
// ─────────────────────────────────────────────────────────────────────────────
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "Database/GameData/TownTypes.h"

namespace TownMap
{
    // ── 구조 상수(코드에 남긴다) — 마을의 Zone · 월드 자리 · 크기. 안전지대 판정(WorldData::IsInTown · IsSafeZone)이
    //    컴파일 때 쓰고(핫 경로 · constexpr 함수), 격자 저장 크기 · 클라 지형 배열 크기다.
    //    GameData/TownInfo.json(mapgen town.txt)이 이 값과 다르면 로드 실패 — 바꾸려면 여기와 town.txt를 함께 바꾼다.
    inline constexpr int kZoneId = 5;
    inline constexpr int kX0 = 708, kY0 = 714, kW = 84, kH = 72;

    // 마을 이름(Town.name — 시트)
    [[nodiscard]] const char* Name() noexcept;

    // 격자(행 = y, kH줄 × kW글자). ground = 바닥 · 막힘 문자(가장자리 R을 채움), surface = R로 바꾸기 전 재질, level = 층 '0'~'4'.
    // 칸 하나는 WorldData::TownCell · TownLevel(O(1))로 읽는다 — 이 셋은 줄 전체가 필요한 곳(클라 지형 · 덤프)용.
    [[nodiscard]] std::span<const char* const> GroundRows() noexcept;
    [[nodiscard]] std::span<const char* const> SurfaceRows() noexcept;
    [[nodiscard]] std::span<const char* const> LevelRows() noexcept;

    [[nodiscard]] std::span<const BuildingRow> Buildings() noexcept;   // buildings.txt 순서
    [[nodiscard]] std::span<const Point16> Spawns() noexcept;          // 부활 · 귀환 · 첫 접속 자리
    [[nodiscard]] std::span<const Point16> Dummies() noexcept;         // 훈련장 허수아비 자리
    [[nodiscard]] Rect16 Yard() noexcept;                              // 훈련장 사각형(양끝 포함)

    // 이름 있는 자리(anchors.txt의 이름 — 예전 이름 상수 kSouthGateOut = FindSpot("south_gate_out")). 행 = key 오름차순.
    [[nodiscard]] std::span<const SpotRow> Spots() noexcept;
    [[nodiscard]] const PointF* FindSpot(std::string_view key) noexcept;   // 없으면 nullptr

    // 마을 NPC의 집(배회 중심) — 행 = npc_id 오름차순. 이름 · 말은 NPC 표, 자리는 여기
    [[nodiscard]] std::span<const NpcHome> NpcHomes() noexcept;
    [[nodiscard]] const NpcHome* FindNpcHome(uint16_t npcId) noexcept;     // 없으면 nullptr

    [[nodiscard]] std::span<const DistrictRow> Districts() noexcept;   // 구획(시트 District — 투어 순서)
    [[nodiscard]] std::span<const AmbientRow> Ambient() noexcept;      // 소리 구역(시트 Ambient)
    // 소품 — 막히는 소품은 격자 ground의 'P' 칸(서버 충돌), 줄 소품(빨랫줄 · 줄깃발)은 두 끝 사이(막지 않는다)
    [[nodiscard]] std::span<const PropRow> Props() noexcept;
}
