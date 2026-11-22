#pragma once
// 정본 표 쓰기 — NPC대사 묶음(탭 Npc · NpcLine).
// NPC의 집 자리(x, y)는 마을 원본(mapgen anchors.txt npc → 마을 표 TownNpcHome)이 주인이라 Npc 탭에 두지 않는다.
// NpcLine: kind = "talk"(대화, 최대 NpcTalk::kMaxTalk) · "bark"(혼잣말, 최대 kMaxBarks), seq는 1부터.
#include <functional>
#include <span>

#include "Database/GameData/Load/Canon.h"
#include "Database/GameData/NpcTalk.h"
#include "Database/GameData/WorldData.h"

namespace Canon
{
    inline void WriteNpc(Book& book, std::span<const WorldData::NpcRow> rows, const std::function<const char*(uint32_t)>& codeName)
    {
        Table& t = book.Get("Npc");
        for (const auto& r : rows)
        {
            Row w;
            w.Put("id", r.id).StrOpt("code_name", codeName(r.id)).Put("kind", r.kind).Put("name", r.name)
             .Put("facing", r.facing).Put("greeting", r.greeting).Put("wander", r.wander).Put("step_ms", r.stepMs)
             .Color("color", r.color).StrOpt("role", r.role);
            t.Add(w);
        }
    }

    inline void WriteNpcLine(Book& book, std::span<const NpcTalk::Lines> lines)
    {
        Table& t = book.Get("NpcLine");
        for (const auto& l : lines)
        {
            auto emit = [&](const char* kind, const char* const* arr, int n)
            {
                bool ended = false;
                uint8_t seq = 0;
                for (int i = 0; i < n; ++i)
                {
                    if (arr[i] == nullptr) { ended = true; continue; }
                    if (ended)
                        throw Error("NpcLine " + std::to_string(l.npcId) + " " + kind + ": 빈 칸 뒤에 줄이 있다");
                    Row w;
                    w.Put("npc_id", l.npcId).Str("kind", kind).Put("seq", ++seq).Put("text", arr[i]);
                    t.Add(w);
                }
            };
            emit("talk", l.talk, NpcTalk::kMaxTalk);
            emit("bark", l.barks, NpcTalk::kMaxBarks);
        }
    }
}
