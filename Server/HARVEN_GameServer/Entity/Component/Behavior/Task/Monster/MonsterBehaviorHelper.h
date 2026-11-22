#pragma once

#include "Database/GameData/MonsterAITypes.h"
#include "Entity/Component/Behavior/EntityBehavior.h"

// ─────────────────────────────────────────────────────────────────────────────
// 몬스터 행동 트리 — AI 종류(MonsterAiType)마다 하나(REIW Task/Monster/MonsterBehaviorHelper의 CreateXxxBT).
//
// REIW는 몬스터 종(Crab · Kaper · Deepers · Yullus …)마다 CreateXxxBT로 그 몬스터 전용 트리(shared_ptr 노드)를 새로 만들고,
// 종마다 다른 공격 기술을 Selector 안의 T_NormalAttack01(AttackID1 · 2 · 3)로 박는다.
// 하르벤은 트리 모양을 종류마다 하나만 만들어 모든 몬스터가 함께 쓰고(불변), 종마다 다른 것 — 어떤 기술을 · 어떤 조건 · 우선순위로 —
// 은 시트(MonsterAISkill)를 기술 잎(T_UseSkill)이 그때그때 읽는다. 그래서
//   · 몬스터마다 트리를 만들지 않는다(깨어 있는 몬스터 몫은 노드 상태 192바이트)
//   · 새 몬스터 · 패턴은 시트 행으로 더한다(코드 수정 없음) — 값만 바꾸면 개발 서버가 핫 리로드로 바로 쓴다
//   · 행동 자체가 다른 몬스터(새 보스 기믹)만 여기 종류 하나와 트리 하나를 더한다(REIW CreateDeepersOverseerBT처럼)
//
// 트리(모두 맨 위가 우선 선택 — 앞 가지가 이기면 달리던 뒤 가지를 끊는다):
//   Melee      [전투? → (기술 > 평타 > 추격)] > [귀환? → 귀환] > 대기(배회 · 잠)
//   Ranged     [전투? → (기술 > 물러서기(4초에 한 번) > 평타 > 사거리 지키기)] > 귀환 > 대기
//   Support    Ranged와 같다(기술 칸이 동료 강화 · 회복이 먼저)
//   Boss       [전투? → (단계 기술 > 기술 > 평타 > 추격)] > 귀환 > 대기
//   Stationary [전투? → (기술 > 평타 > 제자리)] > 대기
// ─────────────────────────────────────────────────────────────────────────────
namespace MonsterBehaviorHelper
{
    // AI 종류 → 트리(서버가 뜰 때 처음 부르는 순간 한 번 만든다 — 함수 지역 static). Dummy · 모르는 종류는 nullptr.
    [[nodiscard]] const CEntityBehavior::Tree* TreeOf(MonsterAiType type);
}
