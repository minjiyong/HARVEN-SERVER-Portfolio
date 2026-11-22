// 순수 논리 — pch.h 없이도 빌드된다(시험 AchieveRuleTests가 이 파일을 함께 컴파일한다).
#include "Content/Achieve/AchieveBook.h"

#include <algorithm>
#include <bit>
#include <limits>

namespace
{
    void Put16(std::vector<uint8_t>& out, uint16_t v)
    {
        out.push_back(static_cast<uint8_t>(v & 0xFF));
        out.push_back(static_cast<uint8_t>(v >> 8));
    }
    void Put32(std::vector<uint8_t>& out, uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            out.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
    }
    uint16_t Get16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
    uint32_t Get32(const uint8_t* p)
    {
        return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
    }

    template <size_t N>
    void PutBits(std::vector<uint8_t>& out, const std::bitset<N>& bits)
    {
        size_t len = 0;
        for (size_t i = 0; i < N; ++i)
            if (bits.test(i))
                len = i / 8 + 1;
        out.push_back(static_cast<uint8_t>(len));
        for (size_t b = 0; b < len; ++b)
        {
            uint8_t v = 0;
            for (int k = 0; k < 8; ++k)
                if (bits.test(b * 8 + static_cast<size_t>(k)))
                    v = static_cast<uint8_t>(v | (1u << k));
            out.push_back(v);
        }
    }

    template <size_t N>
    bool GetBits(const uint8_t* data, size_t size, size_t& at, std::bitset<N>& bits)
    {
        if (at + 1 > size)
            return false;
        const size_t len = data[at++];
        if (len > N / 8 || at + len > size)
            return false;
        for (size_t b = 0; b < len; ++b)
            for (int k = 0; k < 8; ++k)
                if (data[at + b] & (1u << k))
                    bits.set(b * 8 + static_cast<size_t>(k));
        at += len;
        return true;
    }
}

AchieveBook::Stat* AchieveBook::Find(StatKey key) noexcept
{
    const auto it = std::lower_bound(m_Stats.begin(), m_Stats.end(), key, [](const Stat& s, StatKey k) { return s.key < k; });
    return it != m_Stats.end() && it->key == key ? &*it : nullptr;
}

uint32_t AchieveBook::Value(StatKey key) const noexcept
{
    if (key.kind == AchKind::eZone)
        return static_cast<uint32_t>(std::popcount(m_Regions));
    const auto it = std::lower_bound(m_Stats.begin(), m_Stats.end(), key, [](const Stat& s, StatKey k) { return s.key < k; });
    return it != m_Stats.end() && it->key == key ? it->value : 0;
}

void AchieveBook::Check(const Catalog& cat, int keyIndex, uint32_t value, DoneList& done)
{
    for (const uint16_t at : cat.RowsOfKey(keyIndex))
    {
        const AchieveRule::Row& row = cat.rows[at];
        if (row.target > value)
            break;   // target 오름차순
        if (row.id > AchieveRule::kMaxId || m_Done.test(row.id))
            continue;
        m_Done.set(row.id);
        done.push_back(row.id);
    }
}

void AchieveBook::Add(const Catalog& cat, StatKey key, uint32_t delta, DoneList& done)
{
    if (Frozen())
        return;   // 깨진 기록 — 이번 접속은 읽기 전용
    if (delta == 0 || key.kind == AchKind::eZone)
        return;
    const int k = cat.KeyIndex(key);
    if (k < 0)
        return;   // 표가 세지 않는 키
    Stat* s = Find(key);
    if (s == nullptr)
    {
        const auto it = std::lower_bound(m_Stats.begin(), m_Stats.end(), key, [](const Stat& x, StatKey y) { return x.key < y; });
        s = &*m_Stats.insert(it, Stat{ key, 0 });
    }
    const uint32_t room = std::numeric_limits<uint32_t>::max() - s->value;
    s->value += std::min(delta, room);
    dirty = true;
    Check(cat, k, s->value, done);
}

void AchieveBook::Raise(const Catalog& cat, StatKey key, uint32_t value, DoneList& done)
{
    if (Frozen())
        return;   // 깨진 기록 — 이번 접속은 읽기 전용
    if (key.kind == AchKind::eZone)
        return;
    const int k = cat.KeyIndex(key);
    if (k < 0)
        return;
    Stat* s = Find(key);
    if (s != nullptr && s->value >= value)
        return;
    if (s == nullptr)
    {
        if (value == 0)
            return;
        const auto it = std::lower_bound(m_Stats.begin(), m_Stats.end(), key, [](const Stat& x, StatKey y) { return x.key < y; });
        s = &*m_Stats.insert(it, Stat{ key, 0 });
    }
    s->value = value;
    dirty = true;
    Check(cat, k, s->value, done);
}

bool AchieveBook::EnterRegion(const Catalog& cat, int region, DoneList& done)
{
    if (Frozen())
        return false;
    if (region < 0 || region >= AchieveRule::kRegionBits)
        return false;
    const uint16_t bit = static_cast<uint16_t>(1u << region);
    if (m_Regions & bit)
        return false;
    m_Regions = static_cast<uint16_t>(m_Regions | bit);
    dirty = true;
    const int k = cat.KeyIndex(StatKey{ AchKind::eZone, 0 });
    if (k >= 0)
        Check(cat, k, static_cast<uint32_t>(std::popcount(m_Regions)), done);
    return true;
}

void AchieveBook::OnKill(const Catalog& cat, uint16_t species, bool elite, bool boss, bool party, DoneList& done)
{
    Add(cat, StatKey{ AchKind::eKill, 0 }, 1, done);
    if (species != 0)
        Add(cat, StatKey{ AchKind::eKill, species }, 1, done);
    if (elite)
        Add(cat, StatKey{ AchKind::eKillElite, 0 }, 1, done);
    if (boss)
    {
        Add(cat, StatKey{ AchKind::eKillBoss, 0 }, 1, done);
        if (species != 0)
            Add(cat, StatKey{ AchKind::eKillBoss, species }, 1, done);
    }
    if (party)
        Add(cat, StatKey{ AchKind::ePartyKill, 0 }, 1, done);
}

void AchieveBook::OnGear(const Catalog& cat, uint8_t grade, uint32_t count, DoneList& done)
{
    for (int p = 0; p <= std::min<int>(grade, AchieveRule::kMaxGradeParam); ++p)
        Add(cat, StatKey{ AchKind::eGear, static_cast<uint16_t>(p) }, count, done);
}

void AchieveBook::OnQuest(const Catalog& cat, uint16_t questId, DoneList& done)
{
    Add(cat, StatKey{ AchKind::eQuest, 0 }, 1, done);
    if (questId != 0)
        Add(cat, StatKey{ AchKind::eQuest, questId }, 1, done);
}

void AchieveBook::Recheck(const Catalog& cat, DoneList& done)
{
    if (Frozen())
        return;   // 깨진 기록 — 이번 접속은 읽기 전용
    for (size_t k = 0; k < cat.keys.size(); ++k)
    {
        const uint32_t v = Value(cat.keys[k]);
        if (v > 0)
            Check(cat, static_cast<int>(k), v, done);
    }
}

uint32_t AchieveBook::Progress(const AchieveRule::Row& row) const noexcept
{
    if (IsDone(row.id))
        return row.target;
    return std::min(Value(StatKey{ row.kind, row.param }), row.target);
}

AchieveBook::Summary AchieveBook::Sum(const Catalog& cat) const noexcept
{
    Summary s;
    s.total = static_cast<uint16_t>(std::min<size_t>(cat.rows.size(), 0xFFFF));
    for (const AchieveRule::Row& r : cat.rows)
    {
        if (!IsDone(r.id))
            continue;
        s.points += r.points;
        ++s.done;
        if (!IsClaimed(r.id))
            ++s.claimable;
    }
    return s;
}

bool AchieveBook::MarkClaimed(uint16_t id) noexcept
{
    if (Frozen())
        return false;
    if (!IsDone(id) || IsClaimed(id))
        return false;
    m_Claimed.set(id);
    dirty = true;
    return true;
}

void AchieveBook::Write(std::vector<uint8_t>& out, const Catalog* cat) const
{
    if (Frozen())
    {
        out.insert(out.end(), m_Raw.begin(), m_Raw.end());   // 원본 그대로(깨진 절을 빈 절로 덮지 않는다)
        return;
    }
    const size_t countAt = out.size();
    out.push_back(0);
    size_t n = 0;
    for (const Stat& s : m_Stats)
    {
        if (s.value == 0 || (cat != nullptr && !cat->Tracks(s.key)) || n >= static_cast<size_t>(AchieveRule::kMaxStatKeys))
            continue;
        out.push_back(static_cast<uint8_t>(s.key.kind));
        Put16(out, s.key.param);
        Put32(out, s.value);
        ++n;
    }
    for (const auto& raw : m_Unknown)   // 새 서버가 쓴 통계 — 읽은 그대로
    {
        if (n >= static_cast<size_t>(AchieveRule::kMaxStatKeys))
            break;
        out.insert(out.end(), raw.begin(), raw.end());
        ++n;
    }
    out[countAt] = static_cast<uint8_t>(n);
    PutBits(out, m_Done);
    PutBits(out, m_Claimed);
    Put16(out, m_Regions);
}

bool AchieveBook::Read(const uint8_t* data, size_t size, size_t& at)
{
    Clear();
    const size_t start = at;
    auto fail = [&](FreezeReason why) { Freeze(data + start, size - start, why); at = size; return false; };
    if (at + 1 > size)
        return fail(FreezeReason::Truncated);
    const size_t n = data[at++];
    if (at + n * 7 > size)
        return fail(FreezeReason::Truncated);
    for (size_t i = 0; i < n; ++i, at += 7)
    {
        const uint8_t kind = data[at];
        if (kind >= static_cast<uint8_t>(AchKind::Count) || static_cast<AchKind>(kind) == AchKind::eZone)
        {
            // 모르는 kind(이 서버보다 새 판의 표가 센 것) — 건너뛰되 7바이트를 그대로 들고 있다가 되쓴다
            std::array<uint8_t, 7> raw{};
            std::copy(data + at, data + at + 7, raw.begin());
            m_Unknown.push_back(raw);
            continue;
        }
        const StatKey key{ static_cast<AchKind>(kind), Get16(data + at + 1) };
        const uint32_t value = Get32(data + at + 3);
        if (value == 0)
            continue;
        if (Stat* s = Find(key))
            s->value = std::max(s->value, value);   // 겹친 키(손으로 만든 덩어리) — 큰 값
        else
        {
            const auto it = std::lower_bound(m_Stats.begin(), m_Stats.end(), key, [](const Stat& x, StatKey y) { return x.key < y; });
            m_Stats.insert(it, Stat{ key, value });
        }
    }
    if (at + 1 > size || data[at] > AchieveRule::kBitBytes)
        return fail(at + 1 > size ? FreezeReason::Truncated : FreezeReason::BadBits);
    if (!GetBits(data, size, at, m_Done))
        return fail(FreezeReason::Truncated);
    if (at + 1 > size || data[at] > AchieveRule::kBitBytes)
        return fail(at + 1 > size ? FreezeReason::Truncated : FreezeReason::BadBits);
    if (!GetBits(data, size, at, m_Claimed))
        return fail(FreezeReason::Truncated);
    if (at + 2 > size)
        return fail(FreezeReason::Truncated);
    m_Regions = Get16(data + at);
    at += 2;
    m_Done.reset(0);              // id 0은 없다
    m_Claimed &= m_Done;          // 받음 ⊆ 달성
    dirty = false;
    return true;
}

void AchieveBook::Freeze(const uint8_t* data, size_t size, FreezeReason why, uint8_t version)
{
    Clear();
    m_Freeze = why == FreezeReason::None ? FreezeReason::Truncated : why;
    m_FrozenVersion = version;
    m_Raw.assign(data, data + size);
}

void AchieveBook::Clear() noexcept
{
    m_Stats.clear();
    m_Unknown.clear();
    m_Done.reset();
    m_Claimed.reset();
    m_Regions = 0;
    m_Freeze = FreezeReason::None;
    m_FrozenVersion = 0;
    m_Raw.clear();
    dirty = false;
}
