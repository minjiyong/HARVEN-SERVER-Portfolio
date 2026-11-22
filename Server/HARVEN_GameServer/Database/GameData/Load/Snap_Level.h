#pragma once
// 스냅샷 — 레벨 묶음(탭 Level · Class). Bind_Level.cpp가 채우고, 파사드 LevelTable::* · ClassTable::Get이 읽는다.
#include <cstdint>
#include <vector>

#include "Database/GameData/Combat/ClassTable.h"

struct SnapLevel
{
    // 탭 Level 한 행(예전 LevelTable 공식이 내던 값)
    struct Row
    {
        uint8_t  level = 0;
        uint32_t expToNext = 0;
        uint16_t maxHp = 0;
        uint16_t baseAttack = 0;
        uint16_t regen = 0;
        uint16_t regenSafe = 0;
        uint32_t deathPenalty = 0;
    };

    std::vector<Row> levels;         // 행 i = 레벨 i + 1(정확히 LevelTable::kMaxLevel 행 — 구조 상수)
    std::vector<ClassRow> classes;   // 행 i = ClassId i(R002: 행 수 = ClassId::Count)
};
