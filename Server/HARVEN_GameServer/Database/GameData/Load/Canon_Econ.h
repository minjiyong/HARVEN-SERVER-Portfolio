#pragma once
// 정본 표 쓰기 — 경제 묶음(탭 LootConst · TierLoot · EliteGear · BossLoot · CraftRecipe · Dismantle).
// 열 순서 · 형 = Tools/DataTool/schema/econ.yaml(datatool이 메모리 덤프의 _schema.json과 대조한다).
#include <cstdint>
#include <span>
#include <string>

#include "Database/GameData/Load/Canon.h"
#include "Database/GameData/EconRules.h"

namespace Canon
{
    struct LootConstV
    {
        uint16_t    rarePity = 0;
        uint16_t    heroicPity = 0;
        uint8_t     classSwapPercent = 0;
        uint8_t     levelGap = 0;
        uint16_t    elitePermille = 0;
        uint8_t     eliteMinTier = 0;
        uint16_t    eliteHpPercent = 0;
        uint16_t    eliteAttackPercent = 0;
        uint8_t     eliteLevelBonus = 0;
        uint16_t    eliteExpPercent = 0;
        uint16_t    eliteAdenaPercent = 0;
        uint8_t     eliteSharePercent = 0;
        uint8_t     bossSharePercent = 0;
        ItemTableId shardItem = ITEM_TABLE_ID_NONE;
        uint8_t     tradedDismantlePercent = 0;
    };

    inline void WriteLootConst(Book& book, const LootConstV& c)
    {
        Table& t = book.Get("LootConst");
        Row w;
        w.Put("rare_pity", c.rarePity).Put("heroic_pity", c.heroicPity).Put("class_swap_percent", c.classSwapPercent)
         .Put("level_gap", c.levelGap).Put("elite_permille", c.elitePermille).Put("elite_min_tier", c.eliteMinTier)
         .Put("elite_hp_percent", c.eliteHpPercent).Put("elite_attack_percent", c.eliteAttackPercent)
         .Put("elite_level_bonus", c.eliteLevelBonus).Put("elite_exp_percent", c.eliteExpPercent)
         .Put("elite_adena_percent", c.eliteAdenaPercent).Put("elite_share_percent", c.eliteSharePercent)
         .Put("boss_share_percent", c.bossSharePercent).Put("shard_item", c.shardItem)
         .Put("traded_dismantle_percent", c.tradedDismantlePercent);
        t.Add(w);
    }

    inline void WriteTierLoot(Book& book, std::span<const EconRule::TierLootRow> rows)
    {
        Table& t = book.Get("TierLoot");
        for (const auto& r : rows)
        {
            Row w;
            w.Put("tier", r.tier).Put("common_bp", r.commonBp).Put("uncommon_bp", r.uncommonBp).Put("rare_bp", r.rareBp)
             .Put("essence_item", r.essenceItem).Put("essence_permille", r.essencePermille)
             .Put("crystal_item", r.crystalItem).Put("crystal_permille", r.crystalPermille)
             .Put("elite_crystal_item", r.eliteCrystalItem).Put("elite_crystal_count", r.eliteCrystalCount)
             .Put("elite_essence_count", r.eliteEssenceCount).Put("elite_gear_percent", r.eliteGearPercent);
            t.Add(w);
        }
    }

    inline void WriteEliteGear(Book& book, std::span<const EconRule::EliteGearRow> rows)
    {
        Table& t = book.Get("EliteGear");
        for (const auto& r : rows)
        {
            Row w;
            w.Put("tier", r.tier).Put("seq", r.seq).Put("grade", r.grade).Put("rank", r.rank).Put("weight", r.weight);
            t.Add(w);
        }
    }

    inline void WriteBossLoot(Book& book, std::span<const EconRule::BossLootRow> rows)
    {
        Table& t = book.Get("BossLoot");
        for (const auto& r : rows)
        {
            Row w;
            w.Put("species", r.species).Put("seq", r.seq).Put("item_id", r.itemId).Put("weight", r.weight);
            t.Add(w);
        }
    }

    inline void WriteCraftRecipe(Book& book, std::span<const EconRule::CraftRecipeRow> rows)
    {
        static constexpr const char* kItem[EconRule::kCraftInputs] = { "in1_item", "in2_item", "in3_item", "in4_item" };
        static constexpr const char* kCount[EconRule::kCraftInputs] = { "in1_count", "in2_count", "in3_count", "in4_count" };
        Table& t = book.Get("CraftRecipe");
        for (const auto& r : rows)
        {
            Row w;
            w.Put("id", r.id).Put("grade", r.grade).Put("rank", r.rank).Put("part", r.part).Put("fee", r.fee);
            for (int k = 0; k < EconRule::kCraftInputs; ++k)
                w.Put(kItem[k], r.in[static_cast<size_t>(k)].item).Put(kCount[k], r.in[static_cast<size_t>(k)].count);
            t.Add(w);
        }
    }

    inline void WriteDismantle(Book& book, std::span<const EconRule::DismantleRow> rows)
    {
        Table& t = book.Get("Dismantle");
        for (const auto& r : rows)
        {
            Row w;
            w.Put("grade", r.grade).Put("shard_count", r.shardCount).Put("item_id", r.itemId).Put("item_count", r.itemCount);
            t.Add(w);
        }
    }
}
