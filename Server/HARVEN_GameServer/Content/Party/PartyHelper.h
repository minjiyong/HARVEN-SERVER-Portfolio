#pragma once

class CUser;
struct PARTY_REQ_PACKET;

// ─────────────────────────────────────────────────────────────────────────────
// 파티 — 사람 · 몬스터를 가진 Zone 스레드 쪽(상태의 주인은 CPartyService, Content/Party/PartyService.h)
//
//  · 요청: 형식 검사 · 멱등(seq) · 도배 간격을 소유 Zone이 거른 뒤 서비스 큐로(결사와 같은 모양).
//  · 파티원 상태 모으기: Zone마다 시트 sync_interval_ms마다 한 번 자기 섹터의 파티원을 훑어 바뀐 사람만 한 묶음으로 서비스에.
//    (사람마다 예약을 걸지 않는다 — 전역 객체 표의 락을 타지 않고, Zone 하나에 예약 하나.)
//  · 처치 보상(경험치 · 그로시 피해 비례 · 쩔 보장 · 막타 아이템)은 Content/Reward/RewardHelper — 파티 번호(CUser::m_PartyId)만 읽는다.
// ─────────────────────────────────────────────────────────────────────────────
namespace CPartyHelper
{
    // ── 요청한 사람의 소유 Zone 스레드 ──
    void HandleRequest(const UserSharedPtr& user, const PARTY_REQ_PACKET& p);
    void OnEnterWorld(const UserSharedPtr& user);    // 로그인 직후
    void OnLeaveWorld(const UserSharedPtr& user);    // 접속 종료
    void OnDied(const UserSharedPtr& user);          // CContentHelper::HandlePlayerDeath
    void OnRevived(const UserSharedPtr& user);       // 마을에서 다시 섬(FinishRespawn)
    // 파티 채팅: 검사가 끝난 글을 서비스로(서비스가 접속 중인 파티원에게 보낸다)
    void RelayChat(const UserSharedPtr& sender, const char* body);

    // 서비스가 PostToOwner로 부른다(소유 Zone): 파티 번호 사본을 바꾸고, 파티에 들었으면 내 상태를 바로 한 번 보낸다
    void ApplyPartyId(const UserSharedPtr& user, UInt32 partyId, UInt32 applySeq);

    // ── Zone 틱(EV_PARTY_TICK) ──
    void InitForZone(UInt16 zoneId);   // CZone::Run — 이 Zone 스레드에 첫 예약
    void HandleZoneTick();
}
