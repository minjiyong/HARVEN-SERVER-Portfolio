#pragma once
// 정본 표 쓰기 — 몬스터 AI 묶음(탭 MonsterSkill · MonsterSkillHit · MonsterAoE · MonsterAI · MonsterAISkill).
// 서버 --dump-data · datacheck --dump가 쓴다(datatool export와 같은 바이트). 자식 탭은 부모 행의 hits[] · skills[]를 seq 1부터 펼친다.
#include <span>
#include <string>

#include "Database/GameData/Load/Canon.h"
#include "Database/GameData/MonsterAITypes.h"

namespace Canon
{
    inline void WriteMonsterSkill(Book& book, std::span<const MonsterSkillRow> rows)
    {
        Table& t = book.Get("MonsterSkill");
        Table& th = book.Get("MonsterSkillHit");
        for (const MonsterSkillRow& r : rows)
        {
            Row w;
            w.Put("id", r.id).Put("name", r.name).Put("target", r.target)
             .Put("range_min", r.rangeMin).Put("range_max", r.rangeMax)
             .Put("cast_ms", r.castMs).Put("lock_ms", r.lockMs)
             .Put("cooldown_ms", r.cooldownMs).Put("cooldown_jitter_ms", r.cooldownJitterMs)
             .Put("pattern", r.pattern).Put("fx", r.fx)
             .Put("move", r.move).Put("move_tiles", r.moveTiles).Put("move_ms", r.moveMs)
             .Put("counterable", r.counterable)
             .Put("aoe", r.aoe).Put("aoe_anchor", r.aoeAnchor)
             .Put("self_status", r.selfStatus).Put("self_status_ms", r.selfStatusMs).Put("self_status_power", r.selfStatusPower);
            t.Add(w);

            if (r.hitCount > kMonsterSkillHits)
                throw Error("MonsterSkill " + std::to_string(r.id) + ": hitCount > " + std::to_string(kMonsterSkillHits));
            for (uint8_t i = 0; i < r.hitCount; ++i)
            {
                const MonsterSkillHit& h = r.hits[i];
                Row hw;
                hw.Put("skill_id", r.id).Put("seq", static_cast<uint8_t>(i + 1))
                  .Put("at_ms", h.atMs).Put("warn_ms", h.warnMs).Put("coeff", h.coeff)
                  .Put("shape", h.shape).Put("anchor", h.anchor)
                  .Put("a", h.a).Put("b", h.b).Put("along", h.along)
                  .Put("react", h.react).Put("push", h.push)
                  .Put("status", h.status).Put("status_ms", h.statusMs).Put("status_stacks", h.statusStacks)
                  .Put("max_targets", h.maxTargets);
                th.Add(hw);
            }
        }
    }

    inline void WriteMonsterAoe(Book& book, std::span<const MonsterAoeRow> rows)
    {
        Table& t = book.Get("MonsterAoE");
        for (const MonsterAoeRow& r : rows)
        {
            Row w;
            w.Put("id", r.id).Put("name", r.name)
             .Put("duration_ms", r.durationMs).Put("interval_ms", r.intervalMs).Put("first_ms", r.firstMs)
             .Put("radius", r.radius).Put("affect", r.affect)
             .Put("coeff", r.coeff).Put("heal_percent", r.healPercent)
             .Put("status", r.status).Put("status_ms", r.statusMs).Put("status_power", r.statusPower).Put("status_stacks", r.statusStacks)
             .Put("follow", r.follow).Put("owner_bound", r.ownerBound).Put("fx", r.fx);
            t.Add(w);
        }
    }

    inline void WriteMonsterAi(Book& book, std::span<const MonsterAiRow> rows)
    {
        Table& t = book.Get("MonsterAI");
        Table& ts = book.Get("MonsterAISkill");
        for (const MonsterAiRow& r : rows)
        {
            Row w;
            w.Put("monster_id", r.monsterId).Put("ai", r.ai)
             .Put("aggro_range", r.aggroRange).Put("assist_range", r.assistRange).Put("keep_range", r.keepRange)
             .Put("skill_gap_ms", r.skillGapMs).Put("first_skill_ms", r.firstSkillMs)
             .Put("phase2_hp", r.phase2Hp).Put("phase3_hp", r.phase3Hp)
             .Put("phase2_skill", r.phase2Skill).Put("phase3_skill", r.phase3Skill)
             .Put("aura", r.aura);
            t.Add(w);

            if (r.skillCount > kMonsterAiSkills)
                throw Error("MonsterAI " + std::to_string(r.monsterId) + ": skillCount > " + std::to_string(kMonsterAiSkills));
            for (uint8_t i = 0; i < r.skillCount; ++i)
            {
                const MonsterAiSkill& s = r.skills[i];
                Row sw;
                sw.Put("monster_id", r.monsterId).Put("seq", static_cast<uint8_t>(i + 1))
                  .Put("skill_id", s.skillId).Put("priority", s.priority).Put("weight", s.weight)
                  .Put("phase_min", s.phaseMin).Put("phase_max", s.phaseMax)
                  .Put("hp_below", s.hpBelow).Put("allies_min", s.alliesMin).Put("chance", s.chance);
                ts.Add(sw);
            }
        }
    }
}
