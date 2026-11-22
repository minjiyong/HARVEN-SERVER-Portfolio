#include "pch.h"
#include "Base/Core/CoreTLS.h"

thread_local UInt32 LThreadId = 0;
thread_local std::stack<Int32> LLockStack;
thread_local std::mt19937 LRng(std::random_device{}());
