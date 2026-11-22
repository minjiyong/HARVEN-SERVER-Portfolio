#include "pch.h"
// ─────────────────────────────────────────────────────────────────────────────
// 기술성장 묶음 — 탭 GrowthTree · GrowthVariant · GrowthConst → 스냅샷 → 파사드 SkillGrowth::*.
// 모양은 Bind_Monster.cpp(모범 표)와 같다.
//   1) 파사드 — FindTree · TreeCount · TierLevel · DamagePerLevel. 판정(ApplyDef · ApplyRow · FindMod …)은 헤더 코드 그대로.
//   2) Bind    — GrowthVariant(갈래 값) → GrowthTree(갈래 id 넷)를 id로 풀어 이관 전과 같은 Tree(v[2][2] 값 복사)로 다시 조립
//   3) Validate — 옮겨 온 규칙(RULES.md R011~R019) — 위치 + R번호. R020(CostTo) · R022(State 크기)는 헤더 static_assert(구조)
//   4) Dump    — Canon_Growth.h(datatool export와 같은 바이트). 조립한 Tree가 가리킨 갈래 행과 같은지 확인하며 쓴다
// ─────────────────────────────────────────────────────────────────────────────
#include <string>

#include "Database/GameData/Load/Bundles.h"
#include "Database/GameData/Load/Canon_Growth.h"
#include "Database/GameData/Load/JsonRead.h"
#include "Database/GameData/Load/Snapshot.h"
#include "Database/GameData/Load/Validate.h"
#include "Database/GameData/SkillData.h"
#include "Database/GameData/SkillGrowth.h"

// ── 1) 파사드 ────────────────────────────────────────────────────────────────
namespace SkillGrowth
{
    uint8_t TierLevel(int tier) noexcept { return GD().growth.tierLevel[tier]; }

    uint32_t DamagePerLevel() noexcept { return GD().growth.damagePerLevel; }

    int TreeCount() noexcept { return static_cast<int>(GD().growth.trees.size()); }

    const Tree* FindTree(uint16_t id) noexcept
    {
        // 이관 전: kTrees를 순서대로 돌아 처음 맞는 행. 색인은 처음 나온 행을 가리킨다(skill_id 중복은 R018로 로드 실패)
        const SnapGrowth& g = GD().growth;
        if (id >= g.treeIndex.size())
            return nullptr;
        const int32_t i = g.treeIndex[id];
        return i >= 0 ? &g.trees[static_cast<size_t>(i)] : nullptr;
    }
}

namespace
{
    const char* const kRefCol[4] = { "a1", "a2", "b1", "b2" };

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
    void BindGrowth(LoadContext& ctx, GameDataSnapshot& snap)
    {
        SnapGrowth& g = snap.growth;

        // GrowthVariant
        if (const Json* rows = ctx.Rows("GrowthVariant"))
        {
            g.variants.reserve(rows->size());
            for (size_t i = 0; i < rows->size(); ++i)
            {
                RowReader r(ctx, "GrowthVariant", i, (*rows)[i]);
                SnapGrowth::VariantRow vr;
                r.Int("id", vr.id)
                 .Enum("mod", vr.v.mod)
                 .Int("value", vr.v.value)
                 .Enum("status", vr.v.status)
                 .Color("color", vr.v.color)
                 .Str("name", vr.v.name)
                 .Str("desc", vr.v.desc);
                r.Finish();
                g.variants.push_back(vr);
            }
            uint16_t maxId = 0;
            for (const auto& vr : g.variants)
                if (vr.id > maxId) maxId = vr.id;
            g.variantIndex.assign(static_cast<size_t>(maxId) + 1, -1);
            for (size_t i = 0; i < g.variants.size(); ++i)
            {
                int32_t& at = g.variantIndex[g.variants[i].id];
                if (at >= 0)
                    ctx.Error(ctx.Where("GrowthVariant", i, "id"), "id " + std::to_string(g.variants[i].id) + "가 겹친다(" + ctx.Where("GrowthVariant", static_cast<size_t>(at)) + "과)");
                else
                    at = static_cast<int32_t>(i);
            }
        }

        // GrowthTree — 갈래 id → 값 복사(이관 전 Tree.v[2][2]와 같은 모양)
        if (const Json* rows = ctx.Rows("GrowthTree"))
        {
            g.trees.reserve(rows->size());
            g.treeRefs.reserve(rows->size());
            for (size_t i = 0; i < rows->size(); ++i)
            {
                RowReader r(ctx, "GrowthTree", i, (*rows)[i]);
                SkillGrowth::Tree t{};
                std::array<uint16_t, 4> refs{};
                r.Int("skill_id", t.skillId)
                 .Int("a1", refs[0])
                 .Int("a2", refs[1])
                 .Int("b1", refs[2])
                 .Int("b2", refs[3]);
                r.Finish();
                if (r.Ok())
                    for (int q = 0; q < 4; ++q)
                    {
                        const uint16_t ref = refs[static_cast<size_t>(q)];
                        const int32_t vi = ref < g.variantIndex.size() ? g.variantIndex[ref] : -1;
                        if (vi < 0)
                            ctx.Error(ctx.Where("GrowthTree", i, kRefCol[q]), "GrowthVariant에 없는 갈래 id " + std::to_string(ref));
                        else
                            t.v[q / 2][q % 2] = g.variants[static_cast<size_t>(vi)].v;
                    }
                g.trees.push_back(t);
                g.treeRefs.push_back(refs);
            }
            uint16_t maxId = 0;
            for (const auto& t : g.trees)
                if (t.skillId > maxId) maxId = t.skillId;
            g.treeIndex.assign(static_cast<size_t>(maxId) + 1, -1);
            for (size_t i = 0; i < g.trees.size(); ++i)
                if (int32_t& at = g.treeIndex[g.trees[i].skillId]; at < 0)
                    at = static_cast<int32_t>(i);   // 처음 나온 행(중복은 R018)
        }

        // GrowthConst — 한 행
        if (const Json* rows = ctx.Rows("GrowthConst"))
        {
            if (rows->size() != 1)
                ctx.Error("GrowthConst", "한 행이어야 한다(" + std::to_string(rows->size()) + "행)");
            for (size_t i = 0; i < rows->size(); ++i)
            {
                RowReader r(ctx, "GrowthConst", i, (*rows)[i]);
                uint8_t t1 = 0, t2 = 0;
                uint32_t dpl = 0;
                r.Int("tier_level_1", t1).Int("tier_level_2", t2).Int("damage_per_level", dpl);
                r.Finish();
                if (i == 0)
                {
                    g.tierLevel[0] = t1;
                    g.tierLevel[1] = t2;
                    g.damagePerLevel = dpl;
                }
            }
        }
    }

    // ── 3) Validate — 예전 SkillGrowth::Detail::Validate()(static_assert)의 규칙 ──
    void ValidateGrowth(ValidateContext& v, const GameDataSnapshot& snap)
    {
        using namespace SkillGrowth;
        const SnapGrowth& g = snap.growth;
        std::vector<bool> reported(g.variants.size(), false);   // R014 · R015는 갈래 행마다 한 번(여러 기술이 같은 행을 가리킨다)

        for (size_t i = 0; i < g.trees.size(); ++i)
        {
            const Tree& t = g.trees[i];
            // R011 — skill_id < kIdSpan(서버 · 클라 배열 크기)
            if (t.skillId >= kIdSpan)
                v.Fail("R011", v.Where("GrowthTree", i, "skill_id"), "skill_id " + std::to_string(t.skillId) + " ≥ kIdSpan(" + std::to_string(kIdSpan) + ") — 서버 · 클라 배열 크기");
            // R012 — 기술 표(SkillAction · Skill)에 있다
            const SkillDef* d = SkillDefTable::Find(t.skillId);
            const SkillRow* r = SkillTable::Find(t.skillId);
            if (d == nullptr || r == nullptr)
                v.Fail("R012", v.Where("GrowthTree", i, "skill_id"), "기술 " + std::to_string(t.skillId) + "이 " + (d == nullptr ? "SkillAction" : "Skill") + "에 없다");
            // R013 — 자라는 칸은 QWERASDF뿐
            else if (d->key == SkillKey::C || d->key == SkillKey::V || d->key == SkillKey::Space || d->key == SkillKey::Z)
                v.Fail("R013", v.Where("GrowthTree", i, "skill_id"), "기술 " + std::to_string(t.skillId) + "의 칸 " + EnumName(d->key) + " — 갈래는 QWERASDF 칸의 기술만");

            for (int tier = 0; tier < 2; ++tier)
                for (int k = 0; k < 2; ++k)
                {
                    const Variant& vv = t.v[tier][k];
                    const uint16_t ref = g.treeRefs[i][static_cast<size_t>(tier * 2 + k)];
                    const int32_t vi = ref < g.variantIndex.size() ? g.variantIndex[ref] : -1;
                    const bool once = vi >= 0 && !reported[static_cast<size_t>(vi)];
                    const std::string at = vi >= 0 ? v.Where("GrowthVariant", static_cast<size_t>(vi)) : v.Where("GrowthTree", i, kRefCol[tier * 2 + k]);
                    bool bad = false;
                    // R014 — 쓰인 갈래: mod ≠ None, 이름 · 설명이 비어 있지 않다
                    if (vv.mod == Mod::None || vv.name == nullptr || vv.name[0] == 0 || vv.desc == nullptr || vv.desc[0] == 0)
                    {
                        bad = true;
                        if (once)
                        {
                            const char* col = vv.mod == Mod::None ? "mod" : (vv.name == nullptr || vv.name[0] == 0) ? "name" : "desc";
                            v.Fail("R014", at + " " + col, std::string("갈래 ") + std::to_string(ref) + " — " +
                                (vv.mod == Mod::None ? "mod가 None" : "이름 · 설명이 비었다") + "(GrowthTree " + kRefCol[tier * 2 + k] + "가 가리킨다)");
                        }
                    }
                    // R015 — Element면 상태이상이 있다
                    if (vv.mod == Mod::Element && vv.status == StatusId::None)
                    {
                        bad = true;
                        if (once)
                            v.Fail("R015", at + " status", "갈래 " + std::to_string(ref) + " — Element(속성 부여)는 상태이상이 있어야 한다(None)");
                    }
                    if (bad && vi >= 0)
                        reported[static_cast<size_t>(vi)] = true;
                    // R016 — 같은 단의 두 갈래는 달라야 한다(mod · status)
                    if (k == 1 && t.v[tier][0].mod == vv.mod && t.v[tier][0].status == vv.status)
                        v.Fail("R016", v.Where("GrowthTree", i, kRefCol[tier * 2 + 1]),
                            std::string(TierName(tier)) + "의 두 갈래가 같은 효과(" + EnumName(vv.mod) + " · " + EnumName(vv.status) + ") — 서로 달라야 한다");
                }

            // R017 — 모든 조합(9가지)에서 ApplyDef 뒤 타격 수 ≤ 8(메아리 · 세 줄기). ApplyDef 자체가 8칸을 넘지 않게 막는다 — 그 보장의 확인
            if (d != nullptr && r != nullptr)
                for (uint8_t a = 0; a <= 2; ++a)
                    for (uint8_t b = 0; b <= 2; ++b)
                    {
                        State st;
                        st.level = kMaxLevel;
                        st.pick[0] = a;
                        st.pick[1] = b;
                        const SkillDef e = ApplyDef(*d, *r, st);
                        if (e.hitCount > 8)
                            v.Fail("R017", v.Where("GrowthTree", i, "skill_id"), "갈래 (" + std::to_string(a) + ", " + std::to_string(b) + ")에서 타격 " + std::to_string(e.hitCount) + "개 > 8");
                    }

            // R018 — skill_id 중복 없음(FindTree는 처음 맞는 행)
            for (size_t j = i + 1; j < g.trees.size(); ++j)
                if (g.trees[j].skillId == t.skillId)
                    v.Fail("R018", v.Where("GrowthTree", j, "skill_id"), "skill_id " + std::to_string(t.skillId) + "가 겹친다(" + v.Where("GrowthTree", i) + ")");
        }

        // R019 — 세 직업 × QWERASDF 칸의 기본 기술이 모두 갈래를 가진다(기본 배치 = SkillKeyMap)
        for (int c = 0; c < SnapSkill::kKeyMapClasses; ++c)
            for (int k = static_cast<int>(SkillKey::Q); k <= static_cast<int>(SkillKey::F); ++k)
            {
                const uint16_t id = SkillDefTable::Default(static_cast<ClassId>(c), static_cast<SkillKey>(k));
                if (FindTree(id) == nullptr)
                {
                    const int32_t at = snap.skill.keyMapRow[c][k];
                    v.Fail("R019", at >= 0 ? v.Where("SkillKeyMap", static_cast<size_t>(at), "skill_id") : std::string("SkillKeyMap"),
                        "(" + EnumName(static_cast<ClassId>(c)) + ", " + EnumName(static_cast<SkillKey>(k)) + ")의 기본 기술 " + std::to_string(id) + "에 갈래(GrowthTree 행)가 없다");
                }
            }
    }

    // ── 4) Dump ──────────────────────────────────────────────────────────────
    void DumpGrowth(Canon::Book& book, const GameDataSnapshot& snap)
    {
        const SnapGrowth& g = snap.growth;
        std::vector<Canon::GrowthVariantV> vars;
        vars.reserve(g.variants.size());
        for (const auto& vr : g.variants)
            vars.push_back({ vr.id, vr.v });
        std::vector<Canon::GrowthTreeV> trees;
        trees.reserve(g.trees.size());
        for (size_t i = 0; i < g.trees.size(); ++i)
        {
            Canon::GrowthTreeV tv;
            tv.skillId = g.trees[i].skillId;
            for (int q = 0; q < 4; ++q)
            {
                const uint16_t ref = g.treeRefs[i][static_cast<size_t>(q)];
                tv.v[q / 2][q % 2] = ref;
                // 메모리의 Tree(조립한 값)가 가리킨 갈래 행과 같아야 한다 — 3자 일치가 조립까지 덮는다
                const int32_t vi = ref < g.variantIndex.size() ? g.variantIndex[ref] : -1;
                if (vi < 0 || !Canon::SameVariant(g.trees[i].v[q / 2][q % 2], g.variants[static_cast<size_t>(vi)].v))
                    throw Canon::Error("GrowthTree " + std::to_string(tv.skillId) + " " + kRefCol[q] + ": 조립한 갈래가 GrowthVariant " + std::to_string(ref) + "와 다르다");
            }
            trees.push_back(tv);
        }
        Canon::GrowthConstV c;
        c.tierLevel1 = g.tierLevel[0];
        c.tierLevel2 = g.tierLevel[1];
        c.damagePerLevel = g.damagePerLevel;
        Canon::WriteGrowth(book, vars, trees, c);
    }
}
