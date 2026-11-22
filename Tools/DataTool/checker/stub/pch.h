#pragma once
// datacheck 전용 pch.h 대체물 — 로더(Database/GameData/Load)를 서버 PCH 없이 컴파일할 때 쓴다.
#include "prelude.h"
#include <iostream>
#include <map>
#include <mutex>
// 서버 Core의 단언 매크로 — datacheck에서는 조건이 틀리면 멈춘다
#include <cstdlib>
#define ASSERT_CRASH(cond) do { if (!(cond)) std::abort(); } while (0)
