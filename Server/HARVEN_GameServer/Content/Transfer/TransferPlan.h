#pragma once

#include "Database/GameData/Item/ItemTable.h"
#include "Database/GameData/ItemRules.h"

namespace TransferPlan
{
    constexpr UInt32 kMaxAdena = 2'000'000'000u;
    using Bag = std::array<DB_ITEM_SLOT_SAVE, MAX_INVENTORY_SLOTS>;
    inline bool Equal(const DB_ITEM_SLOT_SAVE& a, const DB_ITEM_SLOT_SAVE& b)
    {
        return a.slotIndex == b.slotIndex && a.hasItem == b.hasItem && (!a.hasItem ||
            (a.itemId == b.itemId && a.count == b.count && a.equipped == b.equipped && a.enchant == b.enchant && a.extra == b.extra));
    }

    // 먼저 양쪽 출고를 반영한 사본에 입고한다. 메모리/DB는 변경하지 않는다.
    inline TransferResult Build(TransferKind kind, const std::array<Bag, 2>& before,
        const std::array<std::vector<DB_ITEM_SLOT_SAVE>, 2>& offers,
        const std::array<UInt32, 2>& wallets, UInt32 price, DB_TRANSFER_SAVE& out)
    {
        if (offers[0].empty() || (kind == TransferKind::Exchange ? offers[1].empty() : !offers[1].empty()))
            return TransferResult::BadItem;
        if (kind == TransferKind::Sale ? price == 0 || price > kMaxAdena : price != 0)
            return TransferResult::Invalid;
        auto after = before;
        for (int side = 0; side < 2; ++side)
        {
            std::bitset<MAX_INVENTORY_SLOTS> seen;
            if (offers[side].size() > kTransferMaxItems) return TransferResult::BadCount;
            for (const auto& o : offers[side])
            {
                if (o.slotIndex >= MAX_INVENTORY_SLOTS || seen.test(o.slotIndex)) return TransferResult::BadItem;
                seen.set(o.slotIndex);
                auto& b = after[side][o.slotIndex];
                const auto* row = ItemTable::Find(o.itemId);
                if (!b.hasItem || !row || row->type == ItemType::eCurrency || b.itemId != o.itemId || b.enchant != o.enchant || b.extra != o.extra)
                    return TransferResult::BadItem;
                if (b.equipped) return TransferResult::Equipped;
                if (ItemRules::IsBound(*row, b.extra)) return TransferResult::Bound;
                if (o.count == 0 || o.count > b.count) return TransferResult::BadCount;
                b.count -= o.count;
                if (b.count == 0) b = DB_ITEM_SLOT_SAVE{ o.slotIndex };
            }
        }
        for (int side = 0; side < 2; ++side)
            for (const auto& o : offers[1 - side])
            {
                const auto* row = ItemTable::Find(o.itemId);
                UInt16 left = o.count;
                if (row->maxStack > 1)
                    for (auto& slot : after[side])
                    {
                        if (!slot.hasItem || slot.itemId != o.itemId || slot.enchant != o.enchant || slot.extra != o.extra) continue;
                        const auto n = static_cast<UInt16>(std::min<UInt32>(left, row->maxStack - std::min(row->maxStack, slot.count)));
                        slot.count += n; left -= n;
                        if (!left) break;
                    }
                for (auto& slot : after[side])
                {
                    if (!left) break;
                    if (slot.hasItem) continue;
                    const UInt16 n = std::min(left, row->maxStack);
                    const auto index = slot.slotIndex;
                    slot = o; slot.slotIndex = index; slot.count = n; slot.hasItem = true; slot.equipped = false;
                    slot.extra |= ItemRules::TradedBitFor(*row);
                    left -= n;
                }
                if (left) return TransferResult::BagFull;
            }
        auto resultWallets = wallets;
        if (kind == TransferKind::Sale)
        {
            if (wallets[1] < price) return TransferResult::LackAdena;
            if (wallets[0] > kMaxAdena - price) return TransferResult::WalletFull;
            resultWallets[0] += price; resultWallets[1] -= price;
        }
        for (int side = 0; side < 2; ++side)
        {
            auto& p = out.players[side];
            p = {}; p.adenaBefore = wallets[side]; p.adenaAfter = resultWallets[side];
            for (int i = 0; i < MAX_INVENTORY_SLOTS; ++i)
                if (!Equal(before[side][i], after[side][i]))
                { p.before.push_back(before[side][i]); p.after.push_back(after[side][i]); }
        }
        return TransferResult::Ok;
    }
}
