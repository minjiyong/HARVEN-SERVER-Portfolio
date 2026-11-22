#pragma once

#include <memory>

extern std::shared_ptr<class CThreadManager>   GThreadManager;
extern std::shared_ptr<class CMemory>          GMemory;

extern std::shared_ptr<class CDeadLockProfiler> GDeadLockProfiler;
extern std::shared_ptr<class CDBConnectionPool> GDBConnectionPool;
