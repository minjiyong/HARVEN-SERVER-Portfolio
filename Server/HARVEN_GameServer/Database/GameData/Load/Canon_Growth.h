#pragma once
// 정본 표 쓰기 — 기술성장 묶음(탭 GrowthTree · GrowthVariant · GrowthConst).
//
// 이관 전 kTrees는 갈래 값(Variant)을 행마다 복사해 들고 있었다(공용 프리셋 kWider · kHeavy …).
// 표에서는 갈래를 GrowthVariant(id)로 한 번만 적고 GrowthTree가 id로 가리킨다.
// 이관 전 덤프는 값이 완전히 같은 갈래를 하나로 묶고, 처음 나온 순서(기술 순 · a1 a2 b1 b2)로 id 1부터 매긴다.
// 서버는 로드할 때 id로 값을 복사해 Tree를 예전과 같은 모양으로 만든다 → ApplyDef가 보는 값이 같다.
#include <algorithm>
#include <cstring>
#include <span>
#include <vector>

#include "Database/GameData/Load/Canon.h"
#include "Database/GameData/SkillGrowth.h"

namespace Canon
{
    struct GrowthVariantV { uint16_t id = 0; SkillGrowth::Variant v{}; };
    struct GrowthTreeV { uint16_t skillId = 0; uint16_t v[2][2] = {}; };   // [갈래 Ⅰ · Ⅱ][첫째 · 둘째] → GrowthVariant id
    struct GrowthConstV { uint8_t tierLevel1 = 0, tierLevel2 = 0; uint32_t damagePerLevel = 0; };

    inline bool SameVariant(const SkillGrowth::Variant& a, const SkillGrowth::Variant& b)
    {
        return a.mod == b.mod && a.value == b.value && a.status == b.status && a.color == b.color
            && std::strcmp(a.name, b.name) == 0 && std::strcmp(a.desc, b.desc) == 0;
    }

    // 이관 전 Tree 배열 → 정규화한 두 표
    // 정본 행 순서 = skill_id 오름차순(이관 전 kTrees는 직업별 순서 — FindTree는 id로만 찾는다). 갈래 id는 이 순서로 처음 나온 차례.
    inline void NormalizeGrowth(std::span<const SkillGrowth::Tree> treesIn, std::vector<GrowthVariantV>& vars, std::vector<GrowthTreeV>& out)
    {
        std::vector<const SkillGrowth::Tree*> trees;
        for (const auto& t : treesIn) trees.push_back(&t);
        std::stable_sort(trees.begin(), trees.end(), [](const SkillGrowth::Tree* a, const SkillGrowth::Tree* b) { return a->skillId < b->skillId; });
        for (const SkillGrowth::Tree* pt : trees)
        {
            const SkillGrowth::Tree& t = *pt;
            GrowthTreeV tv;
            tv.skillId = t.skillId;
            for (int i = 0; i < 2; ++i)
                for (int j = 0; j < 2; ++j)
                {
                    uint16_t id = 0;
                    for (const auto& gv : vars)
                        if (SameVariant(gv.v, t.v[i][j])) { id = gv.id; break; }
                    if (id == 0)
                    {
                        id = static_cast<uint16_t>(vars.size() + 1);
                        vars.push_back({ id, t.v[i][j] });
                    }
                    tv.v[i][j] = id;
                }
            out.push_back(tv);
        }
    }

    inline void WriteGrowth(Book& book, std::span<const GrowthVariantV> vars, std::span<const GrowthTreeV> trees, const GrowthConstV& c)
    {
        Table& tv = book.Get("GrowthVariant");
        for (const auto& g : vars)
        {
            Row w;
            w.Put("id", g.id).Put("mod", g.v.mod).Put("value", g.v.value).Put("status", g.v.status)
             .Color("color", g.v.color).Put("name", g.v.name).Put("desc", g.v.desc);
            tv.Add(w);
        }
        Table& tt = book.Get("GrowthTree");
        for (const auto& t : trees)   // NormalizeGrowth · 스냅샷 모두 skill_id 순
        {
            Row w;
            w.Put("skill_id", t.skillId)
             .Put("a1", t.v[0][0]).Put("a2", t.v[0][1]).Put("b1", t.v[1][0]).Put("b2", t.v[1][1]);
            tt.Add(w);
        }
        Table& tc = book.Get("GrowthConst");
        Row w;
        w.Put("tier_level_1", c.tierLevel1).Put("tier_level_2", c.tierLevel2).Put("damage_per_level", c.damagePerLevel);
        tc.Add(w);
    }
}
