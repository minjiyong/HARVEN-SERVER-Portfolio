#pragma once
// 묶음 등록표 — 묶음마다 Bind_<묶음>.cpp가 세 함수를 정의한다(SHEET_DATA_PROMPT [10] 충돌 0 설계).
//   Bind<묶음>     : GameData/<탭>.json → 스냅샷(형 · 열 검사는 RowReader)
//   Validate<묶음> : 옮겨 온 규칙(RULES.md R번호) — 모든 묶음을 묶은 뒤에 부른다(묶음 간 참조)
//   Dump<묶음>     : 스냅샷 → 정본 표(Canon_<묶음>.h — datatool export와 같은 바이트)
// 순서(GameData.cpp kBundles): 참조되는 쪽 먼저 — 마을 → 지형 → 몬스터 → 몬스터 AI → 아이템 → 경제(아이템 행으로 라인업 색인) → 레벨 → 스킬 → 기술성장 → NPC → 퀘스트 → 파티 → 결사 성장 → 개인 업적(몬스터 · 퀘스트 · 아이템 · 강화 · 지역 · 경제 표를 가리킨다).
// 이 파일 · GameData.cpp는 메인만 고친다. 일꾼은 자기 Bind_<묶음>.cpp · Snap_<묶음>.h만.
#include "Database/GameData/Load/Canon.h"

struct GameDataSnapshot;

namespace GameDataLoad
{
    class LoadContext;
    class ValidateContext;

#define HARVEN_DATA_BUNDLE(Name) \
    void Bind##Name(LoadContext& ctx, GameDataSnapshot& snap); \
    void Validate##Name(ValidateContext& v, const GameDataSnapshot& snap); \
    void Dump##Name(Canon::Book& book, const GameDataSnapshot& snap);

    HARVEN_DATA_BUNDLE(Town)
    HARVEN_DATA_BUNDLE(Region)
    HARVEN_DATA_BUNDLE(Monster)
    HARVEN_DATA_BUNDLE(MonsterAI)
    HARVEN_DATA_BUNDLE(Item)
    HARVEN_DATA_BUNDLE(Econ)
    HARVEN_DATA_BUNDLE(Level)
    HARVEN_DATA_BUNDLE(Skill)
    HARVEN_DATA_BUNDLE(Growth)
    HARVEN_DATA_BUNDLE(Npc)
    HARVEN_DATA_BUNDLE(Quest)
    HARVEN_DATA_BUNDLE(Party)
    HARVEN_DATA_BUNDLE(Guild)
    HARVEN_DATA_BUNDLE(Achieve)

#undef HARVEN_DATA_BUNDLE
}
