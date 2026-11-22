#pragma once
// 능력치 형(StatKind · BaseStats · DerivedStats) — 여러 묶음이 함께 쓴다.
// CombatData.h에서 나눴다(내용 이동만 — 이관 Phase 1). 우산 헤더 Data/CombatData.h가 원래 순서로 include한다.
#include <cstddef>
#include <cstdint>

// ── 능력치 ────────────────────────────────────────────────────────────────────
enum class StatKind : uint8_t { Str, Dex, Int, Vit, Wis, Luk, Count };

struct BaseStats
{
    int16_t v[6] = {};   // STR DEX INT VIT WIS LUK
    [[nodiscard]] constexpr int16_t Get(StatKind k) const noexcept { return v[static_cast<int>(k)]; }
};

// 전투 능력치(서버가 계산해서 USER_STATS_INF로 보낸다)
struct DerivedStats
{
    uint16_t maxHp = 100, maxMp = 50;
    uint16_t attack = 10, magicAttack = 10;
    uint16_t defense = 0, magicDefense = 0;
    uint16_t attackSpeed = 100;     // % (높을수록 빠름)
    uint16_t moveSpeed = 100;       // %
    uint16_t critChance = 50;       // ‰ (50 = 5%)
    uint16_t critDamage = 150;      // %
    uint16_t accuracy = 100;
    uint16_t evasion = 0;
    uint16_t penetration = 0;
    uint16_t cooldownReduction = 0; // %
    uint16_t statusResist = 0;      // %
    uint16_t hpRegen = 5, mpRegen = 3;  // 5초마다
};
