#pragma once
#include <sstream>
#include <bitset>

namespace TransferSave
{
    inline bool Valid(const DB_TRANSFER_SAVE& s)
    {
        if (s.receipt.size() != 32 || s.kind > 2 || s.playerIds[0] <= 0 || s.playerIds[1] <= 0 || s.playerIds[0] == s.playerIds[1]) return false;
        for (int side = 0; side < 2; ++side)
        {
            const auto& p = s.players[side];
            if (p.before.size() != p.after.size() || p.before.size() > MAX_INVENTORY_SLOTS || p.adenaBefore > 2'000'000'000u || p.adenaAfter > 2'000'000'000u) return false;
            std::bitset<MAX_INVENTORY_SLOTS> seen;
            for (size_t i = 0; i < p.before.size(); ++i)
            {
                const auto& b = p.before[i]; const auto& a = p.after[i];
                if (b.slotIndex >= MAX_INVENTORY_SLOTS || b.slotIndex != a.slotIndex || seen.test(b.slotIndex)) return false;
                seen.set(b.slotIndex);
                if (a.hasItem && (a.itemId == 0 || a.count == 0)) return false;
            }
        }
        return UInt64(s.players[0].adenaBefore) + s.players[1].adenaBefore == UInt64(s.players[0].adenaAfter) + s.players[1].adenaAfter;
    }
    inline std::string Json(const DB_TRANSFER_SAVE& save)
    {
        std::ostringstream j; j << "{\"kind\":" << unsigned(save.kind) << ",\"players\":[";
        for (int side = 0; side < 2; ++side)
        {
            const auto& p = save.players[side];
            if (side) j << ',';
            j << "{\"id\":" << save.playerIds[side] << ",\"beforeAdena\":" << p.adenaBefore << ",\"afterAdena\":" << p.adenaAfter << ",\"offers\":[";
            for (size_t i = 0; i < save.offers[side].size(); ++i)
            {
                if (i) j << ',';
                const auto& o = save.offers[side][i];
                j << "{\"slot\":" << o.slotIndex << ",\"item\":" << o.itemId << ",\"count\":" << o.count
                  << ",\"enchant\":" << o.enchant << ",\"extra\":" << o.extra << '}';
            }
            j << "],\"slots\":[";
            for (size_t i = 0; i < p.before.size(); ++i)
            {
                if (i) j << ',';
                const auto& b = p.before[i]; const auto& a = p.after[i];
                j << "{\"slot\":" << b.slotIndex << ",\"has\":" << b.hasItem << ",\"item\":" << b.itemId << ",\"count\":" << b.count
                  << ",\"equipped\":" << b.equipped << ",\"enchant\":" << b.enchant << ",\"extra\":" << b.extra
                  << ",\"keep\":" << a.hasItem << ",\"newItem\":" << a.itemId << ",\"newCount\":" << a.count
                  << ",\"newEquipped\":" << a.equipped << ",\"newEnchant\":" << a.enchant << ",\"newExtra\":" << a.extra << '}';
            }
            j << "]}";
        }
        j << "]}"; return j.str();
    }
}
