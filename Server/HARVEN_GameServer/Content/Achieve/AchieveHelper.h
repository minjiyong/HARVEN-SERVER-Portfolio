#pragma once

#include "Database/GameData/Item/ItemTableRow.h"

// ─────────────────────────────────────────────────────────────────────────────
// 개인 업적 — 소유 Zone 스레드 쪽(요청 검사 · 멱등 · 사건 → CUser::m_Achieve · 알림 · 저장 · 보상).
//
//  · 모든 함수는 그 사람을 가진 Zone 스레드에서 부른다. 다른 스레드(결사 · 경매 서비스 · DB 완료 · 다른 Zone의 처치)는
//    Service::PostToOwner로 넘겨 부른다 — 진척을 쓰는 곳은 소유 Zone 하나뿐(락 없음).
//  · 달성(목표에 닿음) → ACHIEVE_INF Completed + 시스템 채팅 「업적 달성: <이름>」 + 바로 저장. 그 밖 진척은 더러움 표시 뒤
//    60초(EV_ACHIEVE_SAVE) · 로그아웃(기존 SaveProgress 길)에 저장.
//  · 받기: 보상(아이템 TryAddItem + OnAcquire · 그로시 AdenaReason::Achievement)이 다 들어갈 때만 받음 표시 → Claimed INF · 바로 저장.
// 설계 · 사건 표: Shared/Document/ACHIEVEMENT.md.
// ─────────────────────────────────────────────────────────────────────────────
namespace CAchieveHelper
{
    // ── 요청(ACHIEVE_REQ) ──
    void HandleRequest(const UserSharedPtr& user, const ACHIEVE_REQ_PACKET& p);

    // ── 접속 ──
    // 월드 입장 직후(진행 기록을 읽은 뒤): 소급(표 다시 보기 · 레벨 · 가진 장비의 최고 강화 · 지금 지역 — 알림 없이 달성만) → Summary
    void OnEnterWorld(const UserSharedPtr& user);

    // ── 사건(소유 Zone) ──
    void OnKill(const UserSharedPtr& user, UInt16 species, bool elite, bool boss);   // 파티 처치는 user->m_PartyId
    void OnLevel(const UserSharedPtr& user);
    void OnDeath(const UserSharedPtr& user);
    void OnAdenaPickup(const UserSharedPtr& user, UInt32 amount);                    // AdenaReason::Pickup으로 실제로 늘어난 양
    void OnLoot(const UserSharedPtr& user, ItemTableId itemId, UInt16 count);         // 처치 보상이 가방에 들어왔다(장비 · 사냥 정수 · 결정)
    void OnGear(const UserSharedPtr& user, ItemGrade grade, UInt32 count);            // 제작 · 퀘스트 보상 장비
    void OnEnchant(const UserSharedPtr& user, UInt32 level);                          // 강화 성공으로 도달한 단계
    void OnCraft(const UserSharedPtr& user, ItemGrade grade);                         // 제작 성공(제작 수 + 장비 획득)
    void OnDismantle(const UserSharedPtr& user);
    void OnQuest(const UserSharedPtr& user, UInt16 questId);
    void OnGuildJoin(const UserSharedPtr& user);                                      // 결사 가입 · 창설(결사 서비스 → PostToOwner)
    void OnGuildPresent(const UserSharedPtr& user);                                   // 로그인 때 결사원이면 적어도 1(오프라인 승인 · 업데이트 전 결사원)
    void OnAuctionSold(const UserSharedPtr& user);                                    // 판매 대금(정산함 Sold 줄)을 받았다
    void OnAuctionBought(const UserSharedPtr& user);                                  // 구매가 커밋됐다
    void OnTrade(const UserSharedPtr& user);                                          // 교환 · 직거래가 커밋됐다
    void OnEnterRegion(const UserSharedPtr& user);                                    // 지금 자리의 지역(Zone 옮김 · 부활 · 로그인)

    // ── 저장 예약(EV_ACHIEVE_SAVE) ──
    void HandleSaveTimer(const ObjID& playerId);
}
