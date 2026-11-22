#pragma once
// 표 하나의 구조 지문 — 행 수 + 첫 열(키) 값들의 해시(정본 JSON은 키 열이 맨 앞).
// 핫 리로드(--dev)는 모든 표의 지문이 지금 판과 같을 때(값만 바뀐 판)만 받는다 — id를 더하거나 빼면 재시작.
#include <cstdint>
#include <string>

namespace GameDataLoad
{
    struct TableShape
    {
        std::string table;
        uint32_t rows = 0;
        uint64_t keyHash = 0;

        bool operator==(const TableShape&) const = default;
    };
}
