#include "pch.h"
// ─────────────────────────────────────────────────────────────────────────────
// 경제 묶음 — 탭 LootConst(한 행) · TierLoot · EliteGear · BossLoot · CraftRecipe · Dismantle → 스냅샷 → 파사드 EconRule::*.
// 아이템 묶음 다음에 묶는다(Item 행으로 라인업 색인을 만든다). 모양은 Bind_Party.cpp와 같다.
//   1) 파사드 — 시트 값 · 라인업 색인을 읽는 함수(구조 상수 · 순수 함수는 헤더 EconRules.h)
//   2) Bind    — 열마다 RowReader + 색인(단계 · 보스 · 레시피 · 등급 · 라인업)
//   3) Validate — R124~R135(장비 단 · 경로 · 레시피 · 드롭 표 · 보스 · 상점 · 퀘스트 · 단계 · 정예 · 재료 · 매입가 · 귀속)
//   4) Dump    — Canon_Econ.h(datatool export와 같은 바이트)
// 설계: Shared/Document/ECONOMY.md
// ─────────────────────────────────────────────────────────────────────────────
#include <algorithm>
#include <string>

#include "Database/GameData/EconRules.h"
#include "Database/GameData/ItemRules.h"
#include "Database/GameData/Item/ItemTableData.h"
#include "Database/GameData/Load/Bundles.h"
#include "Database/GameData/Load/Canon_Econ.h"
#include "Database/GameData/Load/JsonRead.h"
#include "Database/GameData/Load/Snapshot.h"
#include "Database/GameData/Load/Validate.h"

// ── 1) 파사드 ────────────────────────────────────────────────────────────────
namespace EconRule
{
    uint16_t RarePity() noexcept { return GD().econ.c.rarePity; }
    uint16_t HeroicPity() noexcept { return GD().econ.c.heroicPity; }
    uint8_t  ClassSwapPercent() noexcept { return GD().econ.c.classSwapPercent; }
    uint8_t  LevelGap() noexcept { return GD().econ.c.levelGap; }
    uint16_t ElitePermille() noexcept { return GD().econ.c.elitePermille; }
    uint8_t  EliteMinTier() noexcept { return GD().econ.c.eliteMinTier; }
    uint16_t EliteHpPercent() noexcept { return GD().econ.c.eliteHpPercent; }
    uint16_t EliteAttackPercent() noexcept { return GD().econ.c.eliteAttackPercent; }
    uint8_t  EliteLevelBonus() noexcept { return GD().econ.c.eliteLevelBonus; }
    uint16_t EliteExpPercent() noexcept { return GD().econ.c.eliteExpPercent; }
    uint16_t EliteAdenaPercent() noexcept { return GD().econ.c.eliteAdenaPercent; }
    uint8_t  EliteSharePercent() noexcept { return GD().econ.c.eliteSharePercent; }
    uint8_t  BossSharePercent() noexcept { return GD().econ.c.bossSharePercent; }
    ItemTableId ShardItem() noexcept { return GD().econ.c.shardItem; }
    uint8_t  TradedDismantlePercent() noexcept { return GD().econ.c.tradedDismantlePercent; }

    const TierLootRow& Tier(int tier) noexcept
    {
        const int t = tier < 1 ? 1 : (tier > kTierCount ? kTierCount : tier);
        return GD().econ.tiers[static_cast<size_t>(t)];
    }

    std::span<const EliteGearRow> EliteGear(int tier) noexcept
    {
        if (tier < 1 || tier > kTierCount)
            return {};
        const SnapEcon& s = GD().econ;
        const uint32_t a = s.eliteStart[static_cast<size_t>(tier)], b = s.eliteStart[static_cast<size_t>(tier) + 1];
        if (a > b || b > s.elite.size())
            return {};
        return std::span<const EliteGearRow>(s.elite).subspan(a, b - a);
    }

    std::span<const BossLootRow> BossLoot(uint16_t species) noexcept
    {
        const SnapEcon& s = GD().econ;
        if (static_cast<size_t>(species) + 1 >= s.bossStart.size())
            return {};
        const uint32_t a = s.bossStart[species], b = s.bossStart[static_cast<size_t>(species) + 1];
        if (a > b || b > s.boss.size())
            return {};
        return std::span<const BossLootRow>(s.boss).subspan(a, b - a);
    }

    const CraftRecipeRow* RecipeAt(ItemGrade grade, uint8_t rank, CraftPart part) noexcept
    {
        const size_t g = static_cast<size_t>(grade), p = static_cast<size_t>(part);
        if (g >= static_cast<size_t>(kItemGradeCount) || rank < 1 || rank > kRankCount || p >= static_cast<size_t>(CraftPart::Count))
            return nullptr;
        const SnapEcon& s = GD().econ;
        const uint16_t k = s.recipeAt[g][rank][p];
        return k != 0 && k <= s.recipes.size() ? &s.recipes[k - 1u] : nullptr;
    }

    const CraftRecipeRow* RecipeFor(const ItemTableRow& row) noexcept
    {
        if (row.type != ItemType::eEquipment || !HasRoute(row, ItemRoute::Craft))
            return nullptr;
        return RecipeAt(row.grade, row.rank, PartOf(row));
    }

    const DismantleRow* DismantleFor(ItemGrade grade) noexcept
    {
        const SnapEcon& s = GD().econ;
        const size_t g = static_cast<size_t>(grade);
        if (g >= s.dismantleRows)
            return nullptr;
        const DismantleRow& d = s.dismantle[g];
        return d.shardCount == 0 && d.itemCount == 0 ? nullptr : &d;   // 모두 0 = 분해 불가(신화)
    }

    std::span<const ItemTableId> Items(ItemGrade grade, uint8_t rank, ItemRoute route) noexcept
    {
        const int ri = RouteIndex(route);
        if (ri < 0 || static_cast<int>(grade) >= kItemGradeCount || rank > kRankCount)
            return {};
        const SnapEcon& s = GD().econ;
        const size_t c = SnapEcon::Cell(grade, rank, ri);
        return std::span<const ItemTableId>(s.lineupPool).subspan(s.lineupStart[c], s.lineupStart[c + 1] - s.lineupStart[c]);
    }

    std::span<const ItemTableId> ClassWeapons(ClassId cls, ItemGrade grade, uint8_t rank, ItemRoute route) noexcept
    {
        const int ri = RouteIndex(route);
        if (ri < 0 || static_cast<int>(grade) >= kItemGradeCount || rank > kRankCount || static_cast<size_t>(cls) >= SnapEcon::kClasses)
            return {};
        const SnapEcon& s = GD().econ;
        const size_t c = static_cast<size_t>(cls) * SnapEcon::kCells + SnapEcon::Cell(grade, rank, ri);
        return std::span<const ItemTableId>(s.weaponPool).subspan(s.weaponStart[c], s.weaponStart[c + 1] - s.weaponStart[c]);
    }
}

namespace GameDataLoad
{
    namespace
    {
        std::string N(uint64_t v) { return std::to_string(v); }

        const char* GradeName(ItemGrade g) noexcept
        {
            static constexpr const char* kNames[] = { "일반", "고급", "희귀", "영웅", "전설", "신화" };
            const size_t i = static_cast<size_t>(g);
            return i < std::size(kNames) ? kNames[i] : "?";
        }
        const char* RankName(uint8_t r) noexcept
        {
            static constexpr const char* kNames[] = { "없음", "하", "중", "상" };
            return r < std::size(kNames) ? kNames[r] : "?";
        }
        const char* PartName(CraftPart p) noexcept
        {
            static constexpr const char* kNames[] = { "무기", "방어구", "장신구" };
            const size_t i = static_cast<size_t>(p);
            return i < std::size(kNames) ? kNames[i] : "?";
        }
        std::string Cell(ItemGrade g, uint8_t r) { return std::string(GradeName(g)) + "-" + RankName(r); }

        // ── 2) Bind ──────────────────────────────────────────────────────────
        void BindConst(LoadContext& ctx, SnapEcon& s)
        {
            const Json* rows = ctx.Rows("LootConst");
            if (rows == nullptr)
                return;
            if (rows->size() != 1)
                ctx.Error("LootConst", "한 행이어야 한다(" + N(rows->size()) + "행)");
            for (size_t i = 0; i < rows->size(); ++i)
            {
                RowReader r(ctx, "LootConst", i, (*rows)[i]);
                Canon::LootConstV c;
                r.Int("rare_pity", c.rarePity).Int("heroic_pity", c.heroicPity).Int("class_swap_percent", c.classSwapPercent)
                 .Int("level_gap", c.levelGap).Int("elite_permille", c.elitePermille).Int("elite_min_tier", c.eliteMinTier)
                 .Int("elite_hp_percent", c.eliteHpPercent).Int("elite_attack_percent", c.eliteAttackPercent)
                 .Int("elite_level_bonus", c.eliteLevelBonus).Int("elite_exp_percent", c.eliteExpPercent)
                 .Int("elite_adena_percent", c.eliteAdenaPercent).Int("elite_share_percent", c.eliteSharePercent)
                 .Int("boss_share_percent", c.bossSharePercent).Int("shard_item", c.shardItem)
                 .Int("traded_dismantle_percent", c.tradedDismantlePercent);
                r.Finish();
                if (i == 0)
                    s.c = c;
            }
        }

        void BindTiers(LoadContext& ctx, SnapEcon& s)
        {
            const Json* rows = ctx.Rows("TierLoot");
            if (rows == nullptr)
                return;
            for (size_t i = 0; i < rows->size(); ++i)
            {
                RowReader r(ctx, "TierLoot", i, (*rows)[i]);
                EconRule::TierLootRow t;
                r.Int("tier", t.tier).Int("common_bp", t.commonBp).Int("uncommon_bp", t.uncommonBp).Int("rare_bp", t.rareBp)
                 .Int("essence_item", t.essenceItem).Int("essence_permille", t.essencePermille)
                 .Int("crystal_item", t.crystalItem).Int("crystal_permille", t.crystalPermille)
                 .Int("elite_crystal_item", t.eliteCrystalItem).Int("elite_crystal_count", t.eliteCrystalCount)
                 .Int("elite_essence_count", t.eliteEssenceCount).Int("elite_gear_percent", t.eliteGearPercent);
                r.Finish();
                // 구조: EconRule::Tier가 단계로 바로 찾는다
                if (r.Ok() && t.tier != i + 1)
                    ctx.Error(ctx.Where("TierLoot", i, "tier"), "단계는 1부터 연속(행 " + N(i + 1) + " → " + N(t.tier) + ")");
                else if (i < static_cast<size_t>(EconRule::kTierCount))
                    s.tiers[i + 1] = t;
                s.tierSrc.push_back(static_cast<uint32_t>(i));
            }
            if (rows->size() != static_cast<size_t>(EconRule::kTierCount))
                ctx.Error("TierLoot", "행 수 " + N(rows->size()) + " ≠ " + N(EconRule::kTierCount) + "(지역 단계 1~5 — 구조 상수)");
        }

        void BindElite(LoadContext& ctx, SnapEcon& s)
        {
            const Json* rows = ctx.Rows("EliteGear");
            if (rows == nullptr)
                return;
            std::vector<std::pair<EconRule::EliteGearRow, uint32_t>> got;
            for (size_t i = 0; i < rows->size(); ++i)
            {
                RowReader r(ctx, "EliteGear", i, (*rows)[i]);
                EconRule::EliteGearRow e;
                r.Int("tier", e.tier).Int("seq", e.seq).Enum("grade", e.grade).Int("rank", e.rank).Int("weight", e.weight);
                r.Finish();
                if (!r.Ok())
                    continue;
                if (e.tier < 1 || e.tier > EconRule::kTierCount)
                {
                    ctx.Error(ctx.Where("EliteGear", i, "tier"), "단계는 1~5(TierLoot)");
                    continue;
                }
                got.emplace_back(e, static_cast<uint32_t>(i));
            }
            std::stable_sort(got.begin(), got.end(), [](const auto& a, const auto& b)
            {
                return a.first.tier != b.first.tier ? a.first.tier < b.first.tier : a.first.seq < b.first.seq;
            });
            uint8_t tier = 0, seq = 0;
            for (const auto& [e, src] : got)
            {
                if (e.tier != tier) { tier = e.tier; seq = 0; }
                if (e.seq != ++seq)
                    ctx.Error(ctx.Where("EliteGear", src, "seq"), "seq는 단계마다 1부터 연속(단계 " + N(e.tier) + " → " + N(e.seq) + ")");
                s.elite.push_back(e);
                s.eliteSrc.push_back(src);
            }
            // 단계별 시작 위치
            size_t k = 0;
            for (int t = 0; t <= EconRule::kTierCount + 1; ++t)
            {
                while (k < s.elite.size() && s.elite[k].tier < t) ++k;
                s.eliteStart[static_cast<size_t>(t)] = static_cast<uint32_t>(k);
            }
        }

        void BindBoss(LoadContext& ctx, SnapEcon& s)
        {
            const Json* rows = ctx.Rows("BossLoot");
            if (rows == nullptr)
                return;
            std::vector<std::pair<EconRule::BossLootRow, uint32_t>> got;
            for (size_t i = 0; i < rows->size(); ++i)
            {
                RowReader r(ctx, "BossLoot", i, (*rows)[i]);
                EconRule::BossLootRow b;
                r.Int("species", b.species).Int("seq", b.seq).Int("item_id", b.itemId).Int("weight", b.weight);
                r.Finish();
                if (!r.Ok())
                    continue;
                if (b.species == 0)
                {
                    ctx.Error(ctx.Where("BossLoot", i, "species"), "몬스터 id 0은 없다");
                    continue;
                }
                got.emplace_back(b, static_cast<uint32_t>(i));
            }
            std::stable_sort(got.begin(), got.end(), [](const auto& a, const auto& b)
            {
                return a.first.species != b.first.species ? a.first.species < b.first.species : a.first.seq < b.first.seq;
            });
            uint16_t species = 0;
            uint8_t seq = 0;
            for (const auto& [b, src] : got)
            {
                if (b.species != species) { species = b.species; seq = 0; }
                if (b.seq != ++seq)
                    ctx.Error(ctx.Where("BossLoot", src, "seq"), "seq는 보스마다 1부터 연속(몬스터 " + N(b.species) + " → " + N(b.seq) + ")");
                s.boss.push_back(b);
                s.bossSrc.push_back(src);
            }
            const size_t maxSpecies = s.boss.empty() ? 0 : s.boss.back().species;
            s.bossStart.assign(maxSpecies + 2, 0);
            size_t k = 0;
            for (size_t sp = 0; sp < s.bossStart.size(); ++sp)
            {
                while (k < s.boss.size() && s.boss[k].species < sp) ++k;
                s.bossStart[sp] = static_cast<uint32_t>(k);
            }
        }

        void BindRecipes(LoadContext& ctx, SnapEcon& s)
        {
            const Json* rows = ctx.Rows("CraftRecipe");
            if (rows == nullptr)
                return;
            static constexpr const char* kItem[EconRule::kCraftInputs] = { "in1_item", "in2_item", "in3_item", "in4_item" };
            static constexpr const char* kCount[EconRule::kCraftInputs] = { "in1_count", "in2_count", "in3_count", "in4_count" };
            for (size_t i = 0; i < rows->size(); ++i)
            {
                RowReader r(ctx, "CraftRecipe", i, (*rows)[i]);
                EconRule::CraftRecipeRow c;
                r.Int("id", c.id).Enum("grade", c.grade).Int("rank", c.rank).Enum("part", c.part).Int("fee", c.fee);
                for (int k = 0; k < EconRule::kCraftInputs; ++k)
                    r.Int(kItem[k], c.in[static_cast<size_t>(k)].item).Int(kCount[k], c.in[static_cast<size_t>(k)].count);
                r.Finish();
                if (r.Ok() && (c.rank < 1 || c.rank > EconRule::kRankCount))
                    ctx.Error(ctx.Where("CraftRecipe", i, "rank"), "단은 1~3(하 · 중 · 상 — 구조)");
                else if (r.Ok() && c.part >= CraftPart::Count)
                    ctx.Error(ctx.Where("CraftRecipe", i, "part"), "부위 묶음 값이 범위 밖");
                else if (r.Ok())
                {
                    uint16_t& at = s.recipeAt[static_cast<size_t>(c.grade)][c.rank][static_cast<size_t>(c.part)];
                    if (at == 0)   // 겹치면 앞 행(R132가 막는다)
                        at = static_cast<uint16_t>(s.recipes.size() + 1);
                }
                s.recipes.push_back(c);
            }
        }

        void BindDismantle(LoadContext& ctx, SnapEcon& s)
        {
            const Json* rows = ctx.Rows("Dismantle");
            if (rows == nullptr)
                return;
            for (size_t i = 0; i < rows->size(); ++i)
            {
                RowReader r(ctx, "Dismantle", i, (*rows)[i]);
                EconRule::DismantleRow d;
                r.Enum("grade", d.grade).Int("shard_count", d.shardCount).Int("item_id", d.itemId).Int("item_count", d.itemCount);
                r.Finish();
                // 구조: EconRule::DismantleFor가 등급 값으로 바로 찾는다
                if (r.Ok() && static_cast<size_t>(d.grade) != i)
                    ctx.Error(ctx.Where("Dismantle", i, "grade"), "행 순서 = 등급이어야 한다(행 " + N(i + 1) + ")");
                else if (i < s.dismantle.size())
                    s.dismantle[i] = d;
            }
            s.dismantleRows = std::min(rows->size(), s.dismantle.size());
        }

        // 라인업 색인 — Item 행(id 순)에서 등급 × 단(0 = 모두) × 경로 비트, 직업 무기는 직업마다 한 벌 더
        void BuildLineup(const SnapItem& item, SnapEcon& s)
        {
            std::vector<std::vector<ItemTableId>> cells(SnapEcon::kCells);
            std::vector<std::vector<ItemTableId>> weapons(SnapEcon::kClasses * SnapEcon::kCells);
            for (const ItemTableRow& row : item.rows)
            {
                if (row.type != ItemType::eEquipment || static_cast<int>(row.grade) >= kItemGradeCount || row.rank > EconRule::kRankCount)
                    continue;
                const bool weapon = ItemTableData::SlotOf(row) == EquipSlot::Weapon;
                for (int b = 0; b < EconRule::kRouteBits; ++b)
                {
                    if (!(row.route & (1u << b)))
                        continue;
                    // "모든 단"(0) 칸과 자기 단 칸에. 단 없는 장비(R124가 막는다)는 "모든 단"에만
                    const uint8_t ranks[2] = { 0, row.rank };
                    for (int k = 0; k < (row.rank != 0 ? 2 : 1); ++k)
                    {
                        const size_t c = SnapEcon::Cell(row.grade, ranks[k], b);
                        cells[c].push_back(row.id);
                        if (weapon)
                            for (size_t cls = 0; cls < SnapEcon::kClasses; ++cls)
                                if (ItemTableData::ClassCanUse(row, static_cast<uint8_t>(cls)))
                                    weapons[cls * SnapEcon::kCells + c].push_back(row.id);
                    }
                }
            }
            auto flatten = [](const std::vector<std::vector<ItemTableId>>& from, std::vector<ItemTableId>& pool, auto& start)
            {
                pool.clear();
                for (size_t c = 0; c < from.size(); ++c)
                {
                    start[c] = static_cast<uint32_t>(pool.size());
                    std::vector<ItemTableId> v = from[c];
                    std::sort(v.begin(), v.end());
                    v.erase(std::unique(v.begin(), v.end()), v.end());
                    pool.insert(pool.end(), v.begin(), v.end());
                }
                start[from.size()] = static_cast<uint32_t>(pool.size());
            };
            flatten(cells, s.lineupPool, s.lineupStart);
            flatten(weapons, s.weaponPool, s.weaponStart);
        }
    }

    void BindEcon(LoadContext& ctx, GameDataSnapshot& snap)
    {
        SnapEcon& s = snap.econ;
        BindConst(ctx, s);
        BindTiers(ctx, s);
        BindElite(ctx, s);
        BindBoss(ctx, s);
        BindRecipes(ctx, s);
        BindDismantle(ctx, s);
        BuildLineup(snap.item, s);
    }

    // ── 3) Validate ──────────────────────────────────────────────────────────
    void ValidateEcon(ValidateContext& v, const GameDataSnapshot& snap)
    {
        const SnapItem& it = snap.item;
        const SnapEcon& s = snap.econ;
        auto findRow = [&it](ItemTableId id) -> const ItemTableRow*
        {
            return id < it.slotById.size() && it.slotById[id] != 0 ? &it.rows[it.slotById[id] - 1u] : nullptr;
        };
        auto isEquip = [](const ItemTableRow* r) { return r != nullptr && r->type == ItemType::eEquipment; };
        auto has = [](const ItemTableRow& r, ItemRoute x) { return EconRule::HasRoute(r, x); };
        auto label = [](const ItemTableRow& r) { return N(r.id) + "(" + std::string(r.displayName ? r.displayName : "") + ")"; };
        // (등급, 단 — 0이면 모든 단) 칸의 장비 중 route에 x가 있는 것의 수(부위 묶음 p — Count면 모든 부위)
        auto countCell = [&it](ItemGrade g, uint8_t rank, ItemRoute x, CraftPart p)
        {
            size_t n = 0;
            for (const ItemTableRow& r : it.rows)
                if (r.type == ItemType::eEquipment && r.grade == g && (rank == 0 || r.rank == rank) && EconRule::HasRoute(r, x)
                    && (p == CraftPart::Count || EconRule::PartOf(r) == p))
                    ++n;
            return n;
        };
        auto recipeAt = [&s](ItemGrade g, uint8_t rank, CraftPart p) -> const EconRule::CraftRecipeRow*
        {
            const size_t gi = static_cast<size_t>(g), pi = static_cast<size_t>(p);
            if (gi >= static_cast<size_t>(kItemGradeCount) || rank < 1 || rank > EconRule::kRankCount || pi >= static_cast<size_t>(CraftPart::Count))
                return nullptr;
            const uint16_t k = s.recipeAt[gi][rank][pi];
            return k != 0 && k <= s.recipes.size() ? &s.recipes[k - 1u] : nullptr;
        };

        // 장비가 쓰인 곳(R127 · R128 · R129 역방향)
        std::vector<bool> inBoss(it.slotById.size(), false), inShop(it.slotById.size(), false), inQuest(it.slotById.size(), false);
        for (const auto& b : s.boss)
            if (b.itemId < inBoss.size()) inBoss[b.itemId] = true;
        for (const auto& shop : it.shops)
            for (const uint16_t id : shop.items)
                if (id != 0 && id < inShop.size()) inShop[id] = true;
        for (const auto& q : snap.quest.rows)
            for (const uint16_t id : q.reward.item)
                if (id != 0 && id < inQuest.size()) inQuest[id] = true;

        for (size_t i = 0; i < it.rows.size(); ++i)
        {
            const ItemTableRow& row = it.rows[i];
            const bool equip = row.type == ItemType::eEquipment;
            // R124 — 장비는 단 1~3, 장비가 아니면 단 0 · 경로 없음
            if (equip && (row.rank < 1 || row.rank > EconRule::kRankCount))
                v.Fail("R124", v.Where("Item", i, "rank"), "장비는 단 1~3(하 · 중 · 상) — 지금 " + N(row.rank));
            if (!equip && row.rank != 0)
                v.Fail("R124", v.Where("Item", i, "rank"), "장비가 아니면 단 0 — 지금 " + N(row.rank));
            if (!equip && row.route != 0)
                v.Fail("R124", v.Where("Item", i, "route"), "장비가 아니면 얻는 길(route)이 없다 — 재료 · 소모품은 드롭 표 · 상점 · 정수 굴림으로 준다");
            if (!equip)
            {
                // R135 — 겹치는 아이템의 획득 귀속은 재료만(1차 — 소비품 귀속 금지)
                if (row.bind == ItemBind::eAcquire && row.maxStack > 1 && row.type != ItemType::eMaterial)
                    v.Fail("R135", v.Where("Item", i, "bind"), "겹치는 아이템(max_stack " + N(row.maxStack) + ")의 획득 귀속은 재료(eMaterial)만 — 1차는 소비품 귀속을 두지 않는다");
                continue;
            }
            // R125 — 제작 가능(Craft)이면 (등급, 단, 부위 묶음) 레시피가 있다
            const CraftPart part = EconRule::PartOf(row);
            const EconRule::CraftRecipeRow* recipe = recipeAt(row.grade, row.rank, part);
            if (has(row, ItemRoute::Craft) && recipe == nullptr && row.rank >= 1 && row.rank <= EconRule::kRankCount)
                v.Fail("R125", v.Where("Item", i, "route"), "제작 가능(Craft)인데 " + Cell(row.grade, row.rank) + " · " + PartName(part) + " 레시피(CraftRecipe)가 없다");
            // R134 — 제작 가능 장비의 매입가 × 희귀도 매입 배율 ≤ 수수료 × 50%(만들어 되팔기로 그로시가 늘지 않게 — 결과 희귀도 = 등급 바닥)
            if (has(row, ItemRoute::Craft) && recipe != nullptr)
            {
                const Rarity floor = ItemRules::FloorRarity(row);
                const size_t ri = static_cast<size_t>(ItemRules::Idx(floor));
                const uint64_t pct = ri < it.rarities.size() ? it.rarities[ri].sellPercent : 0;
                if (static_cast<uint64_t>(row.sellPrice) * pct > static_cast<uint64_t>(recipe->fee) * 50u)
                    v.Fail("R134", v.Where("Item", i, "sell_price"), "제작 장비의 매입가 " + N(row.sellPrice) + " × " + N(pct) + "%(" +
                        ItemRules::RarityName(floor) + ") > 수수료 " + N(recipe->fee) + "의 50% — 만들어 되팔면 그로시가 는다");
            }
            // R127 — route에 Boss인 장비는 BossLoot 어딘가에 있다
            if (has(row, ItemRoute::Boss) && !(row.id < inBoss.size() && inBoss[row.id]))
                v.Fail("R127", v.Where("Item", i, "route"), "보스(Boss) 장비 " + label(row) + "가 BossLoot 어디에도 없다(얻을 길이 없다)");
            // R128 — route에 Shop인 장비는 Shop 어딘가에
            if (has(row, ItemRoute::Shop) && !(row.id < inShop.size() && inShop[row.id]))
                v.Fail("R128", v.Where("Item", i, "route"), "상점(Shop) 장비 " + label(row) + "를 파는 상점이 없다");
            // R129 — route에 Quest인 장비는 퀘스트 보상 어딘가에
            if (has(row, ItemRoute::Quest) && !(row.id < inQuest.size() && inQuest[row.id]))
                v.Fail("R129", v.Where("Item", i, "route"), "퀘스트(Quest) 장비 " + label(row) + "를 주는 퀘스트가 없다");
        }

        // R125 — 레시피마다 그 (등급, 단) 칸에 Craft 장비가 하나 이상(부위 묶음은 비어 있어도 된다 — 다음 차수 자리, ECONOMY.md)
        for (size_t i = 0; i < s.recipes.size(); ++i)
        {
            const auto& c = s.recipes[i];
            if (c.rank >= 1 && c.rank <= EconRule::kRankCount && countCell(c.grade, c.rank, ItemRoute::Craft, CraftPart::Count) == 0)
                v.Fail("R125", v.Where("CraftRecipe", i, "grade"), Cell(c.grade, c.rank) + " 레시피인데 그 칸에 제작 가능(Craft) 장비가 없다");
        }

        // R126 — 드롭 표에 장비 없음(장비는 등급 굴림으로만 — TierLoot · EliteGear · BossLoot)
        for (const auto& d : it.drops)
            for (const auto& e : d.entries)
                if (isEquip(findRow(e.itemId)))
                    v.Fail("R126", v.Where("DropEntry", e.src, "item_id"), "장비 " + label(*findRow(e.itemId)) + "는 드롭 표에 넣지 않는다(장비는 TierLoot · EliteGear · BossLoot 굴림으로만)");

        // R127 — BossLoot: 보스 몬스터 · 장비는 route에 Boss · 보스마다 가중치 합 > 0
        for (size_t k = 0; k < s.boss.size(); ++k)
        {
            const auto& b = s.boss[k];
            const auto& mons = snap.monster.rows;
            const bool boss = b.species >= 1 && b.species <= mons.size() && mons[b.species - 1u].id == b.species && mons[b.species - 1u].boss;
            if (!boss)
                v.Fail("R127", v.Where("BossLoot", s.bossSrc[k], "species"), "몬스터 " + N(b.species) + "는 보스가 아니다(Monster.boss)");
            if (b.itemId != ITEM_TABLE_ID_NONE)
            {
                const ItemTableRow* row = findRow(b.itemId);
                if (!isEquip(row))
                    v.Fail("R127", v.Where("BossLoot", s.bossSrc[k], "item_id"), "아이템 " + N(b.itemId) + "는 장비가 아니다(보스 개인 굴림은 장비만 — 0 = 없음)");
                else if (!has(*row, ItemRoute::Boss))
                    v.Fail("R127", v.Where("BossLoot", s.bossSrc[k], "item_id"), "장비 " + label(*row) + "의 route에 Boss가 없다");
            }
            if (k + 1 == s.boss.size() || s.boss[k + 1].species != b.species)
            {
                uint32_t total = 0;
                size_t first = k;
                while (first > 0 && s.boss[first - 1].species == b.species) --first;
                for (size_t j = first; j <= k; ++j) total += s.boss[j].weight;
                if (total == 0)
                    v.Fail("R127", v.Where("BossLoot", s.bossSrc[first], "weight"), "몬스터 " + N(b.species) + "의 가중치 합이 0이다(굴릴 수 없다)");
            }
        }

        // R128 — 상점이 파는 장비는 route에 Shop
        for (size_t i = 0; i < it.shopLines.size(); ++i)
        {
            const ItemTableRow* row = findRow(it.shopLines[i].itemId);
            if (isEquip(row) && !has(*row, ItemRoute::Shop))
                v.Fail("R128", v.Where("Shop", i, "item_id"), "상점이 파는 장비 " + label(*row) + "의 route에 Shop이 없다");
        }

        // R129 — 퀘스트 보상 장비는 route에 Quest
        static constexpr const char* kRewardCol[3] = { "reward_item_knight", "reward_item_engraver", "reward_item_hunter" };
        for (size_t i = 0; i < snap.quest.rows.size(); ++i)
            for (int c = 0; c < 3; ++c)
            {
                const ItemTableRow* row = findRow(snap.quest.rows[i].reward.item[c]);
                if (isEquip(row) && !has(*row, ItemRoute::Quest))
                    v.Fail("R129", v.Where("Quest", i, kRewardCol[c]), "퀘스트 보상 장비 " + label(*row) + "의 route에 Quest가 없다");
            }

        // R130 — TierLoot: bp 합 ≤ 10000 · bp > 0인 등급마다 Field 장비 · 정수 · 결정은 재료
        auto material = [&](ItemTableId id) { const ItemTableRow* r = findRow(id); return r != nullptr && r->type == ItemType::eMaterial; };
        for (size_t i = 0; i < s.tierSrc.size() && i < static_cast<size_t>(EconRule::kTierCount); ++i)
        {
            const EconRule::TierLootRow& t = s.tiers[i + 1];
            const uint32_t src = s.tierSrc[i];
            const uint32_t sum = static_cast<uint32_t>(t.commonBp) + t.uncommonBp + t.rareBp;
            if (sum > 10000)
                v.Fail("R130", v.Where("TierLoot", src, "common_bp"), "장비 확률 합 " + N(sum) + " > 10000(만분율 100%)");
            const struct { uint16_t bp; ItemGrade g; const char* col; } kGrades[] = {
                { t.commonBp, ItemGrade::eNone, "common_bp" }, { t.uncommonBp, ItemGrade::eRare, "uncommon_bp" }, { t.rareBp, ItemGrade::eEpic, "rare_bp" } };
            for (const auto& g : kGrades)
                if (g.bp > 0 && countCell(g.g, 0, ItemRoute::Field, CraftPart::Count) == 0)
                    v.Fail("R130", v.Where("TierLoot", src, g.col), std::string(GradeName(g.g)) + " 장비 확률이 있는데 route에 Field인 " + GradeName(g.g) + " 장비가 없다");
            if (!material(t.essenceItem))
                v.Fail("R130", v.Where("TierLoot", src, "essence_item"), "정수 " + N(t.essenceItem) + "는 재료(eMaterial)가 아니다");
            if (t.crystalItem != ITEM_TABLE_ID_NONE ? !material(t.crystalItem) : t.crystalPermille > 0)
                v.Fail("R130", v.Where("TierLoot", src, "crystal_item"), t.crystalItem != ITEM_TABLE_ID_NONE ? "결정 " + N(t.crystalItem) + "는 재료(eMaterial)가 아니다"
                    : std::string("결정 확률이 있는데 결정 아이템이 0"));
            if (t.eliteCrystalItem != ITEM_TABLE_ID_NONE ? !material(t.eliteCrystalItem) : t.eliteCrystalCount > 0)
                v.Fail("R130", v.Where("TierLoot", src, "elite_crystal_item"), t.eliteCrystalItem != ITEM_TABLE_ID_NONE
                    ? "정예 결정 " + N(t.eliteCrystalItem) + "는 재료(eMaterial)가 아니다" : std::string("정예 결정 개수가 있는데 결정 아이템이 0"));
        }

        // R131 — EliteGear: (등급, 단)마다 Elite 장비 · 정예가 나오는 단계(≥ elite_min_tier)는 가중치 합 > 0
        for (size_t k = 0; k < s.elite.size(); ++k)
        {
            const auto& e = s.elite[k];
            if (e.rank < 1 || e.rank > EconRule::kRankCount || countCell(e.grade, e.rank, ItemRoute::Elite, CraftPart::Count) == 0)
                v.Fail("R131", v.Where("EliteGear", s.eliteSrc[k], "grade"), Cell(e.grade, e.rank) + "에 정예(Elite) 장비가 없다");
        }
        for (int t = std::max<int>(1, s.c.eliteMinTier); t <= EconRule::kTierCount; ++t)
        {
            uint32_t total = 0;
            for (uint32_t k = s.eliteStart[static_cast<size_t>(t)]; k < s.eliteStart[static_cast<size_t>(t) + 1]; ++k)
                total += s.elite[k].weight;
            const EconRule::TierLootRow& row = s.tiers[static_cast<size_t>(t)];
            if (total == 0 && row.eliteGearPercent > 0)
                v.Fail("R131", static_cast<size_t>(t - 1) < s.tierSrc.size() ? v.Where("TierLoot", s.tierSrc[static_cast<size_t>(t - 1)], "elite_gear_percent") : std::string("EliteGear"),
                    "단계 " + N(t) + "는 정예가 나오고(≥ elite_min_tier " + N(s.c.eliteMinTier) + ") 정예 장비 확률이 있는데 EliteGear 가중치 합이 0");
        }

        // R132 — CraftRecipe: 재료는 eMaterial · item ≠ 0 ⇔ count > 0 · (등급, 단, 부위) 유일 · id 유일
        static constexpr const char* kInItem[EconRule::kCraftInputs] = { "in1_item", "in2_item", "in3_item", "in4_item" };
        static constexpr const char* kInCount[EconRule::kCraftInputs] = { "in1_count", "in2_count", "in3_count", "in4_count" };
        for (size_t i = 0; i < s.recipes.size(); ++i)
        {
            const auto& c = s.recipes[i];
            for (int k = 0; k < EconRule::kCraftInputs; ++k)
            {
                const EconRule::CraftInput& in = c.in[static_cast<size_t>(k)];
                if (in.item != ITEM_TABLE_ID_NONE && !material(in.item))
                    v.Fail("R132", v.Where("CraftRecipe", i, kInItem[k]), "재료 " + N(in.item) + "는 재료(eMaterial)가 아니다");
                else if (in.item != ITEM_TABLE_ID_NONE && in.count == 0)
                    v.Fail("R132", v.Where("CraftRecipe", i, kInCount[k]), "재료 " + N(in.item) + "의 개수가 0");
                else if (in.item == ITEM_TABLE_ID_NONE && in.count != 0)
                    v.Fail("R132", v.Where("CraftRecipe", i, kInCount[k]), "빈 재료 칸(0)에 개수 " + N(in.count));
            }
            for (size_t j = 0; j < i; ++j)
            {
                const auto& o = s.recipes[j];
                if (o.grade == c.grade && o.rank == c.rank && o.part == c.part)
                {
                    v.Fail("R132", v.Where("CraftRecipe", i, "part"), Cell(c.grade, c.rank) + " · " + PartName(c.part) + " 레시피가 " + v.Where("CraftRecipe", j) + "와 겹친다");
                    break;
                }
                if (o.id == c.id)
                {
                    v.Fail("R132", v.Where("CraftRecipe", i, "id"), "레시피 번호 " + N(c.id) + "가 " + v.Where("CraftRecipe", j) + "와 겹친다");
                    break;
                }
            }
        }

        // R133 — Dismantle: 행 수 = 등급 수 · 재료는 eMaterial · LootConst.shard_item은 재료
        if (s.dismantleRows != static_cast<size_t>(kItemGradeCount))
            v.Fail("R133", "Dismantle", "행 수 " + N(s.dismantleRows) + " ≠ 등급 수 " + N(kItemGradeCount) + "(ItemGrade — 등급마다 한 행)");
        for (size_t i = 0; i < s.dismantleRows; ++i)
        {
            const auto& d = s.dismantle[i];
            if (d.itemId != ITEM_TABLE_ID_NONE ? !material(d.itemId) : d.itemCount != 0)
                v.Fail("R133", v.Where("Dismantle", i, "item_id"), d.itemId != ITEM_TABLE_ID_NONE ? "분해 재료 " + N(d.itemId) + "는 재료(eMaterial)가 아니다"
                    : "재료가 0인데 개수 " + N(d.itemCount));
            else if (d.itemId != ITEM_TABLE_ID_NONE && d.itemCount == 0)
                v.Fail("R133", v.Where("Dismantle", i, "item_count"), "분해 재료 " + N(d.itemId) + "의 개수가 0");
        }
        if (!material(s.c.shardItem))
            v.Fail("R133", v.Where("LootConst", 0, "shard_item"), "장비 파편 " + N(s.c.shardItem) + "는 재료(eMaterial)가 아니다");
    }

    // ── 4) Dump ──────────────────────────────────────────────────────────────
    void DumpEcon(Canon::Book& book, const GameDataSnapshot& snap)
    {
        const SnapEcon& s = snap.econ;
        Canon::WriteLootConst(book, s.c);
        std::vector<EconRule::TierLootRow> tiers;
        for (size_t i = 0; i < s.tierSrc.size() && i < static_cast<size_t>(EconRule::kTierCount); ++i)
            tiers.push_back(s.tiers[i + 1]);
        Canon::WriteTierLoot(book, tiers);
        Canon::WriteEliteGear(book, s.elite);
        Canon::WriteBossLoot(book, s.boss);
        Canon::WriteCraftRecipe(book, s.recipes);
        Canon::WriteDismantle(book, std::span<const EconRule::DismantleRow>(s.dismantle.data(), s.dismantleRows));
    }
}
