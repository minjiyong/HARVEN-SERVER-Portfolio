#pragma once

class CSocketManager;
class CDBThread;
class CSector;
class CTimerThread;
class CAStar;

class CWorkerThread
{
public:
    CWorkerThread() = default;
    ~CWorkerThread() = default;

    void            Disconnect(ObjID clientId);
    // IO 스레드 하나 — ioIndex번 io_context를 돌린다(CSocketManager::Run)
    void            DoWork(UInt32 ioIndex);
};
