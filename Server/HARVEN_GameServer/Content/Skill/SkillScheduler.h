#pragma once

#include <array>

#include "Base/Core/Types.h"
#include "Base/ObjID/ObjID.h"
#include "Database/GameData/MonsterAITypes.h"

// ─────────────────────────────────────────────────────────────────────────────
// 몬스터 기술 일정(REIW Content/Skill/SkillScheduler — IsCooldown · IsUsing) + 지금 시전 하나(MonsterCast).
//
// REIW는 시전마다 CSkillBase 게임 객체를 만들고 그 안의 BT(T_Skill_Cast → Fire → Hit → Terminate)가 단계를 돈다.
// 하르벤은 시전 상태를 이 작은 구조체 하나로 두고, 기술 잎(T_UseSkill)이 다음 단계 시각에 깨워 달라고 남겨
// 행동 트리가 그 시각에 단계를 넘긴다(CMonsterSkillHelper::Advance) — 시전마다 할당 · 별도 타이머 사슬이 없다.
//
//   시전(Cast)   예고(MONSTER_TELEGRAPH_NFY) · 끊길 수 있다(기절 · 빙결 · 반격 · 무력화)
//   발사(Fire)   이동(SUBJECT_FORCED_MOVE_NFY) · 장판 · 자기 강화
//   타격(Hit)    시각표의 타격마다 모양 · 계수 · 상태이상(예고했던 자리에 — 피할 수 있다)
//   끝(Terminate) 후딜 뒤 다음 기술까지 간격(skill_gap_ms). 재사용 대기는 시전 시작부터 칸마다.
// 소유 Zone 스레드 전용.
// ─────────────────────────────────────────────────────────────────────────────

struct MonsterCast
{
    static constexpr UInt8 kPhaseSlot = 0xFE;   // 단계 기술(보스 — 칸이 아니다)
    static constexpr int   kMaxPath = 9;        // 돌진 경로(8칸 + 막은 칸)

    UInt16 skillId = 0;            // 0 = 시전 없음
    UInt8  slot = 0;               // 기술 칸(kPhaseSlot = 단계 기술)
    UInt8  stage = 0;              // 0 시전(예고) · 1 발사 뒤(타격 · 후딜)
    bool   countered = false;      // 예고 중 반격 · 무력화를 맞았다(다음 생각에 끊긴다)
    UInt32 startAt = 0;            // 시전 시작(GetNowTime)
    UInt32 fireAt = 0;             // 발사 = startAt + cast_ms
    UInt32 endAt = 0;              // 후딜 끝(마지막 타격 + lock_ms) — 발사 뒤 정한다
    short  fromX = 0, fromY = 0;   // 시전 시작 자리(Caster 기준점)
    short  aimX = 0, aimY = 0;     // 조준 칸(Aim 기준점)
    float  dirX = 1.f, dirY = 0.f; // 조준 방향(단위 벡터 — 직선 · 부채꼴)
    ObjID  targetId{};
    UInt8  hitDone = 0;            // 비트 i = 타격 i를 끝냈다
    UInt8  hitWarned = 0;          // 비트 i = 타격 i의 예고를 보냈다(그때 중심을 정했다)
    std::array<short, kMonsterSkillHits> hitX{};
    std::array<short, kMonsterSkillHits> hitY{};
    // 돌진 경로(직선 a = 0 타격이 맞히는 칸 — 지나간 칸 + 막은 칸)
    UInt8  pathCount = 0;
    std::array<short, kMaxPath> pathX{};
    std::array<short, kMaxPath> pathY{};

    [[nodiscard]] bool Active() const noexcept { return skillId != 0; }
};

class CSkillScheduler
{
public:
    // 칸마다 다시 쓸 수 있는 시각(GetNowTime, 0 = 바로)
    std::array<UInt32, kMonsterAiSkills> slotReadyAt{};
    UInt32 nextSkillAt = 0;        // 다음 기술을 시작할 수 있는 시각(기술 사이 간격)
    UInt8  lastSlot = 0xFF;        // 마지막에 쓴 칸(같은 칸을 잇달아 덜 고르게)
    MonsterCast cast{};

    // 처음부터(깨어날 때 · 집에 돌아왔을 때) — 모든 칸이 지금 바로 쓸 수 있다
    void Reset(UInt32 now) noexcept
    {
        slotReadyAt.fill(now);
        nextSkillAt = now;
        lastSlot = 0xFF;
        cast = MonsterCast{};
    }

    [[nodiscard]] bool IsCasting() const noexcept { return cast.Active(); }
    // 예고 중(아직 발사 전) — 반격 · 기절로 끊을 수 있는 구간
    [[nodiscard]] bool IsWindingUp() const noexcept { return cast.Active() && cast.stage == 0; }
};
