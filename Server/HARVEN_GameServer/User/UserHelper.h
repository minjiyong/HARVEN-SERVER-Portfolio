#pragma once

class CSubject;
class CGameSession;
class CUser;
struct MonsterTableRow;

namespace CUserHelper
{
    void SendSUBJECT_MOVE_NFY(CSubject::SharedPtr sender, const ObjID& targetId);
    void SendSUBJECT_MOVE_NFY(CSubject::SharedPtr sender, CSubject::SharedPtr target);
    void SendSUBJECT_ADD_NFY(CSubject::SharedPtr sender, const ObjID& targetId);
    void SendSUBJECT_ADD_NFY(CSubject::SharedPtr sender, CSubject::SharedPtr target);
    void SendSUBJECT_REMOVE_NFY(CSubject::SharedPtr sender, const ObjID& targetId);
    void SendUSER_LOGIN_ACK(CSubject::SharedPtr sender);
    void SendUSER_LOGIN_FAIL_ACK(CSubject::SharedPtr sender);
    void SendSUBJECT_DIE_NFY(CSubject::SharedPtr sender, const ObjID& targetId);
    void SendSUBJECT_DIE_NFY(CSubject::SharedPtr sender, CSubject::SharedPtr target);
    void SendSUBJECT_RESPAWN_NFY(CSubject::SharedPtr sender, const ObjID& targetId);
    void SendSUBJECT_RESPAWN_NFY(CSubject::SharedPtr sender, CSubject::SharedPtr target);
    void SendUSER_HEAL_INF(CSubject::SharedPtr sender);
    void SendUSER_STAT_CHANGE_INF(CSubject::SharedPtr sender);
    void SendITEM_EQUIP_ACK(CSubject::SharedPtr sender, UInt16 slotIndex, bool success);
    void SendITEM_UNEQUIP_ACK(CSubject::SharedPtr sender, UInt16 slotIndex, bool success);
    void SendITEM_SWAP_ACK(CSubject::SharedPtr sender, UInt16 slotIndexA, UInt16 slotIndexB, bool success);
    void SendSYSTEM_MESSAGE_INF(CSubject::SharedPtr sender, SystemMessageCode code, Int32 param1, Int32 param2);
    void SendITEM_DISCARD_ACK(CSubject::SharedPtr sender, UInt16 slotIndex, bool success);
    void SendITEM_PICKUP_ACK(CSubject::SharedPtr sender, bool success);
    void SendITEM_USE_ACK(CSubject::SharedPtr sender, UInt16 slotIndex, bool success);
    void SendSUBJECT_EQUIP_CHANGE_NFY(CSubject::SharedPtr sender, const ObjID& targetId, UInt16 itemId);
    [[nodiscard]] bool SaveUserInfo(const ObjID& targetId);

    // ── 접속 종료 ──
    // 소켓이 닫혔을 때(읽기 · 쓰기 실패, 서버가 먼저 닫음) IO 스레드에서 한 번 부른다.
    // 게임 중이면 CUser를 가진 Zone 스레드로 LeaveWorld를 보내고, 로그인 전이면 세션만 반납한다.
    void OnSessionClosed(const SessionSharedPtr& session);
    // CUser를 월드에서 뺀다(저장 · 이웃 알림 · 섹터 · 세션 반납). 가진 Zone 스레드가 아니면 그쪽으로 다시 보낸다.
    void LeaveWorld(ObjID clientId);
    // 같은 계정의 새 로그인 때문에 끊긴다고 알린다(게임 중인 세션에만).
    void NotifyLoggedInElsewhere(const SessionSharedPtr& session);

    // player의 현재 장착 상태(CInventory::GetEquippedItemId())를 다시 계산해서
    // 주변에 이미 보이는 뷰어들에게 SUBJECT_EQUIP_CHANGE_NFY로 알린다. 장착/탈착/
    // (장착 중이던 아이템을) 버리기 중 어느 것이든 성공한 뒤 호출하면 된다 —
    // "무엇이 바뀌었는지"를 따지지 않고 항상 최종 상태를 그대로 알리므로 항상 정확하다.
    void BroadcastEquipChange(const UserSharedPtr& player);

    // damage는 항상 호출자가 명시한다(기본값 없음) — 호출자(CCombatHelper::HitMonster)가
    // 굴린 피해에 몬스터 방어를 적용한 값을 넘긴다.
    // 과거처럼 기본 인자에 기대면 장비 보너스를 깜빡 빠뜨리기 쉬워서 일부러 없앴다.
    // damageFlags(SubjectDamageFlag)는 SUBJECT_DAMAGE_NFY 표시용.
    void AttackMonster(ObjID& monsterId, ObjID& playerId, int damage, UInt8 damageFlags,
        UInt16 skillId = 0, UInt8 comboId = 0, UInt8 flags2 = 0, UInt8 hitIndex = 0);

    void HandleHeal(const ObjID& playerId);
    // deathSeq: 부활 타이머를 예약한 시점의 CUser::m_DeathSeq. 그 사이에 이미
    // 리스타트했거나 다시 죽었다면(값이 다르면) 이 타이머는 무시된다.
    void HandleRespawn(const ObjID& playerId, UInt32 deathSeq);

    // 경험치 획득 처리(레벨업 시 스탯 재계산, 알림, 레벨업 이펙트까지)
    void GainExp(const UserSharedPtr& player, UInt32 amount);

    // ── 리니지풍 컨텐츠 송신 ──────────────────────────────────────────────────────
    void SendUSER_INFO_INF(const UserSharedPtr& user);

    // ── V2 ──
    void SendUSER_MOVE_ACK(const UserSharedPtr& user, UInt16 seq, MoveResult result, UInt32 stepMs);
    void SendSERVER_CONFIG_INF(const UserSharedPtr& user);
    void HandleMove(const UserSharedPtr& player, UInt8 dir, UInt16 seq);
    void SendSUBJECT_INFO_NFY(CSubject::SharedPtr viewer, CSubject::SharedPtr target);
    // player의 확장 정보(SUBJECT_INFO_NFY)가 바뀌었을 때(레벨/성향치/적대/가속/무기 강화)
    // 본인과 주변 세션에 다시 알린다.
    void BroadcastInfo(const UserSharedPtr& player);
    void SendITEM_LIST_ACK(const UserSharedPtr& user);
    void SendITEM_SLOT_INF(const UserSharedPtr& user, UInt16 slotIndex);
    // V2 아이템 성장: 슬롯 인스턴스 값(희귀도·옵션), 주변 플레이어 외형, 소지품 정리
    void SendITEM_DETAIL_INF(const UserSharedPtr& user, UInt16 slotIndex);
    void SendPLAYER_LOOK_NFY(CSubject::SharedPtr viewer, const UserSharedPtr& player);
    void HandleItemSort(const UserSharedPtr& player);
    void SendITEM_ENCHANT_LEVEL_INF(const UserSharedPtr& user, UInt16 slotIndex);
    void SendENCHANT_GRACE_INF(const UserSharedPtr& user, UInt16 itemId, UInt32 currentLevel);
    void SendADENA_INF(const UserSharedPtr& user, Int32 delta, AdenaReason reason);
    void SendSHOP_RESULT_ACK(const UserSharedPtr& user, ShopKind kind, ShopResult result, UInt32 seq, UInt16 itemId, UInt16 count, Int32 adenaDelta);
    // 시스템 메시지 — SendSYSTEM_MESSAGE_INF와 같다(param 기본값 0)
    void SendSystem(const UserSharedPtr& user, SystemMessageCode code, Int32 param1 = 0, Int32 param2 = 0);
    // victim을 볼 수 있는 세션 모두에게 피해를 알린다.
    void BroadcastDamage(const CSubject::SharedPtr& victim, const ObjID& attackerId, Int32 damage, UInt8 flags,
        UInt16 skillId = 0, UInt8 comboId = 0, UInt8 flags2 = 0, UInt8 hitIndex = 0);
    // (x, y)를 볼 수 있는 세션 모두에게 이펙트를 알린다. center는 섹터 탐색 기준.
    void BroadcastEffect(const CSubject::SharedPtr& center, const ObjID& subjectId, EffectId effect, short x, short y, Int32 param);
    // 모든 Zone의 세션에 공지(보스 출현 등). 드물게 일어나는 일이라 세션 목록을 훑는다.
    void BroadcastWorldNotice(WorldNoticeCode code, Int32 param1, Int32 param2, const std::string& name);

    void HandleLoginFail(const SessionSharedPtr& session);
    // 로그인 결과를 처리할 Zone — 저장 위치의 Zone, 걸을 수 없는 칸이면 마을 Zone.
    [[nodiscard]] ZoneId GetLoginZone(int savedX, int savedY);
    void HandleGetUserInfo(const SessionSharedPtr& session, const DB_USER_INFO& userInfo, const vector<DB_ITEM_INFO>& items,
        const std::vector<UInt8>& progress, const std::vector<DB_SKILL_INFO>& skills, const std::vector<DB_ENCHANT_GRACE>& grace);
    // V2 진행 데이터(직업·포인트·퀘스트…) 저장 요청
    void SaveProgress(const UserSharedPtr& user);
    void HandleAddUserInfo(const SessionSharedPtr& session, const DB_USER_INFO& userInfo);
}
