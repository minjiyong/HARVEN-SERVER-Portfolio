#include "pch.h"
// ─────────────────────────────────────────────────────────────────────────────
// 아이템 묶음 — 탭 Item · ItemOption · ItemProc · ItemSet · Rarity · RarityStep · Enchant · EnchantConst · DropTable · DropEntry · Shop ·
// → 스냅샷 → 파사드(ItemTableData · ItemRules · EnchantRule · WorldData::ShopSells · DropTable).
// (옛 장비 드롭 표 ClassWeapon · GearDrop · GearChance는 경제 1차 로직 2부에서 지웠다 — 장비는 econ 묶음의 등급 굴림, R100 · R101 폐지)
// 모양은 Bind_Monster.cpp(모범 표)와 같다: 1) 파사드 2) Bind(형 · 구조) 3) Validate(R025 · R026 · R082~R099) 4) Dump.
// 드롭 굴림 · 직업 무기 · 장비 드롭 규칙은 Item/DropTable.cpp(스냅샷을 읽는다).
// ─────────────────────────────────────────────────────────────────────────────
#include <algorithm>
#include <iterator>

#include "Database/GameData/Load/Bundles.h"
#include "Database/GameData/Load/Canon_Item.h"
#include "Database/GameData/Load/JsonRead.h"
#include "Database/GameData/Load/Snapshot.h"
#include "Database/GameData/Load/Validate.h"
#include "Database/GameData/ItemRules.h"
#include "Database/GameData/LevelTable.h"
#include "Database/GameData/World/WorldNpc.h"
#include "Database/GameData/World/WorldShop.h"
#include "Database/GameData/Item/ItemTableData.h"

// ── 1) 파사드 ────────────────────────────────────────────────────────────────
// 표 행 수는 로드 검증(구조 · R025 · R026)이 보장한다. 검증 중(아직 틀릴 수 있는 판)에 다른 묶음이 불러도
// 범위 밖을 읽지 않게 빈 행으로 한 번 더 막는다.
namespace
{
    const SnapItem::RarityRow& RarityAt(Rarity r) noexcept
    {
        static const SnapItem::RarityRow kNone{};
        const std::vector<SnapItem::RarityRow>& v = GD().item.rarities;
        const size_t i = static_cast<size_t>(ItemRules::Idx(r));
        return i < v.size() ? v[i] : kNone;
    }
}

namespace ItemTableData
{
    std::span<const ItemTableRow> Rows() noexcept { return GD().item.rows; }

    size_t RowCount() noexcept { return GD().item.rows.size(); }

    const ItemTableRow* Find(ItemTableId id) noexcept
    {
        const SnapItem& s = GD().item;
        if (id < s.slotById.size())
        {
            const uint16_t k = s.slotById[id];
            if (k != 0)
                return &s.rows[k - 1u];
        }
        return nullptr;
    }
}

namespace ItemRules
{
    const char* RarityName(Rarity r) noexcept { return RarityAt(r).name; }
    uint32_t RarityColor(Rarity r) noexcept { return RarityAt(r).color; }
    uint8_t RarityOptionCount(Rarity r) noexcept { return RarityAt(r).optionCount; }
    uint16_t RaritySellPercent(Rarity r) noexcept { return RarityAt(r).sellPercent; }
    bool RarityLootNotice(Rarity r) noexcept { return RarityAt(r).lootNotice; }

    const OptionRow& Option(ItemOption k) noexcept
    {
        static const OptionRow kNone{ "", 0, 0, 0, 0, 0 };
        const std::vector<OptionRow>& v = GD().item.options;
        const auto i = static_cast<size_t>(k);
        const size_t at = i < static_cast<size_t>(ItemOption::Count) ? i : 0;
        return at < v.size() ? v[at] : kNone;
    }

    const ProcRow& Proc(ItemProc p) noexcept
    {
        static const ProcRow kNone{ "", 0, 0, 0, 0, 0 };
        const std::vector<ProcRow>& v = GD().item.procs;
        const auto i = static_cast<size_t>(p);
        const size_t at = i < static_cast<size_t>(ItemProc::Count) ? i : 0;
        return at < v.size() ? v[at] : kNone;
    }

    std::span<const SetRow> Sets() noexcept { return GD().item.sets; }

    int SetCount() noexcept { return static_cast<int>(GD().item.sets.size()); }

    const SetRow* FindSet(uint8_t setId) noexcept
    {
        for (const auto& s : GD().item.sets)
            if (s.id == setId)
                return &s;
        return nullptr;
    }

    const uint16_t* StepWeights(int row) noexcept
    {
        const auto& w = GD().item.stepWeights;
        return w[row >= 0 && static_cast<size_t>(row) < w.size() ? static_cast<size_t>(row) : 0].data();
    }
}

namespace EnchantRule
{
    // 행 수 ≥ 1은 로드 검증(구조)이 보장한다 — 검증 중인 판을 다른 묶음이 불러도 범위 밖을 읽지 않게 한 번 더 막는다.
    uint64_t SuccessTickets(Level current) noexcept
    {
        const std::vector<SnapItem::EnchantRow>& e = GD().item.enchant;
        if (e.empty()) return 0;
        return e[current < e.size() ? current : e.size() - 1].successTickets;
    }

    uint16_t GraceAttempts(Level current) noexcept
    {
        const std::vector<SnapItem::EnchantRow>& e = GD().item.enchant;
        if (e.empty()) return 0;
        return e[current < e.size() ? current : e.size() - 1].graceAttempts;
    }

    uint16_t AttackBonus(Level level) noexcept
    {
        const SnapItem& s = GD().item;
        const std::vector<uint16_t>& sum = s.enchantBonus;   // [k] = +k 누적(k = 0 ~ 행 수)
        if (sum.empty()) return 0;
        const size_t rows = sum.size() - 1;
        if (level <= rows) return sum[level];
        const uint64_t n = level - rows;
        // 이미 능력치 표현 범위에 도달한 경우 큰 곱셈을 하지 않는다.
        if (n > 65535) return 65535;
        const uint64_t last = s.enchant.empty() ? 0 : s.enchant.back().statGain;
        const uint64_t total = sum[rows] + n * last + n * (n + 1) / 2 * s.enchantConst.gainPerFurtherLevel;
        return static_cast<uint16_t>(total > 65535 ? 65535 : total);
    }

    Level WorldNoticeLevel() noexcept { return GD().item.enchantConst.worldNoticeLevel; }
    Level FirstNoticeLevel() noexcept { return GD().item.enchantConst.firstNoticeLevel; }
    uint16_t AccessoryHpPerPoint() noexcept { return GD().item.enchantConst.accessoryHpPerPoint; }
    uint16_t AccessoryMpPerPoint() noexcept { return GD().item.enchantConst.accessoryMpPerPoint; }
}

namespace WorldData
{
    std::span<const ShopRow> Shops() noexcept { return GD().item.shops; }

    bool ShopSells(uint16_t npcId, uint16_t itemId) noexcept
    {
        if (itemId == 0)
            return false;
        for (const auto& shop : GD().item.shops)
        {
            if (shop.npcId != npcId)
                continue;
            for (uint16_t id : shop.items)
                if (id == itemId)
                    return true;
        }
        return false;
    }
}

namespace GameDataLoad
{
    // ── 2) Bind ──────────────────────────────────────────────────────────────
    namespace
    {
        constexpr size_t kSetSlots = sizeof(GearExtras::setCount) / sizeof(GearExtras::setCount[0]);   // 4 — 세트 id 0~3
        constexpr size_t kOptSlots = sizeof(ItemInstance::opts) / sizeof(ItemInstance::opts[0]);        // 4 — 옵션 줄 수 상한
        constexpr size_t kShopSlots = sizeof(WorldData::ShopRow::items) / sizeof(WorldData::ShopRow::items[0]);   // 16

        std::string N(size_t x) { return std::to_string(x); }

        void BindItems(LoadContext& ctx, SnapItem& s)
        {
            const Json* rows = ctx.Rows("Item");
            if (rows == nullptr)
                return;
            s.rows.reserve(rows->size());
            for (size_t i = 0; i < rows->size(); ++i)
            {
                RowReader r(ctx, "Item", i, (*rows)[i]);
                ItemTableRow row{};
                const char* codeName = nullptr;
                r.Int("id", row.id)
                 .StrOpt("code_name", codeName)
                 .Enum("type", row.type)
                 .Int("max_stack", row.maxStack)
                 .Chars("name", row.name)
                 .Enum("grade", row.grade)
                 .Int("attack_bonus", row.attackBonus)
                 .Int("heal_amount", row.healAmount)
                 .Enum("effect", row.effect)
                 .Int("effect_value", row.effectValue)
                 .Int("buy_price", row.buyPrice)
                 .Int("sell_price", row.sellPrice)
                 .Str("display_name", row.displayName)
                 .Int("icon_id", row.iconId)
                 .Enum("equip_slot", row.equipSlot)
                 .Enum("weapon_kind", row.weaponKind)
                 .Int("armor_bonus", row.armorBonus)
                 .Int("magic_armor_bonus", row.magicArmorBonus)
                 .Int("tier", row.tier)
                 .Int("set_id", row.setId)
                 .Mask<ClassId>("class_mask", row.classMask)
                 .Int("req_level", row.reqLevel)
                 .Int("rank", row.rank)
                 .Flags<ItemRoute>("route", row.route)
                 .Enum("bind", row.bind);
                r.Finish();
                if (r.Ok() && row.setId >= kSetSlots)
                    ctx.Error(ctx.Where("Item", i, "set_id"), "세트 id " + N(row.setId) + " — 0~" + N(kSetSlots - 1) + "(착용 집계 GearExtras::setCount[" + N(kSetSlots) + "] — 구조 상한)");
                s.rows.push_back(row);
                s.codeName.push_back(codeName);
            }
            // id 색인 — 같은 id가 둘이면 앞 행(R091이 막는다)
            ItemTableId maxId = 0;
            for (const auto& row : s.rows) maxId = std::max(maxId, row.id);
            s.slotById.assign(static_cast<size_t>(maxId) + 1, 0);
            for (size_t i = 0; i < s.rows.size(); ++i)
                if (s.slotById[s.rows[i].id] == 0)
                    s.slotById[s.rows[i].id] = static_cast<uint16_t>(i + 1);
        }

        void BindOptions(LoadContext& ctx, SnapItem& s)
        {
            if (const Json* rows = ctx.Rows("ItemOption"))
                for (size_t i = 0; i < rows->size(); ++i)
                {
                    RowReader r(ctx, "ItemOption", i, (*rows)[i]);
                    ItemOption id = ItemOption::None;
                    ItemRules::OptionRow row{ "", 0, 0, 0, 0, 0 };
                    r.Enum("id", id).Str("name", row.name).Int("lo", row.lo).Int("hi", row.hi)
                     .Int("cap", row.cap).Int("unit", row.unit).Int("display", row.display);
                    r.Finish();
                    s.optionId.push_back(id);
                    s.options.push_back(row);
                }
            if (const Json* rows = ctx.Rows("ItemProc"))
                for (size_t i = 0; i < rows->size(); ++i)
                {
                    RowReader r(ctx, "ItemProc", i, (*rows)[i]);
                    ItemProc id = ItemProc::None;
                    ItemRules::ProcRow row{ "", 0, 0, 0, 0, 0 };
                    r.Enum("id", id).Str("name", row.name).Int("chance", row.chance).Int("chance_emp", row.chanceEmp)
                     .Int("power", row.power).Int("power_emp", row.powerEmp).Color("color", row.color);
                    r.Finish();
                    s.procId.push_back(id);
                    s.procs.push_back(row);
                }
        }

        void BindSets(LoadContext& ctx, SnapItem& s)
        {
            const Json* rows = ctx.Rows("ItemSet");
            if (rows == nullptr)
                return;
            static constexpr const char* kPiece[4] = { "piece_1", "piece_2", "piece_3", "piece_4" };
            for (size_t i = 0; i < rows->size(); ++i)
            {
                RowReader r(ctx, "ItemSet", i, (*rows)[i]);
                ItemRules::SetRow row{};
                row.name = "";
                r.Int("id", row.id).Str("name", row.name);
                for (int k = 0; k < 4; ++k) r.Int(kPiece[k], row.pieces[k]);
                r.Enum("two_1_kind", row.two[0].kind).Int("two_1_value", row.two[0].value)
                 .Enum("two_2_kind", row.two[1].kind).Int("two_2_value", row.two[1].value)
                 .Enum("four_1_kind", row.four[0].kind).Int("four_1_value", row.four[0].value)
                 .Enum("four_2_kind", row.four[1].kind).Int("four_2_value", row.four[1].value)
                 .Enum("four_proc", row.fourProc).Color("color", row.color);
                r.Finish();
                if (r.Ok() && row.id >= kSetSlots)
                    ctx.Error(ctx.Where("ItemSet", i, "id"), "세트 id " + N(row.id) + " — 1~" + N(kSetSlots - 1) + "(착용 집계 GearExtras::setCount[" + N(kSetSlots) + "] — 구조 상한)");
                s.sets.push_back(row);
            }
        }

        void BindRarity(LoadContext& ctx, SnapItem& s)
        {
            if (const Json* rows = ctx.Rows("Rarity"))
            {
                for (size_t i = 0; i < rows->size(); ++i)
                {
                    RowReader r(ctx, "Rarity", i, (*rows)[i]);
                    SnapItem::RarityRow row;
                    r.Enum("id", row.id).Str("name", row.name).Color("color", row.color)
                     .Int("option_count", row.optionCount).Int("sell_percent", row.sellPercent)
                     .Bool("loot_notice", row.lootNotice);
                    r.Finish();
                    // 구조: ItemRules가 희귀도 값으로 바로 찾는다(Idx) · 옵션 줄은 ItemInstance::opts[4]
                    if (r.Ok() && static_cast<size_t>(row.id) != i)
                        ctx.Error(ctx.Where("Rarity", i, "id"), "행 순서 = 희귀도 값이어야 한다(행 " + N(i + 1) + ")");
                    if (r.Ok() && row.optionCount > kOptSlots)
                        ctx.Error(ctx.Where("Rarity", i, "option_count"), N(row.optionCount) + " > " + N(kOptSlots) + "(ItemInstance::opts — 구조 상한)");
                    s.rarities.push_back(row);
                }
                if (s.rarities.size() != static_cast<size_t>(Rarity::Count))
                    ctx.Error("Rarity", "행 수 " + N(s.rarities.size()) + " ≠ Rarity::Count(" + N(static_cast<size_t>(Rarity::Count)) + ")");
            }
            if (const Json* rows = ctx.Rows("RarityStep"))
            {
                static constexpr const char* kW[5] = { "w_plus0", "w_plus1", "w_plus2", "w_plus3", "w_plus4" };
                for (size_t i = 0; i < rows->size(); ++i)
                {
                    RowReader r(ctx, "RarityStep", i, (*rows)[i]);
                    uint8_t id = 0;
                    std::array<uint16_t, 5> w{};
                    r.Int("id", id);
                    for (int k = 0; k < 5; ++k) r.Int(kW[k], w[static_cast<size_t>(k)]);
                    r.Finish();
                    if (r.Ok() && (id != i || i >= s.stepWeights.size()))
                        ctx.Error(ctx.Where("RarityStep", i, "id"), "id는 0부터 연속(0~5 몬스터 단계 · 6 보스) — 행 " + N(i + 1) + " → " + N(id));
                    else if (i < s.stepWeights.size())
                        s.stepWeights[i] = w;
                }
                if (rows->size() != s.stepWeights.size())
                    ctx.Error("RarityStep", "행 수 " + N(rows->size()) + " ≠ " + N(s.stepWeights.size()) + "(단계 0~5 · 보스 — 구조 상수)");
            }
        }

        void BindEnchant(LoadContext& ctx, SnapItem& s)
        {
            if (const Json* rows = ctx.Rows("Enchant"))
            {
                for (size_t i = 0; i < rows->size(); ++i)
                {
                    RowReader r(ctx, "Enchant", i, (*rows)[i]);
                    SnapItem::EnchantRow row;
                    r.Int("level", row.level).Int("success_tickets", row.successTickets).Int("grace_attempts", row.graceAttempts)
                     .Int("stat_gain", row.statGain);
                    r.Finish();
                    // 구조: EnchantRule이 강화 수치로 바로 찾는다(마지막 행이 그 위 단계 모두)
                    if (r.Ok() && row.level != i)
                        ctx.Error(ctx.Where("Enchant", i, "level"), "강화 수치는 +0부터 연속(행 " + N(i + 1) + " → +" + N(row.level) + ")");
                    s.enchant.push_back(row);
                }
                if (s.enchant.empty())
                    ctx.Error("Enchant", "행이 없다(+0 행부터 — 마지막 행이 그 위 단계 모두)");
                // 누적 성능을 미리 더해 둔다(장비 능력치 재계산마다 부른다)
                s.enchantBonus.assign(s.enchant.size() + 1, 0);
                uint32_t sum = 0;
                for (size_t i = 0; i < s.enchant.size(); ++i)
                {
                    sum = std::min<uint32_t>(65535, sum + s.enchant[i].statGain);
                    s.enchantBonus[i + 1] = static_cast<uint16_t>(sum);
                }
            }
            if (const Json* rows = ctx.Rows("EnchantConst"))
            {
                if (rows->size() != 1)
                    ctx.Error("EnchantConst", "한 행이어야 한다(" + N(rows->size()) + "행)");
                for (size_t i = 0; i < rows->size(); ++i)
                {
                    RowReader r(ctx, "EnchantConst", i, (*rows)[i]);
                    SnapItem::EnchantConst c;
                    r.Int("world_notice_level", c.worldNoticeLevel).Int("first_notice_level", c.firstNoticeLevel)
                     .Int("gain_per_further_level", c.gainPerFurtherLevel).Int("accessory_hp_per_point", c.accessoryHpPerPoint)
                     .Int("accessory_mp_per_point", c.accessoryMpPerPoint);
                    r.Finish();
                    if (i == 0)
                        s.enchantConst = c;
                }
            }
        }

        void BindDrops(LoadContext& ctx, SnapItem& s)
        {
            if (const Json* rows = ctx.Rows("DropTable"))
            {
                for (size_t i = 0; i < rows->size(); ++i)
                {
                    RowReader r(ctx, "DropTable", i, (*rows)[i]);
                    SnapItem::Drop d;
                    r.Int("id", d.id).Int("no_drop_weight", d.noDropWeight);
                    r.Finish();
                    // 구조: DropTable::Roll이 표 번호 - 1로 바로 찾는다
                    if (r.Ok() && d.id != i + 1)
                        ctx.Error(ctx.Where("DropTable", i, "id"), "표 번호는 1부터 연속(행 " + N(i + 1) + " → " + N(d.id) + ")");
                    s.drops.push_back(std::move(d));
                }
                if (s.drops.empty())
                    ctx.Error("DropTable", "표가 하나도 없다(없는 표 번호는 1번 표를 쓴다)");
            }
            if (const Json* rows = ctx.Rows("DropEntry"))
                for (size_t i = 0; i < rows->size(); ++i)
                {
                    RowReader r(ctx, "DropEntry", i, (*rows)[i]);
                    uint8_t tableId = 0, seq = 0;
                    SnapItem::DropEntry e;
                    r.Int("table_id", tableId).Int("seq", seq).Int("item_id", e.itemId).Int("weight", e.weight)
                     .Int("min_count", e.minCount).Int("max_count", e.maxCount);
                    r.Finish();
                    if (!r.Ok())
                        continue;
                    if (tableId == 0 || tableId > s.drops.size())
                    {
                        ctx.Error(ctx.Where("DropEntry", i, "table_id"), "없는 드롭 표 " + N(tableId));
                        continue;
                    }
                    SnapItem::Drop& d = s.drops[tableId - 1u];
                    if (seq != d.entries.size() + 1)
                        ctx.Error(ctx.Where("DropEntry", i, "seq"), "seq는 표마다 1부터 연속(표 " + N(tableId) + " → " + N(seq) + ")");
                    e.src = static_cast<uint32_t>(i);
                    d.entries.push_back(e);
                }
            // 굴림 분포를 미리 계산한다(예전 DropTable.cpp가 스레드마다 만들던 것과 같은 가중치 · 같은 순서 — index 0 = 드롭 없음)
            for (SnapItem::Drop& d : s.drops)
            {
                std::vector<double> weights;
                weights.reserve(d.entries.size() + 1);
                weights.push_back(static_cast<double>(d.noDropWeight));
                double total = static_cast<double>(d.noDropWeight);
                for (const auto& e : d.entries)
                {
                    weights.push_back(static_cast<double>(e.weight));
                    total += static_cast<double>(e.weight);
                }
                if (total > 0)   // 0이면 R098로 로드 실패
                    d.dist = std::discrete_distribution<size_t>::param_type(weights.begin(), weights.end());
            }
        }

        void BindShops(LoadContext& ctx, SnapItem& s)
        {
            const Json* rows = ctx.Rows("Shop");
            if (rows == nullptr)
                return;
            for (size_t i = 0; i < rows->size(); ++i)
            {
                RowReader r(ctx, "Shop", i, (*rows)[i]);
                SnapItem::ShopLine line;
                r.Int("npc_id", line.npcId).Int("seq", line.seq).Int("item_id", line.itemId);
                r.Finish();
                s.shopLines.push_back(line);
                if (!r.Ok())
                    continue;
                auto it = std::find_if(s.shops.begin(), s.shops.end(), [&](const WorldData::ShopRow& x) { return x.npcId == line.npcId; });
                if (it == s.shops.end())
                {
                    WorldData::ShopRow row{};
                    row.npcId = line.npcId;
                    s.shops.push_back(row);
                    it = std::prev(s.shops.end());
                }
                const size_t used = static_cast<size_t>(std::count_if(std::begin(it->items), std::end(it->items), [](uint16_t x) { return x != 0; }));
                if (line.itemId == 0)
                    ctx.Error(ctx.Where("Shop", i, "item_id"), "0은 빈 칸 표시라 쓸 수 없다");
                else if (line.seq != used + 1)
                    ctx.Error(ctx.Where("Shop", i, "seq"), "seq는 NPC마다 1부터 연속(NPC " + N(line.npcId) + " → " + N(line.seq) + ")");
                else if (used >= kShopSlots)
                    ctx.Error(ctx.Where("Shop", i, "seq"), "NPC마다 최대 " + N(kShopSlots) + "칸(ShopRow::items — 구조 상한)");
                else
                    it->items[used] = line.itemId;
            }
        }
    }

    void BindItem(LoadContext& ctx, GameDataSnapshot& snap)
    {
        SnapItem& s = snap.item;
        BindItems(ctx, s);
        BindOptions(ctx, s);
        BindSets(ctx, s);
        BindRarity(ctx, s);
        BindEnchant(ctx, s);
        BindDrops(ctx, s);
        BindShops(ctx, s);
    }

    // ── 3) Validate — 예전 static_assert · constexpr 검사 · 시작 시점 ASSERT_CRASH의 규칙 ──
    void ValidateItem(ValidateContext& v, const GameDataSnapshot& snap)
    {
        const SnapItem& s = snap.item;
        auto findRow = [&s](ItemTableId id) -> const ItemTableRow*
        {
            return id < s.slotById.size() && s.slotById[id] != 0 ? &s.rows[s.slotById[id] - 1u] : nullptr;
        };

        // R025 · R026 — 예전 static_assert(sizeof(kOptions) / sizeof(kOptions[0]) == ItemOption::Count) · kProcs
        if (s.options.size() != static_cast<size_t>(ItemOption::Count))
            v.Fail("R025", "ItemOption", "행 수 " + N(s.options.size()) + " ≠ ItemOption::Count(" + N(static_cast<size_t>(ItemOption::Count)) + ")");
        for (size_t i = 0; i < s.optionId.size(); ++i)
            if (static_cast<size_t>(s.optionId[i]) != i)
                v.Fail("R025", v.Where("ItemOption", i, "id"), "행 순서 = 옵션 값이어야 한다(행 " + N(i + 1) + " → " + N(static_cast<size_t>(s.optionId[i])) + ")");
        if (s.procs.size() != static_cast<size_t>(ItemProc::Count))
            v.Fail("R026", "ItemProc", "행 수 " + N(s.procs.size()) + " ≠ ItemProc::Count(" + N(static_cast<size_t>(ItemProc::Count)) + ")");
        for (size_t i = 0; i < s.procId.size(); ++i)
            if (static_cast<size_t>(s.procId[i]) != i)
                v.Fail("R026", v.Where("ItemProc", i, "id"), "행 순서 = 특수 효과 값이어야 한다(행 " + N(i + 1) + " → " + N(static_cast<size_t>(s.procId[i])) + ")");

        // R030(폐지 — 강화 성능이 시트 값이 됐다) → R099: 성공 몫 ≤ 판정 눈금(1조 = 100%)
        for (size_t i = 0; i < s.enchant.size(); ++i)
            if (s.enchant[i].successTickets > EnchantRule::kRollScale)
                v.Fail("R099", v.Where("Enchant", i, "success_tickets"), "성공 몫 " + std::to_string(s.enchant[i].successTickets) + " > 1조(100%)");

        // 상점이 파는 아이템(R090 — 상점에서 산 그대로 되판 값). 진열 칸의 아이템 참조는 아래 상점 참조 검사가 본다
        std::vector<bool> shopSold(s.slotById.size(), false);
        for (const auto& shop : s.shops)
            for (const uint16_t id : shop.items)
                if (id != 0 && id < shopSold.size())
                    shopSold[id] = true;

        // R082~R085 — 예전 ItemTableData::Detail::ValidateV2Rows(), R087~R091 — 예전 ItemTable.cpp IsItemTableValid()
        for (size_t i = 0; i < s.rows.size(); ++i)
        {
            const ItemTableRow& row = s.rows[i];
            if (row.type != ItemType::eEquipment)
            {
                const char* bad = row.equipSlot != EquipSlot::None ? "equip_slot" : row.armorBonus != 0 ? "armor_bonus"
                    : row.magicArmorBonus != 0 ? "magic_armor_bonus" : row.setId != 0 ? "set_id" : nullptr;
                if (bad != nullptr)
                    v.Fail("R082", v.Where("Item", i, bad), "장비가 아니면 equip_slot None · armor_bonus 0 · magic_armor_bonus 0 · set_id 0");
            }
            else
            {
                if (row.tier < 1 || row.tier > 5)
                    v.Fail("R083", v.Where("Item", i, "tier"), "장비는 단계 1~5 — 지금 " + N(row.tier));
                const bool weapon = ItemTableData::SlotOf(row) == EquipSlot::Weapon;
                if (weapon != (row.weaponKind != WeaponKind::None))
                    v.Fail("R084", v.Where("Item", i, "weapon_kind"), weapon ? "무기는 weapon_kind가 있어야 한다" : "무기가 아닌 장비는 weapon_kind None");
                if (!weapon && row.attackBonus != 0)
                    v.Fail("R085", v.Where("Item", i, "attack_bonus"), "무기가 아닌 장비는 attack_bonus 0 — 지금 " + N(row.attackBonus));
            }
            if (row.id == ITEM_TABLE_ID_NONE)
                v.Fail("R087", v.Where("Item", i, "id"), "id 0은 예약값(빈 슬롯 / 없음)");
            if (row.displayName == nullptr || row.displayName[0] == '\0')
                v.Fail("R088", v.Where("Item", i, "display_name"), "표시 이름이 비었다(클라가 그대로 그린다)");
            if (row.type == ItemType::eEquipment && row.maxStack != 1)
                v.Fail("R089", v.Where("Item", i, "max_stack"), "장비는 겹치지 않는다(max_stack 1) — 지금 " + N(row.maxStack));
            // R090 — 사서 되팔기로 그로시가 늘면 안 된다. 되팔 때는 sell_price × 희귀도 매입가 배율(Rarity.sell_percent) / 100을 준다
            // (Inventory::TrySell → ItemRules::SellPrice). 상점에서 산 장비는 등급의 바닥 희귀도(ItemRules::FloorRarity — eRare → 고급) ·
            // +0이라(Inventory::TryBuy — extra 0) 상점이 파는 물건은 그 값으로 본다.
            if (row.buyPrice != 0 && row.sellPrice > row.buyPrice)
                v.Fail("R090", v.Where("Item", i, "sell_price"), "매입가 " + N(row.sellPrice) + " > 판매가 " + N(row.buyPrice) + "(사서 되팔기로 그로시 복사)");
            else if (row.buyPrice != 0 && row.id < shopSold.size() && shopSold[row.id])
            {
                const uint32_t resale = ItemRules::SellPrice(row, 0, 0);
                if (resale > row.buyPrice)
                {
                    const Rarity floor = ItemRules::FloorRarity(row);
                    v.Fail("R090", v.Where("Item", i, "sell_price"), "상점에서 산 그대로 되팔면 " + N(resale) + "(매입가 " + N(row.sellPrice) + " × 희귀도 " +
                        ItemRules::RarityName(floor) + " " + N(ItemRules::RaritySellPercent(floor)) + "%) > 판매가 " + N(row.buyPrice) + "(사서 되팔기로 그로시 복사)");
                }
            }
            const uint16_t first = row.id < s.slotById.size() ? s.slotById[row.id] : 0;
            if (first != 0 && first != i + 1)
                v.Fail("R091", v.Where("Item", i, "id"), "id " + N(row.id) + "가 " + v.Where("Item", first - 1u) + "와 겹친다");
        }

        // R093 · R096 · R097 · R098 — 예전 DropTable.cpp ValidateDropTable()(시작 시점 ASSERT_CRASH).
        // R094 · R095(type · grade = Item 값)는 구조로 보장 — DropEntry에 그 열이 없다(모르는 열은 로드 오류).
        for (size_t t = 0; t < s.drops.size(); ++t)
        {
            const SnapItem::Drop& d = s.drops[t];
            uint32_t total = d.noDropWeight;
            for (const auto& e : d.entries)
            {
                const ItemTableRow* row = findRow(e.itemId);
                if (row == nullptr)
                    v.Fail("R093", v.Where("DropEntry", e.src, "item_id"), "없는 아이템 " + N(e.itemId));
                if (!(e.minCount >= 1 && e.minCount <= e.maxCount))
                    v.Fail("R096", v.Where("DropEntry", e.src, "min_count"), "1 ≤ min_count(" + N(e.minCount) + ") ≤ max_count(" + N(e.maxCount) + ")");
                if (row != nullptr && e.maxCount > row->maxStack)
                    v.Fail("R097", v.Where("DropEntry", e.src, "max_count"), "max_count " + N(e.maxCount) + " > 아이템 최대 겹치기 " + N(row->maxStack));
                total += e.weight;
            }
            if (total == 0)
                v.Fail("R098", v.Where("DropTable", t, "no_drop_weight"), "no_drop_weight + 가중치 합이 0이다(굴릴 수 없다)");
        }

        // 상점 참조(R번호 없음 — 시트 check의 ref 검사와 같은 것을 부팅 때도): NPC는 NPC 묶음 파사드, 아이템은 이 스냅샷
        for (size_t i = 0; i < s.shopLines.size(); ++i)
        {
            const SnapItem::ShopLine& line = s.shopLines[i];
            if (WorldData::FindNpc(line.npcId) == nullptr)
                v.Fail("", v.Where("Shop", i, "npc_id"), "없는 NPC " + N(line.npcId));
            if (line.itemId != 0 && findRow(line.itemId) == nullptr)
                v.Fail("", v.Where("Shop", i, "item_id"), "없는 아이템 " + N(line.itemId));
        }
    }

    // ── 4) Dump — 정본 쓰기(Canon_Item.h) ──
    void DumpItem(Canon::Book& book, const GameDataSnapshot& snap)
    {
        const SnapItem& s = snap.item;
        Canon::WriteItem(book, s.rows, [&s](uint32_t id) -> const char*
        {
            for (size_t i = 0; i < s.rows.size(); ++i)
                if (s.rows[i].id == id) return s.codeName[i];
            return nullptr;
        });
        Canon::WriteItemOption(book);
        Canon::WriteItemProc(book);
        Canon::WriteItemSet(book, s.sets);
        {
            std::vector<Canon::RarityV> rs;
            for (const auto& r : s.rarities)
                rs.push_back({ r.name, r.color, r.optionCount, r.sellPercent, r.lootNotice });
            std::vector<uint16_t> w;
            for (const auto& row : s.stepWeights)
                for (const uint16_t x : row) w.push_back(x);
            Canon::WriteRarity(book, rs, w);
        }
        {
            std::vector<Canon::EnchantV> rows;
            for (const auto& e : s.enchant)
                rows.push_back({ e.level, e.successTickets, e.graceAttempts, e.statGain });
            const auto& c = s.enchantConst;
            Canon::WriteEnchant(book, rows, { c.worldNoticeLevel, c.firstNoticeLevel, c.gainPerFurtherLevel, c.accessoryHpPerPoint, c.accessoryMpPerPoint });
        }
        {
            std::vector<Canon::DropTableV> drops;
            for (const auto& d : s.drops)
            {
                Canon::DropTableV x;
                x.id = d.id;
                x.noDropWeight = d.noDropWeight;
                for (const auto& e : d.entries)
                    x.entries.push_back({ e.itemId, e.weight, e.minCount, e.maxCount });
                drops.push_back(std::move(x));
            }
            Canon::WriteDrop(book, drops);
        }
        {
            std::vector<Canon::ShopV> shops;
            for (const auto& row : s.shops)
            {
                Canon::ShopV x;
                x.npcId = row.npcId;
                for (const uint16_t it : row.items)
                    if (it != 0) x.items.push_back(it);
                shops.push_back(std::move(x));
            }
            Canon::WriteShop(book, shops);
        }
    }
}
