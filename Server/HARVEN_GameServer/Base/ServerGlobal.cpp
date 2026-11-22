#include "pch.h"
#include "Base/ServerGlobal.h"
#include "Base/GameObjectManager.h"
#include "Database/DBThread.h"
#include "Worker/GameLogicThread.h"
#include "Content/Field/ZoneManager.h"
#include "Worker/WorkerThread.h"
#include "Worker/TimerThread.h"
#include "Network/GameSessionManager.h"

shared_ptr<CDBThread>           GDBThread = nullptr;
thread_local CSector*           GSector = nullptr;
thread_local ZoneId             LCurrentZoneId = InvalidZoneId;
shared_ptr<CGameLogicThread>    GGameLogicThread = nullptr;
shared_ptr<CZoneManager>        GZoneManager = nullptr;
shared_ptr<CWorkerThread>       GWorkerThread = nullptr;
shared_ptr<CTimerThread>        GTimerThread = nullptr;
int                             GViewRange = 9;
int                             GServerPort = PORT_NUM;
bool                            GLoadTestMode = false;
shared_ptr<CGameSessionManager> GSessionManager = nullptr;
shared_ptr<CGameObjectManager>  GGameObjectManager = nullptr;

// Previously in Core/CoreGlobal.cpp (now defined here since Core is removed)
shared_ptr<CThreadManager>    GThreadManager    = make_shared<CThreadManager>();
shared_ptr<CDBConnectionPool> GDBConnectionPool = make_shared<CDBConnectionPool>();

class CServerGlobal
{
public:
    CServerGlobal()
    {
        GGameObjectManager = make_shared<CGameObjectManager>();
        GDBThread = make_shared<CDBThread>();
        GGameLogicThread = make_shared<CGameLogicThread>();
        GZoneManager = make_shared<CZoneManager>(GGameLogicThread);
        GWorkerThread = make_shared<CWorkerThread>();
        GTimerThread = make_shared<CTimerThread>();
        GSessionManager = make_shared<CGameSessionManager>();
    }

    ~CServerGlobal()
    {
        GSessionManager.reset();
        GTimerThread.reset();
        GWorkerThread.reset();
        GZoneManager.reset();
        GGameLogicThread.reset();
        GDBThread.reset();
        GGameObjectManager.reset();
    }
} GServerGlobal;
