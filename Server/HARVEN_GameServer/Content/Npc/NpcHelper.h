#pragma once

#include <memory>
#include <cstdint>

class CUser;

// ─────────────────────────────────────────────────────────────────────────────
// 살아 있는 마을 — 서버 권위 NPC 배회 · 인사 · 혼잣말 · 대화 · 서비스.
//
// 상태는 전부 마을 Zone 스레드(LCurrentZoneId == WorldData::kTownZoneId)에서만 읽고
// 쓴다(락 없음). 마을 Zone의 타이머(EV_NPC_TICK, 0.25초)가 NPC를 움직이고, 마을 근처의
// 세션에게 NPC_* 패킷을 보낸다. NPC는 CSector 오브젝트가 아니므로 SUBJECT_* 패킷으로는
// 가지 않는다.
// ─────────────────────────────────────────────────────────────────────────────
namespace CNpcHelper
{
    // CZone::Run에서 호출 — 마을 Zone이면 NPC를 집에 세우고 첫 틱을 건다.
    void InitForZone(UInt16 zoneId);
    // EV_NPC_TICK
    void HandleTick();
    // NPC_TALK_REQ / NPC_SERVICE_REQ
    void HandleTalk(const UserSharedPtr& user, UInt16 npcId);
    void HandleService(const UserSharedPtr& user, UInt16 npcId, UInt8 service);
    // 상호작용 거리 판정(상점·텔레포트 등). 마을 Zone 스레드면 NPC의 지금 위치, 아니면 집 위치.
    [[nodiscard]] bool IsNear(UInt16 npcId, int x, int y);
}
