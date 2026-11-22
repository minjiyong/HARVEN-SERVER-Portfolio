#pragma once
// 로드 검증 — 빌드 때 static_assert · constexpr Validate()가 하던 규칙을 부팅 때로(RULES.md).
// 규칙마다 R번호를 붙여 오류에 남긴다 → D2 시험이 "그 규칙이 그 위치에서" 걸리는지 확인한다.
#include <string>
#include <vector>

#include "Database/GameData/Load/GameData.h"

namespace GameDataLoad
{
    class LoadContext;

    class ValidateContext
    {
    public:
        ValidateContext(LoadContext& load, std::vector<GameData::Issue>& errors) : m_load(load), m_errors(errors) {}

        // rule = "R031", where = 탭 · 행 위치(LoadContext::Where), what = 이유
        void Fail(const char* rule, std::string where, std::string what)
        {
            m_errors.push_back({ std::move(where), std::move(what), rule });
        }

        // 행 위치 — 탭 이름과 그 탭 JSON의 행 번호(0부터)
        std::string Where(const char* table, size_t index, const char* key = nullptr) const;

    private:
        LoadContext& m_load;
        std::vector<GameData::Issue>& m_errors;
    };

    // 공통 도우미
    size_t Utf8Bytes(const char* s) noexcept;
}
