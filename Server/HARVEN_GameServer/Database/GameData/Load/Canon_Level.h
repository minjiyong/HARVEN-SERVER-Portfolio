#pragma once
// 정본 표 쓰기 — 레벨 묶음(탭 Level · Class). 파사드 함수(LevelTable::*, ClassTable::Get)만 쓴다.
// Level은 이관 전 공식(1~50)을 실제로 불러 행으로 푼다 — 이관 뒤 함수는 이 표를 읽는다(같은 값).
#include "Database/GameData/Load/Canon.h"
#include "Database/GameData/CombatData.h"
#include "Database/GameData/LevelTable.h"

namespace Canon
{
    inline void WriteLevel(Book& book)
    {
        Table& t = book.Get("Level");
        for (int lv = 1; lv <= LevelTable::kMaxLevel; ++lv)
        {
            const uint8_t l = static_cast<uint8_t>(lv);
            Row w;
            w.Put("level", l)
             .Put("exp_to_next", LevelTable::ExpToNext(l))
             .Put("max_hp", LevelTable::MaxHp(l))
             .Put("base_attack", LevelTable::BaseAttack(l))
             .Put("regen", LevelTable::RegenPerTick(l, false))
             .Put("regen_safe", LevelTable::RegenPerTick(l, true))
             // 사망 경험치 손실 = 이 값과 지금 경험치 중 작은 쪽(DeathExpPenalty) — 지금 경험치를 충분히 크게 줘서 값만 뽑는다
             .Put("death_penalty", LevelTable::DeathExpPenalty(l, 0xFFFFFFFFu));
            t.Add(w);
        }
    }

    inline void WriteClass(Book& book)
    {
        static constexpr const char* kStat[6] = { "str", "dex", "int", "vit", "wis", "luk" };
        Table& t = book.Get("Class");
        for (int c = 0; c < static_cast<int>(ClassId::Count); ++c)
        {
            const ClassRow& r = ClassTable::Get(static_cast<ClassId>(c));
            Row w;
            w.Put("id", r.id).Put("name", r.name).Put("role", r.role).Put("desc", r.desc);
            for (int i = 0; i < 6; ++i) w.Put((std::string("base_") + kStat[i]).c_str(), r.base[i]);
            for (int i = 0; i < 6; ++i) w.Put((std::string("growth10_") + kStat[i]).c_str(), r.growth10[i]);
            w.Put("base_hp", r.baseHp).Put("hp_per_level", r.hpPerLevel)
             .Put("base_mp", r.baseMp).Put("mp_per_level", r.mpPerLevel)
             .Put("atk_str10", r.atkStr10).Put("atk_dex10", r.atkDex10)
             .Put("basic_interval_ms", r.basicIntervalMs).Put("basic_range", r.basicRange);
            for (int i = 0; i < 6; ++i) w.Put((std::string("skill_") + std::to_string(i)).c_str(), r.skills[i]);
            w.Put("starter_weapon", r.starterWeapon).Color("color", r.color);
            t.Add(w);
        }
    }
}
