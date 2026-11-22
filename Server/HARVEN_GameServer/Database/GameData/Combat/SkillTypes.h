#pragma once
// 기술 형(SkillTarget · SkillShape · DamageKind · SkillRow).
// CombatData.h에서 나눴다(내용 이동만 — 이관 Phase 1). 우산 헤더 Data/CombatData.h가 원래 순서로 include한다.
#include <cstddef>
#include <cstdint>
#include "Database/GameData/Combat/CombatIds.h"

// ── 스킬 ──────────────────────────────────────────────────────────────────────
enum class SkillTarget : uint8_t
{
    Self,       // 자기 주변
    Enemy,      // 대상 지정(몬스터/플레이어)
    Ground,     // 지점 지정(마우스 칸)
    Direction,  // 방향(마우스 쪽) — 돌진·회피
};

enum class SkillShape : uint8_t
{
    Single,     // 대상 하나
    Around,     // 시전자 주변 radius
    Circle,     // 지점 주변 radius
    Line,       // 시전자에서 방향으로 radius칸
    Chain,      // 대상 → 주변으로 연쇄
};

enum class DamageKind : uint8_t { Physical, Magic };

struct SkillRow
{
    uint16_t    id;
    ClassId     cls;
    uint8_t     slot;          // 0 = 기본 공격, 1~5
    const char* name;
    const char* desc;
    const char* combo;         // 연계 설명(툴팁)
    uint8_t     unlockLevel;
    uint16_t    mp;
    uint16_t    cooldownMs;    // 기본 공격은 ClassRow.basicIntervalMs를 공격속도로 나눈 값
    uint16_t    castMs;        // 시전(예고) 시간 — 서버가 이만큼 뒤에 판정한다(피할 수 있다)
    uint8_t     range;         // 체비셰프 칸
    SkillTarget target;
    SkillShape  shape;
    uint8_t     radius;
    DamageKind  kind;
    uint16_t    coeff;         // 공격력 × %
    uint8_t     hits;          // 타수
    StatusId    status;        // 적중 시 거는 상태
    uint16_t    statusMs;
    uint8_t     statusStacks;
    StatusId    comboStatus;   // 대상이 이 상태면 연계
    uint16_t    comboBonus;    // 연계 피해 +%
    uint8_t     comboId;       // 연계 이름(클라 표시) — 0 없음
    bool        consumeCombo;  // 연계 후 상태 제거
    uint8_t     knockback;     // 칸
    uint8_t     dash;          // 이동 칸(돌진/도약/회피)
    uint16_t    projectile;    // 투사체 속도(칸/초 ×10, 표시용) 0 = 즉발
    uint32_t    color;         // 표시 색
};
