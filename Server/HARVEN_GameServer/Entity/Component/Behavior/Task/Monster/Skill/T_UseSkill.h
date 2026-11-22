#pragma once

#include "Base/BehaviorTree/BT_Task.h"

struct CMonsterAgent;

namespace Behavior::Monster
{
    // 기술 쓰기 — REIW Task/Monster/Skill/T_NormalAttack01(OnEnter에서 ExecuteSkill, OnUpdate에서 IsUsing을 본다)의 자리.
    //  · OnEnter: 종족의 기술 칸(MonsterAISkill)에서 지금 쓸 수 있는 것을 골라 시전 시작(CMonsterSkillHelper::TryBegin).
    //             쓸 것이 없으면 실패 — 가장 먼저 쓸 수 있게 되는 시각을 깨우기로 남긴다(그 시각에 우선 선택이 다시 본다).
    //  · OnUpdate: 시전 → 발사 → 타격 → 후딜을 시각표대로 넘긴다(Advance). 끝나면 성공, 끊기면 실패.
    //  · OnLeave(끊김): 앞 가지가 이겼다(귀환 · 단계 기술) — 자기 시전이면 취소(예고 끊김 알림).
    struct T_UseSkill
    {
        static constexpr const char* kName = "T_UseSkill";
        static EnumTaskStatus OnEnter(CMonsterAgent& a, BT_Frame& f);
        static EnumTaskStatus OnUpdate(CMonsterAgent& a, BT_Frame& f);
        static void OnLeave(CMonsterAgent& a, BT_Frame& f, EnumTaskStatus result);
    };
}
