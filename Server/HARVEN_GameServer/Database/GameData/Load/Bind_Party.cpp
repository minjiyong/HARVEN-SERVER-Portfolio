#include "pch.h"
// ─────────────────────────────────────────────────────────────────────────────
// 파티 묶음 — 탭 PartyConst(한 행) → 스냅샷 → 파사드 PartyRule::*.
// 모양은 Bind_Monster.cpp(모범 표)와 같다.
//   1) 파사드 — 시트 값을 읽는 함수(구조 상수 · 경험치 나누기 · 제목 검사는 헤더 PartyRules.h)
//   2) Bind    — 열마다 RowReader
//   3) Validate — R114(인원 ≤ kMaxMembers) · R115(줍기 우선권 순서 · 사라지기 전) · R116(경험치 나눔 거리 ≤ 시야 섹터 셋)
//   4) Dump    — Canon_Party.h(datatool export와 같은 바이트)
// ─────────────────────────────────────────────────────────────────────────────
#include <string>

#include "Database/GameData/Load/Bundles.h"
#include "Database/GameData/Load/Canon_Party.h"
#include "Database/GameData/Load/JsonRead.h"
#include "Database/GameData/Load/Snapshot.h"
#include "Database/GameData/Load/Validate.h"
#include "Database/GameData/PartyRules.h"

// ── 1) 파사드 ────────────────────────────────────────────────────────────────
namespace PartyRule
{
    uint8_t  MaxMembers() noexcept { return GD().party.c.maxMembers; }
    uint32_t InviteExpireMs() noexcept { return GD().party.c.inviteExpireMs; }
    uint32_t OfflineKeepSec() noexcept { return GD().party.c.offlineKeepSec; }
    uint16_t SyncIntervalMs() noexcept { return GD().party.c.syncIntervalMs; }
    uint8_t  ExpShareRange() noexcept { return GD().party.c.expShareRange; }
    uint8_t  CarryLevelRange() noexcept { return GD().party.c.carryLevelRange; }
    uint8_t  CarryMinExpPct() noexcept { return GD().party.c.carryMinExpPct; }
    uint32_t LootOwnerMs() noexcept { return GD().party.c.lootOwnerMs; }
    uint32_t LootPartyMs() noexcept { return GD().party.c.lootPartyMs; }
    uint32_t RecruitIdleSec() noexcept { return GD().party.c.recruitIdleSec; }
}

namespace GameDataLoad
{
    // 떨어진 물건이 사라지는 시간(CItemHelper::SpawnFieldItem — 30초). 우선권은 그 전에 풀려야 남이 주울 틈이 있다.
    inline constexpr uint32_t kFieldItemLifeMs = 30000;
    // 경험치 나눔 거리의 위 — 시야 섹터 셋(SECTOR_RANGE × 3 = 30칸). 그보다 멀면 "함께 사냥"이 아니다
    inline constexpr uint8_t kMaxShareRange = SECTOR_RANGE * 3;

    // ── 2) Bind ──────────────────────────────────────────────────────────────
    void BindParty(LoadContext& ctx, GameDataSnapshot& snap)
    {
        SnapParty& p = snap.party;
        if (const Json* rows = ctx.Rows("PartyConst"))
        {
            if (rows->size() != 1)
                ctx.Error("PartyConst", "한 행이어야 한다(" + std::to_string(rows->size()) + "행)");
            for (size_t i = 0; i < rows->size(); ++i)
            {
                RowReader r(ctx, "PartyConst", i, (*rows)[i]);
                Canon::PartyConstV c;
                r.Int("max_members", c.maxMembers).Int("invite_expire_ms", c.inviteExpireMs).Int("offline_keep_sec", c.offlineKeepSec)
                 .Int("sync_interval_ms", c.syncIntervalMs).Int("exp_share_range", c.expShareRange).Int("carry_level_range", c.carryLevelRange)
                 .Int("carry_min_exp_pct", c.carryMinExpPct).Int("loot_owner_ms", c.lootOwnerMs).Int("loot_party_ms", c.lootPartyMs)
                 .Int("recruit_idle_sec", c.recruitIdleSec);
                r.Finish();
                if (i == 0)
                    p.c = c;
            }
        }
    }

    // ── 3) Validate ──────────────────────────────────────────────────────────
    void ValidateParty(ValidateContext& v, const GameDataSnapshot& snap)
    {
        const Canon::PartyConstV& c = snap.party.c;
        // R114 — 인원은 2 ~ kMaxMembers(패킷 · 배열 크기). 시트 check는 스키마 min · max
        if (c.maxMembers < 2 || c.maxMembers > PartyRule::kMaxMembers)
            v.Fail("R114", v.Where("PartyConst", 0, "max_members"),
                "파티 인원은 2 ~ " + std::to_string(PartyRule::kMaxMembers) + "(PartyRule::kMaxMembers — 패킷 줄 수) — 지금 " + std::to_string(c.maxMembers));
        // R115 — 줍기 우선권: 주인 ≤ 주인 + 파티 < 사라지는 시간(30초)
        if (c.lootPartyMs < c.lootOwnerMs)
            v.Fail("R115", v.Where("PartyConst", 0, "loot_party_ms"),
                "주인 + 파티 시간(" + std::to_string(c.lootPartyMs) + ")이 주인만 시간(" + std::to_string(c.lootOwnerMs) + ")보다 짧다");
        else if (c.lootPartyMs >= kFieldItemLifeMs)
            v.Fail("R115", v.Where("PartyConst", 0, "loot_party_ms"),
                "주인 + 파티 시간(" + std::to_string(c.lootPartyMs) + ")이 물건이 사라지는 시간(" + std::to_string(kFieldItemLifeMs) + "ms) 이상 — 아무도 못 줍는 물건이 된다");
        // R116 — 경험치 나눔 거리 1 ~ 시야 섹터 셋
        if (c.expShareRange < 1 || c.expShareRange > kMaxShareRange)
            v.Fail("R116", v.Where("PartyConst", 0, "exp_share_range"),
                "경험치 나눔 거리는 1 ~ " + std::to_string(kMaxShareRange) + "칸 — 지금 " + std::to_string(c.expShareRange));
    }

    // ── 4) Dump ──────────────────────────────────────────────────────────────
    void DumpParty(Canon::Book& book, const GameDataSnapshot& snap)
    {
        Canon::WriteParty(book, snap.party.c);
    }
}
