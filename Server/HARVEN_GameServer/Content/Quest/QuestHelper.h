#pragma once

#include <memory>
#include <cstdint>

class CUser;
class CMonster;
struct MonsterTableRow;

// ─────────────────────────────────────────────────────────────────────────────
// 퀘스트 · 메인 이야기(Data/QuestData.h) — 서버 권위.
//
// 받기/보고는 NPC 곁에서만(CNpcHelper::IsNear — 마을 Zone이면 배회 중인 지금 위치),
// 진행은 서버가 본 사건(처치 · 대화 · 이동 · 장비 변경)으로만 오른다. 클라이언트는
// 표를 읽어 보여 줄 뿐이다.
//
// 전제(기존 서버와 같다): 모든 함수는 그 사용자를 소유한 Zone 스레드에서 불린다
// (패킷 → Route, 처치 → CUserHelper::AttackMonster, 대화 → CNpcHelper, 이동 → HandleMove).
// 상태는 CUser::m_Quests/m_QuestDone/m_QuestCooldowns(진행 데이터 v3)에 있다.
// ─────────────────────────────────────────────────────────────────────────────
namespace CQuestHelper
{
    // 로그인 직후 전체 상태(QUEST_LIST_INF)
    void SendList(const UserSharedPtr& user);

    // QUEST_ACCEPT_REQ / QUEST_COMPLETE_REQ / QUEST_ABANDON_REQ
    void HandleAccept(const UserSharedPtr& user, UInt16 questId);
    void HandleComplete(const UserSharedPtr& user, UInt16 questId);
    void HandleAbandon(const UserSharedPtr& user, UInt16 questId);

    // ── 진행 훅 ──
    // 몬스터를 쓰러뜨림(막타) — 처치 · 지역 처치 · 수집. (x, y) = 쓰러진 칸
    void OnKill(const UserSharedPtr& killer, const MonsterTableRow* species, short x, short y);
    // 보스가 쓰러짐 — 가까이(QuestData::kBossShareRange) 있던 모두가 함께 인정받는다(막타 포함)
    void OnBossKilled(UInt16 bossSpecies, short x, short y);
    // NPC에게 말을 걸었다(동물이면 쓰다듬기)
    void OnTalk(const UserSharedPtr& user, UInt16 npcId);
    // 한 걸음 옮긴 뒤(존 이동 중이 아닐 때) — 탐험 목표
    void OnMoved(const UserSharedPtr& user);
    // 장비가 바뀜 — "N부위 착용" 목표(지금 상태라 줄어들 수도 있다)
    void OnEquipChanged(const UserSharedPtr& user);
}
