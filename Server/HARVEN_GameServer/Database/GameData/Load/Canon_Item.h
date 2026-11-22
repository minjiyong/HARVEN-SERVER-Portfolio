#pragma once
// 정본 표 쓰기 — 아이템 묶음.
//   Item · ItemOption · ItemProc · ItemSet · Rarity · RarityStep · Enchant · EnchantConst
//   DropTable · DropEntry · Shop
// 파사드가 없는 표(드롭 · 상점)는 아래 V 구조체로 받는다 — 이관 전에는 덤프 프로그램이,
// 이관 뒤에는 서버가 스냅샷에서 같은 구조체를 채운다.
#include <functional>
#include <span>
#include <string>
#include <vector>

#include "Database/GameData/Load/Canon.h"
#include "Database/GameData/CombatData.h"
#include "Database/GameData/ItemRules.h"
#include "Database/GameData/LevelTable.h"
#include "Database/GameData/Item/ItemTableData.h"

namespace Canon
{
    struct RarityV { const char* name = ""; uint32_t color = 0; uint8_t optionCount = 0; uint16_t sellPercent = 0; bool lootNotice = false; };
    struct DropEntryV { uint16_t itemId = 0; uint32_t weight = 0; uint16_t minCount = 0, maxCount = 0; };
    struct DropTableV { uint8_t id = 0; uint32_t noDropWeight = 0; std::vector<DropEntryV> entries; };
    struct ShopV { uint16_t npcId = 0; std::vector<uint16_t> items; };

    inline void WriteItem(Book& book, std::span<const ItemTableRow> rows, const std::function<const char*(uint32_t)>& codeName)
    {
        Table& t = book.Get("Item");
        for (const ItemTableRow& r : rows)
        {
            Row w;
            w.Put("id", r.id).StrOpt("code_name", codeName(r.id)).Put("type", r.type).Put("max_stack", r.maxStack)
             .Put("name", r.name).Put("grade", r.grade).Put("attack_bonus", r.attackBonus).Put("heal_amount", r.healAmount)
             .Put("effect", r.effect).Put("effect_value", r.effectValue).Put("buy_price", r.buyPrice).Put("sell_price", r.sellPrice)
             .Put("display_name", r.displayName).Put("icon_id", r.iconId).Put("equip_slot", r.equipSlot).Put("weapon_kind", r.weaponKind)
             .Put("armor_bonus", r.armorBonus).Put("magic_armor_bonus", r.magicArmorBonus).Put("tier", r.tier).Put("set_id", r.setId)
             .Mask<ClassId>("class_mask", r.classMask).Put("req_level", r.reqLevel)
             .Put("rank", r.rank).Flags<ItemRoute>("route", r.route).Put("bind", r.bind);
            t.Add(w);
        }
    }

    inline void WriteItemOption(Book& book)
    {
        Table& t = book.Get("ItemOption");
        for (int k = 0; k < static_cast<int>(ItemOption::Count); ++k)
        {
            const auto& r = ItemRules::Option(static_cast<ItemOption>(k));
            Row w;
            w.Put("id", static_cast<ItemOption>(k)).Put("name", r.name).Put("lo", r.lo).Put("hi", r.hi)
             .Put("cap", r.cap).Put("unit", r.unit).Put("display", r.display);
            t.Add(w);
        }
    }

    inline void WriteItemProc(Book& book)
    {
        Table& t = book.Get("ItemProc");
        for (int k = 0; k < static_cast<int>(ItemProc::Count); ++k)
        {
            const auto& r = ItemRules::Proc(static_cast<ItemProc>(k));
            Row w;
            w.Put("id", static_cast<ItemProc>(k)).Put("name", r.name).Put("chance", r.chance).Put("chance_emp", r.chanceEmp)
             .Put("power", r.power).Put("power_emp", r.powerEmp).Color("color", r.color);
            t.Add(w);
        }
    }

    inline void WriteItemSet(Book& book, std::span<const ItemRules::SetRow> sets)
    {
        Table& t = book.Get("ItemSet");
        for (const auto& s : sets)
        {
            Row w;
            w.Put("id", s.id).Put("name", s.name);
            for (int i = 0; i < 4; ++i) w.Put(("piece_" + std::to_string(i + 1)).c_str(), s.pieces[i]);
            for (int i = 0; i < 2; ++i) w.Put(("two_" + std::to_string(i + 1) + "_kind").c_str(), s.two[i].kind).Put(("two_" + std::to_string(i + 1) + "_value").c_str(), s.two[i].value);
            for (int i = 0; i < 2; ++i) w.Put(("four_" + std::to_string(i + 1) + "_kind").c_str(), s.four[i].kind).Put(("four_" + std::to_string(i + 1) + "_value").c_str(), s.four[i].value);
            w.Put("four_proc", s.fourProc).Color("color", s.color);
            t.Add(w);
        }
    }

    inline void WriteRarity(Book& book, std::span<const RarityV> rarities, std::span<const uint16_t> stepWeights /* 7 × 5: 단계 0~5, 6 = 보스 */)
    {
        Table& t = book.Get("Rarity");
        for (size_t i = 0; i < rarities.size(); ++i)
        {
            Row w;
            w.Put("id", static_cast<Rarity>(i)).Put("name", rarities[i].name).Color("color", rarities[i].color)
             .Put("option_count", rarities[i].optionCount).Put("sell_percent", rarities[i].sellPercent)
             .Put("loot_notice", rarities[i].lootNotice);
            t.Add(w);
        }
        if (stepWeights.size() != 7 * 5)
            throw Error("RarityStep: 7 × 5여야 한다");
        Table& s = book.Get("RarityStep");
        for (uint8_t row = 0; row < 7; ++row)
        {
            Row w;
            w.Put("id", row);
            for (int k = 0; k < 5; ++k) w.Put(("w_plus" + std::to_string(k)).c_str(), stepWeights[row * 5u + k]);
            s.Add(w);
        }
    }

    struct EnchantV { uint8_t level = 0; uint64_t successTickets = 0; uint16_t graceAttempts = 0, statGain = 0; };
    struct EnchantConstV { uint32_t worldNoticeLevel = 0, firstNoticeLevel = 0; uint16_t gainPerFurtherLevel = 0, accessoryHpPerPoint = 0, accessoryMpPerPoint = 0; };

    inline void WriteEnchant(Book& book, std::span<const EnchantV> rows, const EnchantConstV& c)
    {
        Table& t = book.Get("Enchant");
        for (const auto& r : rows)
        {
            Row w;
            w.Put("level", r.level).Put("success_tickets", r.successTickets).Put("grace_attempts", r.graceAttempts).Put("stat_gain", r.statGain);
            t.Add(w);
        }
        Table& tc = book.Get("EnchantConst");
        Row w;
        w.Put("world_notice_level", c.worldNoticeLevel).Put("first_notice_level", c.firstNoticeLevel)
         .Put("gain_per_further_level", c.gainPerFurtherLevel).Put("accessory_hp_per_point", c.accessoryHpPerPoint)
         .Put("accessory_mp_per_point", c.accessoryMpPerPoint);
        tc.Add(w);
    }

    inline void WriteDrop(Book& book, std::span<const DropTableV> tables)
    {
        Table& t = book.Get("DropTable");
        Table& e = book.Get("DropEntry");
        for (const auto& d : tables)
        {
            Row w;
            w.Put("id", d.id).Put("no_drop_weight", d.noDropWeight);
            t.Add(w);
            uint8_t seq = 0;
            for (const auto& x : d.entries)
            {
                Row ew;
                ew.Put("table_id", d.id).Put("seq", ++seq).Put("item_id", x.itemId).Put("weight", x.weight)
                  .Put("min_count", x.minCount).Put("max_count", x.maxCount);
                e.Add(ew);
            }
        }
    }

    inline void WriteShop(Book& book, std::span<const ShopV> shops)
    {
        Table& t = book.Get("Shop");
        for (const auto& s : shops)
        {
            uint8_t seq = 0;
            for (const uint16_t item : s.items)
            {
                Row w;
                w.Put("npc_id", s.npcId).Put("seq", ++seq).Put("item_id", item);
                t.Add(w);
            }
        }
    }
}
