#pragma once

#include <array>
#include <vector>

#include "Base/Core/Types.h"
#include "Base/ObjID/ObjID.h"
#include "Database/GameData/SkillData.h"

// ─────────────────────────────────────────────────────────────────────────────
// 플레이어 기술 일정 — REIW Content/Skill(CSkillBase · CSkillScheduler · CSkillSequence)을 하르벤 플레이어에 맞게 옮겼다.
//
// REIW는 시전마다 CSkillBase 게임 객체를 만들고(BT: T_Skill_Cast → T_Skill_Fire → T_Skill_Hit → T_Skill_Terminate,
// 재사용 대기는 T_Skill_Cooldown) 액터의 CSkillScheduler가 그 시전들을 들고 있다(IsCooldown · IsUsing · CancelCasting · Clear).
// 하르벤은 몬스터(MonsterCast · CSkillScheduler)처럼 시전을 작은 구조체(UserCast)로 두고, 시전이 남긴 일정(타격표 한 줄 ·
// 틱 · 사슬 = UserCastStep)을 플레이어마다 한 줄로 모아 "가장 이른 일정"에 한 번만 깨운다(EV_SKILL_IMPACT + 세대 번호 —
// 몬스터 EV_MONSTER_THINK와 같은 방식). 예전에는 타격 · 틱마다 타이머 하나였다(타이머 스레드 → 전역 조회 잠금을 그만큼).
//
//   단계(EnumSkillStage — REIW T_Skill_*)
//     Cast       시전 알림(SKILL_NFY)을 보냈고 시전 창이 열려 있다 — 차지(놓을 때까지) · 홀딩 · 토글.
//                걷기 · 다른 기술 · 기절이 끊는다(CancelCasting). 캐스팅(castMs) 창은 아래 casting이 따로 센다.
//     Fire       발사(REIW ExecuteBehavior) — 이동기 · 돌진 · 특수 동작 · 차지 놓음 · 홀딩 끝(잠깐 거치는 단계)
//     Hit        남은 일정이 있다(타격표 · 홀딩 틱 · 안개 · 장판 · 울림 · 튕김 …)
//     Terminate  끝 — 일정에서 빠진다
//   재사용 대기(REIW IsCooldown)는 칸(SkillKey)마다 다시 쓸 수 있는 시각이다 — 거의 모든 기술은 누를 때부터 센다
//   (REIW CooldownStartType::Start), 토글만 끌 때부터 센다(End).
//   연속 입력(REIW CSkillSequence)은 기본 공격 콤보(몇 타) · 체인(다음 단 입력 창)이다.
// 소유 Zone 스레드 전용.
// ─────────────────────────────────────────────────────────────────────────────

enum class EnumSkillStage : UInt8
{
    Cast,
    Fire,
    Hit,
    Terminate,
};

// 시전이 남긴 일정의 종류(예전 SkillCast::CastEvent와 같은 값)
enum class EnumSkillStep : UInt8
{
    None = 0,
    Hit,            // 타격표 한 줄(hit = 번호)
    Legacy,         // 예전 판정(SkillDetail::Execute)
    HoldTick,
    HoldTimeout,
    ChargeTimeout,
    ToggleTick,
    MistTick,       // 맹독 안개 틱(extra = 남은 틱)
    Resonance,      // 새김 울림 폭발(target)
    Bounce,         // 방패 투척 튕김(extra = 몇 번째)
    MarkHit,        // 기억한 대상(그믐 베기 · 달 가르기 · 방패 되튕김)에게 타격표 한 줄
    Linger,         // 갈래 「장판」 틱(stage = 반경, extra = 남은 틱)
};

// 일정 한 줄(예전 CUser::PendingImpact + 언제 · 어느 시전)
struct UserCastStep
{
    UInt32        dueAt = 0;       // GetNowTime
    UInt32        order = 0;       // 같은 시각이면 넣은 순서(예전 타이머 큐와 같다)
    UInt32        castId = 0;      // 이 일정을 남긴 시전(UserCast::id)
    UInt16        skillId = 0;
    ObjID         target{};
    short         x = 0, y = 0;
    EnumSkillStep kind = EnumSkillStep::None;
    UInt8         hit = 0;         // 타격표 번호
    UInt8         stage = 0;       // 콤보 타 · 차지 단계 · 체인 단
    UInt8         flags = 0;       // 완벽 · 강제 공격
    UInt32        castSeq = 0;     // 남길 때의 시전 번호(분신은 시전마다 한 번)
    short         ox = 0, oy = 0;  // 시전 위치(REIW CCasterPosition)
    float         dx = 1.f, dy = 0.f;   // 조준 방향(칸 공간 단위 벡터)
    UInt32        extra = 0;
    UInt8         grow = 0;        // 시전 때의 기술 성장 상태(SkillGrowth::Pack) — 갈래를 도중에 바꿔도 이 시전은 그대로
};

// 시전 하나(REIW CSkillBase)
struct UserCast
{
    UInt32         id = 0;          // 일정 안의 번호(일정 한 줄이 이것으로 시전을 찾는다)
    UInt16         skillId = 0;
    SkillKind      kind = SkillKind::Normal;
    EnumSkillStage stage = EnumSkillStage::Cast;
    UInt32         castSeq = 0;     // 시작 때의 시전 번호
    UInt32         beginAt = 0;     // 시작 시각(차지 · 홀딩은 모으기 · 돌기를 시작한 때)
    UInt16         pending = 0;     // 남은 일정 수 — 0이고 시전 창이 닫혀 있으면 끝(Terminate)
};

class CUserSkillScheduler
{
public:
    // ── 재사용 대기(REIW IsCooldown) — 칸(SkillKey)마다 다시 쓸 수 있는 시각 ──
    std::array<UInt32, static_cast<size_t>(SkillKey::Count)> keyReadyAt{};
    // ── 시전 ──
    UInt32 castSeq = 0;                  // 시전마다 증가 — 차지를 놓을 때 · 홀딩이 끝날 때 · 체인 단마다도 새 번호
    std::vector<UserCast> casts;         // 진행 중인 시전(보통 0~3개)
    std::vector<UserCastStep> steps;     // 그 시전들이 남긴 일정
    // 캐스팅 창(SkillKind::Cast) — 마지막 캐스팅 기술 하나: 창이 열려 있는 동안 걸으면 끊기고(창 뒤 타격을 버린다),
    // 이동기를 쓰거나 다른 캐스팅을 시작하면 창 뒤 타격을 버린다(castingSeq가 바뀐다).
    UInt32 castingUntil = 0;
    UInt32 castingSeq = 0;
    // ── 연속 입력(REIW CSkillSequence) ──
    UInt8  comboStage = 0;   UInt32 comboAt = 0;                       // 기본 공격 콤보
    UInt16 chainSkill = 0;   UInt8  chainStage = 0;  UInt32 chainUntil = 0;   // 체인 다음 단 입력 창
    // ── 판정 기억 ──
    UInt32 lastCloneCast = 0;            // 먹물 분신: 시전마다 한 번만 줄인다
    UInt32 dodgeAt = 0;                  // 잔상 회피 시각(완벽 회피 판정)
    std::vector<ObjID> marks;            // 방패 투척 · 그믐 베기 · 달 가르기가 기억한 대상
    // ── 깨우기(EV_SKILL_IMPACT) ──
    UInt32 wakeAt = 0;                   // 걸어 둔 깨우기 시각(wakeArmed일 때)
    UInt32 wakeEpoch = 0;                // 깨우기 세대 — 더 이른 깨우기를 걸면 앞의 것은 낡는다
    bool   wakeArmed = false;
    UInt32 nextCastId = 0;
    UInt32 nextOrder = 0;

    [[nodiscard]] UserCast* Find(UInt32 castId) noexcept
    {
        for (UserCast& c : casts)
            if (c.id == castId)
                return &c;
        return nullptr;
    }

    // 시전 창이 열린 차지 · 홀딩 · 토글(종류마다 하나뿐 — 새로 시작하기 전에 앞의 것을 끊는다)
    [[nodiscard]] UserCast* FindCasting(SkillKind kind) noexcept
    {
        for (UserCast& c : casts)
            if (c.kind == kind && c.stage == EnumSkillStage::Cast)
                return &c;
        return nullptr;
    }

    // REIW IsUsing — 차지 · 홀딩 · 토글 중 하나라도 켜져 있다(기술 성장을 바꿀 수 없다)
    [[nodiscard]] bool IsChanneling() noexcept
    {
        return FindCasting(SkillKind::Charge) != nullptr || FindCasting(SkillKind::Hold) != nullptr || FindCasting(SkillKind::Toggle) != nullptr;
    }
};
