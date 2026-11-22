#pragma once

#include "Base/BehaviorTree/BehaviorTree.h"
#include "Content/Aggro/Aggro.h"
#include "Content/Skill/SkillScheduler.h"

struct CMonsterAgent;

// REIW EnumEntityBehaviorState — 행동 상태(트리의 조건이 읽는다)
enum class EnumEntityBehaviorState : UInt8
{
    Idle,      // 대상 없음 — 배회하거나 잠든다
    Combat,    // 대상과 싸운다
    Return,    // 집(스폰 지점)으로 돌아간다 — 그동안 새 대상을 받지 않는다(리쉬 악용 막기)
};

// ─────────────────────────────────────────────────────────────────────────────
// CEntityBehavior — 깨어 있는 몬스터 하나의 행동(REIW Entity/Component/Behavior/EntityBehavior).
//  · 트리 정의는 AI 종류(MonsterAiType)마다 하나를 함께 쓰고(MonsterBehaviorHelper), 여기에는 노드 상태 · 블랙보드만 있다.
//  · 몬스터가 깨어날 때 풀(CObjectPool)에서 꺼내 붙이고 잠들 때 돌려준다 — 몬스터 20만 마리 중 깨어 있는 것만 메모리를 쓴다.
//  · REIW는 OnUpdate를 주기마다 불러 트리를 돌린다. 하르벤은 잎이 남긴 깨우기 시각에만 돈다(EV_MONSTER_THINK —
//    CEntityBehaviorHelper::ScheduleThink). 맞기 · 기절 · 반격 같은 사건은 곧바로 한 번 더 돌린다(Poke).
//  · 소유 Zone 스레드 전용 — 잠금 없음.
// ─────────────────────────────────────────────────────────────────────────────
class CEntityBehavior
{
public:
    static constexpr size_t kMaxNodes = 24;   // 가장 큰 트리(보스 · 원거리)보다 넉넉히 — BT_Builder가 넘으면 서버를 멈춘다
    using Tree = BT_Tree<CMonsterAgent>;
    using Instance = BT_Instance<kMaxNodes>;

    // now = 깨어난 시각 — 시각 칸을 모두 여기서 시작한다(0으로 두면 서버가 켜진 지 24.8일이 지나 GetNowTime이 2^31을 넘을 때
    // "0 시각"이 미래로 보여 몬스터가 굳는다 — 시각은 늘 차이로 비교한다)
    CEntityBehavior(const Tree* tree, UInt32 now) noexcept
        : m_pTree(tree), m_CombatSince(now), m_NextAttackAt(now), m_NextMoveAt(now), m_ObservedUntil(now)
    {
        m_Skills.Reset(now);
    }

    const Tree* m_pTree = nullptr;
    Instance    m_Run{};

    // ── 블랙보드 ──
    EnumEntityBehaviorState m_State = EnumEntityBehaviorState::Idle;
    ObjID   m_TargetId{};
    UInt32  m_CombatSince = 0;       // 이번 전투를 시작한 시각(첫 기술 대기)
    bool    m_bAssisting = false;    // 동료의 도움 요청으로 싸운다(다시 부르지 않는다 — 연쇄 방지)
    UInt8   m_Phase = 1;             // 보스 단계(1 ~ 3)

    // ── 위협 · 기술 ──
    CAggro          m_Aggro;
    CSkillScheduler m_Skills;

    // ── 평타 · 걸음 ──
    UInt32  m_NextAttackAt = 0;
    UInt32  m_NextMoveAt = 0;
    UInt32  m_ObservedUntil = 0;     // 배회: 이 시각까지는 보는 사람이 있다고 본다(걸음마다 시야를 훑지 않게 — T_Idle)

    // ── 장판 ──
    UInt32  m_AuraHandle = 0;        // 오라(깨어 있는 동안 두르는 장판)
    bool    m_bOwnsAoe = false;      // 이번에 깨어 있는 동안 장판을 깔았다 — 잠들 때만 거두러 간다(대부분의 몬스터는 깔지 않는다)
};
