#pragma once

#include "Worker/GameLogicThread.h"
#include "Content/Field/ZoneLayout.h"

class CGameSession;
class CZone;

class CZoneManager
{
public:
    using Task = CGameLogicThread::Task;

public:
    explicit CZoneManager(shared_ptr<CGameLogicThread> defaultThread);
    ~CZoneManager();

    void Enqueue(Task task);
    void EnqueueByZone(ZoneId zoneId, Task task);
    void EnqueueByWorld(int worldX, int worldY, Task task);
    void EnqueueByObject(const ObjID& objectId, Task task);
    void EnqueueBySession(const SessionSharedPtr& session, Task task);

    void RunZone(ZoneId zoneId);

    void UpdateObjectZone(const ObjID& objectId, ZoneId zoneId);
    void RemoveObject(const ObjID& objectId);

    [[nodiscard]] ZoneId GetObjectZone(const ObjID& objectId) const;
    [[nodiscard]] size_t GetZoneCount() const noexcept { return m_Zones.size(); }

    // 지금 이 함수를 호출한 스레드가 objectId를 소유한 Zone 스레드인지 확인한다.
    // ASSERT_OWNED_BY_CURRENT_ZONE 매크로가 내부적으로 사용하는 판정 함수다.
    [[nodiscard]] bool IsCurrentThreadOwner(const ObjID& objectId) const noexcept;

private:
    shared_ptr<CGameLogicThread> m_pDefaultThread;
    vector<unique_ptr<CZone>> m_Zones;
    mutable mutex m_ObjectZoneLock;
    unordered_map<ObjID, ZoneId> m_ObjectZones;
    atomic<int> m_ReadyZoneCount{ 0 };
};

extern shared_ptr<CZoneManager> GZoneManager;

// objectId를 소유한 Zone 스레드 위에서 실행 중이 아니면 즉시 크래시한다.
// 인벤토리처럼 "항상 자기 자신의 Zone 스레드에서만 mutate되어야 하는" 데이터를
// 락 없이 안전하게 두기 위한 전제 조건을 코드 레벨에서 강제하는 용도.
// Release 빌드에서는 완전히 제거되어 성능 영향이 없다.
#ifdef _DEBUG
#define ASSERT_OWNED_BY_CURRENT_ZONE(objId) ASSERT_CRASH(GZoneManager->IsCurrentThreadOwner(objId))
#else
#define ASSERT_OWNED_BY_CURRENT_ZONE(objId) ((void)0)
#endif
