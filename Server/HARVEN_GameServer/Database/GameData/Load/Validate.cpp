#include "pch.h"
// 로드 검증 공통 — 규칙 자체는 묶음마다 Bind_<묶음>.cpp의 Validate<묶음>(RULES.md R번호).
#include "Database/GameData/Load/Validate.h"
#include "Database/GameData/Load/JsonRead.h"

namespace GameDataLoad
{
    std::string ValidateContext::Where(const char* table, size_t index, const char* key) const
    {
        return m_load.Where(table, index, key);
    }

    size_t Utf8Bytes(const char* s) noexcept
    {
        size_t n = 0;
        if (s != nullptr)
            while (s[n] != '\0') ++n;
        return n;
    }
}
