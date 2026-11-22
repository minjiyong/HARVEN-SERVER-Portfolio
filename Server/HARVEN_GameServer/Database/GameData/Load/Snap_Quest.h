#pragma once
// 스냅샷 — 퀘스트 묶음(탭 Quest · QuestObjective · Poi · Chapter). Bind_Quest.cpp가 채우고, 파사드 QuestData::*가 읽는다.
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "Database/GameData/QuestData.h"

struct SnapQuest
{
    // id 색인 크기 — R049(id 1~255)라 256칸이면 모든 퀘스트가 O(1). 넘는 id는 순차 검색(이관 전과 같은 답).
    static constexpr size_t kIndex = 256;

    std::vector<QuestData::QuestRow> rows;      // Quest.json 행 순서(export가 id 순으로 쓴다) — 목표 objs[]는 QuestObjective에서
    std::array<int16_t, kIndex> byId{};         // id → rows 위치(-1 = 없음 — Bind가 채운다). 같은 id가 여럿이면 첫 행(이관 전 순차 검색과 같다)
    // rows[i].objs[s]가 온 QuestObjective.json 행(검증 오류 위치). -1 = 빈 목표(표에 없음 — 기본값 None)
    std::vector<std::array<int16_t, QuestData::kMaxObjs>> objSrc;

    std::vector<QuestData::Poi> pois;           // Poi.json 행 순서
    std::vector<QuestData::Chapter> chapters;   // 행 i = 장 i(0번 빈 장 포함, 정확히 kChapterCount + 1)
};
