#include "pch.h"
// ─────────────────────────────────────────────────────────────────────────────
// 몬스터 묶음 — 탭 Monster(GameData/Monster.json) → 스냅샷 → 파사드 MonsterTable::*.
// Phase 1 모범 표: 다른 묶음은 이 파일의 모양을 따른다(Shared/Document/DATA_PIPELINE.md "새 표 · 새 열을 더하는 법").
//   1) 파사드(헤더에 선언된 함수) 구현 — GD()의 스냅샷을 읽는다
//   2) Bind    — 열마다 RowReader(형 · 범위 · 열거형 · 빠진 열 · 모르는 열 검사)
//   3) Validate — 옮겨 온 규칙(RULES.md R031~R035) + 새 규칙(R102) — 위치 + R번호를 남긴다
//   4) Dump    — Canon_Monster.h(datatool export와 같은 바이트)로 정본 표
// ─────────────────────────────────────────────────────────────────────────────
#include "Database/GameData/Load/Bundles.h"
#include "Database/GameData/Load/Canon_Monster.h"
#include "Database/GameData/Load/JsonRead.h"
#include "Database/GameData/Load/Snapshot.h"
#include "Database/GameData/Load/Validate.h"
#include "Database/GameData/MonsterTable.h"

// ── 1) 파사드 ────────────────────────────────────────────────────────────────
namespace MonsterTable
{
    std::span<const MonsterTableRow> Rows() noexcept { return GD().monster.rows; }

    size_t RowCount() noexcept { return GD().monster.rows.size(); }

    const char* BossEpithet(uint16_t id) noexcept
    {
        const SnapMonster& m = GD().monster;
        if (id >= 1 && id <= m.rows.size() && m.rows[id - 1].id == id)
            return m.epithet[id - 1] != nullptr ? m.epithet[id - 1] : "";
        for (size_t i = 0; i < m.rows.size(); ++i)
            if (m.rows[i].id == id)
                return m.epithet[i] != nullptr ? m.epithet[i] : "";
        return "";
    }

    const MonsterTableRow* Find(uint16_t id) noexcept
    {
        // id가 1부터 연속이라 인덱스로 바로 찾을 수 있지만, 누군가 중간을 비우는
        // 실수를 해도 틀린 행을 돌려주지 않도록 id를 확인한다(이관 전과 같은 동작).
        const std::vector<MonsterTableRow>& rows = GD().monster.rows;
        if (id >= 1 && id <= rows.size() && rows[id - 1].id == id)
            return &rows[id - 1];
        for (const auto& row : rows)
            if (row.id == id)
                return &row;
        return nullptr;
    }
}

namespace GameDataLoad
{
    // ── 2) Bind ──────────────────────────────────────────────────────────────
    void BindMonster(LoadContext& ctx, GameDataSnapshot& snap)
    {
        const Json* rows = ctx.Rows("Monster");
        if (rows == nullptr)
            return;
        SnapMonster& m = snap.monster;
        m.rows.reserve(rows->size());
        for (size_t i = 0; i < rows->size(); ++i)
        {
            RowReader r(ctx, "Monster", i, (*rows)[i]);
            MonsterTableRow row{};
            const char* codeName = nullptr;
            const char* epithet = nullptr;
            r.Int("id", row.id)
             .StrOpt("code_name", codeName)
             .Str("name", row.name)
             .StrOpt("epithet", epithet)
             .Bool("aggressive", row.aggressive)
             .Int("level", row.level)
             .Int("max_hp", row.maxHp)
             .Int("attack", row.attack)
             .Int("exp", row.exp)
             .Int("adena_min", row.adenaMin)
             .Int("adena_max", row.adenaMax)
             .Int("adena_chance", row.adenaChance)
             .Int("drop_table", row.dropTable)
             .Int("drop_rolls", row.dropRolls)
             .Int("attack_interval_ms", row.attackIntervalMs)
             .Int("move_interval_ms", row.moveIntervalMs)
             .Int("leash", row.leash)
             .Bool("boss", row.boss)
             .Enum("archetype", row.archetype)
             .Enum("variant", row.variant)
             .Color("color1", row.color1)
             .Color("color2", row.color2)
             .Int("scale", row.scale)
             .Bool("dummy", row.dummy);
            r.Finish();
            m.rows.push_back(row);
            m.codeName.push_back(codeName);
            m.epithet.push_back(epithet);
        }
    }

    // ── 3) Validate — 예전 MonsterTable::Detail::Validate()(static_assert)의 규칙 ──
    void ValidateMonster(ValidateContext& v, const GameDataSnapshot& snap)
    {
        const auto& rows = snap.monster.rows;
        for (size_t i = 0; i < rows.size(); ++i)
        {
            const MonsterTableRow& r = rows[i];
            if (r.id != i + 1)
                v.Fail("R031", v.Where("Monster", i, "id"), "id는 1부터 연속이어야 한다(행 " + std::to_string(i + 1) + " → id " + std::to_string(r.id) + ")");
            const size_t bytes = Utf8Bytes(r.name);
            if (bytes == 0 || bytes > 19)
                v.Fail("R032", v.Where("Monster", i, "name"), std::to_string(bytes) + "바이트 — 1~19바이트(SUBJECT_ADD_NFY.name[20])");
            if (r.maxHp == 0 || r.attack == 0)
                v.Fail("R033", v.Where("Monster", i, r.maxHp == 0 ? "max_hp" : "attack"), "최대 HP · 공격력은 0일 수 없다");
            if (r.adenaMin > r.adenaMax || r.adenaMax > 65535)
                v.Fail("R034", v.Where("Monster", i, r.adenaMin > r.adenaMax ? "adena_min" : "adena_max"),
                    "그로시 최소(" + std::to_string(r.adenaMin) + ") ≤ 최대(" + std::to_string(r.adenaMax) + ") ≤ 65535(필드 아이템 수량 uint16)");
            if (r.dropRolls == 0)
                v.Fail("R035", v.Where("Monster", i, "drop_rolls"), "드롭 횟수는 1 이상");
            // R102 — 공격 · 이동 간격의 바닥(0이면 타이머가 지연 없이 다시 걸려 Zone · 타이머 스레드가 넘친다 — MonsterHelper)
            if (r.attackIntervalMs < MonsterTable::kMinActIntervalMs)
                v.Fail("R102", v.Where("Monster", i, "attack_interval_ms"),
                    "공격 간격 " + std::to_string(r.attackIntervalMs) + "ms < " + std::to_string(MonsterTable::kMinActIntervalMs) + "ms(MonsterTable::kMinActIntervalMs)");
            if (r.moveIntervalMs < MonsterTable::kMinActIntervalMs)
                v.Fail("R102", v.Where("Monster", i, "move_interval_ms"),
                    "이동 간격 " + std::to_string(r.moveIntervalMs) + "ms < " + std::to_string(MonsterTable::kMinActIntervalMs) + "ms(MonsterTable::kMinActIntervalMs)");
        }
    }

    // ── 4) Dump ──────────────────────────────────────────────────────────────
    void DumpMonster(Canon::Book& book, const GameDataSnapshot& snap)
    {
        const SnapMonster& m = snap.monster;
        Canon::WriteMonster(book, m.rows, [&m](uint32_t id) -> const char*
        {
            for (size_t i = 0; i < m.rows.size(); ++i)
                if (m.rows[i].id == id) return m.codeName[i];
            return nullptr;
        });
    }
}
