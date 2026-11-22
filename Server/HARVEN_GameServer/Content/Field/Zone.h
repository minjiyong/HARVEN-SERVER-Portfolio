#pragma once

#include "Worker/GameLogicThread.h"
#include "Content/Field/ZoneTypes.h"
#include "Content/Field/Sector.h"

class CZone
{
public:
    using Task = CGameLogicThread::Task;

public:
    explicit CZone(ZoneId id);
    ~CZone() = default;

    [[nodiscard]] ZoneId GetId() const noexcept { return m_Id; }
    [[nodiscard]] CSector& GetSector() noexcept { return m_Sector; }

    void Enqueue(Task task);
    void Run();

private:
    ZoneId m_Id = InvalidZoneId;
    CSector m_Sector;
    CGameLogicThread m_LogicThread;
};
