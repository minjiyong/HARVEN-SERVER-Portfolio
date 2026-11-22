#include "pch.h"
#include "Worker/WorkerThread.h"
#include "Network/SocketManager.h"
#include "User/User.h"
#include "User/UserHelper.h"
#include "Entity/Subject/SubjectHelper.h"
#include "Network/GameSession.h"
#include "Network/GameSessionManager.h"
#include "Content/Field/Sector.h"
#include "Content/Field/ZoneManager.h"
#include "Content/Field/ZoneLayout.h"
#include "Content/Guild/GuildService.h"
#include "Content/Auction/AuctionService.h"

void CWorkerThread::DoWork(UInt32 ioIndex) { CSocketManager::Run(ioIndex); }

void CWorkerThread::Disconnect(ObjID clientId)
{
    CUserHelper::LeaveWorld(clientId);
}
