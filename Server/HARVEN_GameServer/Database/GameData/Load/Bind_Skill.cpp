#include "pch.h"
// ─────────────────────────────────────────────────────────────────────────────
// 스킬 묶음 — 탭 Skill · SkillAction · SkillHit · Status · Combo · SkillKeyMap → 스냅샷 →
// 파사드 SkillTable · SkillDefTable · StatusTable · ComboTable. 모양은 Bind_Monster.cpp(모범 표)와 같다.
//   1) 파사드 — GD()의 스냅샷을 읽는다(이관 전과 같은 동작: 범위 밖 입력 · nullptr · 순차 검색 폴백까지)
//   2) Bind    — 열마다 RowReader. SkillHit(자식 탭)은 SkillAction 행의 hits[8]로 모은다(8개 = 구조 상수, 넘으면 로드 실패)
//   3) Validate — 옮겨 온 규칙(RULES.md R001 · R003~R009) + 새 규칙(R103) — 위치 + R번호
//   4) Dump    — Canon_Skill.h(datatool export와 같은 바이트)
// ─────────────────────────────────────────────────────────────────────────────
#include <string>

#include "Database/GameData/Load/Bundles.h"
#include "Database/GameData/Load/Canon_Skill.h"
#include "Database/GameData/Load/JsonRead.h"
#include "Database/GameData/Load/Snapshot.h"
#include "Database/GameData/Load/Validate.h"
#include "Database/GameData/CombatData.h"
#include "Database/GameData/SkillData.h"

// ── 1) 파사드 ────────────────────────────────────────────────────────────────
namespace StatusTable
{
    const StatusRow& Get(StatusId id) noexcept
    {
        // 로드가 끝난 판은 행 수 = StatusId::Count(R001)라 늘 rows[i]. 아래 kNone은 검증 중(아직 틀린 판)에 불려도 넘지 않게.
        static const StatusRow kNone{ StatusId::None, "", false, false, false, 0, 0, 0, 0, 0 };
        const std::vector<StatusRow>& rows = GD().skill.status;
        const uint8_t raw = static_cast<uint8_t>(id);
        const size_t i = raw < static_cast<uint8_t>(StatusId::Count) ? raw : 0;
        return i < rows.size() ? rows[i] : kNone;
    }
}

namespace ComboTable
{
    uint8_t Count() noexcept { return static_cast<uint8_t>(GD().skill.combo.size()); }

    const char* Name(uint8_t id) noexcept
    {
        const std::vector<const char*>& names = GD().skill.combo;
        return id < names.size() ? names[id] : "";
    }
}

namespace SkillTable
{
    uint16_t Count() noexcept { return static_cast<uint16_t>(GD().skill.rows.size()); }

    const SkillRow* Find(uint16_t id) noexcept
    {
        const std::vector<SkillRow>& rows = GD().skill.rows;
        return (id > 0 && id < rows.size() && rows[id].id == id) ? &rows[id] : nullptr;
    }
}

namespace SkillDefTable
{
    int Count() noexcept { return static_cast<int>(GD().skill.defs.size()); }

    const SkillDef* Find(uint16_t id) noexcept
    {
        if (id == 0)
            return nullptr;
        const SnapSkill& s = GD().skill;
        if (id >= s.defIndex.size())
            return nullptr;
        const int32_t i = s.defIndex[id];
        return i >= 0 ? &s.defs[static_cast<size_t>(i)] : nullptr;
    }

    uint16_t Default(ClassId cls, SkillKey key) noexcept
    {
        const int c = static_cast<int>(cls);
        const int k = static_cast<int>(key);
        return (c >= 0 && c < SnapSkill::kKeyMapClasses && k >= 0 && k < kSkillKeys) ? GD().skill.keyMap[c][k] : 0;
    }
}

namespace
{
    const char* const kClassSkillCol[6] = { "skill_0", "skill_1", "skill_2", "skill_3", "skill_4", "skill_5" };

    template <class E>
    std::string EnumName(E e)
    {
        const int v = static_cast<int>(e);
        for (const auto& it : GameDataEnums::Info<E>::kItems)
            if (it.value == v) return std::string(it.name);
        return std::to_string(v);
    }
}

namespace GameDataLoad
{
    // ── 2) Bind ──────────────────────────────────────────────────────────────
    void BindSkill(LoadContext& ctx, GameDataSnapshot& snap)
    {
        SnapSkill& s = snap.skill;

        // Skill
        if (const Json* rows = ctx.Rows("Skill"))
        {
            s.rows.reserve(rows->size());
            for (size_t i = 0; i < rows->size(); ++i)
            {
                RowReader r(ctx, "Skill", i, (*rows)[i]);
                SkillRow row{};
                r.Int("id", row.id)
                 .Enum("cls", row.cls)
                 .Int("slot", row.slot)
                 .Str("name", row.name)
                 .Str("desc", row.desc)
                 .Str("combo", row.combo)
                 .Int("unlock_level", row.unlockLevel)
                 .Int("mp", row.mp)
                 .Int("cooldown_ms", row.cooldownMs)
                 .Int("cast_ms", row.castMs)
                 .Int("range", row.range)
                 .Enum("target", row.target)
                 .Enum("shape", row.shape)
                 .Int("radius", row.radius)
                 .Enum("kind", row.kind)
                 .Int("coeff", row.coeff)
                 .Int("hits", row.hits)
                 .Enum("status", row.status)
                 .Int("status_ms", row.statusMs)
                 .Int("status_stacks", row.statusStacks)
                 .Enum("combo_status", row.comboStatus)
                 .Int("combo_bonus", row.comboBonus)
                 .Int("combo_id", row.comboId)
                 .Bool("consume_combo", row.consumeCombo)
                 .Int("knockback", row.knockback)
                 .Int("dash", row.dash)
                 .Int("projectile", row.projectile)
                 .Color("color", row.color);
                r.Finish();
                s.rows.push_back(row);
            }
            // 구조: 행 수는 uint16_t(이관 전 kCount) — id u16이 0부터 연속(R003)이면 65536행까지 갈 수 있다
            if (s.rows.size() > 65535)
                ctx.Error("Skill", "행이 65535개를 넘는다(SkillTable::Count — uint16_t)");
        }

        // Status
        if (const Json* rows = ctx.Rows("Status"))
        {
            s.status.reserve(rows->size());
            for (size_t i = 0; i < rows->size(); ++i)
            {
                RowReader r(ctx, "Status", i, (*rows)[i]);
                StatusRow row{};
                r.Enum("id", row.id)
                 .Str("name", row.name)
                 .Bool("debuff", row.debuff)
                 .Bool("blocks_move", row.blocksMove)
                 .Bool("blocks_attack", row.blocksAttack)
                 .Int("max_stacks", row.maxStacks)
                 .Int("tick_ms", row.tickMs)
                 .Int("tick_percent", row.tickPercent)
                 .Int("slow_percent", row.slowPercent)
                 .Color("color", row.color);
                r.Finish();
                s.status.push_back(row);
            }
        }

        // Combo — 행 순서 = id(이관 전 kNames[id]). 구조: id u8 · 행 수 uint8_t(이관 전 kCount)
        if (const Json* rows = ctx.Rows("Combo"))
        {
            s.combo.reserve(rows->size());
            for (size_t i = 0; i < rows->size(); ++i)
            {
                RowReader r(ctx, "Combo", i, (*rows)[i]);
                uint8_t id = 0;
                const char* name = "";
                r.Int("id", id).Str("name", name);
                r.Finish();
                if (r.Ok() && id != i)
                    ctx.Error(ctx.Where("Combo", i, "id"), "id는 0부터 연속이어야 한다(행 " + std::to_string(i + 1) + " → id " + std::to_string(id) + ")");
                s.combo.push_back(name);
            }
            if (s.combo.size() > 255)
                ctx.Error("Combo", "행이 255개를 넘는다(ComboTable::Count — uint8_t)");
        }

        // SkillAction
        if (const Json* rows = ctx.Rows("SkillAction"))
        {
            s.defs.reserve(rows->size());
            s.hitRow.reserve(rows->size());
            for (size_t i = 0; i < rows->size(); ++i)
            {
                RowReader r(ctx, "SkillAction", i, (*rows)[i]);
                SkillDef d{};
                r.Int("id", d.id)
                 .Enum("key", d.key)
                 .Enum("kind", d.kind)
                 .Int("tier", d.tier)
                 .Enum("special", d.special)
                 .Int("cooldown_ms", d.cooldownMs)
                 .Int("cast_ms", d.castMs)
                 .Int("lock_ms", d.lockMs)
                 .Int("super_armor_ms", d.superArmorMs)
                 .Int("invuln_ms", d.invulnMs)
                 .Int("stages", d.stages)
                 .Int("stage_ms", d.stageMs)
                 .Int("perfect_ms", d.perfectMs)
                 .Int("hold_max_ms", d.holdMaxMs)
                 .Int("tick_ms", d.tickMs)
                 .Int("tick_mp", d.tickMp)
                 .Bool("move_while_casting", d.moveWhileCasting)
                 .Int("range", d.range)
                 .Int("move_ms", d.moveMs)
                 .Str("kind_text", d.kindText);
                r.Finish();
                s.defs.push_back(d);
                std::array<int32_t, 8> hr;
                hr.fill(-1);
                s.hitRow.push_back(hr);
            }
            // id → 행. 이관 전 Find는 배열을 순서대로 돌아 처음 맞는 행을 돌려줬다 — id가 겹치지 않으면 색인과 같다(겹치면 로드 실패)
            uint16_t maxId = 0;
            for (const SkillDef& d : s.defs)
                if (d.id > maxId) maxId = d.id;
            s.defIndex.assign(static_cast<size_t>(maxId) + 1, -1);
            for (size_t i = 0; i < s.defs.size(); ++i)
            {
                int32_t& slot = s.defIndex[s.defs[i].id];
                if (slot >= 0)
                    ctx.Error(ctx.Where("SkillAction", i, "id"), "id " + std::to_string(s.defs[i].id) + "가 겹친다(행 " + ctx.Where("SkillAction", static_cast<size_t>(slot)) + "과)");
                else
                    slot = static_cast<int32_t>(i);
            }
        }

        // SkillHit — 자식 탭(skill_id, seq 1부터 연속) → SkillAction 행의 hits[]
        if (const Json* rows = ctx.Rows("SkillHit"))
        {
            for (size_t i = 0; i < rows->size(); ++i)
            {
                RowReader r(ctx, "SkillHit", i, (*rows)[i]);
                uint16_t skillId = 0;
                uint8_t seq = 0;
                SkillHit h{};
                r.Int("skill_id", skillId)
                 .Int("seq", seq)
                 .Int("at_ms", h.atMs)
                 .Int("coeff", h.coeff)
                 .Enum("shape", h.shape)
                 .Enum("anchor", h.anchor)
                 .Int("a", h.a)
                 .Int("b", h.b)
                 .Int("along", h.along)
                 .Enum("react", h.react)
                 .Int("stagger", h.stagger)
                 .Flags<HitFlag>("flags", h.flags)
                 .Int("stage", h.stage)
                 .Int("max_targets", h.maxTargets);
                r.Finish();
                if (!r.Ok())
                    continue;
                const int32_t di = skillId < s.defIndex.size() ? s.defIndex[skillId] : -1;
                if (di < 0)
                {
                    ctx.Error(ctx.Where("SkillHit", i, "skill_id"), "SkillAction에 없는 기술 id " + std::to_string(skillId));
                    continue;
                }
                SkillDef& d = s.defs[static_cast<size_t>(di)];
                if (d.hitCount >= 8)
                {
                    ctx.Error(ctx.Where("SkillHit", i, "seq"), "기술 " + std::to_string(skillId) + "의 타격이 8개를 넘는다(SkillDef::hits[8] — 구조 상수)");
                    continue;
                }
                if (seq != d.hitCount + 1)
                {
                    ctx.Error(ctx.Where("SkillHit", i, "seq"), "seq는 기술마다 1부터 연속이어야 한다(기술 " + std::to_string(skillId) + ": " +
                        std::to_string(d.hitCount + 1) + "이어야 하는데 " + std::to_string(seq) + ")");
                    continue;
                }
                s.hitRow[static_cast<size_t>(di)][d.hitCount] = static_cast<int32_t>(i);
                d.hits[d.hitCount++] = h;
            }
        }

        // SkillKeyMap — (직업, 칸)마다 한 행(3 × 12). 빠진 칸 · 겹친 칸은 로드 실패(빈 칸을 0으로 읽지 않는다)
        for (auto& row : s.keyMapRow)
            row.fill(-1);
        if (const Json* rows = ctx.Rows("SkillKeyMap"))
        {
            for (size_t i = 0; i < rows->size(); ++i)
            {
                RowReader r(ctx, "SkillKeyMap", i, (*rows)[i]);
                ClassId cls = ClassId::Knight;
                SkillKey key = SkillKey::C;
                uint16_t skillId = 0;
                r.Enum("cls", cls).Enum("key", key).Int("skill_id", skillId);
                r.Finish();
                if (!r.Ok())
                    continue;
                const int c = static_cast<int>(cls);
                const int k = static_cast<int>(key);
                if (c < 0 || c >= SnapSkill::kKeyMapClasses || k < 0 || k >= kSkillKeys)
                {
                    ctx.Error(ctx.Where("SkillKeyMap", i, "cls"), "직업 · 칸이 범위 밖(직업 0~2 · 칸 0~11)");
                    continue;
                }
                int32_t& at = s.keyMapRow[c][k];
                if (at >= 0)
                {
                    ctx.Error(ctx.Where("SkillKeyMap", i, "key"), "(" + EnumName(cls) + ", " + EnumName(key) + ")이 두 번 있다(" + ctx.Where("SkillKeyMap", static_cast<size_t>(at)) + ")");
                    continue;
                }
                at = static_cast<int32_t>(i);
                s.keyMap[c][k] = skillId;
            }
            for (int c = 0; c < SnapSkill::kKeyMapClasses; ++c)
                for (int k = 0; k < kSkillKeys; ++k)
                    if (s.keyMapRow[c][k] < 0)
                        ctx.Error("SkillKeyMap", "(" + EnumName(static_cast<ClassId>(c)) + ", " + EnumName(static_cast<SkillKey>(k)) + ") 행이 없다 — 직업 × 칸 36행이 모두 있어야 한다");
        }
    }

    // ── 3) Validate — 예전 StatusTable static_assert · SkillTable::Detail::Validate · SkillDefTable::Detail::Validate의 규칙 ──
    void ValidateSkill(ValidateContext& v, const GameDataSnapshot& snap)
    {
        const SnapSkill& s = snap.skill;

        // R001 — Status: 행 수 = StatusId::Count, 행 순서 = id
        const size_t statusCount = static_cast<size_t>(StatusId::Count);
        for (size_t i = 0; i < s.status.size(); ++i)
            if (static_cast<size_t>(s.status[i].id) != i)
                v.Fail("R001", v.Where("Status", i, "id"), "id는 행 순서여야 한다(행 " + std::to_string(i + 1) + " → " + EnumName(s.status[i].id) + ")");
        if (s.status.size() != statusCount)
            v.Fail("R001", v.Where("Status", s.status.empty() ? 0 : s.status.size() - 1, "id"),
                "행 수 " + std::to_string(s.status.size()) + " ≠ StatusId::Count(" + std::to_string(statusCount) + ")");

        // R003 — Skill: id = 행 순서(0부터 연속, 0 = 빈 행)
        for (size_t i = 0; i < s.rows.size(); ++i)
            if (s.rows[i].id != i)
                v.Fail("R003", v.Where("Skill", i, "id"), "id는 0부터 연속이어야 한다(행 " + std::to_string(i + 1) + " → id " + std::to_string(s.rows[i].id) + ")");

        // R004 — Class의 skill_0~5 ↔ Skill: 있는 기술 · 같은 직업 · slot = 열 번호. Class 행은 파사드로(묶음이 따로 옮겨 간다)
        for (int ci = 0; ci < static_cast<int>(ClassId::Count); ++ci)
        {
            const ClassRow& c = ClassTable::Get(static_cast<ClassId>(ci));
            for (int slot = 0; slot < 6; ++slot)
            {
                const uint16_t id = c.skills[slot];
                const SkillRow* r = SkillTable::Find(id);
                if (r == nullptr)
                    v.Fail("R004", v.Where("Class", static_cast<size_t>(ci), kClassSkillCol[slot]), "Skill에 없는 기술 id " + std::to_string(id));
                else if (r->cls != c.id)
                    v.Fail("R004", v.Where("Skill", r->id, "cls"), EnumName(r->cls) + " — " + EnumName(c.id) + "의 " + kClassSkillCol[slot] + "가 이 기술이다(직업이 같아야 한다)");
                else if (r->slot != slot)
                    v.Fail("R004", v.Where("Skill", r->id, "slot"), "slot " + std::to_string(r->slot) + " — " + EnumName(c.id) + "의 " + kClassSkillCol[slot] + "가 이 기술이다(slot " + std::to_string(slot) + "이어야 한다)");
            }
        }

        // R006 ~ R008 — SkillAction(0번 빈 행 제외)
        for (size_t i = 0; i < s.defs.size(); ++i)
        {
            const SkillDef& d = s.defs[i];
            if (d.id == 0)
                continue;
            const SkillRow* r = SkillTable::Find(d.id);
            if (r == nullptr)
                v.Fail("R006", v.Where("SkillAction", i, "id"), "Skill에 없는 기술 id " + std::to_string(d.id));
            else if (const uint16_t want = SkillDefTable::Default(r->cls, d.key); want != d.id)
                v.Fail("R007", v.Where("SkillAction", i, "key"), "SkillKeyMap(" + EnumName(r->cls) + ", " + EnumName(d.key) + ") = " + std::to_string(want) +
                    " ≠ 이 기술 " + std::to_string(d.id) + "(기본 배치의 칸과 표의 칸이 같아야 한다)");
            for (int j = 0; j < d.hitCount; ++j)
                if (d.hits[j].shape == HitShape::Cone && d.hits[j].b == 0)
                    v.Fail("R008", v.Where("SkillHit", static_cast<size_t>(s.hitRow[i][static_cast<size_t>(j)]), "b"), "부채꼴(Cone) 타격은 b(반각 °) > 0");
            // R103 — 누르고 있기(Hold) · 켜 두기(Toggle): 틱 간격 ≥ kMinTickMs(0이면 틱이 지연 없이 연달아 걸린다 — UserSkillHelper.cpp), 최대 시간 > 0
            if (d.kind == SkillKind::Hold || d.kind == SkillKind::Toggle)
            {
                if (d.tickMs < SkillDefTable::kMinTickMs)
                    v.Fail("R103", v.Where("SkillAction", i, "tick_ms"), EnumName(d.kind) + " 기술의 틱 간격 " + std::to_string(d.tickMs) + "ms < " +
                        std::to_string(SkillDefTable::kMinTickMs) + "ms(SkillDefTable::kMinTickMs)");
                if (d.holdMaxMs == 0)
                    v.Fail("R103", v.Where("SkillAction", i, "hold_max_ms"), EnumName(d.kind) + " 기술은 hold_max_ms > 0(누르고 있기 · 켜 두기의 최대 시간)");
            }
        }

        // R009 — SkillKeyMap: 세 직업 × 12칸의 기술이 모두 SkillAction에 있다
        for (int c = 0; c < SnapSkill::kKeyMapClasses; ++c)
            for (int k = 0; k < kSkillKeys; ++k)
            {
                const uint16_t id = SkillDefTable::Default(static_cast<ClassId>(c), static_cast<SkillKey>(k));
                if (SkillDefTable::Find(id) == nullptr)
                {
                    const int32_t at = s.keyMapRow[c][k];
                    v.Fail("R009", at >= 0 ? v.Where("SkillKeyMap", static_cast<size_t>(at), "skill_id") : std::string("SkillKeyMap"),
                        "(" + EnumName(static_cast<ClassId>(c)) + ", " + EnumName(static_cast<SkillKey>(k)) + ") → 기술 " + std::to_string(id) + "이 SkillAction에 없다");
                }
            }
    }

    // ── 4) Dump ──────────────────────────────────────────────────────────────
    // Status · Combo · SkillKeyMap은 파사드로 쓴다 — DumpToDir가 GD()를 이 스냅샷으로 맞춘다.
    void DumpSkill(Canon::Book& book, const GameDataSnapshot& snap)
    {
        Canon::WriteSkill(book, snap.skill.rows);
        Canon::WriteSkillAction(book, snap.skill.defs);
        Canon::WriteStatus(book);
        Canon::WriteCombo(book, ComboTable::Count());
        Canon::WriteSkillKeyMap(book);
    }
}
