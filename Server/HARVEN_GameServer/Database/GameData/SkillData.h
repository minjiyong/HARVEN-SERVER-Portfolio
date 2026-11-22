#pragma once

#include "Database/GameData/CombatData.h"

#include <cmath>
#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// 기술 구조 V3(Phase 2-B) — 로스트아크식 칸(C · QWERASDF · V · Space · Z)과 유형 · 타격 시각표. 서버 · 클라 공용.
//
//  · SkillTable(CombatData.h)의 행은 이름 · 자원 · 사거리 · 조준 · 상태이상 · 연계 같은 "무엇"이고,
//    이 표(SkillDefTable)는 "언제 · 어디를 · 어떻게"다 — 유형, 타격 시각(ms) 배열, 타격마다 모양 · 계수 · 반응 · 무력화,
//    차지 · 콤보 · 체인 단계, 홀딩 · 토글 틱, 슈퍼아머 · 무적 구간, 연출 등급.
//  · 서버와 클라가 같은 타격 시각표를 쓴다 → 연출의 임팩트와 서버 피해가 맞는다.
//    지연 타격은 서버가 그 시각에 모양을 다시 계산한다(예고를 보고 피할 수 있다).
//  · 기술 요청(SKILL_REQ)은 모두 이 표로 판정한다.
//  · 타격이 없는 행(hitCount = 0)은 예전 판정(SkillHelper Execute)을 이 표의 시각에 부른다 — 기존 18기술의 연계를 그대로 살린다.
// ─────────────────────────────────────────────────────────────────────────────

enum class SkillKey : uint8_t { C, Q, W, E, R, A, S, D, F, V, Space, Z, Count };
inline constexpr int kSkillKeys = static_cast<int>(SkillKey::Count);

enum class SkillKind : uint8_t
{
    Basic,      // 기본 공격 — 콤보(stages타, 입력 창 안에 다시 누르면 다음 타)
    Normal,     // 일반(누르면 바로)
    Charge,     // 차지: 누르고 있다 떼면 단계(stages)별 강화, 마지막 perfectMs = 완벽 구간
    Hold,       // 홀딩: 누르는 동안 tickMs마다 타격, 떼면(또는 holdMaxMs) 마무리 타격
    Cast,       // 캐스팅: castMs 뒤 지점에
    Chain,      // 체인: 입력 창(stageMs) 안에 다시 누르면 다음 단계(stages까지)
    Toggle,     // 토글: 켜 둔 동안 tickMs마다 MP 소모(최대 holdMaxMs), 다시 누르면 끔
    Move,       // 이동기(Space) — 다른 기술의 후딜을 끊는다
    Identity,   // 고유 기술(Z) — 직업 게이지가 가득일 때
    Awaken,     // 각성기(V) — 암전 · 컷인 · 무적
};

enum class HitShape : uint8_t { Target, Circle, Cone, Line, Ring };
enum class HitAnchor : uint8_t { Caster, Aim, Target };   // 타격 시각의 시전자 위치 · 시전 때 정한 지점 · 대상(따라간다)
enum class HitReact : uint8_t { None, Flinch, Knockback, Launch, Knockdown, Pull };

enum HitFlag : uint8_t
{
    HF_Status   = 1 << 0,   // 표의 상태이상(SkillRow.status)을 건다
    HF_Heavy    = 1 << 1,   // 강타(큰 히트스톱 · DAMAGE2_HEAVY)
    HF_Counter  = 1 << 2,   // 반격: 보스 강공격 예고를 끊는다
    HF_Execute  = 1 << 3,   // 단죄 규칙: 기절 · 체력 30% 이하 대상 ×2
    HF_Tick     = 1 << 4,   // 홀딩 · 토글의 반복 타격(tickMs마다)
    HF_Release  = 1 << 5,   // 홀딩 · 차지를 놓는 순간(atMs = 놓은 뒤)
    HF_Perfect  = 1 << 6,   // 차지 완벽 구간에서만
    HF_Final    = 1 << 7,   // 마지막 타(각성기 총 피해 · 크게)
};

enum class SkillSpecial : uint8_t
{
    None,
    Legacy,        // 예전 판정(SkillHelper Execute)을 castMs 뒤 한 번
    ShieldBounce,  // 방패 투척: 1단 = 적 사이 3번 튕김, 2단 = 튕긴 적을 끌어당기며 되돌아옴
    Rally,         // 맹세 함성: 주변 도발 + 나에게 함성(공격 +15%)
    Roll,          // 몸 굴리기: 3칸 + 짧은 무적
    OathRelease,   // 서약 해방
    Barrier,       // 결계 고리: 둔화 + 표식
    Resonance,     // 새김 울림: 8칸 안의 표식 대상이 차례로 폭발
    Clones,        // 먹물 분신: 다음 3타를 분신 둘이 40%로 따라 한다
    Veil,          // 밤 장막: 은신 + 이속 + 다음 기술 급소
    PoisonMist,    // 맹독 안개: 지점 장판(4초) — 틱마다 중독 · 둔화
    MoonCleave,    // 달 가르기: 차지 → 직선 돌진(지나간 적은 0.3초 뒤 한꺼번에)
    DuskAwaken,    // 그믐 베기(각성): 최대 7대상 사이 순간이동 → 한꺼번에 폭발, 중독 폭발
    Dodge,         // 잔상 회피(이동기)
    Blink,         // 옛길 도약(이동기)
    Dash,          // 돌진
    ShadowStep,    // 그늘 걸음
};

struct SkillHit
{
    uint16_t  atMs = 0;            // 시전(차지 · 홀딩은 놓은 순간) 기준
    uint16_t  coeff = 100;         // 공격력 × %
    HitShape  shape = HitShape::Target;
    HitAnchor anchor = HitAnchor::Caster;
    uint8_t   a = 0;               // 반경 · 길이(칸)
    uint8_t   b = 0;               // 부채꼴: 반각(°) / 직선: 폭(칸) / 고리: 안쪽 반경
    int8_t    along = 0;           // 중심을 조준 방향으로 이만큼(칸)
    HitReact  react = HitReact::None;
    uint8_t   stagger = 0;         // 무력화 수치
    uint8_t   flags = 0;           // HitFlag
    uint8_t   stage = 0;           // 기본 공격: 콤보 몇 타(1~) · 차지: 최소 단계 · 체인: 몇 단(1~). 0 = 언제나
    uint8_t   maxTargets = 0;      // 0 = 무제한
    // ── Phase 2-D 갈래(Data/SkillGrowth.h ApplyDef가 복사본에만 쓴다 — 표에는 0) ──
    int8_t    turn = 0;            // 조준 방향에서 돌린 각(°) — 세 줄기
    uint8_t   tag = 0;             // HitTag(메아리 · 세 줄기로 덧붙은 타격)
    uint8_t   src = 0;             // 덧붙은 타격이 따라 한 원래 타격 번호(연출이 같은 모양을 다시 그린다)
};

enum HitTag : uint8_t { HT_None = 0, HT_Echo = 1, HT_Twin = 2 };

struct SkillDef
{
    uint16_t     id = 0;           // SkillTable id
    SkillKey     key = SkillKey::C;
    SkillKind    kind = SkillKind::Normal;
    uint8_t      tier = 2;         // 연출 등급 T1~T4(DESIGN_SYSTEM 10장)
    SkillSpecial special = SkillSpecial::None;
    uint32_t     cooldownMs = 0;   // 재사용 대기(0 = SkillRow.cooldownMs) — 각성기 90초는 16비트를 넘는다
    uint16_t     castMs = 0;       // 첫 타 기준 예고(각성기 연출 시간 포함)
    uint16_t     lockMs = 300;     // 동작 잠금(후딜) — 이동기(Space)로 끊을 수 있다
    uint16_t     superArmorMs = 0; // 시전 시작부터 경직 면역
    uint16_t     invulnMs = 0;     // 무적(이동기 · 각성기)
    uint8_t      stages = 0;       // 콤보 타수 · 차지 단계 · 체인 단수
    uint16_t     stageMs = 0;      // 차지: 한 단계 시간 / 콤보 · 체인: 다음 입력 창
    uint16_t     perfectMs = 0;    // 차지: 마지막 완벽 구간
    uint16_t     holdMaxMs = 0;    // 홀딩 · 토글 최대
    uint16_t     tickMs = 0;       // 홀딩 · 토글 틱
    uint8_t      tickMp = 0;       // 토글: 틱마다 MP
    bool         moveWhileCasting = false;
    uint8_t      range = 0;        // 사거리(0 = SkillRow.range) — 각성기처럼 예전보다 멀리 닿는 것
    uint8_t      moveMs = 0;       // 기술 이동의 칸당 시간(ms) — 돌진 · 구르기 · 회피 · 달 가르기. 0 = 순간이동(도약 · 그늘 걸음)
    uint8_t      hitCount = 0;
    SkillHit     hits[8] = {};
    const char*  kindText = "";    // 툴팁 한 줄(유형 · 판정)
};

// 기술 판정 표(SKILL_REQ) — 파사드. 값은 GameData/SkillAction.json · SkillHit.json(hits[8] → 자식 탭, seq 1부터) ·
// SkillKeyMap.json(직업 × 칸 → 기본 기술) — 부팅 때 로더가 불변 스냅샷에 둔다(Data/Load/Bind_Skill.cpp).
// 규칙(예전 static_assert(Detail::Validate()) → 부팅 검증 R006~R009): SkillAction id마다 Skill 행이 있다 ·
// SkillKeyMap(직업, 칸) = 그 id · 부채꼴(Cone) 타격은 b > 0 · 세 직업 × 12칸이 모두 SkillAction에 있다.
// 구조 상수: 타격은 기술마다 8개까지(hits[8]) — 넘으면 로드 실패(이관 전 빌더는 조용히 버렸다).
namespace SkillDefTable
{
    // 행 수(0번 빈 행 포함 — 이관 전 kCount)
    [[nodiscard]] int Count() noexcept;

    // id → 행(0 · 없는 id면 nullptr). 행은 id 오름차순(정본) — id가 겹치지 않아(로드 검증) 이관 전 순차 검색과 같은 행
    [[nodiscard]] const SkillDef* Find(uint16_t id) noexcept;

    // 직업 · 칸 → 기술 id(기본 배치 — 칸은 클라 설정으로 바꿀 수 있고, 서버는 id로 받는다). 범위 밖이면 0. 값은 SkillKeyMap 탭
    [[nodiscard]] uint16_t Default(ClassId cls, SkillKey key) noexcept;

    // 기술 → 칸(재사용 대기 칸). 이 직업의 기술이 아니면 Count
    [[nodiscard]] inline SkillKey KeyOf(uint16_t id) noexcept
    {
        const SkillDef* d = Find(id);
        return d ? d->key : SkillKey::Count;
    }

    // 재사용 대기(ms) — 표에 따로 있으면 그것(각성기 90초), 없으면 SkillRow. 쿨다운 감소 %는 SkillTable과 같은 규칙
    [[nodiscard]] constexpr uint32_t CooldownMs(const SkillDef& d, const SkillRow& row, uint16_t cdr) noexcept
    {
        const uint32_t r = cdr > 50 ? 50 : cdr;
        const uint32_t base = d.cooldownMs > 0 ? d.cooldownMs : row.cooldownMs;
        return base * (100u - r) / 100u;
    }

    // 차지 단계(0 = 모자람, 1~stages) · 완벽 구간
    [[nodiscard]] constexpr uint8_t ChargeStage(const SkillDef& d, uint32_t heldMs) noexcept
    {
        if (d.stageMs == 0) return 1;
        const uint32_t s = heldMs / d.stageMs;
        return static_cast<uint8_t>(s >= d.stages ? d.stages : (s < 1 ? 1 : s));
    }
    [[nodiscard]] constexpr uint32_t ChargeMaxMs(const SkillDef& d) noexcept { return static_cast<uint32_t>(d.stageMs) * d.stages + d.perfectMs; }
    // 누르고 있기의 첫 틱(ms) — 그 전 0.12초가 예고(힘이 모이는 빛). 서버 판정 · 클라 예측 · 기술 창 시연이 같이 쓴다
    inline constexpr uint16_t kHoldFirstTickMs = 120;
    // 누르고 있기(Hold) · 켜 두기(Toggle) 틱 간격의 바닥(ms) — 표는 R103이 막고, 틱을 거는 곳(UserSkillHelper.cpp)도 이 값으로 자른다.
    // 0이면 틱이 지연 없이 연달아 걸려 Zone · 타이머 스레드가 넘친다.
    inline constexpr uint16_t kMinTickMs = 50;
    [[nodiscard]] constexpr bool ChargePerfect(const SkillDef& d, uint32_t heldMs, uint32_t slackMs) noexcept
    {
        if (d.perfectMs == 0) return false;
        const uint32_t full = static_cast<uint32_t>(d.stageMs) * d.stages;
        return heldMs + slackMs >= full && heldMs <= full + d.perfectMs + slackMs;
    }

    // 타격의 방향(칸 공간 단위 벡터) — 세 줄기(갈래)로 덧붙은 타격은 turn°만큼 돌린다. 서버 · 클라가 판정 전에 부른다
    inline void TurnDir(const SkillHit& h, float& dx, float& dy) noexcept
    {
        if (h.turn == 0) return;
        const float a = static_cast<float>(h.turn) * 3.14159265f / 180.f;
        const float c = std::cos(a), s = std::sin(a);
        const float nx = dx * c - dy * s, ny = dx * s + dy * c;
        dx = nx; dy = ny;
    }

    // 판정 모양(칸 공간) — 서버 판정과 클라 예측 연출이 같은 함수를 쓴다.
    //   원 = 체비셰프 반경 · 부채꼴 = 유클리드 반경 + 0.5 안, 조준 방향과의 각 ≤ b° · 직선 = 투영 0 < t ≤ a + 0.5, 옆 ≤ b/2 + 0.35 · 고리 = b ≤ 체비셰프 ≤ a
    [[nodiscard]] inline bool InsideHit(const SkillHit& h, float cx, float cy, float dx, float dy, float x, float y) noexcept
    {
        const float rx = x - cx, ry = y - cy;
        const float ax = rx < 0.f ? -rx : rx, ay = ry < 0.f ? -ry : ry;
        const float cheb = ax > ay ? ax : ay;
        switch (h.shape)
        {
        case HitShape::Target: return cheb < 0.5f;
        case HitShape::Circle: return cheb <= static_cast<float>(h.a) + 0.001f;
        case HitShape::Ring: return cheb <= static_cast<float>(h.a) + 0.001f && cheb >= static_cast<float>(h.b) - 0.001f;
        case HitShape::Cone:
        {
            const float dist = std::sqrt(rx * rx + ry * ry);
            if (dist > h.a + 0.5f) return false;
            if (dist < 0.5f) return true;
            return (rx * dx + ry * dy) / dist >= std::cos(static_cast<float>(h.b) * 3.14159265f / 180.f);
        }
        case HitShape::Line:
        {
            const float t = rx * dx + ry * dy;
            if (t <= 0.f || t > h.a + 0.5f) return false;
            const float side = rx * dy - ry * dx;
            return (side < 0.f ? -side : side) <= static_cast<float>(h.b) * 0.5f + 0.35f;
        }
        }
        return false;
    }
}
