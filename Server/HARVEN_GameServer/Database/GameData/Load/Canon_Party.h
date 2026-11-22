#pragma once
// 정본 표 쓰기 — 파티 묶음(탭 PartyConst — 한 행).
#include <cstdint>

#include "Database/GameData/Load/Canon.h"

namespace Canon
{
    struct PartyConstV
    {
        uint8_t  maxMembers = 0;
        uint32_t inviteExpireMs = 0;
        uint32_t offlineKeepSec = 0;
        uint16_t syncIntervalMs = 0;
        uint8_t  expShareRange = 0;
        uint8_t  carryLevelRange = 0;
        uint8_t  carryMinExpPct = 0;
        uint32_t lootOwnerMs = 0;
        uint32_t lootPartyMs = 0;
        uint32_t recruitIdleSec = 0;
    };

    inline void WriteParty(Book& book, const PartyConstV& c)
    {
        Table& t = book.Get("PartyConst");
        Row w;
        w.Put("max_members", c.maxMembers).Put("invite_expire_ms", c.inviteExpireMs).Put("offline_keep_sec", c.offlineKeepSec)
         .Put("sync_interval_ms", c.syncIntervalMs).Put("exp_share_range", c.expShareRange).Put("carry_level_range", c.carryLevelRange)
         .Put("carry_min_exp_pct", c.carryMinExpPct).Put("loot_owner_ms", c.lootOwnerMs).Put("loot_party_ms", c.lootPartyMs)
         .Put("recruit_idle_sec", c.recruitIdleSec);
        t.Add(w);
    }
}
