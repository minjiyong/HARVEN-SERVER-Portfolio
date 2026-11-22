#include "pch.h"
#include "Content/Field/ZoneManager.h"
#include "Network/GameSession.h"
#include "Content/Field/Zone.h"

CZoneManager::CZoneManager(shared_ptr<CGameLogicThread> defaultThread)
    : m_pDefaultThread(std::move(defaultThread))
{
    ASSERT_CRASH(m_pDefaultThread != nullptr);

    m_Zones.reserve(CZoneLayout::ZoneCount);
    for (ZoneId id = 0; id < static_cast<ZoneId>(CZoneLayout::ZoneCount); ++id)
        m_Zones.push_back(make_unique<CZone>(id));
}

CZoneManager::~CZoneManager() = default;

void CZoneManager::Enqueue(Task task)
{
    ASSERT_CRASH(m_pDefaultThread != nullptr);
    m_pDefaultThread->Enqueue(std::move(task));
}

void CZoneManager::EnqueueByZone(ZoneId zoneId, Task task)
{
    if (CZoneLayout::IsValidZoneId(zoneId))
    {
        m_Zones[zoneId]->Enqueue(std::move(task));
        return;
    }

    Enqueue(std::move(task));
}

void CZoneManager::EnqueueByWorld(int worldX, int worldY, Task task)
{
    EnqueueByZone(CZoneLayout::GetZoneIdByWorld(worldX, worldY), std::move(task));
}

void CZoneManager::EnqueueByObject(const ObjID& objectId, Task task)
{
    EnqueueByZone(GetObjectZone(objectId), std::move(task));
}

void CZoneManager::EnqueueBySession(const SessionSharedPtr& session, Task task)
{
    const ZoneId zoneId = session != nullptr ? session->GetRoutingZoneId() : InvalidZoneId;
    EnqueueByZone(zoneId, std::move(task));
}

void CZoneManager::RunZone(ZoneId zoneId)
{
    ASSERT_CRASH(CZoneLayout::IsValidZoneId(zoneId));

    const int ready = ++m_ReadyZoneCount;
    if (ready == static_cast<int>(m_Zones.size()))
        std::cout << "[Server] All " << ready << " zone threads are ready.\n";

    m_Zones[zoneId]->Run();
}

void CZoneManager::UpdateObjectZone(const ObjID& objectId, ZoneId zoneId)
{
    if (!CZoneLayout::IsValidZoneId(zoneId))
        return;

    scoped_lock lock(m_ObjectZoneLock);
    m_ObjectZones[objectId] = zoneId;
}

void CZoneManager::RemoveObject(const ObjID& objectId)
{
    scoped_lock lock(m_ObjectZoneLock);
    m_ObjectZones.erase(objectId);
}

ZoneId CZoneManager::GetObjectZone(const ObjID& objectId) const
{
    scoped_lock lock(m_ObjectZoneLock);
    const auto it = m_ObjectZones.find(objectId);
    if (it == m_ObjectZones.end())
        return InvalidZoneId;

    return it->second;
}

bool CZoneManager::IsCurrentThreadOwner(const ObjID& objectId) const noexcept
{
    return LCurrentZoneId != InvalidZoneId && GetObjectZone(objectId) == LCurrentZoneId;
}

