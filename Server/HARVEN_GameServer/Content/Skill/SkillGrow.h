#pragma once

#include "Database/GameData/SkillGrowth.h"
#include "User/User.h"

struct SKILL_GROW_REQ_PACKET;

// ─────────────────────────────────────────────────────────────────────────────
// 기술 성장(Phase 2-D) — 기술 레벨 · 점수 · 갈래의 서버 권위(Data/SkillGrowth.h 규칙).
//
//  · 상태: CUser::m_SkillGrow(기술 id → 한 바이트). 소유 Zone 스레드만 읽고 쓴다(요청은 Route → 세션의 Zone 스레드).
//  · 저장: 바뀔 때마다 그 기술 한 줄을 절대값으로 DB 샤드(playerId)에 넣는다 — SavePlayerSkill(upsert, 한 트랜잭션).
//    초기화는 ResetPlayerSkills(행 삭제). 같은 샤드라 넣은 순서대로 커밋된다.
//  · 멱등: SKILL_GROW_REQ.seq가 직전과 같으면 다시 처리하지 않고 지금 상태로 결과만 다시 보낸다.
//  · 남은 점수 = 캐릭터 레벨의 점수 − 기술 레벨들의 값(저장하지 않는다 — 어긋날 수 없다).
//  · 기술 레벨 1 = 성장 전과 같은 피해(STRESS_TEST 봇은 SKILL_GROW_REQ를 보내지 않아 늘 레벨 1).
// ─────────────────────────────────────────────────────────────────────────────
namespace SkillGrow
{
    // 로그인: DB 행을 적용한다(직업 · 범위 검사). 점수보다 많이 쓴 기록이면 모두 초기화하고 true
    bool Load(const UserSharedPtr& u, const std::vector<DB_SKILL_INFO>& rows);
    [[nodiscard]] UInt16 Spent(const UserSharedPtr& u);
    [[nodiscard]] UInt16 Points(const UserSharedPtr& u);   // 남은 점수
    void SendTree(const UserSharedPtr& u);
    void HandleRequest(const UserSharedPtr& u, const SKILL_GROW_REQ_PACKET& req);
}
