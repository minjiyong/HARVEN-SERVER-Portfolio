#pragma once
#include <random>
#include <stack>

extern thread_local UInt32 LThreadId;
extern thread_local std::stack<Int32> LLockStack;

// 스레드별 독립 RNG — rand()의 글로벌 CRT 락 없이 사용
extern thread_local std::mt19937 LRng;
