#pragma once

class CUser;

// ─────────────────────────────────────────────────────────────────────────────
// Phase 3-B 채널 채팅(CS_CHAT)
//
//  스레드: 모든 함수는 보낸 사람을 소유한 Zone 스레드에서(Route가 세션의 Zone 큐로 넘긴다).
//   · 보낸 사람의 도배 상태(토큰 버킷 · 되풀이 · 채널 재사용 대기)는 CUser 안 — 단일 작성자.
//   · 일반 = 같은 Zone(지역) 전체: 그 Zone 스레드의 섹터 표만 읽는다(GSector).
//   · 전체 · 거래 · 시스템(서버가 말함) = 서버 전체 세션(SnapshotSessions — 월드 공지와 같은 경로). 귓속말 = 이름 색인.
//   · 결사 · 파티 = 구성원 목록의 주인인 서비스(CGuildService · CPartyService)가 접속 중인 구성원에게.
//     다른 Zone의 세션에는 PostSend(세션별 송신 락)만 한다 — 그 사용자의 상태는 건드리지 않는다.
//  받는 쪽 패킷은 기존 SC_CHAT(168B) 그대로, 채널 값만 끝에 추가(Protocol.h ChatChannel).
// ─────────────────────────────────────────────────────────────────────────────
namespace CChatHelper
{
    std::string ReportEvidence(const std::string& target);
    // 검사 규칙(서버 상수 — 문서 PHASE_3.md 2.2)
    inline constexpr int      kBucketMax       = 5;       // 5줄을 몰아서
    inline constexpr UInt32   kBucketRefillMs  = 1000;    // 1초에 1줄씩 찬다 → 5줄/5초
    inline constexpr UInt32 kWorldCooldownMs   = 10000;   // 전체 10초
    inline constexpr UInt32 kTradeCooldownMs   = 5000;    // 거래 5초
    inline constexpr UInt32   kRepeatWindowMs  = 30000;   // 같은 말: 30초 안의 세 번째부터 막는다
    inline constexpr size_t   kRecentMax       = 20;      // 접속 직후 보내는 최근 대화

    void HandleChat(const UserSharedPtr& sender, UInt8 channel, const char* targetName, const char* mess);
    // 로그인 직후: 이름 색인에 넣고 최근 전체 · 거래 · 시스템 · 같은 지역 일반 대화를 보낸다
    void OnEnterWorld(const UserSharedPtr& user);

    // 서버가 말하는 시스템 채팅(ChatChannel::System) 한 줄을 서버 전체에 — 전체 대화와 같은 경로(SnapshotSessions),
    // 최근 대화에도 남긴다. subject · name = 그 줄의 주인공(전리품을 얻은 사람 — 클라가 이름을 눌러 귓속말 등). 아무 스레드.
    void BroadcastSystem(const ObjID& subject, const std::string& name, const char* text);
    // 시스템 채팅 한 줄을 그 사람에게만(업적 달성 등 — 최근 대화에 남기지 않는다). 그 사람의 소유 Zone 스레드.
    void SendSystemTo(const UserSharedPtr& user, const char* text);

    // 시험용(샌드박스 회귀): UTF-8 검사
    [[nodiscard]] bool IsValidUtf8(const char* s, size_t n);
}
