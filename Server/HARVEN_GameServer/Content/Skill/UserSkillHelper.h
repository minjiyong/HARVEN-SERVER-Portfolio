#pragma once

#include "Database/GameData/SkillData.h"
#include "User/User.h"

class CMonster;
class CSubject;
struct SKILL_REQ_PACKET;

// ─────────────────────────────────────────────────────────────────────────────
// 플레이어 기술 — REIW 기술 수명(ExecuteSkill → T_Skill_Cast → T_Skill_Fire → T_Skill_Hit → T_Skill_Terminate,
// 재사용 대기 T_Skill_Cooldown)을 SkillDefTable(Data/SkillData.h) 한 줄과 시전 구조체(UserCast) · 플레이어 일정
// (CUserSkillScheduler)으로 옮겼다. 몬스터 기술(CMonsterSkillHelper)과 같은 틀이다.
//
//   ExecuteSkill  SKILL_REQ(누름 · 놓음 · 취소) — 검증(사망 · 직업 · 레벨 · 안전지대 · 기절 · 재사용 대기 · 자원 · 대상 · 사거리)
//                 → 시전을 일정에 넣고(REIW MakeNewSkill · Insert) 재사용 대기 시작(누를 때부터 — 토글은 끌 때부터)
//   Cast          시전 알림(SKILL_NFY) · 시전 창(차지 · 홀딩 · 토글 · 캐스팅) — 걷기 · 다른 기술 · 기절이 끊는다
//   Fire          발사 — 이동기 · 돌진 · 특수 동작(함성 · 분신 · 안개 · 결계 …) · 차지 놓음 · 홀딩 끝
//   Hit           일정 — 타격표의 시각(ms)마다 모양을 다시 계산해 판정한다(예고를 보고 피할 수 있다) · 틱 · 사슬
//   Terminate     남은 일정이 없고 시전 창이 닫혔으면 일정에서 뺀다
//  · 알림: SKILL_NFY(주변 세션) — 클라이언트는 같은 표의 시각으로 연출을 재생한다. 패킷 · 시각은 예전(SkillCast)과 같다.
//  · 깨우기: 플레이어마다 가장 이른 일정에 EV_SKILL_IMPACT 하나(세대 번호로 낡은 깨우기를 거른다).
//  · 스레드: 모든 함수는 시전자의 소유 Zone 스레드에서 불린다(요청 경로 · 타이머 경로 모두). 다른 플레이어 상태는 바꾸지 않는다
//    (PvP 피해는 기존 CContentHelper 경로 — 같은 Zone 안).
// ─────────────────────────────────────────────────────────────────────────────
namespace CUserSkillHelper
{
    // REIW CSkillHelper::ExecuteSkill — 기술 요청(SKILL_REQ)
    void ExecuteSkill(const UserSharedPtr& u, const SKILL_REQ_PACKET& req);
    // 일정 깨우기(EV_SKILL_IMPACT) — epoch가 지금 세대가 아니면(더 이른 깨우기가 걸렸다) 버린다
    void HandleWake(const ObjID& casterId, UInt32 epoch);
    // REIW CSkillScheduler::CancelCasting — 걸었다: 캐스팅 · (움직일 수 없는) 차지 · 홀딩을 끊는다
    void OnMoved(const UserSharedPtr& u);
    // REIW CSkillScheduler::Clear — 되살아났다: 진행 중이던 시전 · 일정을 모두 버린다(재사용 대기는 그대로)
    void Clear(const UserSharedPtr& u);
    // REIW IsUsing — 차지 · 홀딩 · 토글이 켜져 있다(기술 성장을 바꿀 수 없다)
    [[nodiscard]] bool IsChanneling(const UserSharedPtr& u);

    // ── 판정 뒤 처리(SkillDetail · CCombatHelper가 부른다) ──
    // DealHit 뒤(확장 타격): 고유 기술 게이지 · 잔향(서약 해방) · 분신
    void OnHitDealt(const UserSharedPtr& u, const SkillRow& row, const SubjectSharedPtr& target, UInt32 dealt, UInt8 flags2, UInt32 castSeq);
    // 보스 무력화 게이지
    void ApplyStagger(const UserSharedPtr& u, const MonsterSharedPtr& m, UInt8 amount);
    void AddIdentity(const UserSharedPtr& u, int amount);
    void SendGauge(const UserSharedPtr& u, bool force);
    // 몬스터 공격을 피했다(잔상 회피 직후면 완벽 회피)
    void OnEvaded(const UserSharedPtr& u, UInt32 now);
}
