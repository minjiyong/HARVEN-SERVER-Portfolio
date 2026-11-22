#pragma once
// 스냅샷 — NPC대사 묶음(탭 Npc · NpcLine). Bind_Npc.cpp가 채우고, 파사드 WorldData::FindNpc · NpcTalk::Find …가 읽는다.
// NPC 집 자리(NpcRow.x · y)는 Npc 탭이 아니라 마을 원본(TownNpcHome)에서 온다 — Bind가 채운다.
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "Database/GameData/NpcTalk.h"
#include "Database/GameData/World/WorldNpc.h"

struct SnapNpc
{
    // id 색인 크기 — R067(id 1~63)이라 64칸이면 모든 NPC가 O(1). 넘는 id는 순차 검색(이관 전과 같은 답).
    static constexpr size_t kIndex = 64;

    std::vector<WorldData::NpcRow> rows;        // Npc.json 행 순서(export가 id 순으로 쓴다)
    std::vector<const char*> codeName;          // 행과 같은 순서 — NpcIds 이름(없으면 nullptr)
    std::array<int16_t, kIndex> byId{};         // id → rows 위치(-1 = 없음 — Bind가 채운다). 같은 id가 여럿이면 첫 행(이관 전 순차 검색과 같다)

    std::vector<NpcTalk::Lines> lines;          // NpcLine을 NPC마다 묶은 것(npc_id가 처음 나온 순서)
    std::array<int16_t, kIndex> linesById{};    // npc_id → lines 위치(-1 = 없음 — Bind가 채운다)
};
