#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// 정본(canonical) JSON 쓰기 — 기획 데이터 표를 "한 가지 바이트"로 적는다.
//
// 두 곳이 같은 바이트를 내야 한다(datatool verify가 매번 확인한다).
//   ① datatool export(파이썬, Tools/DataTool/dt/canon.py) — 시트 → Bin/GameData/
//   ② 서버 --dump-data · datacheck --dump                 — 로드한 스냅샷 → 폴더(이 파일과 Canon_<묶음>.h)
// (이관 때는 ③ 이관 전 constexpr 배열을 같은 코드로 덤프한 골든과도 맞췄다 — 이관이 끝나 골든 · 덤프 프로그램은 지웠다.)
//
// 형식(바꾸면 두 곳을 함께 바꾼다):
//   {"table": "Monster", "rows": [
//   {"id": 1, "name": "수액덩이", ...},
//   {"id": 2, ...}
//   ]}
//   · 한 행 = 한 줄, 열 순서 = 스키마 순서(행마다 같아야 한다), 구분자 ", " · ": ", 파일 끝 줄바꿈 하나
//   · 정수는 10진, bool은 true/false, 색은 "#RRGGBB"(대문자), f32는 std::to_chars 최단 표기
//   · 문자열은 UTF-8 그대로. 이스케이프는 " \ 와 제어 문자만(\b \f \n \r \t, 나머지 \u00xx)
//   · 열거형은 이름 글자(GameDataEnums), 비트 플래그는 이름을 '|'로 이은 글자("" = 0)
//   · 널 가능 문자열(str?)은 nullptr이면 null
// ─────────────────────────────────────────────────────────────────────────────

#include <charconv>
#include <cstdint>
#include <cstdio>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "Database/GameData/Gen/EnumNames.h"

namespace Canon
{
    // 덤프 중 형식 위반(널 문자열 · 이름 없는 열거 값 · 열 순서 불일치)은 예외로 멈춘다 — 조용히 넘어가지 않는다.
    struct Error : std::runtime_error { using std::runtime_error::runtime_error; };

    inline void AppendJsonString(std::string& out, std::string_view s)
    {
        out.push_back('"');
        for (const char ch : s)
        {
            const unsigned char c = static_cast<unsigned char>(ch);
            switch (c)
            {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20)
                {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                }
                else
                    out.push_back(ch);
            }
        }
        out.push_back('"');
    }

    inline std::string ColorText(uint32_t rgb)
    {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "\"#%06X\"", static_cast<unsigned>(rgb & 0xFFFFFFu));
        if (rgb > 0xFFFFFFu)
            throw Error("색 값이 0xRRGGBB를 넘는다");
        return buf;
    }

    inline std::string F32Text(float v)
    {
        char buf[64];
        const auto r = std::to_chars(buf, buf + sizeof(buf), v);
        return std::string(buf, r.ptr);
    }

    template <class E>
    std::string_view EnumName(E v)
    {
        using Info = GameDataEnums::Info<E>;
        const int iv = static_cast<int>(v);
        for (const auto& it : Info::kItems)
            if (it.value == iv)
                return it.name;
        throw Error("열거형 " + std::string(Info::kName) + "에 이름 없는 값 " + std::to_string(iv));
    }

    // flags: 열거자 값 자체가 비트(HitFlag). 이름은 비트 오름차순으로 '|'.
    template <class E>
    std::string FlagsText(uint32_t bits)
    {
        using Info = GameDataEnums::Info<E>;
        std::string s;
        for (int b = 0; b < 32; ++b)
        {
            const uint32_t bit = 1u << b;
            if (!(bits & bit)) continue;
            bool found = false;
            for (const auto& it : Info::kItems)
                if (static_cast<uint32_t>(it.value) == bit)
                {
                    if (!s.empty()) s.push_back('|');
                    s += it.name;
                    found = true;
                    break;
                }
            if (!found)
                throw Error("플래그 " + std::string(Info::kName) + "에 이름 없는 비트 " + std::to_string(b));
        }
        return s;
    }

    // mask: 열거자 값 = 비트 번호(ClassId → 1 << id).
    template <class E>
    std::string MaskText(uint32_t bits)
    {
        using Info = GameDataEnums::Info<E>;
        std::string s;
        for (int b = 0; b < 32; ++b)
        {
            if (!(bits & (1u << b))) continue;
            bool found = false;
            for (const auto& it : Info::kItems)
                if (it.value == b)
                {
                    if (!s.empty()) s.push_back('|');
                    s += it.name;
                    found = true;
                    break;
                }
            if (!found)
                throw Error("마스크 " + std::string(Info::kName) + "에 이름 없는 비트 " + std::to_string(b));
        }
        return s;
    }

    template <class T> inline constexpr const char* kIntType = nullptr;
    template <> inline constexpr const char* kIntType<uint8_t>  = "u8";
    template <> inline constexpr const char* kIntType<uint16_t> = "u16";
    template <> inline constexpr const char* kIntType<uint32_t> = "u32";
    template <> inline constexpr const char* kIntType<uint64_t> = "u64";
    template <> inline constexpr const char* kIntType<int8_t>   = "i8";
    template <> inline constexpr const char* kIntType<int16_t>  = "i16";
    template <> inline constexpr const char* kIntType<int32_t>  = "i32";

    // 한 행. 열은 넣는 순서대로 쓴다.
    class Row
    {
    public:
        template <class T>
        Row& Put(const char* key, const T& v)
        {
            if constexpr (std::is_same_v<T, bool>)
                return Raw(key, "bool", v ? "true" : "false");
            else if constexpr (std::is_enum_v<T>)
            {
                using Info = GameDataEnums::Info<T>;
                std::string s;
                AppendJsonString(s, EnumName(v));
                return Raw(key, "enum:" + std::string(Info::kName), s);
            }
            else if constexpr (std::is_same_v<T, float>)
                return Raw(key, "f32", F32Text(v));
            else if constexpr (std::is_integral_v<T>)
            {
                static_assert(kIntType<T> != nullptr, "지원하지 않는 정수 형");
                if constexpr (std::is_unsigned_v<T>)
                    return Raw(key, kIntType<T>, std::to_string(static_cast<unsigned long long>(v)));
                else
                    return Raw(key, kIntType<T>, std::to_string(static_cast<long long>(v)));
            }
            else if constexpr (std::is_array_v<T> && std::is_same_v<std::remove_extent_t<T>, char>)
            {
                // 고정 길이 char 배열(아이템 ASCII 이름) — 널 앞까지
                size_t n = 0;
                while (n < std::extent_v<T> && v[n] != '\0') ++n;
                if (n == std::extent_v<T>)
                    throw Error(std::string(key) + ": 고정 길이 문자열에 널이 없다");
                return Str(key, std::string_view(v, n));
            }
            else
            {
                static_assert(std::is_same_v<T, const char*>, "지원하지 않는 열 형");
                if (v == nullptr)
                    throw Error(std::string(key) + ": 널 문자열(널 가능 열은 StrOpt)");
                return Str(key, v);
            }
        }

        Row& Str(const char* key, std::string_view s)
        {
            std::string t;
            AppendJsonString(t, s);
            return Raw(key, "str", t);
        }

        // 널 가능 문자열: nullptr ↔ null. ""(널이 아닌 빈 글자)는 시트에서 null과 구별되지 않으므로 거부한다.
        Row& StrOpt(const char* key, const char* s)
        {
            if (s == nullptr)
                return Raw(key, "str?", "null");
            if (*s == '\0')
                throw Error(std::string(key) + ": 널 가능 열에 빈 글자(\"\") — null과 구별되지 않는다");
            std::string t;
            AppendJsonString(t, s);
            return Raw(key, "str?", t);
        }

        Row& Color(const char* key, uint32_t rgb) { return Raw(key, "color", ColorText(rgb)); }

        template <class E>
        Row& Flags(const char* key, uint32_t bits)
        {
            std::string t;
            AppendJsonString(t, FlagsText<E>(bits));
            return Raw(key, "flags:" + std::string(GameDataEnums::Info<E>::kName), t);
        }

        template <class E>
        Row& Mask(const char* key, uint32_t bits)
        {
            std::string t;
            AppendJsonString(t, MaskText<E>(bits));
            return Raw(key, "mask:" + std::string(GameDataEnums::Info<E>::kName), t);
        }

        const std::vector<std::pair<std::string, std::string>>& Columns() const noexcept { return m_cols; }
        const std::string& Body() const noexcept { return m_body; }

    private:
        Row& Raw(const char* key, std::string type, const std::string& text)
        {
            if (!m_body.empty())
                m_body += ", ";
            AppendJsonString(m_body, key);
            m_body += ": ";
            m_body += text;
            m_cols.emplace_back(key, std::move(type));
            return *this;
        }

        std::vector<std::pair<std::string, std::string>> m_cols;
        std::string m_body;
    };

    // 한 표 = 한 파일.
    class Table
    {
    public:
        explicit Table(std::string name) : m_name(std::move(name)) {}

        void Add(const Row& row)
        {
            if (m_rows.empty() && m_cols.empty())
                m_cols = row.Columns();
            else if (!SameColumns(row.Columns()))
                throw Error(m_name + ": 행마다 열(이름 · 순서 · 형)이 같아야 한다 — " + std::to_string(m_rows.size() + 1) + "번째 행");
            m_rows.push_back("{" + row.Body() + "}");
        }

        // 행이 없을 수 있는 표는 열 목록을 따로 알려 준다(스키마 비교용).
        void SetColumns(std::vector<std::pair<std::string, std::string>> cols) { if (m_cols.empty()) m_cols = std::move(cols); }

        const std::string& Name() const noexcept { return m_name; }
        size_t RowCount() const noexcept { return m_rows.size(); }
        const std::vector<std::pair<std::string, std::string>>& Columns() const noexcept { return m_cols; }

        std::string Json() const
        {
            std::string s = "{\"table\": ";
            AppendJsonString(s, m_name);
            s += ", \"rows\": [\n";
            for (size_t i = 0; i < m_rows.size(); ++i)
            {
                s += m_rows[i];
                s += (i + 1 < m_rows.size()) ? ",\n" : "\n";
            }
            s += "]}\n";
            return s;
        }

    private:
        bool SameColumns(const std::vector<std::pair<std::string, std::string>>& c) const
        {
            if (c.size() != m_cols.size()) return false;
            for (size_t i = 0; i < c.size(); ++i)
            {
                if (c[i].first != m_cols[i].first) return false;
                // 널 가능 문자열은 행에 따라 null이어도 형 이름은 같다("str?")
                if (c[i].second != m_cols[i].second) return false;
            }
            return true;
        }

        std::string m_name;
        std::vector<std::pair<std::string, std::string>> m_cols;
        std::vector<std::string> m_rows;
    };

    // 여러 표의 묶음(이름 순으로 파일을 쓴다).
    class Book
    {
    public:
        Table& Get(const std::string& name)
        {
            auto it = m_tables.find(name);
            if (it == m_tables.end())
                it = m_tables.emplace(name, Table(name)).first;
            return it->second;
        }
        const std::map<std::string, Table>& Tables() const noexcept { return m_tables; }

        // 스키마(표 → [[열, 형]…]) — datatool이 YAML 스키마와 대조한다.
        std::string SchemaJson() const
        {
            std::string s = "{\n";
            size_t n = 0;
            for (const auto& [name, t] : m_tables)
            {
                AppendJsonString(s, name);
                s += ": [";
                for (size_t i = 0; i < t.Columns().size(); ++i)
                {
                    if (i) s += ", ";
                    s += "[";
                    AppendJsonString(s, t.Columns()[i].first);
                    s += ", ";
                    AppendJsonString(s, t.Columns()[i].second);
                    s += "]";
                }
                s += "]";
                s += (++n < m_tables.size()) ? ",\n" : "\n";
            }
            s += "}\n";
            return s;
        }

    private:
        std::map<std::string, Table> m_tables;
    };

    // 열거형 이름 표 JSON(GameDataEnums에서) — 서버 · datatool이 같은 표를 쓰는지 확인용.
    template <class E>
    void AppendEnumJson(std::string& s)
    {
        using Info = GameDataEnums::Info<E>;
        AppendJsonString(s, Info::kName);
        s += ": {\"kind\": ";
        s += Info::kFlags ? "\"flags\"" : "\"enum\"";
        s += ", \"items\": [";
        for (size_t i = 0; i < Info::kCount; ++i)
        {
            if (i) s += ", ";
            s += "[";
            AppendJsonString(s, Info::kItems[i].name);
            s += ", " + std::to_string(Info::kItems[i].value) + "]";
        }
        s += "]}";
    }
}
