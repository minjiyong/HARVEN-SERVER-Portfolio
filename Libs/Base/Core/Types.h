#pragma once
// 정수 형 별칭(REIW) — 서버 · 엔진 코드는 int32_t 대신 Int32를 쓴다.
// 클라와 같이 쓰는 헤더(Shared/** · Server/HARVEN_GameServer/Database/GameData/** · Base/ObjID)는 표준 이름 그대로다.

#include <cstdint>

using Int8   = std::int8_t;
using Int16  = std::int16_t;
using Int32  = std::int32_t;
using Int64  = std::int64_t;
using UInt8  = std::uint8_t;
using UInt16 = std::uint16_t;
using UInt32 = std::uint32_t;
using UInt64 = std::uint64_t;
