#pragma once
// 정본 표 쓰기 — 퀘스트 묶음(탭 Quest · QuestObjective · Poi · Chapter).
// 목표 objs[kMaxObjs]는 자식 탭 QuestObjective(quest_id, seq 1~)로 — kind None(빈 칸)은 뒤쪽에만, 표에 적지 않는다.
// 보상 아이템 item[3]은 직업별 열(reward_item_knight · _engraver · _hunter).
#include <span>

#include "Database/GameData/Load/Canon.h"
#include "Database/GameData/QuestData.h"

namespace Canon
{
    inline void WriteQuest(Book& book, std::span<const QuestData::QuestRow> rows)
    {
        Table& t = book.Get("Quest");
        Table& o = book.Get("QuestObjective");
        for (const auto& q : rows)
        {
            Row w;
            w.Put("id", q.id).Put("kind", q.kind).Put("chapter", q.chapter).Put("name", q.name)
             .Put("giver", q.giver).Put("turn_in", q.turnIn).Put("min_level", q.minLevel).Put("prereq", q.prereq)
             .Mask<ClassId>("class_mask", q.classMask)
             .Put("summary", q.summary).Put("offer", q.offer).Put("during", q.during).Put("done", q.done)
             .Put("reward_exp", q.reward.exp).Put("reward_exp_pct", q.reward.expPct).Put("reward_adena", q.reward.adena)
             .Put("reward_item_knight", q.reward.item[0]).Put("reward_item_engraver", q.reward.item[1]).Put("reward_item_hunter", q.reward.item[2])
             .Put("reward_count", q.reward.count).Put("reward_rarity", q.reward.rarity)
             .Put("cooldown_min", q.cooldownMin);
            t.Add(w);

            bool ended = false;
            uint8_t seq = 0;
            for (const auto& ob : q.objs)
            {
                if (ob.kind == QuestData::ObjKind::None)
                {
                    const QuestData::Objective z{};
                    if (ob.target != z.target || ob.count != z.count || ob.zone != z.zone || ob.chance != z.chance || ob.label != z.label || ob.icon != z.icon)
                        throw Error("QuestObjective " + std::to_string(q.id) + ": 빈 목표(None)에 값이 있다");
                    ended = true;
                    continue;
                }
                if (ended) throw Error("QuestObjective " + std::to_string(q.id) + ": 빈 목표 뒤에 목표가 있다");
                Row ow;
                ow.Put("quest_id", q.id).Put("seq", ++seq).Put("kind", ob.kind).Put("target", ob.target).Put("count", ob.count)
                  .Put("zone", ob.zone).Put("chance", ob.chance).StrOpt("label", ob.label).Put("icon", ob.icon);
                o.Add(ow);
            }
        }
    }

    inline void WritePoi(Book& book, std::span<const QuestData::Poi> rows)
    {
        Table& t = book.Get("Poi");
        for (const auto& p : rows)
        {
            Row w;
            w.Put("id", p.id).Put("name", p.name).Put("x", p.x).Put("y", p.y).Put("radius", p.radius).Put("flavor", p.flavor);
            t.Add(w);
        }
    }

    inline void WriteChapter(Book& book, std::span<const QuestData::Chapter> rows)
    {
        Table& t = book.Get("Chapter");
        uint8_t id = 0;
        for (const auto& c : rows)
        {
            Row w;
            w.Put("id", id++).Put("title", c.title).Put("place", c.place).Put("line", c.line);
            t.Add(w);
        }
    }
}
