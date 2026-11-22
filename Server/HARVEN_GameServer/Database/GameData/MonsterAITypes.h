#pragma once

#include <cstddef>
#include <cstdint>

#include "Database/GameData/SkillData.h"      // HitShape · HitAnchor · HitReact · StatusId(CombatData.h)
#include "Database/GameData/MonsterTypes.h"   // MonsterPattern

// ─────────────────────────────────────────────────────────────────────────────
// 몬스터 AI · 기술 — 형(열거형 · 행 구조체). 값은 GameData/MonsterAI.json · MonsterAISkill.json · MonsterSkill.json ·
// MonsterSkillHit.json · MonsterAoE.json(시트 HARVEN_DATA의 같은 이름 표)에서 온다(파사드: Database/GameData/MonsterAITable.h).
//
//  · MonsterAI      종족마다 한 행 — 어떤 행동 트리(ai)로 · 어디까지 감지하고(aggro_range) · 누구를 돕고(assist_range) ·
//                   보스 단계 문턱 · 두르는 오라. 행이 없는 종족은 근접 · 기술 없음(예전 평타 몬스터와 같다).
//  · MonsterAISkill 종족의 기술 칸(최대 8) — 어느 기술을 · 어떤 우선순위 · 가중치 · 조건(단계 · 체력 · 동료 수 · 확률)으로.
//  · MonsterSkill   기술 하나의 수명: 시전(예고) → 발사(이동 · 장판 · 자기 강화) → 타격(최대 8, 시각표) → 후딜 → 재사용 대기.
//                   REIW 기술 BT(T_Skill_Cast → Fire → Hit → Cooldown)의 단계를 표 한 줄로 적는다.
//  · MonsterSkillHit 타격 시각표(발사 기준 at_ms) — 모양 · 기준점 · 계수 · 상태이상 · 예고.
//  · MonsterAoE     장판(REIW AreaOfEffect · Buff-Aura) — 놓인 자리(또는 주인을 따라)에서 간격마다 적에게 피해 · 상태,
//                   동료에게 회복 · 강화.
// 구조 상수: 기술 하나의 타격 kMonsterSkillHits개, 종족 하나의 기술 칸 kMonsterAiSkills개 — 넘으면 로드 실패.
// ─────────────────────────────────────────────────────────────────────────────

inline constexpr int kMonsterSkillHits = 8;
inline constexpr int kMonsterAiSkills = 8;

// 행동 트리 종류 — 서버(MonsterBehaviorHelper)가 종류마다 트리 하나를 만들어 그 종류의 몬스터가 함께 쓴다.
enum class MonsterAiType : uint8_t
{
    Melee,        // 근접: 쫓아가 친다(기본)
    Ranged,       // 원거리: keep_range를 지키며 기술 · 투척, 붙으면 물러선다
    Support,      // 지원: 동료 곁에서 강화 · 회복 장판, 붙으면 물러선다
    Boss,         // 보스: 체력 문턱마다 단계(phase2_hp · phase3_hp) — 단계에 들어서면 그 단계 기술(phase2_skill · phase3_skill), 기술 칸은 단계로 거른다
    Stationary,   // 고정: 움직이지 않는다(토템 · 포탑) — 사거리 안이면 기술 · 평타
    Dummy,        // 허수아비: 아무것도 하지 않는다(깨어나지 않는다)
};

// 기술 대상
enum class MonsterSkillTarget : uint8_t
{
    Enemy,        // 지금 대상(플레이어) — 거리가 range_min ~ range_max일 때
    Self,         // 자기 중심 — 지금 대상이 range_max 안일 때(주변 광역 · 자기 강화)
    Ally,         // 동료 하나(체력 비율이 가장 낮은 · range_max 안) — 회복 · 강화
    AllyGroup,    // 동료 무리(range_max 안에 동료가 allies_min 이상) — 무리 강화 · 회복 장판
};

// 발사 순간의 이동
enum class MonsterMove : uint8_t
{
    None,
    Dash,         // 조준 방향 직선 돌진 — move_tiles칸(막히거나 대상 앞이면 거기까지), 칸당 move_ms
    Leap,         // 조준 칸(막혔으면 옆 빈 칸)으로 뛰어든다 — 전체 move_ms(포물선)
    Blink,        // 조준 칸 옆으로 순간이동
    Retreat,      // 대상 반대쪽으로 move_tiles칸 물러선다 — 칸당 move_ms
};

// 장판이 무엇에 닿는가
enum class AoeAffect : uint8_t
{
    Enemies,      // 플레이어(마을 밖 · 살아 있는)
    Allies,       // 몬스터(살아 있는 · 주인 포함)
    All,
};

// 시전 · 타격 · 장판 틱 때 클라에 보내는 효과(EFFECT_NFY) — 서버가 EffectId로 옮긴다(MonsterSkillHelper).
// 클라가 이미 그리는 효과를 고른다. 몬스터 전용 연출이 생기면 여기에만 이어 붙인다(끝에만 추가).
enum class MonsterFx : uint8_t
{
    None,
    BossSlam,     // 시전: 보스 광역기 예고(붉은 원 1.2초 · 경고 문구) · 타격: 큰 흔들림
    Fire,         // 불길(잔불)
    Poison,       // 독(맹독)
    Frost,        // 서리
    Thunder,      // 벼락
    Rune,         // 마력 폭발
    Steam,        // 증기 폭발
    Heal,         // 금빛 기둥(대상 몸에)
    Haste,        // 가속 소용돌이
    Guard,        // 방패
};

struct MonsterSkillHit
{
    uint16_t  atMs = 0;                       // 발사 기준 타격 시각(ms)
    uint16_t  warnMs = 0;                     // 이 타격의 예고를 몇 ms 앞서 보낼지(0 = 예고 없음). 시전 시작보다 앞이면 시전 시작에 보낸다
    uint16_t  coeff = 100;                    // 피해 = 몬스터 공격력 × %(0 = 피해 없음 — 상태 · 예고만)
    HitShape  shape = HitShape::Circle;       // Target(한 칸) · Circle · Cone · Line · Ring
    HitAnchor anchor = HitAnchor::Caster;     // Caster = 시전을 시작한 자리 · Aim = 시전 때 정한 조준 칸 · Target = 타격 순간의 대상 자리
    uint8_t   a = 0;                          // 반경 · 길이(칸). 직선(Line)이 0이면 이번 이동 경로 길이
    uint8_t   b = 0;                          // 부채꼴 반각(°) · 직선 폭(칸) · 고리 안쪽 반경
    int8_t    along = 0;                      // 중심을 조준 방향으로 이만큼(칸)
    HitReact  react = HitReact::None;         // 플레이어 반응: Knockback(밀기) · Pull(당기기) — push칸
    uint8_t   push = 0;
    StatusId  status = StatusId::None;        // 맞은 플레이어에게
    uint16_t  statusMs = 0;
    uint8_t   statusStacks = 1;
    uint8_t   maxTargets = 0;                 // 0 = 무제한
};

struct MonsterSkillRow
{
    uint16_t           id = 0;
    const char*        name = "";                           // 기획 메모(클라에 나가지 않는다)
    MonsterSkillTarget target = MonsterSkillTarget::Enemy;
    uint8_t            rangeMin = 0;                        // 시작 거리(체비셰프, 대상 기준)
    uint8_t            rangeMax = 1;
    uint16_t           castMs = 0;                          // 예고(시전) — 이 뒤에 발사
    uint16_t           lockMs = 300;                        // 마지막 타격 뒤 멈춤(후딜)
    uint32_t           cooldownMs = 5000;                   // 재사용 대기(시전 시작부터)
    uint16_t           cooldownJitterMs = 0;                // 0 ~ 이 값을 더한다(같은 무리가 한꺼번에 쓰지 않게)
    MonsterPattern     pattern = MonsterPattern::None;      // 클라 예고 연출(차오름 끝 효과 — 돌진 · 내려찍기 · 투척 · 도약)
    MonsterFx          fx = MonsterFx::None;                // 시전 · 타격 때 효과
    MonsterMove        move = MonsterMove::None;
    uint8_t            moveTiles = 0;
    uint16_t           moveMs = 0;                          // Dash · Retreat: 칸당 ms · Leap: 전체 ms · Blink: 쓰지 않는다
    bool               counterable = true;                  // 예고 중 기절 · 빙결 · 반격 · 무력화에 끊긴다
    uint16_t           aoe = 0;                             // 발사 순간 놓는 장판(MonsterAoE.id, 0 = 없음)
    HitAnchor          aoeAnchor = HitAnchor::Caster;       // 장판 자리(타격의 anchor와 같은 뜻)
    StatusId           selfStatus = StatusId::None;         // 발사 순간 자신에게(광폭 · 방어 태세)
    uint16_t           selfStatusMs = 0;
    uint16_t           selfStatusPower = 0;                 // 강화 세기(%) — Empower · Fortify · Swift
    uint8_t            hitCount = 0;
    MonsterSkillHit    hits[kMonsterSkillHits] = {};
};

struct MonsterAoeRow
{
    uint16_t    id = 0;
    const char* name = "";               // 기획 메모
    uint32_t    durationMs = 0;
    uint16_t    intervalMs = 1000;       // 틱 간격(바닥 kMinAoeIntervalMs — R109)
    uint16_t    firstMs = 0;             // 놓인 뒤 첫 틱까지
    uint8_t     radius = 1;              // 체비셰프
    AoeAffect   affect = AoeAffect::Enemies;
    uint16_t    coeff = 0;               // 적 틱 피해 = 놓은 몬스터 공격력 × %
    uint8_t     healPercent = 0;         // 동료 회복 = 최대 HP × %
    StatusId    status = StatusId::None; // 닿은 대상에게(적이면 해로운 상태, 동료면 강화)
    uint16_t    statusMs = 0;
    uint16_t    statusPower = 0;         // 강화 세기(%) — Empower · Fortify · Swift
    uint8_t     statusStacks = 1;
    bool        follow = false;          // 주인을 따라 움직인다(오라)
    bool        ownerBound = true;       // 주인이 죽거나 잠들면 사라진다
    MonsterFx   fx = MonsterFx::None;    // 틱마다 효과(가운데 · 닿은 대상)
};

struct MonsterAiSkill
{
    uint16_t skillId = 0;
    uint8_t  priority = 0;     // 높을수록 먼저 본다
    uint8_t  weight = 1;       // 같은 우선순위끼리 무작위 가중치
    uint8_t  phaseMin = 1;     // 이 단계 사이에서만(보스 — 1 ~ 3)
    uint8_t  phaseMax = 3;
    uint8_t  hpBelow = 0;      // 내 체력이 이 % 이하일 때만(0 = 조건 없음)
    uint8_t  alliesMin = 0;    // 기술 사거리 안에 동료가 이 수 이상일 때만
    uint8_t  chance = 100;     // 쓸 수 있을 때 이 확률(%)로만
};

struct MonsterAiRow
{
    uint16_t       monsterId = 0;
    MonsterAiType  ai = MonsterAiType::Melee;
    uint8_t        aggroRange = 0;     // 선공 감지(칸) — 0 = 맞을 때만 반격
    uint8_t        assistRange = 0;    // 이 칸 안의 동료가 싸우기 시작하면 함께 싸운다(0 = 돕지 않는다)
    uint8_t        keepRange = 0;      // 원거리 · 지원: 대상과 지키는 거리(0 = 붙는다)
    uint16_t       skillGapMs = 1500;  // 기술 사이 최소 간격(후딜 끝부터)
    uint16_t       firstSkillMs = 0;   // 전투 시작 뒤 첫 기술까지
    uint8_t        phase2Hp = 0;       // 보스 단계 문턱(체력 %, 0 = 없음) — phase2 > phase3
    uint8_t        phase3Hp = 0;
    uint16_t       phase2Skill = 0;    // 2단계에 들어설 때 곧바로 쓰는 기술(0 = 없음)
    uint16_t       phase3Skill = 0;    // 3단계에 들어설 때(2단계를 건너뛰어 곧바로 3단계여도 이것)
    uint16_t       aura = 0;           // 깨어 있는 동안 두르는 장판(MonsterAoE.id, follow — R112)
    uint8_t        skillCount = 0;
    MonsterAiSkill skills[kMonsterAiSkills] = {};
};
