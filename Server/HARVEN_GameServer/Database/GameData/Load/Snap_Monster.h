#pragma once
// 스냅샷 — 몬스터 묶음(탭 Monster). Bind_Monster.cpp가 채우고, 파사드 MonsterTable::*가 읽는다.
#include <vector>

#include "Database/GameData/MonsterTypes.h"

struct SnapMonster
{
    std::vector<MonsterTableRow> rows;   // 행 i = id i + 1 (R031: 1부터 연속)
    std::vector<const char*> epithet;    // 행과 같은 순서 — 보스 별칭(없으면 nullptr)
    std::vector<const char*> codeName;   // 행과 같은 순서 — MonsterTable::Id 이름(없으면 nullptr)
};
