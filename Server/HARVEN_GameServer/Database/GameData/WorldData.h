#pragma once

#include <cstdint>
#include <cstddef>

#include "Database/GameData/TownMap.h"   // 마을 칸 · 층 · 건물 · 이름 있는 자리(파사드 — 값은 GameData/Town*.json, 주인은 HARVEN-DESIGN tools/mapgen)

// ─────────────────────────────────────────────────────────────────────────────
// 월드 데이터 — 지역(=Zone) 테마/난이도, 마을 레이아웃, NPC, 상점, 텔레포트, 충돌.
//
// 서버와 클라이언트가 같은 파일을 읽는다. 특히 충돌 판정(IsBlocked)을 한 곳에서
// 정의해서 "클라에선 길인데 서버에선 벽" 같은 불일치가 구조적으로 생기지 않게 했다.
// Windows/서버 전용 헤더에 의존하지 않는다(<cstdint>만 사용).
//
// 설계 메모: 서버의 Zone(스레드 분할 단위, 500x500칸)을 그대로 "지역"으로 삼았다.
// 존 경계에서 시야가 끊기는 기존 구조(Sector 격자가 Zone마다 따로 있음)가 게임적으로는
// "다른 지역으로 넘어간다"는 자연스러운 연출이 된다 — 새 경계를 만들 필요가 없다.
// ─────────────────────────────────────────────────────────────────────────────

// 묶음별 헤더로 나눴다(내용 이동만 — 이관 Phase 1). 이 헤더는 예전처럼 전부를 원래 순서로 include한다.
#include "Database/GameData/World/WorldRegion.h"
#include "Database/GameData/World/WorldTown.h"
#include "Database/GameData/World/WorldNpc.h"
#include "Database/GameData/World/WorldShop.h"

namespace WorldData
{
    // 필드 보스 재등장 시간
    inline constexpr uint32_t kBossRespawnSeconds = 300;

    // 표 값은 GameData/(시트 HARVEN_DATA · mapgen)에서 온다. 예전 컴파일 타임 검증(static_assert)은 부팅 검증으로 옮겼다 —
    // 마을 R064 · R072~R075(Bind_Town.cpp) · 지역 R076~R079(Bind_Region.cpp) · NPC R066~R070(Bind_Npc.cpp), Shared/Document/DATA_RULES.md.
}
