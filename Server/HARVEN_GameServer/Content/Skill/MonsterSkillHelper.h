#pragma once

#include "Base/BehaviorTree/BT_Task.h"
#include "Database/GameData/MonsterAITypes.h"
#include "Protocol/Protocol.h"

struct CMonsterAgent;

// ─────────────────────────────────────────────────────────────────────────────
// 몬스터 기술 — REIW 기술 BT(T_Skill_Cast → T_Skill_Fire → T_Skill_Hit → T_Skill_Cooldown · Terminate)를 표 한 줄
// (MonsterSkill + MonsterSkillHit)과 작은 시전 상태(MonsterCast)로 옮겼다. 기술 잎(T_UseSkill)이 부르고, 행동 트리가
// 다음 단계 시각(Agent::WakeAt)에 다시 불러 단계를 넘긴다.
//
//   고르기   AI 표의 기술 칸(MonsterAISkill) — 단계 · 대기 · 체력 · 거리 · 동료 수 · 확률을 보고, 가장 높은 우선순위 중 가중치 무작위
//   시전     조준(대상 · 자기 · 동료) · 방향을 정하고 예고 · 재사용 대기 시작
//   발사     이동(돌진 · 도약 · 순간이동 · 물러서기 — FORCED_MOVE를 먼저 보낸다) · 장판 · 자기 강화
//   타격     시각표마다 — 예고했던 자리(피할 수 있다)에 모양 판정 · 피해 · 상태이상 · 밀기
//   끝       후딜 뒤 다음 기술까지 간격(skill_gap_ms)
// 모든 함수는 몬스터의 소유 Zone 스레드에서.
// ─────────────────────────────────────────────────────────────────────────────
namespace CMonsterSkillHelper
{
    // 쓸 수 있는 칸을 골라 시전을 시작한다 — 시작했으면 true. 못 쓰면 가장 먼저 쓸 수 있게 되는 시각을 a.WakeAt으로 남긴다.
    bool TryBegin(CMonsterAgent& a);
    // 정해진 기술을 곧바로 시작한다(보스 단계 기술 — 거리 · 대기를 보지 않는다). slot은 MonsterCast::kPhaseSlot.
    bool Begin(CMonsterAgent& a, UInt16 skillId, UInt8 slot);
    // 시전 진행 — Running(다음 단계 시각을 a.WakeAt으로) · Success(끝 — 다음 기술까지 간격) · Failure(끊김)
    EnumTaskStatus Advance(CMonsterAgent& a);
    // 끊기 — 예고가 남아 있으면 클라에 끊김(예고 0ms)을 알린다. 시전 중이 아니면 아무것도 하지 않는다.
    void Cancel(CMonsterAgent& a);

    // ── 공용(평타 · 장판 · 이동이 같이 쓴다) ──
    // 몬스터 피해 굴림: 공격력 ±15% × coeff% × (100 + 광폭 세기)%
    [[nodiscard]] UInt32 RollDamage(const MonsterSharedPtr& m, UInt32 coeffPercent);
    // 서버가 옮기는 몬스터 이동 — 보는 사람에게 SUBJECT_FORCED_MOVE_NFY를 먼저 보낸 뒤 칸을 옮긴다
    // (클라는 한 동작으로 미끄러지고, 뒤따르는 SUBJECT_MOVE_NFY는 이미 그 칸으로 가는 중이라 무시한다).
    void ForcedMove(const MonsterSharedPtr& m, short x, short y, UInt32 durationMs, ForcedMoveKind kind);
    // 몬스터 효과(MonsterFx → EFFECT_NFY). subjectId = 효과의 주인(몸에 붙는 효과는 그 개체).
    void SendFx(const SubjectSharedPtr& center, const ObjID& subjectId, MonsterFx fx, short x, short y, Int32 param);
    // 몬스터 강공격 예고 — durationMs 0 = 끊김(받아치기)
    void SendTelegraph(const MonsterSharedPtr& m, TelegraphShape shape, short x, short y, short fromX, short fromY,
        UInt8 radius, UInt16 durationMs, MonsterPattern pattern);
    // (cx, cy) 체비셰프 radius 안의 살아 있는 동료 몬스터 수(자신 · 허수아비 빼고)
    [[nodiscard]] int CountAllies(const MonsterSharedPtr& m, int cx, int cy, int radius);
}
