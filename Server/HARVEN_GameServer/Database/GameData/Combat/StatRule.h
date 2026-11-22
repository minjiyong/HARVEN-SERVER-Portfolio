#pragma once
// 능력치 → 전투 능력치 규칙(식 — 코드). 직업 표를 읽는다.
// CombatData.h에서 나눴다(내용 이동만 — 이관 Phase 1). 우산 헤더 Data/CombatData.h가 원래 순서로 include한다.
#include <cstddef>
#include <cstdint>
#include "Database/GameData/Combat/ClassTable.h"

// ── 능력치 → 전투 능력치 ──────────────────────────────────────────────────────
// 각 능력치가 플레이 방식에 닿도록:
//   STR 물리 공격·방어 / DEX 공격속도·명중·회피·치명(작게)·물리 공격(사냥꾼)
//   INT 마법 공격·마법 관통·MP / VIT 최대 HP·방어·상태이상 저항
//   WIS 최대 MP·MP 회복·마법 방어·쿨다운 감소 / LUK 치명타 확률·치명타 피해·회피(작게)
struct GearBonus
{
    int16_t  stats[6] = {};
    uint16_t weaponAttack = 0, weaponMagic = 0;
    uint16_t armor = 0, magicArmor = 0;
    int16_t  maxHp = 0, maxMp = 0;
    int16_t  attackSpeed = 0, moveSpeed = 0;
    int16_t  critChance = 0, critDamage = 0;   // ‰, %
    int16_t  penetration = 0, cooldownReduction = 0, statusResist = 0;
    int16_t  hpRegen = 0, mpRegen = 0;
    int16_t  attackPercent = 0;                // 공격력 %
};

namespace StatRule
{
    [[nodiscard]] constexpr uint16_t Clamp16(int v) noexcept { return static_cast<uint16_t>(v < 0 ? 0 : (v > 65535 ? 65535 : v)); }

    // 직업 값은 GameData(Class 탭) — ClassTable::Get이 런타임이 되어 constexpr를 뗐다(계산식은 그대로)
    [[nodiscard]] inline DerivedStats Compute(ClassId cls, uint8_t level, const BaseStats& base, const GearBonus& gear) noexcept
    {
        const ClassRow& c = ClassTable::Get(cls);
        int s[6];
        for (int i = 0; i < 6; ++i) s[i] = base.v[i] + gear.stats[i];
        const int STR = s[0], DEX = s[1], INT = s[2], VIT = s[3], WIS = s[4], LUK = s[5];
        const int l = level > 0 ? level - 1 : 0;

        DerivedStats d{};
        d.maxHp = Clamp16(c.baseHp + c.hpPerLevel * l + VIT * 6 + gear.maxHp);
        d.maxMp = Clamp16(c.baseMp + c.mpPerLevel * l + WIS * 5 + INT * 2 + gear.maxMp);
        const int atk = gear.weaponAttack + (STR * c.atkStr10) / 10 + (DEX * c.atkDex10) / 10;
        d.attack = Clamp16(atk * (100 + gear.attackPercent) / 100);
        d.magicAttack = Clamp16((gear.weaponMagic + (INT * 15) / 10 + (WIS * 3) / 10) * (100 + gear.attackPercent) / 100);
        d.defense = Clamp16(gear.armor + VIT / 2 + STR / 5);
        d.magicDefense = Clamp16(gear.magicArmor + (WIS * 8) / 10 + INT / 5);
        d.attackSpeed = Clamp16(100 + (DEX * 6) / 10 + gear.attackSpeed);
        d.moveSpeed = Clamp16(100 + DEX / 8 + gear.moveSpeed);
        d.critChance = Clamp16(50 + LUK * 4 + DEX + gear.critChance);
        d.critDamage = Clamp16(150 + LUK + gear.critDamage);
        d.accuracy = Clamp16(80 + DEX * 2 + level * 2);
        d.evasion = Clamp16(DEX + (LUK * 3) / 10);
        d.penetration = Clamp16(gear.penetration + INT / 5);
        d.cooldownReduction = Clamp16((WIS * 3) / 10 + gear.cooldownReduction > 30 ? 30 : (WIS * 3) / 10 + gear.cooldownReduction);
        d.statusResist = Clamp16((VIT * 3 + WIS * 3) / 10 + gear.statusResist > 50 ? 50 : (VIT * 3 + WIS * 3) / 10 + gear.statusResist);
        d.hpRegen = Clamp16(4 + level / 2 + VIT / 4 + gear.hpRegen);
        d.mpRegen = Clamp16(3 + WIS / 2 + level / 4 + gear.mpRegen);
        return d;
    }

    // 받는 피해 배율(방어/마방) — 100/(100+유효 방어)
    [[nodiscard]] constexpr uint32_t Mitigate(uint32_t damage, int defense, int penetration) noexcept
    {
        int eff = defense - penetration;
        if (eff < 0) eff = 0;
        const uint32_t out = damage * 100u / static_cast<uint32_t>(100 + eff);
        return out > 0 ? out : 1;
    }

    // 회피 확률(%) — 몬스터 레벨이 높을수록 줄어든다. 최대 40%.
    [[nodiscard]] constexpr uint32_t EvadeChance(int evasion, int attackerLevel) noexcept
    {
        const int denom = evasion + 100 + attackerLevel * 10;
        const int p = denom > 0 ? evasion * 100 / denom : 0;
        return static_cast<uint32_t>(p > 40 ? 40 : (p < 0 ? 0 : p));
    }

    // 몬스터 방어(레벨 기반)
    [[nodiscard]] constexpr int MonsterDefense(int monsterLevel) noexcept { return monsterLevel * 3 / 2; }
}
