#include "pch.h"
#include "WorkerThread/ThreadManager.h"
#include "Base/Core/CoreTLS.h"
#include "Base/Core/CoreGlobal.h"
#include "Base/Core/CrashHandler.h"
#include <pthread.h>

/*------------------
    CThreadManager
-------------------*/

CThreadManager::CThreadManager()
{
    // Main Thread
    InitTLS();
}

CThreadManager::~CThreadManager()
{
    Join();
}

void CThreadManager::Launch(function<void(void)> callback, const char* name)
{
    LockGuard guard(m_Lock);

    // pthread 이름은 끝의 '\0' 포함 16바이트 — 넘치면 자른다
    char threadName[16]{};
    if (name != nullptr)
        std::snprintf(threadName, sizeof(threadName), "%s", name);

    m_Threads.push_back(thread([callback = std::move(callback), threadName]()
        {
            if (threadName[0] != '\0')
                ::pthread_setname_np(::pthread_self(), threadName);
            InitTLS();
            callback();
            DestroyTLS();
        }));
}

void CThreadManager::Join()
{
    for (thread& t : m_Threads)
    {
        if (t.joinable())
            t.join();
    }
    m_Threads.clear();
}

void CThreadManager::InitTLS()
{
    static Atomic<UInt32> SThreadId = 1;
    if (LThreadId == 0)
        LThreadId = SThreadId.fetch_add(1);
    CCrashHandler::InitThread();
}

void CThreadManager::DestroyTLS()
{

}
