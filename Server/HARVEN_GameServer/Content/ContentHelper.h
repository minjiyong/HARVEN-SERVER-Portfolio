#pragma once

class CUser;
class CMonster;

// ─────────────────────────────────────────────────────────────────────────────
// 리니지풍 컨텐츠 처리 — 대상 지정 공격/PK·성향치, 사망·리스타트, 아이템 효과
// (귀환/순간이동/가속), 무기 강화, 상점·길잡이.
//
// 전제(기존 서버와 동일): 모든 함수는 대상 플레이어를 소유한 Zone 스레드 위에서
// 실행된다(Route::Dispatch 경유 또는 같은 Zone의 타이머). 그래서 인벤토리/스탯/성향치를
// 락 없이 다룬다. 다른 Zone으로 넘어가야 하는 일(리스타트, 텔레포트)은 기존 Zone
// Transfer와 같은 방식 — 소유권을 넘기고 새 Zone 큐에 메시지로 위임 — 으로 처리한다.
// ─────────────────────────────────────────────────────────────────────────────
namespace CContentHelper
{
    // V2 스킬의 PvP 피해(적대/악명 규칙·방어·회피·철벽 태세 포함)
    void ApplyPvpDamage(const UserSharedPtr& attacker, const UserSharedPtr& victim, UInt32 damage, UInt8 flags,
        UInt16 skillId, UInt8 comboId, UInt8 flags2, UInt8 hitIndex);

    // 플레이어 사망 공통 처리(몬스터에게든 PK든): 사망 알림, 섹터 제거, 경험치 페널티,
    // 악명 아이템 드롭, 부활 타이머 예약.
    void HandlePlayerDeath(const UserSharedPtr& victim, const ObjID& killerId, bool killedByPlayer);

    // 몬스터 처치 보상 중 성향치/보스 공지(경험치·드롭은 CUserHelper::AttackMonster가 한다)
    void OnMonsterKilled(const UserSharedPtr& killer, const MonsterSharedPtr& monster);

    // ── 사망/부활 ──
    void HandleRestartRequest(const UserSharedPtr& player);   // USER_RESTART_REQ
    void RestartInTown(const UserSharedPtr& player);          // 마을 광장에서 부활(필요하면 Zone 이동)

    // ── 이동 ──
    // 살아 있는 플레이어를 (x, y)로 순간이동(같은 Zone이든 다른 Zone이든). 이펙트 포함.
    void TeleportPlayer(const UserSharedPtr& player, short x, short y);

    // ── 아이템 ──
    // 회복 물약이 아닌 소비 아이템(귀환/옛길 두루마리, 바람걸음 물약) 사용
    void UseEffectItem(const UserSharedPtr& player, UInt16 slotIndex);
    void HandleEnchant(const UserSharedPtr& player, const ITEM_ENCHANT_REQ_PACKET& request);
    void GiveStarterKit(const UserSharedPtr& player);

    // ── 상점/길잡이 ──
    void HandleShopBuy(const UserSharedPtr& player, UInt16 npcId, UInt16 itemId, UInt16 count, UInt32 seq);
    void HandleShopSell(const UserSharedPtr& player, UInt16 npcId, UInt16 slotIndex, UInt16 count, UInt32 seq);
    void HandleNpcTeleport(const UserSharedPtr& player, UInt16 npcId, UInt16 destZone, UInt32 seq);

    // ── 상태 만료 타이머 ──
    void HandlePurpleExpire(const ObjID& playerId);
    void HandleHasteExpire(const ObjID& playerId);

    // 성향치 DB 저장(force가 아니면 변화가 충분히 클 때만)
    void SaveAlignment(const UserSharedPtr& player, bool force);
}
