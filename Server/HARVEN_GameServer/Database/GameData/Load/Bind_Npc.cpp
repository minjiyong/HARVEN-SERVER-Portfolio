#include "pch.h"
// ─────────────────────────────────────────────────────────────────────────────
// NPC대사 묶음 — 탭 Npc · NpcLine(GameData/Npc.json · NpcLine.json) → 스냅샷 → 파사드 WorldData::FindNpc · NpcTalk::Find ….
// 모양은 Bind_Monster.cpp(모범 표)를 따른다: 1) 파사드 2) Bind 3) Validate(R066~R071) 4) Dump.
//
// NPC 집 자리(NpcRow.x · y)는 Npc 탭이 아니라 마을 원본(mapgen anchors.txt npc → 마을 표 TownNpcHome)이 주인이다.
// 마을 표를 읽는 곳은 이 파일의 TownNpcHomes() 하나뿐(snap.town) — Bind(자리 채우기) · Validate(R069) · 파사드 HomeX/HomeY가 모두 여기를 지난다.
// ─────────────────────────────────────────────────────────────────────────────
#include <cstring>
#include <span>
#include <string>

#include "Database/GameData/Load/Bundles.h"
#include "Database/GameData/Load/Canon_Npc.h"
#include "Database/GameData/Load/JsonRead.h"
#include "Database/GameData/Load/Snapshot.h"
#include "Database/GameData/Load/Validate.h"
#include "Database/GameData/NpcTalk.h"
#include "Database/GameData/WorldData.h"

namespace
{
    // ── 마을 원본의 NPC 집(TownNpcHome) — 이 묶음에서 마을 표를 읽는 단 한 곳 ──────────────
    // 마을 묶음이 먼저 Bind되므로 만들고 있는 판의 snap.town.npcHomes(GameData/TownNpcHome.json — mapgen이 주인)를 읽는다.
    // Bind 중에도 불리므로 GD() · 파사드를 부르지 않고 인자로 받은 snap(만들고 있는 판)만 읽는다.
    // npc_id 오름차순 · 중복 없음(Bind_Town 구조 검사) — 이관 전 "anchors 순서에서 처음 맞는 집"과 같은 답.
    std::span<const TownMap::NpcHome> TownNpcHomes(const GameDataSnapshot& snap) noexcept
    {
        return snap.town.npcHomes;
    }

    // NPC 집 자리 — 이관 전 WorldData::HomeX/HomeY와 같은 답(마을 원본에서 id가 처음 맞는 집, 없으면 -1, -1).
    bool NpcHome(const GameDataSnapshot& snap, uint16_t id, int16_t& x, int16_t& y) noexcept
    {
        for (const auto& h : TownNpcHomes(snap))
            if (h.id == id)
            {
                x = h.x;
                y = h.y;
                return true;
            }
        x = -1;
        y = -1;
        return false;
    }
}

// ── 1) 파사드 ────────────────────────────────────────────────────────────────
namespace WorldData
{
    std::span<const NpcRow> Npcs() noexcept { return GD().npc.rows; }

    size_t NpcCount() noexcept { return GD().npc.rows.size(); }

    // 색인은 "id가 처음 나온 행" — 이관 전 constexpr 표의 순차 검색(첫 일치)과 같은 답. 색인 밖 id는 순차 검색.
    const NpcRow* FindNpc(uint16_t id) noexcept
    {
        const SnapNpc& n = GD().npc;
        if (id < SnapNpc::kIndex)
            return n.byId[id] >= 0 ? &n.rows[static_cast<size_t>(n.byId[id])] : nullptr;
        for (const auto& r : n.rows)
            if (r.id == id)
                return &r;
        return nullptr;
    }

    int16_t HomeX(uint16_t id) noexcept
    {
        int16_t x = -1, y = -1;
        NpcHome(GD(), id, x, y);
        return x;
    }

    int16_t HomeY(uint16_t id) noexcept
    {
        int16_t x = -1, y = -1;
        NpcHome(GD(), id, x, y);
        return y;
    }
}

namespace NpcTalk
{
    const Lines* Find(uint16_t npcId) noexcept
    {
        const SnapNpc& n = GD().npc;
        if (npcId < SnapNpc::kIndex)
            return n.linesById[npcId] >= 0 ? &n.lines[static_cast<size_t>(n.linesById[npcId])] : nullptr;
        for (const auto& l : n.lines)
            if (l.npcId == npcId)
                return &l;
        return nullptr;
    }
}

namespace GameDataLoad
{
    // ── 2) Bind ──────────────────────────────────────────────────────────────
    void BindNpc(LoadContext& ctx, GameDataSnapshot& snap)
    {
        SnapNpc& n = snap.npc;
        n.byId.fill(-1);
        n.linesById.fill(-1);

        // Npc — 자리(x, y)는 마을 원본에서
        if (const Json* rows = ctx.Rows("Npc"))
        {
            n.rows.reserve(rows->size());
            for (size_t i = 0; i < rows->size(); ++i)
            {
                RowReader r(ctx, "Npc", i, (*rows)[i]);
                WorldData::NpcRow row{};
                const char* codeName = nullptr;
                r.Int("id", row.id)
                 .StrOpt("code_name", codeName)
                 .Enum("kind", row.kind)
                 .Str("name", row.name)
                 .Int("facing", row.facing)
                 .Str("greeting", row.greeting)
                 .Int("wander", row.wander)
                 .Int("step_ms", row.stepMs)
                 .Color("color", row.color)
                 .StrOpt("role", row.role);
                r.Finish();
                NpcHome(snap, row.id, row.x, row.y);
                if (row.id < SnapNpc::kIndex && n.byId[row.id] < 0)
                    n.byId[row.id] = static_cast<int16_t>(n.rows.size());
                n.rows.push_back(row);
                n.codeName.push_back(codeName);
            }
        }

        // NpcLine — (npc_id, kind, seq) → Lines.talk[seq-1] · barks[seq-1]. 칸 수는 구조 상수(넘으면 로드 실패, 자르지 않는다).
        if (const Json* rows = ctx.Rows("NpcLine"))
        {
            for (size_t i = 0; i < rows->size(); ++i)
            {
                const size_t before = ctx.ErrorCount();
                RowReader r(ctx, "NpcLine", i, (*rows)[i]);
                uint16_t npcId = 0;
                const char* kind = nullptr;
                uint8_t seq = 0;
                const char* text = nullptr;
                r.Int("npc_id", npcId).Str("kind", kind).Int("seq", seq).Str("text", text);
                r.Finish();
                if (ctx.ErrorCount() != before)
                    continue;
                const bool talk = std::strcmp(kind, "talk") == 0;
                if (!talk && std::strcmp(kind, "bark") != 0)
                {
                    ctx.Error(r.Where("kind"), "talk(대화) · bark(혼잣말) 중 하나여야 한다('" + std::string(kind) + "')");
                    continue;
                }
                const int max = talk ? NpcTalk::kMaxTalk : NpcTalk::kMaxBarks;
                if (seq < 1 || seq > max)
                {
                    ctx.Error(r.Where("seq"), std::string(talk ? "talk" : "bark") + " seq는 1~" + std::to_string(max) +
                        "(구조 상수 NpcTalk::" + (talk ? "kMaxTalk" : "kMaxBarks") + ") — " + std::to_string(seq));
                    continue;
                }
                // NPC마다 Lines 하나(npc_id가 처음 나온 순서)
                NpcTalk::Lines* lines = nullptr;
                for (auto& l : n.lines)
                    if (l.npcId == npcId) { lines = &l; break; }
                if (lines == nullptr)
                {
                    if (npcId < SnapNpc::kIndex)
                        n.linesById[npcId] = static_cast<int16_t>(n.lines.size());
                    NpcTalk::Lines blank{};
                    blank.npcId = npcId;
                    n.lines.push_back(blank);
                    lines = &n.lines.back();
                }
                const char*& slot = talk ? lines->talk[static_cast<size_t>(seq - 1)] : lines->barks[static_cast<size_t>(seq - 1)];
                if (slot != nullptr)
                {
                    ctx.Error(r.Where("seq"), "npc_id " + std::to_string(npcId) + " " + kind + " seq " + std::to_string(seq) + "가 겹친다");
                    continue;
                }
                slot = text;
            }
            // seq는 종류마다 1부터 연속 — 빈 칸 뒤에 줄이 있으면 오류(이관 전 표에는 그런 행이 없다: 덤프가 확인)
            for (const auto& l : n.lines)
            {
                auto gap = [&](const char* const* arr, int count, const char* kind)
                {
                    bool ended = false;
                    for (int s = 0; s < count; ++s)
                    {
                        if (arr[s] == nullptr) { ended = true; continue; }
                        if (ended)
                        {
                            ctx.Error("NpcLine npc_id " + std::to_string(l.npcId), std::string(kind) + " seq가 1부터 연속이 아니다(" + std::to_string(s + 1) + " 앞이 비었다)");
                            return;
                        }
                    }
                };
                gap(l.talk, NpcTalk::kMaxTalk, "talk");
                gap(l.barks, NpcTalk::kMaxBarks, "bark");
            }
        }
    }

    // ── 3) Validate — 예전 WorldData::Detail::ValidateNpcs()(static_assert R071)의 규칙 R066~R070 ──
    void ValidateNpc(ValidateContext& v, const GameDataSnapshot& snap)
    {
        constexpr uint16_t kIdLimit = 64;   // 클라 그림 표(64칸) · NPC_LIST 페이지 — id 1~63
        constexpr size_t kMaxNpcs = 48;
        const auto& rows = snap.npc.rows;
        for (size_t i = 0; i < rows.size(); ++i)
        {
            const WorldData::NpcRow& r = rows[i];
            // R066: 집(마을 원본 TownNpcHome — 로더가 x, y에 채웠다)은 마을 안의 걸을 수 있는 칸. 마을 파사드로 본다.
            if (!WorldData::IsInTown(r.x, r.y) || WorldData::IsBlocked(r.x, r.y))
                v.Fail("R066", v.Where("Npc", i, "id"),
                    "NPC " + std::to_string(r.id) + "의 집(TownNpcHome " + std::to_string(r.x) + ", " + std::to_string(r.y) + ")이 " +
                    (WorldData::IsInTown(r.x, r.y) ? "막힌 칸이다" : (r.x < 0 ? "마을 원본에 없다" : "마을 밖이다")) + " — 집은 마을 안 걸을 수 있는 칸");
            if (r.id == 0 || r.id >= kIdLimit)
                v.Fail("R067", v.Where("Npc", i, "id"), "id " + std::to_string(r.id) + " — 1~63(클라 그림 표 64칸 · NPC_LIST)");
            for (size_t j = 0; j < i; ++j)
                if (rows[j].id == r.id)
                {
                    v.Fail("R068", v.Where("Npc", i, "id"), "id " + std::to_string(r.id) + "가 겹친다(" + v.Where("Npc", j) + ")");
                    break;
                }
        }
        // R069: 마을 원본의 NPC 집은 모두 Npc 탭에 있는 NPC여야 한다(오타로 빠진 NPC가 없게)
        const auto homes = TownNpcHomes(snap);
        for (size_t k = 0; k < homes.size(); ++k)
        {
            bool found = false;
            for (const auto& r : rows)
                found = found || r.id == homes[k].id;
            if (!found)
                v.Fail("R069", v.Where("TownNpcHome", k, "npc_id"), "마을 원본에 집이 있는 NPC " + std::to_string(homes[k].id) + "가 Npc 탭에 없다");
        }
        // R070: 최대 48명
        if (rows.size() > kMaxNpcs)
            v.Fail("R070", v.Where("Npc", kMaxNpcs, "id"), "NPC " + std::to_string(rows.size()) + "명 > 최대 " + std::to_string(kMaxNpcs));
    }

    // ── 4) Dump ──────────────────────────────────────────────────────────────
    void DumpNpc(Canon::Book& book, const GameDataSnapshot& snap)
    {
        const SnapNpc& n = snap.npc;
        Canon::WriteNpc(book, n.rows, [&n](uint32_t id) -> const char*
        {
            for (size_t i = 0; i < n.rows.size(); ++i)
                if (n.rows[i].id == id) return n.codeName[i];
            return nullptr;
        });
        Canon::WriteNpcLine(book, n.lines);
    }
}
