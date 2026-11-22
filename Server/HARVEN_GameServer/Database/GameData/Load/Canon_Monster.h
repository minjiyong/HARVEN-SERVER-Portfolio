#pragma once
// 정본 표 쓰기 — 몬스터 묶음(탭 Monster). 서버 --dump-data · datacheck --dump가 쓴다(datatool export와 같은 바이트).
// 이관 전 · 뒤에 모두 있는 파사드(MonsterTableRow · MonsterTable::BossEpithet)만 쓴다.
#include <functional>
#include <span>

#include "Database/GameData/Load/Canon.h"
#include "Database/GameData/MonsterTable.h"

namespace Canon
{
    // codeName(id): 코드가 이름으로 부르는 id면 이름(MonsterTable::Id::Moroz → "Moroz"), 아니면 nullptr
    inline void WriteMonster(Book& book, std::span<const MonsterTableRow> rows, const std::function<const char*(uint32_t)>& codeName)
    {
        Table& t = book.Get("Monster");
        for (const MonsterTableRow& r : rows)
        {
            Row w;
            w.Put("id", r.id)
             .StrOpt("code_name", codeName(r.id))
             .Put("name", r.name);
            const char* epithet = MonsterTable::BossEpithet(r.id);
            w.StrOpt("epithet", (epithet != nullptr && *epithet != '\0') ? epithet : nullptr)
             .Put("aggressive", r.aggressive)
             .Put("level", r.level)
             .Put("max_hp", r.maxHp)
             .Put("attack", r.attack)
             .Put("exp", r.exp)
             .Put("adena_min", r.adenaMin)
             .Put("adena_max", r.adenaMax)
             .Put("adena_chance", r.adenaChance)
             .Put("drop_table", r.dropTable)
             .Put("drop_rolls", r.dropRolls)
             .Put("attack_interval_ms", r.attackIntervalMs)
             .Put("move_interval_ms", r.moveIntervalMs)
             .Put("leash", r.leash)
             .Put("boss", r.boss)
             .Put("archetype", r.archetype)
             .Put("variant", r.variant)
             .Color("color1", r.color1)
             .Color("color2", r.color2)
             .Put("scale", r.scale)
             .Put("dummy", r.dummy);
            t.Add(w);
        }
    }
}
