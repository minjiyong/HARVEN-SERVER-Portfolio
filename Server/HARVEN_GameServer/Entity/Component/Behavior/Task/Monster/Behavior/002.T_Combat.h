#pragma once

#include "Base/BehaviorTree/BT_Task.h"

struct CMonsterAgent;

namespace Behavior::Monster
{
    // 싸우는 중인가(조건 — 틱마다 다시 본다, REIW CondBuilder의 Combat 깃발 자리).
    //  · 귀환 중이면 아니다.
    //  · 싸우던 중 리쉬(스폰에서 leash칸)를 넘거나 Zone 경계에 닿으면 위협을 버리고 귀환(BeginReturn) — 아니다.
    //  · 대상 고르기: 도발(Taunt)한 사람 → 위협 표에서 큰 순서로 유효한 사람 → (표가 비었으면) 감지 거리 안 가장 가까운 사람.
    //    유효 = 게임 중 · 살아 있음 · 같은 Zone · 마을 밖 · 은신(밤 장막) 아님 · 보인다.
    //  · 대상이 없으면 전투를 끝낸다(집에서 멀면 귀환). 대상이 새로 생기면 전투 시작(도움 요청).
    struct C_InCombat
    {
        static bool Check(CMonsterAgent& a, const BT_Param& p);
    };

    // 평타 — 붙어 있을 때(대각선 포함 인접) 공격 간격마다(예전 EV_MONSTER_ATTACK_TO_USER). 떨어지면 실패(추격 차례).
    // 붙어 있는 동안 걸음 간격마다 다시 본다(대상이 떨어지면 곧 알아챈다).
    struct T_Attack
    {
        static constexpr const char* kName = "T_Attack";
        static EnumTaskStatus OnUpdate(CMonsterAgent& a, BT_Frame& f);
    };

    // 추격 — 대상 쪽으로 이동 간격마다 한 걸음(캐시 경로 → 직진 → A*). 붙으면 실패(평타 차례).
    struct T_Chase
    {
        static constexpr const char* kName = "T_Chase";
        static EnumTaskStatus OnUpdate(CMonsterAgent& a, BT_Frame& f);
    };

    // 사거리 지키기(원거리 · 지원) — keep_range 안이면 서서 기술을 기다리고, 밖이면 다가간다.
    struct T_ChaseToRange
    {
        static constexpr const char* kName = "T_ChaseToRange";
        static EnumTaskStatus OnUpdate(CMonsterAgent& a, BT_Frame& f);
    };

    // 물러서기(원거리 · 지원) — 붙으면 대상 반대쪽으로 두 칸 뛴다(트리가 Cooldown으로 감싸 몇 초에 한 번).
    struct T_KeepDistance
    {
        static constexpr const char* kName = "T_KeepDistance";
        static EnumTaskStatus OnUpdate(CMonsterAgent& a, BT_Frame& f);
    };

    // 제자리(고정 몬스터) — 할 것이 없으면 서서 기다린다
    struct T_Hold
    {
        static constexpr const char* kName = "T_Hold";
        static EnumTaskStatus OnUpdate(CMonsterAgent& a, BT_Frame& f);
    };

    // 보스 단계 — 체력이 phase2_hp · phase3_hp 아래로 내려가면 단계를 올리고 그 단계 기술(phase2_skill · phase3_skill)을 곧바로 쓴다(진행 중인 기술을 끊는다).
    struct T_PhaseShift
    {
        static constexpr const char* kName = "T_PhaseShift";
        static EnumTaskStatus OnEnter(CMonsterAgent& a, BT_Frame& f);
        static EnumTaskStatus OnUpdate(CMonsterAgent& a, BT_Frame& f);
        static void OnLeave(CMonsterAgent& a, BT_Frame& f, EnumTaskStatus result);
    };
}
