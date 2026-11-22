#pragma once
// 스냅샷이 소유하는 문자열 영역 — 표의 const char* 필드가 가리키는 곳(SHEET_DATA_PROMPT [4] · [13]).
// 블록 단위로 붙여 쓰고 옮기지 않는다 → 돌려준 포인터는 스냅샷이 살아 있는 동안 유효하다.
// (임시 std::string의 c_str()를 필드에 넣지 않는다.)
#include <cstddef>
#include <cstring>
#include <memory>
#include <string_view>
#include <vector>

class StringArena
{
public:
    StringArena() = default;
    StringArena(const StringArena&) = delete;
    StringArena& operator=(const StringArena&) = delete;
    StringArena(StringArena&&) noexcept = default;
    StringArena& operator=(StringArena&&) noexcept = default;

    // s를 복사해(끝에 널) 안정된 포인터를 돌려준다.
    const char* Add(std::string_view s)
    {
        const size_t need = s.size() + 1;
        if (m_blocks.empty() || m_used + need > m_blockSize)
        {
            const size_t size = need > kBlock ? need : kBlock;
            m_blocks.emplace_back(new char[size]);
            m_blockSize = size;
            m_used = 0;
        }
        char* p = m_blocks.back().get() + m_used;
        if (!s.empty())
            std::memcpy(p, s.data(), s.size());
        p[s.size()] = '\0';
        m_used += need;
        m_total += need;
        return p;
    }

    size_t Bytes() const noexcept { return m_total; }

private:
    static constexpr size_t kBlock = 64 * 1024;
    std::vector<std::unique_ptr<char[]>> m_blocks;
    size_t m_blockSize = 0;
    size_t m_used = 0;
    size_t m_total = 0;
};
