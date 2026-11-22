#pragma once

#include "Content/Navigator/AStar.h"
#include "Content/Reward/DamageLedger.h"
#include "Entity/Subject/Subject.h"
#include "Database/GameData/MonsterTable.h"

class CEntityBehavior;

class CMonster : public CSubject
{
    MONSTER_TYPE m_eType;
    vector<NODE> m_AstarPath;

    short m_SpawnX = 0;
    short m_SpawnY = 0;
    short m_PathGoalX = -1;
    short m_PathGoalY = -1;
    short m_PathFailGoalX = -1;
    short m_PathFailGoalY = -1;
    UInt32 m_PathFailUntil = 0;

    // 마지막 이동 시점에 이 몬스터를 보고 있던 유저 목록.
    // 다음 이동 시 oldList로 재사용해 CollectUsers 호출을 절반으로 줄인다.
    // CGameLogicThread 단일 소비자이므로 동기화 불필요.
    vector<ObjID> m_ViewList;

    // ── 리니지풍 컨텐츠 확장 ──
    // 종족 정보(Data/MonsterTable.h). ObjID의 KIND에 실린 종족 id로 InitInstance에서
    // 한 번 찾아 둔다 — 테이블은 불변 스냅샷이라 포인터를 들고 있어도 안전하다.
    const MonsterTableRow* m_pSpecies = nullptr;

    // 행동(행동 트리 상태 · 위협 · 기술 일정) — 깨어 있는 동안만 붙는다(CEntityBehaviorHelper가 풀에서 꺼내고 돌려준다).
    // 소유 Zone 스레드 전용.
    CEntityBehavior* m_pBehavior = nullptr;

public:
    // ── 생각 예약(EV_MONSTER_THINK) — 행동이 다음에 깨어날 시각. 예약 번호가 맞지 않는 예약은 낡은 것(거른다) ──
    UInt32 m_ThinkEpoch = 0;
    UInt32 m_ThinkAt = 0;
    bool   m_bThinkPending = false;
    bool   m_bThinking = false;          // 지금 트리를 도는 중(그 사이 죽으면 잠재우기를 미룬다 — 도는 중인 행동을 돌려주지 않게)
    bool   m_bSleepAfterThink = false;

    // Phase 2-B 보스 무력화(게이지가 비면 3초 무력화 + 받는 피해 +30%) — 무력화되면 예고 중인 기술이 끊긴다
    UInt16 m_Stagger = 0;           // 남은 무력화 게이지(0이면 무력화 중 · 회복 대기)
    UInt16 m_StaggerMax = 0;
    UInt32 m_StaggerRefillAt = 0;   // 이 시각 이후 첫 타격 때 게이지를 다시 채운다
    UInt32 m_StaggerSentAt = 0;

    // 처치 보상 나눔(그로시 · 경험치 피해 비례 · 퀘스트 인정 — Content/Reward/RewardHelper::HandleKill) — 이번 생애에 플레이어가 실제로 깎은 HP. 소유 Zone 스레드 전용.
    // 다시 태어날 때(HandleRespawn) · 집에 돌아가 HP를 다 채울 때(T_ReturnHome) 지운다.
    CDamageLedger m_DamageLedger;

private:
    // 정예(경제 1차 명세 §6 — Shared/Document/ECONOMY.md 「정예」) — 스폰 · 재등장마다 CMonsterHelper::RollElite가 정한다. 소유 Zone 스레드 전용.
    // 기본값 = InitInstance가 정한 종 최대 HP · 공격력(정예를 풀 때 되돌릴 값).
    bool   m_bElite = false;
    UInt16 m_BaseMaxHp = 0;
    UInt16 m_BaseOffensive = 0;

public:
    [[nodiscard]] bool IsElite() const noexcept { return m_bElite; }
    // 정예면 최대 HP · 공격력을 시트 배율(elite_hp_percent · elite_attack_percent — 65535 상한)로, 아니면 기본값으로. 지금 HP는 부른 쪽이 채운다.
    void SetElite(bool elite) noexcept;
    // 방어 · 회피 · 경직 계산 레벨 = 종 레벨(정예면 + elite_level_bonus)
    [[nodiscard]] int CombatLevel() noexcept;

public:
    using SharedPtr = MonsterSharedPtr;
    using WeakPtr = MonsterWeakPtr;

public:
    CMonster() = default;
    CMonster(const ObjID& objID) : CSubject(objID) {};
    ~CMonster() override;

    virtual void InitInstance() override;

    MONSTER_TYPE GetType() const { return m_eType; }
    vector<NODE>& GetPath() { return m_AstarPath; }
    bool IsPathTarget(short x, short y) const noexcept { return m_PathGoalX == x && m_PathGoalY == y; }
    bool HasCachedPathTo(short x, short y) const noexcept { return IsPathTarget(x, y) && !m_AstarPath.empty(); }

    // 깨어 있는가(행동이 붙어 있다)
    [[nodiscard]] bool IsActive() const noexcept { return m_pBehavior != nullptr; }
    [[nodiscard]] CEntityBehavior* GetBehavior() const noexcept { return m_pBehavior; }
    void SetBehavior(CEntityBehavior* behavior) noexcept { m_pBehavior = behavior; }

    void SetType(MONSTER_TYPE t) { m_eType = t; }
    void CachePathTarget(short x, short y) noexcept { m_PathGoalX = x; m_PathGoalY = y; }
    void ClearPath()             { m_AstarPath.clear(); m_PathGoalX = -1; m_PathGoalY = -1; }

    // 길을 못 찾은 목표는 잠깐 다시 찾지 않는다 — 닿을 수 없는 목표를 걸음마다 A* 상한까지 뒤지지 않게(목표가 바뀌면 바로 다시 찾는다)
    [[nodiscard]] bool IsPathSearchBackedOff(short x, short y, UInt32 now) const noexcept
    {
        return m_PathFailGoalX == x && m_PathFailGoalY == y && static_cast<Int32>(m_PathFailUntil - now) > 0;
    }
    void BackOffPathSearch(short x, short y, UInt32 until) noexcept { m_PathFailGoalX = x; m_PathFailGoalY = y; m_PathFailUntil = until; }

    const vector<ObjID>& GetViewList() const noexcept { return m_ViewList; }
    void SetViewList(vector<ObjID> list) noexcept { m_ViewList = std::move(list); }
    // 이번 걸음에 보던 플레이어로 바꾼다(벡터 용량은 그대로 다시 쓴다)
    void AssignViewList(const vector<SubjectSharedPtr>& viewers)
    {
        m_ViewList.clear();
        for (const auto& viewer : viewers)
            m_ViewList.push_back(viewer->GetObjID());
    }
    void ClearViewList() noexcept { m_ViewList.clear(); }

    void  SetSpawn(short x, short y) { m_SpawnX = x; m_SpawnY = y; }
    short GetSpawnX() const { return m_SpawnX; }
    short GetSpawnY() const { return m_SpawnY; }

    [[nodiscard]] const MonsterTableRow* GetSpecies() const noexcept { return m_pSpecies; }
    [[nodiscard]] bool IsBoss() const noexcept { return m_pSpecies != nullptr && m_pSpecies->boss; }
    // Phase 2-E 훈련장 허수아비: 깨어나지 않고(움직임 · 공격 없음) 죽지 않는다
    [[nodiscard]] bool IsDummy() const noexcept { return m_pSpecies != nullptr && m_pSpecies->dummy; }
    // 이동 · 공격 타이머 간격 — 표는 R102(≥ 100ms)가 막지만, 검증을 건너뛴 판(핫 리로드)에서도 지연 없는 타이머가 연달아 걸리지 않게 여기서 자른다
    [[nodiscard]] UInt16 GetMoveIntervalMs() const noexcept
    {
        const UInt16 ms = m_pSpecies != nullptr ? m_pSpecies->moveIntervalMs : 500;
        return ms < MonsterTable::kMinActIntervalMs ? MonsterTable::kMinActIntervalMs : ms;
    }
    [[nodiscard]] UInt16 GetAttackIntervalMs() const noexcept
    {
        const UInt16 ms = m_pSpecies != nullptr ? m_pSpecies->attackIntervalMs : 1000;
        return ms < MonsterTable::kMinActIntervalMs ? MonsterTable::kMinActIntervalMs : ms;
    }
    [[nodiscard]] int GetLeash() const noexcept { return m_pSpecies != nullptr ? m_pSpecies->leash : 7; }
};
