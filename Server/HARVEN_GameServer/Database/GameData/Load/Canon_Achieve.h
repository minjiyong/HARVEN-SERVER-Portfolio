#pragma once
// 정본 표 쓰기 — 개인 업적 묶음(탭 Achievement).
// 열 순서 · 형 = Tools/DataTool/schema/achieve.yaml(datatool이 메모리 덤프의 _schema.json과 대조한다).
#include <span>

#include "Database/GameData/AchieveRules.h"
#include "Database/GameData/Load/Canon.h"

namespace Canon
{
    inline void WriteAchievements(Book& book, std::span<const AchieveRule::Row> rows)
    {
        Table& t = book.Get("Achievement");
        for (const auto& r : rows)
        {
            Row w;
            w.Put("id", r.id).Put("category", r.category).Put("group", r.group).Put("step", r.step).Put("name", r.name)
             .Put("desc", r.desc).Put("kind", r.kind).Put("param", r.param).Put("target", r.target).Put("points", r.points)
             .Put("reward_item", r.rewardItem).Put("reward_count", r.rewardCount).Put("reward_adena", r.rewardAdena);
            t.Add(w);
        }
    }
}
