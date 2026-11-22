#pragma once

#include <thread>
#include <functional>

/*------------------
    CThreadManager
-------------------*/

class CThreadManager
{
public:
    CThreadManager();
    ~CThreadManager();

    // name: 스레드 이름(top -H · perf · crash.log에 보인다, 15자까지)
    void    Launch(function<void(void)> callback, const char* name = nullptr);
    void    Join();

    static void InitTLS();
    static void DestroyTLS();

private:
    Mutex           m_Lock;
    vector<thread>  m_Threads;
};

