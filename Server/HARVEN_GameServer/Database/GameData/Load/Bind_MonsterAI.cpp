#include "pch.h"
// ─────────────────────────────────────────────────────────────────────────────
// 몬스터 AI 묶음 — 탭 MonsterSkill · MonsterSkillHit · MonsterAoE · MonsterAI · MonsterAISkill → 스냅샷 →
// 파사드 MonsterSkillTable · MonsterAoeTable · MonsterAiTable. 모양은 Bind_Monster.cpp(모범 표)와 같다.
//   1) 파사드 — GD()의 스냅샷을 읽는다(id → 행은 인덱스로 O(1))
//   2) Bind    — 열마다 RowReader. 자식 탭(MonsterSkillHit · MonsterAISkill)은 부모 행의 hits[8] · skills[8]로 모은다
//                (8개 = 구조 상수, 넘으면 로드 실패 · seq는 1부터 연속)
//   3) Validate — 새 규칙 R105~R113(DATA_RULES.md) — 위치 + R번호
//   4) Dump    — Canon_MonsterAI.h(datatool export와 같은 바이트)
// ─────────────────────────────────────────────────────────────────────────────
#include <string>

#include "Database/GameData/Load/Bundles.h"
#include "Database/GameData/Load/Canon_MonsterAI.h"
#include "Database/GameData/Load/JsonRead.h"
#include "Database/GameData/Load/Snapshot.h"
#include "Database/GameData/Load/Validate.h"
#include "Database/GameData/MonsterAITable.h"
#include "Database/GameData/MonsterTable.h"

// ── 1) 파사드 ────────────────────────────────────────────────────────────────
namespace
{
    // id가 1부터 연속(R105)이라 행 i = id i + 1. 틀린 판(검증 중)에 불려도 다른 행을 돌려주지 않게 id를 확인한다.
    template <class Row>
    const Row* FindById(const std::vector<Row>& rows, uint16_t id) noexcept
    {
        if (id >= 1 && id <= rows.size() && rows[id - 1].id == id)
            return &rows[id - 1];
        return nullptr;
    }
}

namespace MonsterSkillTable
{
    std::span<const MonsterSkillRow> Rows() noexcept { return GD().monsterAI.skills; }
    const MonsterSkillRow* Find(uint16_t id) noexcept { return FindById(GD().monsterAI.skills, id); }
}

namespace MonsterAoeTable
{
    std::span<const MonsterAoeRow> Rows() noexcept { return GD().monsterAI.aoes; }
    const MonsterAoeRow* Find(uint16_t id) noexcept { return FindById(GD().monsterAI.aoes, id); }
}

namespace MonsterAiTable
{
    std::span<const MonsterAiRow> Rows() noexcept { return GD().monsterAI.ais; }

    uint8_t MaxAssistRange() noexcept { return GD().monsterAI.maxAssistRange; }

    const MonsterAiRow* Find(uint16_t monsterId) noexcept
    {
        const SnapMonsterAI& m = GD().monsterAI;
        if (monsterId >= m.aiIndex.size())
            return nullptr;
        const int32_t i = m.aiIndex[monsterId];
        return i >= 0 ? &m.ais[static_cast<size_t>(i)] : nullptr;
    }
}

namespace GameDataLoad
{
    // ── 2) Bind ──────────────────────────────────────────────────────────────
    void BindMonsterAI(LoadContext& ctx, GameDataSnapshot& snap)
    {
        SnapMonsterAI& m = snap.monsterAI;

        // MonsterSkill
        if (const Json* rows = ctx.Rows("MonsterSkill"))
        {
            m.skills.reserve(rows->size());
            for (size_t i = 0; i < rows->size(); ++i)
            {
                RowReader r(ctx, "MonsterSkill", i, (*rows)[i]);
                MonsterSkillRow row{};
                r.Int("id", row.id)
                 .Str("name", row.name)
                 .Enum("target", row.target)
                 .Int("range_min", row.rangeMin)
                 .Int("range_max", row.rangeMax)
                 .Int("cast_ms", row.castMs)
                 .Int("lock_ms", row.lockMs)
                 .Int("cooldown_ms", row.cooldownMs)
                 .Int("cooldown_jitter_ms", row.cooldownJitterMs)
                 .Enum("pattern", row.pattern)
                 .Enum("fx", row.fx)
                 .Enum("move", row.move)
                 .Int("move_tiles", row.moveTiles)
                 .Int("move_ms", row.moveMs)
                 .Bool("counterable", row.counterable)
                 .Int("aoe", row.aoe)
                 .Enum("aoe_anchor", row.aoeAnchor)
                 .Enum("self_status", row.selfStatus)
                 .Int("self_status_ms", row.selfStatusMs)
                 .Int("self_status_power", row.selfStatusPower);
                r.Finish();
                m.skills.push_back(row);
                m.hitRow.emplace_back().fill(-1);
            }
        }

        // MonsterSkillHit — 자식 탭(skill_id, seq 1부터 연속) → MonsterSkill 행의 hits[]
        if (const Json* rows = ctx.Rows("MonsterSkillHit"))
        {
            for (size_t i = 0; i < rows->size(); ++i)
            {
                RowReader r(ctx, "MonsterSkillHit", i, (*rows)[i]);
                uint16_t skillId = 0;
                uint8_t seq = 0;
                MonsterSkillHit h{};
                r.Int("skill_id", skillId)
                 .Int("seq", seq)
                 .Int("at_ms", h.atMs)
                 .Int("warn_ms", h.warnMs)
                 .Int("coeff", h.coeff)
                 .Enum("shape", h.shape)
                 .Enum("anchor", h.anchor)
                 .Int("a", h.a)
                 .Int("b", h.b)
                 .Int("along", h.along)
                 .Enum("react", h.react)
                 .Int("push", h.push)
                 .Enum("status", h.status)
                 .Int("status_ms", h.statusMs)
                 .Int("status_stacks", h.statusStacks)
                 .Int("max_targets", h.maxTargets);
                r.Finish();
                if (!r.Ok())
                    continue;
                if (skillId < 1 || skillId > m.skills.size() || m.skills[skillId - 1].id != skillId)
                {
                    ctx.Error(ctx.Where("MonsterSkillHit", i, "skill_id"), "MonsterSkill에 없는 기술 id " + std::to_string(skillId));
                    continue;
                }
                MonsterSkillRow& s = m.skills[skillId - 1];
                if (s.hitCount >= kMonsterSkillHits)
                {
                    ctx.Error(ctx.Where("MonsterSkillHit", i, "seq"), "기술 " + std::to_string(skillId) + "의 타격이 " +
                        std::to_string(kMonsterSkillHits) + "개를 넘는다(MonsterSkillRow::hits — 구조 상수)");
                    continue;
                }
                if (seq != s.hitCount + 1)
                {
                    ctx.Error(ctx.Where("MonsterSkillHit", i, "seq"), "seq는 기술마다 1부터 연속이어야 한다(기술 " + std::to_string(skillId) + ": " +
                        std::to_string(s.hitCount + 1) + "이어야 하는데 " + std::to_string(seq) + ")");
                    continue;
                }
                m.hitRow[skillId - 1][s.hitCount] = static_cast<int32_t>(i);
                s.hits[s.hitCount++] = h;
            }
        }

        // MonsterAoE
        if (const Json* rows = ctx.Rows("MonsterAoE"))
        {
            m.aoes.reserve(rows->size());
            for (size_t i = 0; i < rows->size(); ++i)
            {
                RowReader r(ctx, "MonsterAoE", i, (*rows)[i]);
                MonsterAoeRow row{};
                r.Int("id", row.id)
                 .Str("name", row.name)
                 .Int("duration_ms", row.durationMs)
                 .Int("interval_ms", row.intervalMs)
                 .Int("first_ms", row.firstMs)
                 .Int("radius", row.radius)
                 .Enum("affect", row.affect)
                 .Int("coeff", row.coeff)
                 .Int("heal_percent", row.healPercent)
                 .Enum("status", row.status)
                 .Int("status_ms", row.statusMs)
                 .Int("status_power", row.statusPower)
                 .Int("status_stacks", row.statusStacks)
                 .Bool("follow", row.follow)
                 .Bool("owner_bound", row.ownerBound)
                 .Enum("fx", row.fx);
                r.Finish();
                m.aoes.push_back(row);
            }
        }

        // MonsterAI
        if (const Json* rows = ctx.Rows("MonsterAI"))
        {
            m.ais.reserve(rows->size());
            for (size_t i = 0; i < rows->size(); ++i)
            {
                RowReader r(ctx, "MonsterAI", i, (*rows)[i]);
                MonsterAiRow row{};
                r.Int("monster_id", row.monsterId)
                 .Enum("ai", row.ai)
                 .Int("aggro_range", row.aggroRange)
                 .Int("assist_range", row.assistRange)
                 .Int("keep_range", row.keepRange)
                 .Int("skill_gap_ms", row.skillGapMs)
                 .Int("first_skill_ms", row.firstSkillMs)
                 .Int("phase2_hp", row.phase2Hp)
                 .Int("phase3_hp", row.phase3Hp)
                 .Int("phase2_skill", row.phase2Skill)
                 .Int("phase3_skill", row.phase3Skill)
                 .Int("aura", row.aura);
                r.Finish();
                m.ais.push_back(row);
                m.slotRow.emplace_back().fill(-1);
            }
            // 종족 id → 칸. 겹치면 오류(R110)는 검증에서 — 여기서는 처음 것을 쓴다
            uint16_t maxId = 0;
            for (const MonsterAiRow& a : m.ais)
                maxId = a.monsterId > maxId ? a.monsterId : maxId;
            m.aiIndex.assign(static_cast<size_t>(maxId) + 1, -1);
            for (size_t i = 0; i < m.ais.size(); ++i)
                if (m.aiIndex[m.ais[i].monsterId] < 0)
                    m.aiIndex[m.ais[i].monsterId] = static_cast<int32_t>(i);
            for (const MonsterAiRow& a : m.ais)
                m.maxAssistRange = a.assistRange > m.maxAssistRange ? a.assistRange : m.maxAssistRange;
        }

        // MonsterAISkill — 자식 탭(monster_id, seq 1부터 연속) → MonsterAI 행의 skills[]
        if (const Json* rows = ctx.Rows("MonsterAISkill"))
        {
            for (size_t i = 0; i < rows->size(); ++i)
            {
                RowReader r(ctx, "MonsterAISkill", i, (*rows)[i]);
                uint16_t monsterId = 0;
                uint8_t seq = 0;
                MonsterAiSkill s{};
                r.Int("monster_id", monsterId)
                 .Int("seq", seq)
                 .Int("skill_id", s.skillId)
                 .Int("priority", s.priority)
                 .Int("weight", s.weight)
                 .Int("phase_min", s.phaseMin)
                 .Int("phase_max", s.phaseMax)
                 .Int("hp_below", s.hpBelow)
                 .Int("allies_min", s.alliesMin)
                 .Int("chance", s.chance);
                r.Finish();
                if (!r.Ok())
                    continue;
                const int32_t ai = monsterId < m.aiIndex.size() ? m.aiIndex[monsterId] : -1;
                if (ai < 0)
                {
                    ctx.Error(ctx.Where("MonsterAISkill", i, "monster_id"), "MonsterAI에 없는 종족 id " + std::to_string(monsterId));
                    continue;
                }
                MonsterAiRow& a = m.ais[static_cast<size_t>(ai)];
                if (a.skillCount >= kMonsterAiSkills)
                {
                    ctx.Error(ctx.Where("MonsterAISkill", i, "seq"), "종족 " + std::to_string(monsterId) + "의 기술 칸이 " +
                        std::to_string(kMonsterAiSkills) + "개를 넘는다(MonsterAiRow::skills — 구조 상수)");
                    continue;
                }
                if (seq != a.skillCount + 1)
                {
                    ctx.Error(ctx.Where("MonsterAISkill", i, "seq"), "seq는 종족마다 1부터 연속이어야 한다(종족 " + std::to_string(monsterId) + ": " +
                        std::to_string(a.skillCount + 1) + "이어야 하는데 " + std::to_string(seq) + ")");
                    continue;
                }
                m.slotRow[static_cast<size_t>(ai)][a.skillCount] = static_cast<int32_t>(i);
                a.skills[a.skillCount++] = s;
            }
        }
    }

    // ── 3) Validate — 새 규칙 R105~R113 ──────────────────────────────────────
    void ValidateMonsterAI(ValidateContext& v, const GameDataSnapshot& snap)
    {
        const SnapMonsterAI& m = snap.monsterAI;
        auto hitWhere = [&](size_t skill, int hit, const char* key)
        {
            const int32_t row = m.hitRow[skill][static_cast<size_t>(hit)];
            return row >= 0 ? v.Where("MonsterSkillHit", static_cast<size_t>(row), key) : v.Where("MonsterSkill", skill, "id");
        };
        auto slotWhere = [&](size_t ai, int slot, const char* key)
        {
            const int32_t row = m.slotRow[ai][static_cast<size_t>(slot)];
            return row >= 0 ? v.Where("MonsterAISkill", static_cast<size_t>(row), key) : v.Where("MonsterAI", ai, "monster_id");
        };
        auto fortifyOver = [](StatusId status, uint16_t power) { return status == StatusId::Fortify && power > MonsterAoeTable::kMaxFortifyPower; };

        for (size_t i = 0; i < m.skills.size(); ++i)
        {
            const MonsterSkillRow& s = m.skills[i];
            // R105 — id 1부터 연속(파사드가 인덱스로 찾는다)
            if (s.id != i + 1)
                v.Fail("R105", v.Where("MonsterSkill", i, "id"), "id는 1부터 연속이어야 한다(행 " + std::to_string(i + 1) + " → id " + std::to_string(s.id) + ")");
            // R106 — 거리 · 이동
            if (s.rangeMin > s.rangeMax)
                v.Fail("R106", v.Where("MonsterSkill", i, "range_min"), "range_min(" + std::to_string(s.rangeMin) + ") ≤ range_max(" + std::to_string(s.rangeMax) + ")");
            if ((s.move == MonsterMove::Dash || s.move == MonsterMove::Retreat) && (s.moveTiles == 0 || s.moveMs < 30))
                v.Fail("R106", v.Where("MonsterSkill", i, s.moveTiles == 0 ? "move_tiles" : "move_ms"), "돌진 · 물러서기는 move_tiles ≥ 1 · 칸당 move_ms ≥ 30");
            if (s.move == MonsterMove::Leap && s.moveMs < 100)
                v.Fail("R106", v.Where("MonsterSkill", i, "move_ms"), "도약은 전체 move_ms ≥ 100(클라가 그 시간 동안 포물선으로 옮긴다)");
            // R107 — 장판 참조
            if (s.aoe != 0 && FindById(m.aoes, s.aoe) == nullptr)
                v.Fail("R107", v.Where("MonsterSkill", i, "aoe"), "MonsterAoE에 없는 장판 id " + std::to_string(s.aoe));
            // R113 — 굳건 세기 상한
            if (fortifyOver(s.selfStatus, s.selfStatusPower))
                v.Fail("R113", v.Where("MonsterSkill", i, "self_status_power"), "Fortify 세기 ≤ " + std::to_string(MonsterAoeTable::kMaxFortifyPower) + "%");
            for (int j = 0; j < s.hitCount; ++j)
            {
                const MonsterSkillHit& h = s.hits[j];
                // R108 — 모양
                if (h.shape == HitShape::Cone && h.b == 0)
                    v.Fail("R108", hitWhere(i, j, "b"), "부채꼴(Cone) 타격은 b(반각 °) > 0");
                if (h.shape == HitShape::Line && h.a == 0 && s.move != MonsterMove::Dash)
                    v.Fail("R108", hitWhere(i, j, "a"), "직선 a = 0(돌진 경로를 따라 맞는다)은 돌진(Dash) 기술만");
                if (h.shape == HitShape::Ring && h.b > h.a)
                    v.Fail("R108", hitWhere(i, j, "b"), "고리(Ring)는 안쪽 반경 b ≤ 바깥 반경 a");
            }
        }

        for (size_t i = 0; i < m.aoes.size(); ++i)
        {
            const MonsterAoeRow& a = m.aoes[i];
            if (a.id != i + 1)
                v.Fail("R105", v.Where("MonsterAoE", i, "id"), "id는 1부터 연속이어야 한다(행 " + std::to_string(i + 1) + " → id " + std::to_string(a.id) + ")");
            // R109 — 틱 바닥 · 지속 시간(0이면 틱이 지연 없이 다시 걸린다)
            if (a.intervalMs < MonsterAoeTable::kMinIntervalMs)
                v.Fail("R109", v.Where("MonsterAoE", i, "interval_ms"), "틱 간격 " + std::to_string(a.intervalMs) + "ms < " + std::to_string(MonsterAoeTable::kMinIntervalMs) + "ms(MonsterAoeTable::kMinIntervalMs)");
            if (a.durationMs == 0 || a.durationMs > 600000)
                v.Fail("R109", v.Where("MonsterAoE", i, "duration_ms"), "지속 시간은 1ms ~ 10분");
            if (fortifyOver(a.status, a.statusPower))
                v.Fail("R113", v.Where("MonsterAoE", i, "status_power"), "Fortify 세기 ≤ " + std::to_string(MonsterAoeTable::kMaxFortifyPower) + "%");
        }

        std::vector<bool> seen;
        for (size_t i = 0; i < m.ais.size(); ++i)
        {
            const MonsterAiRow& a = m.ais[i];
            // R110 — 종족이 있다 · 한 종족에 한 행
            if (MonsterTable::Find(a.monsterId) == nullptr)
                v.Fail("R110", v.Where("MonsterAI", i, "monster_id"), "Monster에 없는 종족 id " + std::to_string(a.monsterId));
            if (a.monsterId >= seen.size())
                seen.resize(static_cast<size_t>(a.monsterId) + 1, false);
            if (seen[a.monsterId])
                v.Fail("R110", v.Where("MonsterAI", i, "monster_id"), "종족 " + std::to_string(a.monsterId) + "의 행이 둘");
            seen[a.monsterId] = true;
            // R112 — 단계 · 단계 기술 · 오라
            if (a.phase2Hp >= 100 || a.phase3Hp >= 100 || (a.phase3Hp != 0 && a.phase2Hp <= a.phase3Hp))
                v.Fail("R112", v.Where("MonsterAI", i, "phase2_hp"), "단계 문턱은 100 미만 · phase2_hp > phase3_hp(" + std::to_string(a.phase2Hp) + " · " + std::to_string(a.phase3Hp) + ")");
            if (a.phase2Skill != 0 && FindById(m.skills, a.phase2Skill) == nullptr)
                v.Fail("R112", v.Where("MonsterAI", i, "phase2_skill"), "MonsterSkill에 없는 기술 id " + std::to_string(a.phase2Skill));
            if (a.phase3Skill != 0 && FindById(m.skills, a.phase3Skill) == nullptr)
                v.Fail("R112", v.Where("MonsterAI", i, "phase3_skill"), "MonsterSkill에 없는 기술 id " + std::to_string(a.phase3Skill));
            if ((a.phase2Skill != 0 && a.phase2Hp == 0) || (a.phase3Skill != 0 && a.phase3Hp == 0))
                v.Fail("R112", v.Where("MonsterAI", i, a.phase2Skill != 0 && a.phase2Hp == 0 ? "phase2_skill" : "phase3_skill"), "단계 기술은 그 단계 문턱(phase2_hp · phase3_hp)이 있어야 쓴다");
            if (a.aura != 0)
            {
                const MonsterAoeRow* aura = FindById(m.aoes, a.aura);
                if (aura == nullptr || !aura->follow)
                    v.Fail("R112", v.Where("MonsterAI", i, "aura"), aura == nullptr ? "MonsterAoE에 없는 장판 id " + std::to_string(a.aura) : "오라는 주인을 따라다니는 장판(follow)이어야 한다");
            }
            for (int j = 0; j < a.skillCount; ++j)
            {
                const MonsterAiSkill& s = a.skills[j];
                // R111 — 기술 칸
                if (FindById(m.skills, s.skillId) == nullptr)
                    v.Fail("R111", slotWhere(i, j, "skill_id"), "MonsterSkill에 없는 기술 id " + std::to_string(s.skillId));
                if (s.phaseMin < 1 || s.phaseMin > s.phaseMax || s.phaseMax > 3)
                    v.Fail("R111", slotWhere(i, j, "phase_min"), "1 ≤ phase_min ≤ phase_max ≤ 3");
                if (s.weight == 0)
                    v.Fail("R111", slotWhere(i, j, "weight"), "가중치는 1 이상");
                if (s.chance == 0 || s.chance > 100)
                    v.Fail("R111", slotWhere(i, j, "chance"), "확률은 1 ~ 100");
            }
        }
    }

    // ── 4) Dump ──────────────────────────────────────────────────────────────
    void DumpMonsterAI(Canon::Book& book, const GameDataSnapshot& snap)
    {
        const SnapMonsterAI& m = snap.monsterAI;
        Canon::WriteMonsterSkill(book, m.skills);
        Canon::WriteMonsterAoe(book, m.aoes);
        Canon::WriteMonsterAi(book, m.ais);
    }
}
