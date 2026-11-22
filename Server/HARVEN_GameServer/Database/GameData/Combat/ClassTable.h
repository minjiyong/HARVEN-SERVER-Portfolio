#pragma once
// 직업 표(레벨 묶음 — 탭 Class).
// CombatData.h에서 나눴다(내용 이동만 — 이관 Phase 1). 우산 헤더 Data/CombatData.h가 원래 순서로 include한다.
// 값은 GameData/Class.json(시트 HARVEN_DATA의 Class 탭) — 서버 부팅 때 GameData::LoadFromDir가 스냅샷에 올리고
// 아래 파사드가 읽는다(구현 Data/Load/Bind_Level.cpp). 규칙(예전 static_assert → 부팅 검증 R002): 행 수 = ClassId::Count, 행 순서 = id.
#include <cstddef>
#include <cstdint>
#include "Database/GameData/Combat/CombatIds.h"
#include "Database/GameData/Combat/Stats.h"

struct ClassRow
{
    ClassId     id;
    const char* name;
    const char* role;
    const char* desc;
    int16_t     base[6];        // 1레벨 STR DEX INT VIT WIS LUK
    uint8_t     growth10[6];    // 레벨당 성장 ×10 (15 = 1.5)
    uint16_t    baseHp, hpPerLevel;
    uint16_t    baseMp, mpPerLevel;
    uint8_t     atkStr10, atkDex10;   // 공격력 = 무기 + STR×atkStr/10 + DEX×atkDex/10
    uint16_t    basicIntervalMs;      // 기본 공격 간격(공격속도 100%)
    uint8_t     basicRange;           // 기본 공격 사거리(체비셰프)
    uint16_t    skills[6];            // [0] = 기본 공격
    uint16_t    starterWeapon;        // 초보자 무기 ItemTableId
    uint32_t    color;
};

namespace ClassTable
{
    // 직업 → 행. 범위 밖 id는 0번 행(이관 전과 같다).
    [[nodiscard]] const ClassRow& Get(ClassId id) noexcept;
    [[nodiscard]] constexpr bool IsValid(uint8_t id) noexcept { return id < static_cast<uint8_t>(ClassId::Count); }

    // 레벨에 따른 기본 능력치(자유 포인트 제외)
    [[nodiscard]] inline BaseStats BaseAt(ClassId id, uint8_t level) noexcept
    {
        const ClassRow& c = Get(id);
        BaseStats s{};
        const int l = level > 0 ? level - 1 : 0;
        for (int i = 0; i < 6; ++i)
            s.v[i] = static_cast<int16_t>(c.base[i] + (c.growth10[i] * l) / 10);
        return s;
    }
}
