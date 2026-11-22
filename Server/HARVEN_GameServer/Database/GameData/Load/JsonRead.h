#pragma once
// 형 검사 읽기 도우미 — GameData/*.json 한 행을 런타임 구조체로(SHEET_DATA_PROMPT [7]).
//
//  · 오류는 모아서 한 번에(첫 오류에서 멈추지 않는다). 위치는 시트 원본("Monster!14 name: …") —
//    export가 남긴 GameData/_src.json이 있으면 시트 행 번호, 없으면 "Monster.json#3(id=3)".
//  · 형 · 범위를 넘으면 오류(조용히 자르지 않는다). 정본에 없는 열 · 빠진 열도 오류.
//  · 문자열은 스냅샷의 StringArena로 복사한다(const char* 필드가 가리킬 곳).
// 이 헤더는 Data/Load/*.cpp에서만 include한다(nlohmann/json이 크다).
#include <array>
#include <chrono>
#include <cstdint>
#include <thread>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <vector>

#include "ThirdParty/nlohmann/json.hpp"
#include "Database/GameData/Gen/EnumNames.h"
#include "Database/GameData/Load/GameData.h"
#include "Database/GameData/Load/StringArena.h"
#include "Database/GameData/Load/TableShape.h"

namespace GameDataLoad
{
    // ── 일시 메모리(JSON 트리 · 파일 원문) — 읽기(Build) 동안만 쓴다 ─────────────────────────
    // 할당 자리를 한 곳에 모아 둔 것 — 지금은 malloc(glibc는 스레드별 arena라 로직 스레드와 잠금을 다투지 않는다).
    // Scope는 Build 하나의 범위 표시(다른 할당기로 바꿀 때 여기서 묶는다).
    namespace Temp
    {
        inline void* Alloc(size_t n)
        {
            void* raw = std::malloc(n);
            if (raw == nullptr) throw std::bad_alloc();
            return raw;
        }

        inline void Free(void* p) noexcept
        {
            std::free(p);
        }

        class Scope
        {
        public:
            Scope() noexcept = default;
            Scope(const Scope&) = delete;
            Scope& operator=(const Scope&) = delete;
        };
    }

    template <class T>
    struct TempAlloc
    {
        using value_type = T;
        TempAlloc() noexcept = default;
        template <class U> TempAlloc(const TempAlloc<U>&) noexcept {}
        T* allocate(size_t n) { return static_cast<T*>(Temp::Alloc(n * sizeof(T))); }
        void deallocate(T* p, size_t) noexcept { Temp::Free(p); }
        template <class U> bool operator==(const TempAlloc<U>&) const noexcept { return true; }
        template <class U> bool operator!=(const TempAlloc<U>&) const noexcept { return false; }
    };

    using TempString = std::basic_string<char, std::char_traits<char>, TempAlloc<char>>;
    using Json = nlohmann::basic_json<std::map, std::vector, TempString, bool, std::int64_t, std::uint64_t, double, TempAlloc>;

    inline std::string ToStd(const TempString& s) { return std::string(s.data(), s.size()); }

    // 표의 구조 지문(핫 리로드 — Snapshot.h TableShape). 정본 JSON은 한 행 한 줄이고 키 열이 맨 앞이다:
    //   {"id": 5, "name": "…", …}  →  '{"id": 5'까지(첫 열 이름 · 값)를 FNV-1a 64로 잇는다.
    inline TableShape ShapeOf(const char* table, size_t rows, std::string_view text)
    {
        TableShape t;
        t.table = table;
        t.rows = static_cast<uint32_t>(rows);
        uint64_t h = 1469598103934665603ull;
        size_t pos = 0;
        while ((pos = text.find("\n{\"", pos)) != std::string_view::npos)
        {
            ++pos;
            size_t end = text.find(", \"", pos);
            const size_t eol = text.find('\n', pos);
            if (end == std::string_view::npos || (eol != std::string_view::npos && end > eol))
                end = eol == std::string_view::npos ? text.size() : eol;
            for (size_t i = pos; i < end; ++i) { h ^= static_cast<uint8_t>(text[i]); h *= 1099511628211ull; }
            h ^= 0xFF; h *= 1099511628211ull;   // 행 경계
        }
        t.keyHash = h;
        return t;
    }

    // 개발 서버 핫 리로드용 파일 원문 기억 — 크기 · 수정 시각이 같으면 다시 열지 않는다(datatool export는 바뀐 파일만 쓴다).
    // Wine(샌드박스)에서는 파일 열기가 wineserver를 거쳐 로직 스레드의 대기 · 깨우기와 다툰다 — 리로드마다 바뀐 표 몇 개만 연다.
    // 내용이 다른데 크기 · 시각이 같아도 manifest의 sha256 확인이 잡는다(그 판은 거부).
    struct FileCache
    {
        struct Entry
        {
            uintmax_t size = 0;
            long long mtime = 0;
            std::string text;
        };
        std::map<std::string, Entry, std::less<>> files;
        size_t hits = 0, reads = 0;
    };

    class LoadContext
    {
    public:
        LoadContext(std::filesystem::path dir, StringArena& strings, std::vector<GameData::Issue>& errors)
            : m_dir(std::move(dir)), m_strings(strings), m_errors(errors)
        {
            LoadSrc();
        }

        const std::filesystem::path& Dir() const noexcept { return m_dir; }
        StringArena& Strings() noexcept { return m_strings; }

        // <dir>/<name> 파일 내용(한 번만 읽고 기억한다 — manifest 확인과 표 읽기가 같은 바이트를 쓴다). 없으면 nullptr.
        const TempString* Text(const std::string& name)
        {
            auto it = m_text.find(name);
            if (it == m_text.end())
            {
                TempString text;
                if (m_cache != nullptr)
                {
                    std::error_code ec1, ec2;
                    const auto path = m_dir / name;
                    const uintmax_t size = std::filesystem::file_size(path, ec1);
                    const long long mtime = static_cast<long long>(std::filesystem::last_write_time(path, ec2).time_since_epoch().count());
                    auto c = m_cache->files.find(name);
                    if (!ec1 && !ec2 && c != m_cache->files.end() && c->second.size == size && c->second.mtime == mtime)
                    {
                        text.assign(c->second.text.data(), c->second.text.size());
                        ++m_cache->hits;
                    }
                    else
                    {
                        if (!ReadFile(path, text))
                            return nullptr;
                        ++m_cache->reads;
                        if (!ec1 && !ec2)
                            m_cache->files[name] = FileCache::Entry{ size, mtime, std::string(text.data(), text.size()) };
                    }
                }
                else if (!ReadFile(m_dir / name, text))
                    return nullptr;
                it = m_text.emplace(name, std::move(text)).first;
            }
            return &it->second;
        }

        void UseCache(FileCache* cache) noexcept { m_cache = cache; }

        // <dir>/<table>.json의 rows 배열. 없거나 깨졌으면 오류를 남기고 nullptr.
        const Json* Rows(const char* table)
        {
            auto it = m_docs.find(table);
            if (it == m_docs.end())
            {
                Json doc;
                const TempString* text = Text(std::string(table) + ".json");
                if (text == nullptr)
                {
                    Error(std::string(table) + ".json", "파일이 없다");
                    it = m_docs.emplace(table, Json()).first;
                }
                else
                {
                    doc = Json::parse(*text, nullptr, false);
                    if (doc.is_discarded() || !doc.is_object() || !doc.contains("rows") || !doc["rows"].is_array())
                    {
                        Error(std::string(table) + ".json", "JSON 형식이 아니다(rows 배열 없음)");
                        doc = Json();
                    }
                    else
                    {
                        if (!doc.contains("table") || doc["table"] != table)
                            Error(std::string(table) + ".json", "table 이름이 파일 이름과 다르다");
                        m_shape[table] = ShapeOf(table, doc["rows"].size(), std::string_view(text->data(), text->size()));
                        m_rowCounts[table] = doc["rows"].size();
                    }
                    m_text.erase(std::string(table) + ".json");   // 파싱했으면 원문은 놓는다(일시 메모리를 줄인다)
                    it = m_docs.emplace(table, std::move(doc)).first;
                    if (m_gentle)
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));   // 핫 리로드: 로직 스레드에 CPU를 자주 내준다
                }
            }
            if (it->second.is_null())
                return nullptr;
            return &it->second["rows"];
        }

        // 표마다 행 수 · 첫 열(키 — 스키마는 키 열이 맨 앞) 지문. 핫 리로드가 "값만 바뀐 판"인지 가린다.
        std::vector<TableShape> Shapes() const
        {
            std::vector<TableShape> out;
            for (const auto& [name, shape] : m_shape)
                out.push_back(shape);
            return out;
        }

        // 행 위치 글자 — 시트 행(_src.json)이 있으면 "Monster!14", 없으면 "Monster.json#3"
        std::string Where(const char* table, size_t index, const char* key = nullptr) const
        {
            std::string w;
            auto it = m_src.find(table);
            if (it != m_src.end() && index < it->second.size())
                w = std::string(table) + "!" + std::to_string(it->second[index]);
            else
                w = std::string(table) + ".json#" + std::to_string(index + 1);
            if (key)
            {
                w += ' ';
                w += key;
            }
            return w;
        }

        void Error(std::string where, std::string what, std::string rule = {})
        {
            m_errors.push_back({ std::move(where), std::move(what), std::move(rule) });
        }

        size_t ErrorCount() const noexcept { return m_errors.size(); }

        // 읽은 표 수 · 행 수(부팅 로그)
        void Stats(size_t& tables, size_t& rows) const
        {
            tables = m_rowCounts.size();
            rows = 0;
            for (const auto& [name, n] : m_rowCounts)
                rows += n;
        }

        // 묶음 하나를 다 묶었으면 그 표들의 JSON 트리를 놓는다(일시 메모리 최대치 = 가장 큰 묶음 하나).
        void ReleaseDocs() { m_docs.clear(); }

        // 핫 리로드(--dev)용 — 표 사이 · 묶음 사이에 잠깐 쉰다(로드는 늦어도 되고, 로직 틱은 늦으면 안 된다)
        void SetGentle(bool on) noexcept { m_gentle = on; }
        bool Gentle() const noexcept { return m_gentle; }

    private:
        static bool ReadFile(const std::filesystem::path& path, TempString& out)
        {
            FILE* f = nullptr;
            f = std::fopen(path.string().c_str(), "rb");
            if (!f) return false;
            char buf[65536];
            size_t n;
            while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
            std::fclose(f);
            // 정본은 LF — Windows git(core.autocrlf)이 CRLF로 바꿔 받아도 같은 바이트로 본다(manifest sha256 · 파싱).
            // JSON 문자열 안의 줄바꿈은 \n 이스케이프라 원문에 날 CR은 줄 끝뿐이다.
            size_t w = 0;
            for (size_t r = 0; r < out.size(); ++r)
                if (!(out[r] == '\r' && r + 1 < out.size() && out[r + 1] == '\n'))
                    out[w++] = out[r];
            out.resize(w);
            return true;
        }

        void LoadSrc()
        {
            TempString text;
            if (!ReadFile(m_dir / "_src.json", text))
                return;
            const Json j = Json::parse(text, nullptr, false);
            if (j.is_discarded() || !j.is_object())
                return;
            for (auto it = j.begin(); it != j.end(); ++it)
            {
                std::vector<uint32_t> rows;
                if (it.value().is_array())
                    for (const auto& v : it.value())
                        rows.push_back(v.is_number_unsigned() ? v.get<uint32_t>() : 0u);
                m_src.emplace(ToStd(it.key()), std::move(rows));
            }
        }

        std::filesystem::path m_dir;
        StringArena& m_strings;
        std::vector<GameData::Issue>& m_errors;
        std::map<std::string, Json, std::less<>> m_docs;
        std::map<std::string, TempString, std::less<>> m_text;     // 파일 내용(한 번만 읽는다 — 전용 힙)
        std::map<std::string, TableShape, std::less<>> m_shape;
        std::map<std::string, size_t, std::less<>> m_rowCounts;
        bool m_gentle = false;
        FileCache* m_cache = nullptr;
        std::map<std::string, std::vector<uint32_t>, std::less<>> m_src;
    };

    // 한 행 읽기. 읽은 열을 기억했다가 Finish()에서 정본에 없는 열을 잡는다.
    class RowReader
    {
    public:
        RowReader(LoadContext& ctx, const char* table, size_t index, const Json& row)
            : m_ctx(ctx), m_table(table), m_index(index), m_row(row)
        {
            if (!row.is_object())
            {
                m_ctx.Error(m_ctx.Where(table, index), "행이 객체가 아니다");
                m_bad = true;
            }
        }

        bool Ok() const noexcept { return !m_bad; }

        template <class T>
        RowReader& Int(const char* key, T& out)
        {
            static_assert(std::is_integral_v<T> && !std::is_same_v<T, bool>);
            const Json* v = Get(key);
            if (!v) return *this;
            if (v->is_number_unsigned())
            {
                const uint64_t x = v->get<uint64_t>();
                if (x > static_cast<uint64_t>(std::numeric_limits<T>::max()))
                    return Fail(key, "범위를 넘는다(" + std::to_string(x) + " > " + std::to_string(static_cast<unsigned long long>(std::numeric_limits<T>::max())) + ")");
                out = static_cast<T>(x);
            }
            else if (v->is_number_integer())
            {
                const int64_t x = v->get<int64_t>();
                if (x < static_cast<int64_t>(std::numeric_limits<T>::min()) || (x > 0 && static_cast<uint64_t>(x) > static_cast<uint64_t>(std::numeric_limits<T>::max())))
                    return Fail(key, "범위를 넘는다(" + std::to_string(x) + ")");
                out = static_cast<T>(x);
            }
            else
                return Fail(key, "정수가 아니다");
            return *this;
        }

        RowReader& Bool(const char* key, bool& out)
        {
            const Json* v = Get(key);
            if (!v) return *this;
            if (!v->is_boolean()) return Fail(key, "true/false가 아니다");
            out = v->get<bool>();
            return *this;
        }

        RowReader& F32(const char* key, float& out)
        {
            const Json* v = Get(key);
            if (!v) return *this;
            if (!v->is_number()) return Fail(key, "수가 아니다");
            out = static_cast<float>(v->get<double>());
            return *this;
        }

        RowReader& Color(const char* key, uint32_t& out)
        {
            const Json* v = Get(key);
            if (!v) return *this;
            if (!v->is_string()) return Fail(key, "색은 \"#RRGGBB\" 글자여야 한다");
            const std::string_view s = v->get_ref<const TempString&>();
            if (s.size() != 7 || s[0] != '#') return Fail(key, "색은 #RRGGBB(" + std::string(s) + ")");
            uint32_t x = 0;
            for (size_t i = 1; i < 7; ++i)
            {
                const char c = s[i];
                uint32_t d;
                if (c >= '0' && c <= '9') d = static_cast<uint32_t>(c - '0');
                else if (c >= 'A' && c <= 'F') d = static_cast<uint32_t>(c - 'A' + 10);
                else return Fail(key, "색은 대문자 16진 #RRGGBB(" + std::string(s) + ")");
                x = (x << 4) | d;
            }
            out = x;
            return *this;
        }

        // 널이 아닌 문자열("" 허용)
        RowReader& Str(const char* key, const char*& out)
        {
            const Json* v = Get(key);
            if (!v) return *this;
            if (!v->is_string()) return Fail(key, "문자열이 아니다");
            { const TempString& t = v->get_ref<const TempString&>(); out = m_ctx.Strings().Add(std::string_view(t.data(), t.size())); }
            return *this;
        }

        // 널 가능 문자열: null → nullptr. ""는 정본에 없다(오류).
        RowReader& StrOpt(const char* key, const char*& out)
        {
            const Json* v = Get(key);
            if (!v) return *this;
            if (v->is_null()) { out = nullptr; return *this; }
            if (!v->is_string()) return Fail(key, "문자열 또는 null이 아니다");
            const std::string_view s = v->get_ref<const TempString&>();
            if (s.empty()) return Fail(key, "빈 글자 — 비우려면 null");
            out = m_ctx.Strings().Add(s);
            return *this;
        }

        // 고정 길이 char 배열(널 포함 N) — 넘으면 오류(자르지 않는다)
        template <size_t N>
        RowReader& Chars(const char* key, char (&out)[N])
        {
            const Json* v = Get(key);
            if (!v) return *this;
            if (!v->is_string()) return Fail(key, "문자열이 아니다");
            const std::string_view s = v->get_ref<const TempString&>();
            if (s.size() + 1 > N) return Fail(key, std::to_string(s.size()) + "바이트 > " + std::to_string(N - 1));
            std::memset(out, 0, N);
            std::memcpy(out, s.data(), s.size());
            return *this;
        }

        template <class E>
        RowReader& Enum(const char* key, E& out)
        {
            const Json* v = Get(key);
            if (!v) return *this;
            if (!v->is_string()) return Fail(key, "열거형 이름 글자가 아니다");
            const std::string_view s = v->get_ref<const TempString&>();
            using Info = GameDataEnums::Info<E>;
            for (const auto& it : Info::kItems)
                if (it.name == s) { out = static_cast<E>(it.value); return *this; }
            return Fail(key, "열거형 " + std::string(Info::kName) + "에 없는 이름 '" + std::string(s) + "'");
        }

        // flags: 열거자 값이 비트(HitFlag) — "A|B", "" = 0
        template <class E, class T>
        RowReader& Flags(const char* key, T& out)
        {
            uint32_t bits = 0;
            if (!Names<E>(key, false, bits)) return *this;
            if (bits > std::numeric_limits<T>::max()) return Fail(key, "플래그가 형 범위를 넘는다");
            out = static_cast<T>(bits);
            return *this;
        }

        // mask: 열거자 값 = 비트 번호(ClassId) — "Knight|NightHunter", "" = 0
        template <class E, class T>
        RowReader& Mask(const char* key, T& out)
        {
            uint32_t bits = 0;
            if (!Names<E>(key, true, bits)) return *this;
            if (bits > std::numeric_limits<T>::max()) return Fail(key, "마스크가 형 범위를 넘는다");
            out = static_cast<T>(bits);
            return *this;
        }

        // 행에 정본에 없는 열이 있으면 오류
        void Finish()
        {
            if (m_bad || !m_row.is_object()) return;
            for (auto it = m_row.begin(); it != m_row.end(); ++it)
                if (!Seen(it.key().c_str()))
                    m_ctx.Error(m_ctx.Where(m_table, m_index, it.key().c_str()), "모르는 열");
        }

        std::string Where(const char* key = nullptr) const { return m_ctx.Where(m_table, m_index, key); }

    private:
        const Json* Get(const char* key)
        {
            if (m_bad) return nullptr;
            if (!Seen(key) && m_seenCount < m_seen.size())
                m_seen[m_seenCount++] = KeyHash(key);
            auto it = m_row.find(key);
            if (it == m_row.end())
            {
                m_ctx.Error(m_ctx.Where(m_table, m_index, key), "열이 없다");
                return nullptr;
            }
            return &*it;
        }

        RowReader& Fail(const char* key, const std::string& what)
        {
            m_ctx.Error(m_ctx.Where(m_table, m_index, key), what);
            return *this;
        }

        template <class E>
        bool Names(const char* key, bool bitIndex, uint32_t& bits)
        {
            const Json* v = Get(key);
            if (!v) return false;
            if (!v->is_string()) { Fail(key, "이름을 '|'로 이은 글자가 아니다"); return false; }
            const std::string_view s = v->get_ref<const TempString&>();
            using Info = GameDataEnums::Info<E>;
            size_t start = 0;
            while (start < s.size())
            {
                size_t end = s.find('|', start);
                if (end == std::string_view::npos) end = s.size();
                const std::string_view name(s.data() + start, end - start);
                bool found = false;
                for (const auto& it : Info::kItems)
                    if (it.name == name)
                    {
                        bits |= bitIndex ? (1u << it.value) : static_cast<uint32_t>(it.value);
                        found = true;
                        break;
                    }
                if (!found) { Fail(key, std::string(Info::kName) + "에 없는 이름 '" + std::string(name) + "'"); return false; }
                start = end + 1;
            }
            return true;
        }

        LoadContext& m_ctx;
        const char* m_table;
        size_t m_index;
        const Json& m_row;
        bool m_bad = false;
        // 읽은 열 이름의 해시(FNV-1a 64) — 힙을 쓰지 않는다(핫 리로드가 로직 스레드와 프로세스 힙을 덜 다투게).
        // Bind 코드가 열 이름을 그때그때 만들어 넘기기도 해서(예: "base_" + 능력치) 포인터가 아니라 해시를 적는다. 표의 열은 32개 이하.
        std::array<uint64_t, 64> m_seen{};
        size_t m_seenCount = 0;

        static uint64_t KeyHash(const char* k) noexcept
        {
            uint64_t h = 1469598103934665603ull;
            for (; *k; ++k) { h ^= static_cast<uint8_t>(*k); h *= 1099511628211ull; }
            return h;
        }

        bool Seen(const char* key) const noexcept
        {
            const uint64_t h = KeyHash(key);
            for (size_t i = 0; i < m_seenCount; ++i)
                if (m_seen[i] == h) return true;
            return false;
        }
    };
}
