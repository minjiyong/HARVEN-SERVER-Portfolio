#pragma once
// 스냅샷 — 개인 업적 묶음(탭 Achievement).
// Bind_Achieve.cpp가 채우고, 파사드 AchieveRule::Get · IsHuntMaterial이 읽는다. 업적은 id로 바로(Catalog::at — 해시맵 없음).
#include <cstdint>
#include <vector>

#include "Database/GameData/AchieveRules.h"
#include "Database/GameData/Load/Canon_Achieve.h"

struct SnapAchieve
{
    AchieveRule::Catalog cat;
    std::vector<uint32_t> src;             // cat.rows와 같은 순서 — Achievement 탭 JSON 행 번호(검증 위치)
    std::vector<uint16_t> huntMaterials;   // 사냥 정수 · 결정 아이템(정렬) — TierLoot에서
};
