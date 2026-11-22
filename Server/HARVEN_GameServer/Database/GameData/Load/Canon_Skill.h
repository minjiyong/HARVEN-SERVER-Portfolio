#pragma once
// 정본 표 쓰기 — 스킬 묶음(탭 Skill · SkillAction · SkillHit · Status · Combo · SkillKeyMap).
// SkillDef의 hits[8]은 자식 탭 SkillHit(skill_id, seq 1~)로, hitCount는 SkillHit 행 수로 나타난다.
// SkillHit의 turn · tag · src는 갈래(ApplyDef)가 복사본에만 쓰는 값이라 표에는 늘 0 — 0이 아니면 멈춘다.
#include <algorithm>
#include <span>
#include <string>
#include <vector>

#include "Database/GameData/Load/Canon.h"
#include "Database/GameData/CombatData.h"
#include "Database/GameData/SkillData.h"

namespace Canon
{
    inline void WriteSkill(Book& book, std::span<const SkillRow> rows)
    {
        Table& t = book.Get("Skill");
        for (const SkillRow& r : rows)
        {
            Row w;
            w.Put("id", r.id).Put("cls", r.cls).Put("slot", r.slot)
             .Put("name", r.name).Put("desc", r.desc).Put("combo", r.combo)
             .Put("unlock_level", r.unlockLevel).Put("mp", r.mp)
             .Put("cooldown_ms", r.cooldownMs).Put("cast_ms", r.castMs).Put("range", r.range)
             .Put("target", r.target).Put("shape", r.shape).Put("radius", r.radius)
             .Put("kind", r.kind).Put("coeff", r.coeff).Put("hits", r.hits)
             .Put("status", r.status).Put("status_ms", r.statusMs).Put("status_stacks", r.statusStacks)
             .Put("combo_status", r.comboStatus).Put("combo_bonus", r.comboBonus).Put("combo_id", r.comboId)
             .Put("consume_combo", r.consumeCombo).Put("knockback", r.knockback).Put("dash", r.dash)
             .Put("projectile", r.projectile).Color("color", r.color);
            t.Add(w);
        }
    }

    // 정본 행 순서 = id 오름차순(이관 전 배열은 직업별로 묶여 id 순서가 아니었다 — 순서에 기대는 코드는 없다: id로만 찾는다).
    inline void WriteSkillAction(Book& book, std::span<const SkillDef> rowsIn)
    {
        std::vector<const SkillDef*> rows;
        for (const SkillDef& d : rowsIn) rows.push_back(&d);
        std::stable_sort(rows.begin(), rows.end(), [](const SkillDef* a, const SkillDef* b) { return a->id < b->id; });
        Table& ta = book.Get("SkillAction");
        Table& th = book.Get("SkillHit");
        for (const SkillDef* pd : rows)
        {
            const SkillDef& d = *pd;
            Row w;
            w.Put("id", d.id).Put("key", d.key).Put("kind", d.kind).Put("tier", d.tier).Put("special", d.special)
             .Put("cooldown_ms", d.cooldownMs).Put("cast_ms", d.castMs).Put("lock_ms", d.lockMs)
             .Put("super_armor_ms", d.superArmorMs).Put("invuln_ms", d.invulnMs)
             .Put("stages", d.stages).Put("stage_ms", d.stageMs).Put("perfect_ms", d.perfectMs)
             .Put("hold_max_ms", d.holdMaxMs).Put("tick_ms", d.tickMs).Put("tick_mp", d.tickMp)
             .Put("move_while_casting", d.moveWhileCasting).Put("range", d.range).Put("move_ms", d.moveMs)
             .Put("kind_text", d.kindText);
            ta.Add(w);

            if (d.hitCount > 8)
                throw Error("SkillAction " + std::to_string(d.id) + ": hitCount > 8");
            for (uint8_t i = 0; i < d.hitCount; ++i)
            {
                const SkillHit& h = d.hits[i];
                if (h.turn != 0 || h.tag != 0 || h.src != 0)
                    throw Error("SkillHit " + std::to_string(d.id) + "/" + std::to_string(i + 1) + ": turn · tag · src는 표에서 0이어야 한다");
                Row hw;
                hw.Put("skill_id", d.id).Put("seq", static_cast<uint8_t>(i + 1))
                  .Put("at_ms", h.atMs).Put("coeff", h.coeff).Put("shape", h.shape).Put("anchor", h.anchor)
                  .Put("a", h.a).Put("b", h.b).Put("along", h.along).Put("react", h.react)
                  .Put("stagger", h.stagger).Flags<HitFlag>("flags", h.flags)
                  .Put("stage", h.stage).Put("max_targets", h.maxTargets);
                th.Add(hw);
            }
            // hits[hitCount..7]은 기본값이어야 한다(표에 없는 값이 숨어 있으면 안 된다)
            for (int i = d.hitCount; i < 8; ++i)
            {
                const SkillHit& h = d.hits[i];
                const SkillHit z{};
                if (h.atMs != z.atMs || h.coeff != z.coeff || h.shape != z.shape || h.anchor != z.anchor || h.a != z.a || h.b != z.b
                    || h.along != z.along || h.react != z.react || h.stagger != z.stagger || h.flags != z.flags || h.stage != z.stage
                    || h.maxTargets != z.maxTargets || h.turn != z.turn || h.tag != z.tag || h.src != z.src)
                    throw Error("SkillAction " + std::to_string(d.id) + ": hitCount 뒤 칸에 값이 있다");
            }
        }
    }

    inline void WriteStatus(Book& book)
    {
        Table& t = book.Get("Status");
        for (int i = 0; i < static_cast<int>(StatusId::Count); ++i)
        {
            const StatusRow& r = StatusTable::Get(static_cast<StatusId>(i));
            Row w;
            w.Put("id", r.id).Put("name", r.name).Put("debuff", r.debuff)
             .Put("blocks_move", r.blocksMove).Put("blocks_attack", r.blocksAttack)
             .Put("max_stacks", r.maxStacks).Put("tick_ms", r.tickMs).Put("tick_percent", r.tickPercent)
             .Put("slow_percent", r.slowPercent).Color("color", r.color);
            t.Add(w);
        }
    }

    inline void WriteCombo(Book& book, uint8_t count)
    {
        Table& t = book.Get("Combo");
        for (uint8_t i = 0; i < count; ++i)
        {
            Row w;
            w.Put("id", i).Put("name", ComboTable::Name(i));
            t.Add(w);
        }
    }

    // 직업 × 칸 → 기본 기술(SkillDefTable::Default의 표)
    inline void WriteSkillKeyMap(Book& book)
    {
        Table& t = book.Get("SkillKeyMap");
        for (int c = 0; c < static_cast<int>(ClassId::Count); ++c)
            for (int k = 0; k < static_cast<int>(SkillKey::Count); ++k)
            {
                Row w;
                w.Put("cls", static_cast<ClassId>(c)).Put("key", static_cast<SkillKey>(k))
                 .Put("skill_id", SkillDefTable::Default(static_cast<ClassId>(c), static_cast<SkillKey>(k)));
                t.Add(w);
            }
    }
}
