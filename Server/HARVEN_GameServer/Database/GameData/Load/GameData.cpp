#include "pch.h"
// ─────────────────────────────────────────────────────────────────────────────
// 기획 데이터 로더 — 읽기(Bind) · 검증(Validate) · 올리기(publish) · 덤프(--dump-data).
// 묶음 순서 · 등록은 kBundles 하나(메인만 고친다). 묶음별 코드는 Bind_<묶음>.cpp.
// ─────────────────────────────────────────────────────────────────────────────
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>

#include "Database/GameData/Load/Bundles.h"
#include "Database/GameData/Load/GameData.h"
#include "Database/GameData/Load/JsonRead.h"
#include "Database/GameData/Load/Sha256.h"
#include "Database/GameData/Load/Snapshot.h"
#include "Database/GameData/Load/Validate.h"

namespace
{
    using namespace GameDataLoad;

    struct BundleOps
    {
        const char* name;
        void (*bind)(LoadContext&, GameDataSnapshot&);
        void (*validate)(ValidateContext&, const GameDataSnapshot&);
        void (*dump)(Canon::Book&, const GameDataSnapshot&);
    };

    // 참조되는 쪽 먼저(Bundles.h 머리말)
    constexpr BundleOps kBundles[] =
    {
        { "town",    &BindTown,    &ValidateTown,    &DumpTown },
        { "region",  &BindRegion,  &ValidateRegion,  &DumpRegion },
        { "monster", &BindMonster, &ValidateMonster, &DumpMonster },
        { "monsterai", &BindMonsterAI, &ValidateMonsterAI, &DumpMonsterAI },
        { "item",    &BindItem,    &ValidateItem,    &DumpItem },
        { "econ",    &BindEcon,    &ValidateEcon,    &DumpEcon },
        { "level",   &BindLevel,   &ValidateLevel,   &DumpLevel },
        { "skill",   &BindSkill,   &ValidateSkill,   &DumpSkill },
        { "growth",  &BindGrowth,  &ValidateGrowth,  &DumpGrowth },
        { "npc",     &BindNpc,     &ValidateNpc,     &DumpNpc },
        { "quest",   &BindQuest,   &ValidateQuest,   &DumpQuest },
        { "party",   &BindParty,   &ValidateParty,   &DumpParty },
        { "guild",   &BindGuild,   &ValidateGuild,   &DumpGuild },
        { "achieve", &BindAchieve, &ValidateAchieve, &DumpAchieve },
    };

    std::atomic<const GameDataSnapshot*> g_current{ nullptr };
    // 이 스레드가 보는 판: 검증 · 덤프 중인 새 판(ScopedSnapshot) 또는 로직 스레드가 이번 묶음(틱)에 고정한 판(PinThread).
    // 비었으면 지금 판(g_current). GD()는 TLS 하나 + (비었을 때만) 원자 읽기 하나 — 이관 전 constexpr 표 대신 드는 값.
    thread_local const GameDataSnapshot* t_view = nullptr;

    // 올린 세대는 해제하지 않는다 — 옛 세대를 가리키는 포인터(EquippedPiece.row 등)가 남아 있을 수 있다(Phase 4 핫 리로드).
    std::mutex g_generationsLock;
    std::vector<std::unique_ptr<GameDataSnapshot>> g_generations;

    // manifest.json — {"format": 1, "version": "...", "tables": {"Monster": {"rows": 35, "sha256": "..."}}}
    // 표 파일이 manifest와 다르면(손으로 고침 · export 빠뜨림) 오류. manifest가 없으면 "unversioned"(개발 중 허용).
    std::string CheckManifest(LoadContext& ctx)
    {
        const TempString* text = ctx.Text("manifest.json");
        if (text == nullptr)
            return "unversioned";
        const Json m = Json::parse(*text, nullptr, false);
        if (m.is_discarded() || !m.is_object() || !m.contains("tables") || !m["tables"].is_object() || !m.contains("version"))
        {
            ctx.Error("manifest.json", "형식이 아니다");
            return "invalid";
        }
        Sha256 all;
        for (auto it = m["tables"].begin(); it != m["tables"].end(); ++it)   // nlohmann::json 객체는 키 순서(정렬)로 돈다
        {
            const std::string name = ToStd(it.key());
            const TempString* body = ctx.Text(name + ".json");   // 여기서 한 번 읽은 것을 Bind가 그대로 쓴다
            if (body == nullptr)
            {
                ctx.Error(name + ".json", "manifest에 있는 표 파일이 없다");
                continue;
            }
            const std::string h = Sha256::Of(std::string_view(body->data(), body->size()));
            const auto sha = it.value().find("sha256");
            const std::string want = sha != it.value().end() && sha->is_string() ? ToStd(sha->get_ref<const TempString&>()) : std::string();
            if (h != want)
                ctx.Error(name + ".json", "manifest와 내용이 다르다 — 시트에서 datatool sync(또는 export)로 다시 만들 것");
            all.Update(name + "\n" + h + "\n");
            if (ctx.Gentle())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const std::string version = all.HexDigest().substr(0, 16);
        const std::string want = m["version"].is_string() ? ToStd(m["version"].get_ref<const TempString&>()) : std::string();
        if (version != want)
            ctx.Error("manifest.json", "판 해시가 다르다(" + want + " ≠ " + version + ")");
        return version;
    }

    GameData::LoadResult Build(const std::filesystem::path& dir, std::unique_ptr<GameDataSnapshot>& out, bool gentle = false,
                               FileCache* cache = nullptr)
    {
        using Clock = std::chrono::steady_clock;
        auto ms = [](Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
        const auto t0 = Clock::now();
        GameData::LoadResult r;
        auto snap = std::make_unique<GameDataSnapshot>();
        Temp::Scope temp;   // JSON 트리 · 파일 원문의 일시 메모리 범위(ctx보다 오래 산다 — ctx가 먼저 부서진다)
        LoadContext ctx(dir, snap->strings, r.errors);
        ctx.SetGentle(gentle);
        ctx.UseCache(cache);
        snap->version = CheckManifest(ctx);   // 표 파일을 모두 읽는다(한 번)
        const auto t1 = Clock::now();
        for (const auto& b : kBundles)
        {
            b.bind(ctx, *snap);
            ctx.ReleaseDocs();
            if (gentle)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const auto t2 = Clock::now();
        if (r.errors.empty())
        {
            // 검증 중 파사드(MonsterTable::Find …)가 새 판을 읽게 한다
            ScopedSnapshot scope(snap.get());
            ValidateContext v(ctx, r.errors);
            for (const auto& b : kBundles)
            {
                b.validate(v, *snap);
                if (gentle)
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        const auto t3 = Clock::now();
        snap->shape = ctx.Shapes();
        r.version = snap->version;
        r.stringBytes = snap->strings.Bytes();
        ctx.Stats(r.tables, r.rows);
        r.ok = r.errors.empty();
        r.readMs = ms(t0, t1);
        r.bindMs = ms(t1, t2);
        r.validateMs = ms(t2, t3);
        r.ms = ms(t0, Clock::now());
        out = std::move(snap);
        return r;
    }

    // 핫 리로드 · 감시(--dev)
    std::mutex g_reloadLock;
    FileCache g_fileCache;   // g_reloadLock 아래서만
    std::atomic<bool> g_dev{ false };
    std::mutex g_requestLock;
    std::vector<std::function<void(const GameData::ReloadResult&)>> g_requests;
    std::atomic<bool> g_requested{ false };
}

namespace GameDataLoad
{
    void SetThreadSnapshot(const GameDataSnapshot* snap) noexcept { t_view = snap; }
    const GameDataSnapshot* ThreadSnapshot() noexcept { return t_view; }
}

const GameDataSnapshot& GD() noexcept
{
    const GameDataSnapshot* s = t_view != nullptr ? t_view : g_current.load(std::memory_order_acquire);
    if (s == nullptr)
    {
        std::fputs("[GameData] 기획 데이터를 읽기 전에 GD()를 불렀다 — main에서 GameData::LoadFromDir를 스레드보다 먼저 부를 것\n", stderr);
        std::fflush(stderr);
        std::abort();
    }
    return *s;
}

namespace GameData
{
    LoadResult LoadFromDir(const std::filesystem::path& dir)
    {
        std::unique_ptr<GameDataSnapshot> snap;
        LoadResult r = Build(dir, snap);
        if (r.ok)
        {
            std::scoped_lock lock(g_generationsLock);
            g_current.store(snap.get(), std::memory_order_release);
            g_generations.push_back(std::move(snap));
        }
        return r;
    }

    LoadResult CheckDir(const std::filesystem::path& dir)
    {
        std::unique_ptr<GameDataSnapshot> snap;
        return Build(dir, snap);
    }

    bool DumpToDir(const std::filesystem::path& dir, std::string& error)
    {
        const GameDataSnapshot& snap = GD();
        ScopedSnapshot scope(&snap);
        Canon::Book book;
        try
        {
            for (const auto& b : kBundles)
                b.dump(book, snap);
        }
        catch (const std::exception& e)
        {
            error = e.what();
            return false;
        }
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        auto write = [&](const std::string& name, const std::string& text) -> bool
        {
            std::ofstream f(dir / name, std::ios::binary);
            f.write(text.data(), static_cast<std::streamsize>(text.size()));
            if (!f) { error = "쓰기 실패: " + (dir / name).string(); return false; }
            return true;
        };
        for (const auto& [name, table] : book.Tables())
            if (!write(name + ".json", table.Json()))
                return false;
        return write("_schema.json", book.SchemaJson());
    }

    std::filesystem::path ResolveDir(const std::string& overrideDir, const char* argv0)
    {
        namespace fs = std::filesystem;
        if (!overrideDir.empty())
            return fs::path(overrideDir);
        std::error_code ec;
        std::vector<fs::path> starts;
        if (argv0 != nullptr && *argv0 != '\0')
            starts.push_back(fs::absolute(fs::path(argv0), ec).parent_path());
        starts.push_back(fs::current_path(ec));
        for (const auto& start : starts)
        {
            fs::path p = start;
            for (int up = 0; up < 6 && !p.empty(); ++up)
            {
                if (fs::exists(p / "GameData" / "manifest.json", ec) || fs::exists(p / "GameData" / "Monster.json", ec))
                    return p / "GameData";
                if (fs::exists(p / "Bin" / "GameData" / "manifest.json", ec))
                    return p / "Bin" / "GameData";
                const fs::path parent = p.parent_path();
                if (parent == p) break;
                p = parent;
            }
        }
        return starts.front() / "GameData";
    }

    bool Loaded() noexcept { return g_current.load(std::memory_order_acquire) != nullptr; }

    const std::string& Version() noexcept
    {
        static const std::string kNone = "none";
        const GameDataSnapshot* s = g_current.load(std::memory_order_acquire);
        return s != nullptr ? s->version : kNone;
    }

    std::string Describe(const LoadResult& r, size_t maxErrors)
    {
        std::ostringstream o;
        char ms[32];
        std::snprintf(ms, sizeof(ms), "%.1f", r.ms);
        if (r.ok)
        {
            char parts[96];
            std::snprintf(parts, sizeof(parts), "읽기 %.1f · 묶기 %.1f · 검증 %.1f ms", r.readMs, r.bindMs, r.validateMs);
            o << "[GameData] loaded " << r.rows << " rows / " << r.tables << " tables in " << ms << " ms (version " << r.version
              << ", strings " << r.stringBytes << " B, " << parts << ")";
            return o.str();
        }
        o << "[GameData] 오류 " << r.errors.size() << "개 — 서버를 띄우지 않는다(" << ms << " ms)\n";
        size_t n = 0;
        for (const auto& e : r.errors)
        {
            if (n++ >= maxErrors) { o << "  … " << (r.errors.size() - maxErrors) << "개 더\n"; break; }
            o << "  " << e.where << ": " << e.what;
            if (!e.rule.empty()) o << " [" << e.rule << "]";
            o << "\n";
        }
        return o.str();
    }

    // ── 핫 리로드(--dev) ─────────────────────────────────────────────────────────
    ReloadResult Reload(const std::filesystem::path& dir)
    {
        std::scoped_lock reloadLock(g_reloadLock);   // 한 번에 하나
        ReloadResult rr;
        std::unique_ptr<GameDataSnapshot> snap;
        const size_t reads0 = g_fileCache.reads;
        rr.load = Build(dir, snap, /*gentle*/ true, &g_fileCache);
        rr.filesOpened = g_fileCache.reads - reads0;
        const GameDataSnapshot* cur = g_current.load(std::memory_order_acquire);
        rr.from = cur != nullptr ? cur->version : "none";
        if (!rr.load.ok)
        {
            rr.reason = "검증 실패 — 지금 판을 그대로 쓴다";
            return rr;
        }
        if (cur != nullptr && snap->version == cur->version && snap->version != "unversioned")
        {
            rr.reason = "같은 판";
            return rr;
        }
        if (cur != nullptr && snap->shape != cur->shape)
        {
            std::string what;
            const auto& a = cur->shape;
            const auto& b = snap->shape;
            for (size_t i = 0, j = 0; i < a.size() || j < b.size();)
            {
                if (j >= b.size() || (i < a.size() && a[i].table < b[j].table)) { what = a[i].table + " 표가 없어졌다"; break; }
                if (i >= a.size() || b[j].table < a[i].table) { what = b[j].table + " 표가 새로 생겼다"; break; }
                if (a[i].rows != b[j].rows) { what = a[i].table + " 행 수 " + std::to_string(a[i].rows) + " → " + std::to_string(b[j].rows); break; }
                if (a[i].keyHash != b[j].keyHash) { what = a[i].table + " 키(id) 열이 바뀌었다"; break; }
                ++i; ++j;
            }
            rr.reason = "구조가 바뀌었다(" + what + ") — 핫 리로드는 값만 바뀐 판을 받는다. 서버를 다시 시작할 것";
            return rr;
        }
        std::scoped_lock lock(g_generationsLock);
        g_current.store(snap.get(), std::memory_order_release);
        g_generations.push_back(std::move(snap));
        rr.published = true;
        rr.generation = g_generations.size();
        return rr;
    }

    std::string DescribeReload(const ReloadResult& r)
    {
        char ms[32];
        std::snprintf(ms, sizeof(ms), "%.1f", r.load.ms);
        if (r.published)
            return "[GameData] reloaded version " + r.from + " → " + r.load.version + " in " + ms + " ms (세대 " + std::to_string(r.generation) +
                ", 연 파일 " + std::to_string(r.filesOpened) + ", 다음 로직 틱부터)";
        std::string s = "[GameData] reload 거부 — " + r.reason + " (지금 판 " + r.from + ", " + ms + " ms)";
        size_t n = 0;
        for (const auto& e : r.load.errors)   // 틀린 곳(최대 20) — 서버는 지금 판으로 계속 돈다
        {
            if (n++ >= 20) { s += "\n  … " + std::to_string(r.load.errors.size() - 20) + "개 더"; break; }
            s += "\n  " + e.where + ": " + e.what + (e.rule.empty() ? "" : " [" + e.rule + "]");
        }
        return s;
    }

    size_t Generations() noexcept
    {
        std::scoped_lock lock(g_generationsLock);
        return g_generations.size();
    }

    void PinThread() noexcept { t_view = g_current.load(std::memory_order_acquire); }

    bool DevMode() noexcept { return g_dev.load(std::memory_order_relaxed); }

    void RequestReload(std::function<void(const ReloadResult&)> done)
    {
        {
            std::scoped_lock lock(g_requestLock);
            if (done) g_requests.push_back(std::move(done));
        }
        g_requested.store(true, std::memory_order_release);
    }

    void StartDevWatch(const std::filesystem::path& dir)
    {
        bool expected = false;
        if (!g_dev.compare_exchange_strong(expected, true))
            return;
        {
            // 스레드를 띄우기 전에(부팅 중) 파일 원문을 한 번 기억해 둔다 — 첫 리로드부터 바뀐 파일만 연다
            std::scoped_lock reloadLock(g_reloadLock);
            std::error_code ec;
            for (const auto& e : std::filesystem::directory_iterator(dir, ec))
            {
                if (!e.is_regular_file() || e.path().extension() != ".json") continue;
                std::error_code e1, e2;
                FileCache::Entry entry;
                entry.size = std::filesystem::file_size(e.path(), e1);
                entry.mtime = static_cast<long long>(std::filesystem::last_write_time(e.path(), e2).time_since_epoch().count());
                std::ifstream f(e.path(), std::ios::binary);
                if (e1 || e2 || !f) continue;
                std::ostringstream ss;
                ss << f.rdbuf();
                entry.text = ss.str();
                for (size_t pos; (pos = entry.text.find("\r\n")) != std::string::npos;)   // LoadContext::ReadFile과 같게 — CRLF로 받아도 LF로
                    entry.text.erase(pos, 1);
                g_fileCache.files[e.path().filename().string()] = std::move(entry);
            }
        }
        std::thread([dir]()
        {
            // 우선순위는 보통 그대로 — 낮추면 이 스레드가 프로세스 힙 잠금을 쥔 채 오래 밀려 로직 스레드가 함께 멈출 수 있다(우선순위 역전).
            // 대신 읽기 · 묶기 · 검증을 잘게 나눠 사이사이 쉰다(Build gentle).
            namespace fs = std::filesystem;
            auto stamp = [&dir]() -> std::string
            {
                std::error_code ec;
                const auto p = dir / "manifest.json";
                const auto t = fs::last_write_time(p, ec);
                if (ec) return {};
                const auto n = fs::file_size(p, ec);
                return std::to_string(t.time_since_epoch().count()) + "/" + std::to_string(ec ? 0 : n);
            };
            std::string last = stamp();
            std::printf("[GameData] --dev: %s/manifest.json을 지켜본다(바뀌면 다시 읽기 — 값만 바뀐 판)\n", dir.string().c_str());
            std::fflush(stdout);
            for (;;)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
                const bool asked = g_requested.exchange(false, std::memory_order_acq_rel);
                std::string now = stamp();
                if (!asked && (now.empty() || now == last))
                    continue;
                if (!asked)
                {
                    // datatool export는 표 파일을 다 쓴 뒤 manifest를 마지막에 쓴다 — 잠깐 기다려 쓰기가 끝나게
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                    now = stamp();
                }
                last = now;
                const ReloadResult r = Reload(dir);
                std::printf("%s\n", DescribeReload(r).c_str());
                std::fflush(stdout);
                std::vector<std::function<void(const ReloadResult&)>> waiting;
                {
                    std::scoped_lock lock(g_requestLock);
                    waiting.swap(g_requests);
                }
                for (auto& f : waiting)
                    f(r);
            }
        }).detach();
    }
}
