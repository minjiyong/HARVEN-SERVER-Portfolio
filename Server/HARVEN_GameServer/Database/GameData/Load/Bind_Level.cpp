#include "pch.h"
// ─────────────────────────────────────────────────────────────────────────────
// 레벨 묶음 — 탭 Level · Class(GameData/Level.json · Class.json) → 스냅샷 → 파사드 LevelTable::* · ClassTable::Get.
// 모양은 Bind_Monster.cpp(모범 표)와 같다: 1) 파사드 2) Bind 3) Validate(R002 · R027~R029) 4) Dump.
// 레벨은 예전 공식(1~50)을 행으로 풀었다 — 표 밖 입력(0 · 51~255)은 LevelTable::Beyond(이관 전 식)로 같은 값을 낸다.
// ─────────────────────────────────────────────────────────────────────────────
#include "Database/GameData/Load/Bundles.h"
#include "Database/GameData/Load/Canon_Level.h"
#include "Database/GameData/Load/JsonRead.h"
#include "Database/GameData/Load/Snapshot.h"
#include "Database/GameData/Load/Validate.h"
#include "Database/GameData/CombatData.h"
#include "Database/GameData/LevelTable.h"

// ── 1) 파사드 ────────────────────────────────────────────────────────────────
namespace
{
    const SnapLevel::Row* LevelRow(uint8_t level) noexcept
    {
        const std::vector<SnapLevel::Row>& rows = GD().level.levels;
        return (level >= 1 && level <= rows.size()) ? &rows[level - 1u] : nullptr;
    }
}

namespace LevelTable
{
    uint32_t ExpToNext(uint8_t level) noexcept
    {
        const SnapLevel::Row* r = LevelRow(level);
        return r != nullptr ? r->expToNext : Beyond::ExpToNext(level);
    }

    uint16_t MaxHp(uint8_t level) noexcept
    {
        const SnapLevel::Row* r = LevelRow(level);
        return r != nullptr ? r->maxHp : Beyond::MaxHp(level);
    }

    uint16_t BaseAttack(uint8_t level) noexcept
    {
        const SnapLevel::Row* r = LevelRow(level);
        return r != nullptr ? r->baseAttack : Beyond::BaseAttack(level);
    }

    uint16_t RegenPerTick(uint8_t level, bool inSafeZone) noexcept
    {
        const SnapLevel::Row* r = LevelRow(level);
        if (r == nullptr)
            return Beyond::RegenPerTick(level, inSafeZone);
        return inSafeZone ? r->regenSafe : r->regen;
    }

    uint32_t DeathExpPenalty(uint8_t level, uint32_t currentExp) noexcept
    {
        const SnapLevel::Row* r = LevelRow(level);
        const uint32_t penalty = r != nullptr ? r->deathPenalty : Beyond::DeathExpPenalty(level);
        return penalty < currentExp ? penalty : currentExp;
    }
}

namespace ClassTable
{
    const ClassRow& Get(ClassId id) noexcept
    {
        // 이관 전: kRows[id < Count ? id : 0]. 행 수는 R002가 보장한다 — 검증 전(틀린 판)에도 범위 밖을 읽지 않게 한 번 더 막는다.
        static const ClassRow kNone{};
        const std::vector<ClassRow>& rows = GD().level.classes;
        const uint8_t i = static_cast<uint8_t>(id) < static_cast<uint8_t>(ClassId::Count) ? static_cast<uint8_t>(id) : 0;
        return i < rows.size() ? rows[i] : (rows.empty() ? kNone : rows[0]);
    }
}

namespace GameDataLoad
{
    // ── 2) Bind ──────────────────────────────────────────────────────────────
    void BindLevel(LoadContext& ctx, GameDataSnapshot& snap)
    {
        SnapLevel& s = snap.level;
        if (const Json* rows = ctx.Rows("Level"))
        {
            s.levels.reserve(rows->size());
            for (size_t i = 0; i < rows->size(); ++i)
            {
                RowReader r(ctx, "Level", i, (*rows)[i]);
                SnapLevel::Row row;
                r.Int("level", row.level)
                 .Int("exp_to_next", row.expToNext)
                 .Int("max_hp", row.maxHp)
                 .Int("base_attack", row.baseAttack)
                 .Int("regen", row.regen)
                 .Int("regen_safe", row.regenSafe)
                 .Int("death_penalty", row.deathPenalty);
                r.Finish();
                // 구조: 파사드가 레벨 - 1로 바로 찾는다
                if (r.Ok() && row.level != i + 1)
                    ctx.Error(ctx.Where("Level", i, "level"), "레벨은 1부터 연속(행 " + std::to_string(i + 1) + " → 레벨 " + std::to_string(row.level) + ")");
                s.levels.push_back(row);
            }
            if (s.levels.size() != LevelTable::kMaxLevel)
                ctx.Error("Level", "행 수 " + std::to_string(s.levels.size()) + " ≠ " + std::to_string(LevelTable::kMaxLevel) + "(LevelTable::kMaxLevel — 구조 상수)");
        }

        if (const Json* rows = ctx.Rows("Class"))
        {
            static constexpr const char* kStat[6] = { "str", "dex", "int", "vit", "wis", "luk" };
            s.classes.reserve(rows->size());
            for (size_t i = 0; i < rows->size(); ++i)
            {
                RowReader r(ctx, "Class", i, (*rows)[i]);
                ClassRow row{};
                r.Enum("id", row.id).Str("name", row.name).Str("role", row.role).Str("desc", row.desc);
                for (int k = 0; k < 6; ++k) r.Int((std::string("base_") + kStat[k]).c_str(), row.base[k]);
                for (int k = 0; k < 6; ++k) r.Int((std::string("growth10_") + kStat[k]).c_str(), row.growth10[k]);
                r.Int("base_hp", row.baseHp).Int("hp_per_level", row.hpPerLevel)
                 .Int("base_mp", row.baseMp).Int("mp_per_level", row.mpPerLevel)
                 .Int("atk_str10", row.atkStr10).Int("atk_dex10", row.atkDex10)
                 .Int("basic_interval_ms", row.basicIntervalMs).Int("basic_range", row.basicRange);
                for (int k = 0; k < 6; ++k) r.Int(("skill_" + std::to_string(k)).c_str(), row.skills[k]);
                r.Int("starter_weapon", row.starterWeapon).Color("color", row.color);
                r.Finish();
                s.classes.push_back(row);
            }
        }
    }

    // ── 3) Validate — 예전 static_assert(ClassTable 행 수 · LevelTable 고정값) ──
    void ValidateLevel(ValidateContext& v, const GameDataSnapshot& snap)
    {
        const std::vector<ClassRow>& classes = snap.level.classes;
        if (classes.size() != static_cast<size_t>(ClassId::Count))
            v.Fail("R002", "Class", "행 수 " + std::to_string(classes.size()) + " ≠ ClassId::Count(" + std::to_string(static_cast<int>(ClassId::Count)) + ")");
        for (size_t i = 0; i < classes.size(); ++i)
            if (static_cast<size_t>(classes[i].id) != i)
                v.Fail("R002", v.Where("Class", i, "id"), "행 순서 = id여야 한다(행 " + std::to_string(i + 1) + " → id " + std::to_string(static_cast<int>(classes[i].id)) + ")");

        const std::vector<SnapLevel::Row>& levels = snap.level.levels;
        if (!levels.empty())
        {
            const SnapLevel::Row& l1 = levels[0];
            if (l1.expToNext != 30)
                v.Fail("R027", v.Where("Level", 0, "exp_to_next"), "1레벨 exp_to_next는 30(기존 EXP_TABLE[0]과 같게) — 지금 " + std::to_string(l1.expToNext));
            if (l1.maxHp != 100)
                v.Fail("R028", v.Where("Level", 0, "max_hp"), "1레벨 max_hp는 100(PLAYER_MAX_HP) — 지금 " + std::to_string(l1.maxHp));
            if (l1.baseAttack != 10)
                v.Fail("R029", v.Where("Level", 0, "base_attack"), "1레벨 base_attack은 10(PLAYER_OFFENSIVE) — 지금 " + std::to_string(l1.baseAttack));
        }
    }

    // ── 4) Dump — 정본 쓰기(Canon_Level.h, 파사드를 부른다) ──
    void DumpLevel(Canon::Book& book, const GameDataSnapshot&)
    {
        Canon::WriteLevel(book);
        Canon::WriteClass(book);
    }
}
