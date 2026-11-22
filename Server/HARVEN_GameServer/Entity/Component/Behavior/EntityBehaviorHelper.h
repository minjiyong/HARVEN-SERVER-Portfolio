#pragma once

#include "Base/Core/Types.h"
#include "Base/ObjID/ObjID.h"

struct CMonsterAgent;
class CEntityBehavior;

// ─────────────────────────────────────────────────────────────────────────────
// 몬스터 행동 — 깨우기 · 생각(트리 한 번) · 재우기와 바깥 사건(맞음 · 도발 · 반격 · 기절 · 죽음)의 입구.
// REIW EntityBehaviorHelper · Monster OnUpdate 자리. 예전 하르벤의 몬스터 타이머 사슬 넷(배회 EV_RANOM_MOVE ·
// 추격 EV_AGGRO_MOVE · 평타 EV_MONSTER_ATTACK_TO_USER · 패턴 EV_MONSTER_PATTERN · 보스 광역기 EV_BOSS_SKILL_*)을
// 생각 예약 하나(EV_MONSTER_THINK)로 합쳤다 — 몬스터마다 예약은 늘 하나고(예약 번호로 낡은 것을 거른다), 무엇을 할지는 트리가 정한다.
// 모두 몬스터의 소유 Zone 스레드에서.
// ─────────────────────────────────────────────────────────────────────────────
namespace CEntityBehaviorHelper
{
    // ── 바깥 사건 ──
    // 플레이어가 보이거나 다가왔다(SectorHelper — 걸음마다 시야 안 몬스터마다). 선공은 감지 거리(aggro_range) 안이면 쫓고,
    // 비선공은 깨어나 배회한다. force = 부활 · 순간이동으로 나타났다(감지 거리를 보지 않는다 — 예전 forceWake).
    void Wake(const MonsterSharedPtr& monster, const UserSharedPtr& waker, bool force = false);
    // 맞았다 — 위협(준 피해)을 쌓고, 싸우고 있지 않았으면 곧바로 반격한다(리니지의 반격 — 비선공도)
    void OnDamaged(const MonsterSharedPtr& monster, const UserSharedPtr& attacker, UInt32 damage);
    // 도발(맹세 함성) — 깨워 그 사람을 쫓게 한다(대상은 Taunt 상태의 건 사람이 먼저 — T_Combat)
    void OnTaunted(const MonsterSharedPtr& monster, const UserSharedPtr& taunter);
    // 이 사람을 잊는다(밤 장막 은신)
    void Forget(const MonsterSharedPtr& monster, const ObjID& userId);
    // 반격 타격(HF_Counter) · 무력화 — 예고 중인 기술이 다음 생각에 끊긴다(곧바로 생각한다)
    void OnCountered(const MonsterSharedPtr& monster);
    // 기절 · 빙결 · 무력화가 걸렸다 — 시전 중이면 곧바로 생각해 끊는다
    void OnDisabled(const MonsterSharedPtr& monster);
    // 죽었다 — 시전 · 장판을 정리하고 잠재운다
    void OnDeath(const MonsterSharedPtr& monster);
    [[nodiscard]] bool IsWindingUp(const MonsterSharedPtr& monster);

    // ── 생각 ──
    // EV_MONSTER_THINK(targetId.DatabaseID = 예약 번호)
    void HandleThink(const ObjID& monsterId, UInt32 epoch);
    // at에 생각한다 — 그보다 이르거나 같은 예약이 이미 있으면 그대로 둔다
    void ScheduleThink(const MonsterSharedPtr& monster, UInt32 at);
    // 지금 곧바로(다음 예약 차례에)
    void Poke(const MonsterSharedPtr& monster);
    // 잠재운다 — 달리던 가지를 끊고(시전 취소) 오라 · 주인에 묶인 장판을 거둔 뒤 행동을 풀에 돌려준다
    void Sleep(const MonsterSharedPtr& monster);

    // ── 잎(T_)이 부르는 상태 바꾸기 ──
    void EnterCombat(CMonsterAgent& a);   // 대상이 생겼다 — 전투 시작(동료에게 도움 요청)
    void EndCombat(CMonsterAgent& a);     // 대상이 모두 사라졌다 — 집에서 멀면 귀환
    void BeginReturn(CMonsterAgent& a);   // 리쉬 · Zone 경계 — 위협을 버리고 집으로
    // 감지 거리(선공) — 표에 없으면 선공 종족은 예전 WAKE_RANGE(3)
    [[nodiscard]] UInt8 AggroRange(const CMonsterAgent& a);

    // 깨어 있는 몬스터 수(모든 Zone 합 — 통계 · 시험)
    [[nodiscard]] Int64 AwakeCount();
}
