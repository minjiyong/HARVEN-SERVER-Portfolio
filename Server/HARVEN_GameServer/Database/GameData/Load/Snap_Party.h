#pragma once
// 스냅샷 — 파티 묶음(탭 PartyConst — 한 행).
// Bind_Party.cpp가 채우고, 파사드 PartyRule::MaxMembers · LootOwnerMs …가 읽는다.
#include "Database/GameData/Load/Canon_Party.h"

struct SnapParty
{
    Canon::PartyConstV c{};
};
