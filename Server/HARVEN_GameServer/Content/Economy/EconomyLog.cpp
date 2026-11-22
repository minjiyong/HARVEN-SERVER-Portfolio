#include "pch.h"
#include "Content/Economy/EconomyLog.h"

#include <atomic>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <sstream>

#include "Database/GameData/GuildGrowth.h"   // PeriodId(하루 번호 — 결사 성장과 같은 방식)

namespace
{
    // 사유 수 — AdenaReason 끝(Achievement)까지. AdenaReason에 값을 더하면 여기와 kReasonNames도 늘린다(범위 밖 사유는 세지 않는다).
    constexpr size_t kReasonCount = static_cast<size_t>(AdenaReason::Achievement) + 1;
    constexpr const char* kReasonNames[kReasonCount] = {
        "Pickup", "ShopBuy", "ShopSell", "Teleport", "Init", "Service", "Quest",
        "GuildCreate", "AuctionDeposit", "AuctionBuy", "AuctionClaim", "Refund", "Craft", "DirectTrade", "Achievement" };
    constexpr size_t kGradeCount = static_cast<size_t>(kItemGradeCount);
    constexpr const char* kGradeNames[kGradeCount] = { "eNone", "eRare", "eEpic", "eUnique", "eLegendary", "eMythic" };
    constexpr size_t kGearSources = static_cast<size_t>(GearSource::Count);
    constexpr const char* kGearSourceNames[kGearSources] = { "Field", "Elite", "Boss", "Craft", "Quest", "Shop", "Achievement" };
    constexpr size_t kGearSinks = static_cast<size_t>(GearSink::Count);
    constexpr const char* kGearSinkNames[kGearSinks] = { "Dismantle", "ShopSell", "EnchantBreak" };
    constexpr size_t kMatSources = static_cast<size_t>(MaterialSource::Count);
    constexpr const char* kMatSourceNames[kMatSources] = { "Kill", "Elite", "Boss", "Dismantle", "Achievement" };
    constexpr size_t kMatSinks = static_cast<size_t>(MaterialSink::Count);
    constexpr const char* kMatSinkNames[kMatSinks] = { "Craft", "ShopSell" };
    // 재료는 아이템 id로 바로 칸을 고른다(해시맵 없음). 이 위의 id는 0번 칸("other")에 모은다.
    constexpr size_t kMaxItemId = 1024;
    static_assert(kItemGradeCount == 6, "등급을 더하면 kGradeNames도 늘린다");

    using Counter = std::atomic<uint64_t>;
    struct Counters
    {
        Counter adenaIn[kReasonCount]{}, adenaOut[kReasonCount]{};
        Counter gearIn[kGearSources][kGradeCount]{};
        Counter gearOut[kGearSinks][kGradeCount]{};
        Counter matIn[kMatSources][kMaxItemId]{};
        Counter matOut[kMatSinks][kMaxItemId]{};
        Counter craftFee{};
    };
    Counters g_c;

    constexpr uint32_t kDayPeriodSec = 86400;
    constexpr uint32_t kDayOffsetSec = 75600;   // 06:00 KST(= 21:00 UTC) — GuildConst.day_offset_sec 기본값과 같다
    int64_t g_period = -1;                      // Tick(svc-tick 스레드 하나)만 만진다

    uint32_t PeriodSec()
    {
        static const uint32_t sec = []
        {
            const char* v = std::getenv("HARVEN_ECONOMY_PERIOD_SEC");
            const long n = v != nullptr ? std::strtol(v, nullptr, 10) : 0;
            return n > 0 ? static_cast<uint32_t>(n) : kDayPeriodSec;
        }();
        return sec;
    }

    size_t MatIndex(ItemTableId item) noexcept { return item < kMaxItemId ? item : 0; }

    uint64_t Take(Counter& c, bool reset) noexcept
    {
        return reset ? c.exchange(0, std::memory_order_relaxed) : c.load(std::memory_order_relaxed);
    }

    // "이름":값,… — 0인 칸은 뺀다. 하나라도 썼으면 true
    template <size_t N>
    bool WriteNamed(std::ostringstream& o, Counter (&arr)[N], const char* const (&names)[N], bool reset, uint64_t* sum = nullptr)
    {
        bool any = false;
        for (size_t i = 0; i < N; ++i)
        {
            const uint64_t v = Take(arr[i], reset);
            if (v == 0) continue;
            o << (any ? "," : "") << '"' << names[i] << "\":" << v;
            any = true;
            if (sum != nullptr) *sum += v;
        }
        return any;
    }

    // {"출처":{"등급":n,…},…}
    template <size_t S, size_t G>
    void WriteGear(std::ostringstream& o, Counter (&arr)[S][G], const char* const (&names)[S], bool reset, uint64_t& sum)
    {
        o << '{';
        bool any = false;
        for (size_t s = 0; s < S; ++s)
        {
            std::ostringstream inner;
            if (!WriteNamed(inner, arr[s], kGradeNames, reset, &sum)) continue;
            o << (any ? "," : "") << '"' << names[s] << "\":{" << inner.str() << '}';
            any = true;
        }
        o << '}';
    }

    // {"출처":{"아이템 id":n,…},…} — id 0 = kMaxItemId 이상 모음("other")
    template <size_t S>
    void WriteMaterial(std::ostringstream& o, Counter (&arr)[S][kMaxItemId], const char* const (&names)[S], bool reset, uint64_t& sum)
    {
        o << '{';
        bool anySrc = false;
        for (size_t s = 0; s < S; ++s)
        {
            std::ostringstream inner;
            bool any = false;
            for (size_t id = 0; id < kMaxItemId; ++id)
            {
                const uint64_t v = Take(arr[s][id], reset);
                if (v == 0) continue;
                inner << (any ? "," : "") << '"';
                if (id == 0) inner << "other"; else inner << id;
                inner << "\":" << v;
                any = true;
                sum += v;
            }
            if (!any) continue;
            o << (anySrc ? "," : "") << '"' << names[s] << "\":{" << inner.str() << '}';
            anySrc = true;
        }
        o << '}';
    }

    struct Totals { uint64_t adenaIn = 0, adenaOut = 0, gearIn = 0, gearOut = 0, matIn = 0, matOut = 0, fee = 0; };

    // 한 줄 JSON. period ≥ 0이면 그 하루의 머리(번호 · 시작 시각 · KST 날짜)를 붙인다
    std::string BuildJson(bool reset, int64_t period, Totals& t)
    {
        std::ostringstream o;
        o << '{';
        if (period >= 0)
        {
            const int64_t start = period * static_cast<int64_t>(PeriodSec()) + kDayOffsetSec;
            const std::time_t kst = static_cast<std::time_t>(start + 9 * 3600);
            std::tm tm{};
            gmtime_r(&kst, &tm);
            char day[32] = {};
            std::strftime(day, sizeof(day), "%Y-%m-%d", &tm);
            o << "\"period\":" << period << ",\"start\":" << start << ",\"day\":\"" << day << "\",";
        }
        o << "\"adena\":{\"in\":{";
        WriteNamed(o, g_c.adenaIn, kReasonNames, reset, &t.adenaIn);
        o << "},\"out\":{";
        WriteNamed(o, g_c.adenaOut, kReasonNames, reset, &t.adenaOut);
        o << "}},\"gear\":{\"in\":";
        WriteGear(o, g_c.gearIn, kGearSourceNames, reset, t.gearIn);
        o << ",\"out\":";
        WriteGear(o, g_c.gearOut, kGearSinkNames, reset, t.gearOut);
        o << "},\"material\":{\"in\":";
        WriteMaterial(o, g_c.matIn, kMatSourceNames, reset, t.matIn);
        o << ",\"out\":";
        WriteMaterial(o, g_c.matOut, kMatSinkNames, reset, t.matOut);
        t.fee = Take(g_c.craftFee, reset);
        o << "},\"craftFee\":" << t.fee << '}';
        return o.str();
    }
}

void CEconomyLog::OnAdena(AdenaReason reason, int64_t delta) noexcept
{
    const size_t r = static_cast<size_t>(reason);
    if (r >= kReasonCount || delta == 0)
        return;
    if (delta > 0)
        g_c.adenaIn[r].fetch_add(static_cast<uint64_t>(delta), std::memory_order_relaxed);
    else
        g_c.adenaOut[r].fetch_add(static_cast<uint64_t>(-delta), std::memory_order_relaxed);
}

void CEconomyLog::OnGearIn(GearSource source, ItemGrade grade, uint32_t count) noexcept
{
    const size_t s = static_cast<size_t>(source), g = static_cast<size_t>(grade);
    if (s < kGearSources && g < kGradeCount && count != 0)
        g_c.gearIn[s][g].fetch_add(count, std::memory_order_relaxed);
}

void CEconomyLog::OnGearOut(GearSink sink, ItemGrade grade, uint32_t count) noexcept
{
    const size_t s = static_cast<size_t>(sink), g = static_cast<size_t>(grade);
    if (s < kGearSinks && g < kGradeCount && count != 0)
        g_c.gearOut[s][g].fetch_add(count, std::memory_order_relaxed);
}

void CEconomyLog::OnMaterialIn(MaterialSource source, ItemTableId item, uint32_t count) noexcept
{
    const size_t s = static_cast<size_t>(source);
    if (s < kMatSources && item != ITEM_TABLE_ID_NONE && count != 0)
        g_c.matIn[s][MatIndex(item)].fetch_add(count, std::memory_order_relaxed);
}

void CEconomyLog::OnMaterialOut(MaterialSink sink, ItemTableId item, uint32_t count) noexcept
{
    const size_t s = static_cast<size_t>(sink);
    if (s < kMatSinks && item != ITEM_TABLE_ID_NONE && count != 0)
        g_c.matOut[s][MatIndex(item)].fetch_add(count, std::memory_order_relaxed);
}

void CEconomyLog::OnCraftFee(uint32_t fee) noexcept
{
    if (fee != 0)
        g_c.craftFee.fetch_add(fee, std::memory_order_relaxed);
}

std::string CEconomyLog::SnapshotJson()
{
    Totals t;
    return BuildJson(false, -1, t);
}

void CEconomyLog::Tick(int64_t unixNow)
{
    const int64_t period = GuildGrowth::PeriodId(unixNow, PeriodSec(), kDayOffsetSec);
    if (g_period < 0)
    {
        g_period = period;   // 켠 뒤 첫 틱 — 지금 하루부터 센다
        return;
    }
    if (period == g_period)
        return;
    Totals t;
    const std::string line = BuildJson(true, g_period, t);
    g_period = period;

    const char* env = std::getenv("HARVEN_ECONOMY_LOG");
    const std::string path = env != nullptr && *env != '\0' ? env : "economy.log";
    std::ofstream out(path, std::ios::app);
    if (out)
        out << line << '\n';
    std::cout << "[Economy] adena in=" << t.adenaIn << " out=" << t.adenaOut
              << " net=" << (static_cast<int64_t>(t.adenaIn) - static_cast<int64_t>(t.adenaOut))
              << " | gear in=" << t.gearIn << " out=" << t.gearOut << " | material in=" << t.matIn << " out=" << t.matOut
              << " | craftFee=" << t.fee << " → " << path << (out ? "" : " (쓰기 실패)") << "\n" << std::flush;
}
