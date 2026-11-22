#pragma once

#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// 전투 데이터 — 직업 · 능력치 규칙 · 스킬 · 상태이상. 서버·클라 공용.
//
// 서버는 이 표로 판정(사거리·쿨다운·MP·피해·상태이상·연계)하고, 클라이언트는 같은
// 표로 스킬바/툴팁/사거리 표시/예측 연출을 한다. 모양(이펙트·소리)은 클라 몫이다.
//
// 설계 원칙
//  · 직업마다 "플레이 방식"이 다르다: 서약기사=버티며 반격, 각인술사=거리 두고 제어·폭딜,
//    밤사냥꾼=파고들어 뒤를 잡고 연타. 이름 규칙은 HARVEN-DESIGN/WORLD.md.
//  · 연계는 짧고 직관적이다: "상태를 걸고 → 그 상태에 강한 기술로 터뜨린다".
//    연계가 성공하면 서버가 DAMAGE_COMBO + comboId를 실어 보내고 클라가 크게 연출한다.
// ─────────────────────────────────────────────────────────────────────────────

// 묶음별 헤더로 나눴다(내용 이동만 — 이관 Phase 1). 이 헤더는 예전처럼 전부를 원래 순서로 include한다.
#include "Database/GameData/Combat/CombatIds.h"
#include "Database/GameData/Combat/StatusTable.h"
#include "Database/GameData/Combat/Stats.h"
#include "Database/GameData/Combat/ClassTable.h"
#include "Database/GameData/Combat/StatRule.h"
#include "Database/GameData/Combat/SkillTypes.h"
#include "Database/GameData/Combat/SkillTable.h"
