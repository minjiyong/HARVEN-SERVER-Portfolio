// 결사 성장 진행 — 저장(버전 있는 바이너리) · 규칙 적용(GuildProgress.h 머리말). pch 없이도 빌드된다(단위 시험이 이 파일만 끌어온다).
#include "Content/Guild/GuildProgress.h"

#include <algorithm>
#include <cstring>

namespace GuildModel
{
    // ─────────────────────────────────────────────────────────────────────────
    // 저장 — 작은 끝(little-endian) 정수 · 길이 앞붙임 글자. 판(kVersion)이 바뀌면 읽는 쪽이 옛 판을 옮긴다.
    // ─────────────────────────────────────────────────────────────────────────
    namespace
    {
        struct Writer
        {
            std::vector<uint8_t> out;
            void U8(uint8_t v) { out.push_back(v); }
            void U16(uint16_t v) { U8(static_cast<uint8_t>(v)); U8(static_cast<uint8_t>(v >> 8)); }
            void U32(uint32_t v) { U16(static_cast<uint16_t>(v)); U16(static_cast<uint16_t>(v >> 16)); }
            void I32(int32_t v) { U32(static_cast<uint32_t>(v)); }
            void Str(const std::string& s)
            {
                const size_t n = std::min<size_t>(s.size(), 255);
                U8(static_cast<uint8_t>(n));
                out.insert(out.end(), s.begin(), s.begin() + static_cast<std::ptrdiff_t>(n));
            }
        };
        struct Reader
        {
            std::span<const uint8_t> in;
            size_t pos = 0;
            bool bad = false;
            bool Need(size_t n) { if (pos + n > in.size()) bad = true; return !bad; }
            uint8_t U8() { if (!Need(1)) return 0; return in[pos++]; }
            uint16_t U16() { const uint16_t lo = U8(); return static_cast<uint16_t>(lo | (static_cast<uint16_t>(U8()) << 8)); }
            uint32_t U32() { const uint32_t lo = U16(); return lo | (static_cast<uint32_t>(U16()) << 16); }
            int32_t I32() { return static_cast<int32_t>(U32()); }
            std::string Str()
            {
                const size_t n = U8();
                if (!Need(n)) return {};
                std::string s(reinterpret_cast<const char*>(in.data() + pos), n);
                pos += n;
                return s;
            }
            // 줄 수 — 남은 바이트로 다 읽을 수 없는 수면 깨진 것(엉뚱한 큰 수로 메모리를 잡지 않는다)
            size_t Count(size_t minBytesEach)
            {
                const size_t n = U16();
                if (bad || n * minBytesEach > in.size() - pos) { bad = true; return 0; }
                return n;
            }
        };
    }

    std::vector<uint8_t> Serialize(const GuildProgress& p)
    {
        Writer w;
        w.out.reserve(256 + p.members.size() * 40);
        w.U8(kVersion);
        w.U8(p.level); w.U32(p.exp); w.U8(p.title); w.U8(p.recruiting); w.U8(p.recruitMinLevel); w.Str(p.intro);
        w.U8(p.permsSet); for (uint8_t v : p.perms) w.U8(v);
        w.U32(p.kills); w.U32(p.bossKills); w.U32(p.coopKills); w.U32(p.missionsDone); w.U32(p.attendDays); w.U8(p.peakMembers);
        w.U16(static_cast<uint16_t>(p.achievements.size()));
        for (const auto& [id, at] : p.achievements) { w.U16(id); w.U32(at); }
        w.U32(p.weekId);
        w.U16(static_cast<uint16_t>(p.missions.size()));
        for (const auto& m : p.missions)
        {
            w.U16(m.id); w.U32(m.progress); w.U8(m.done);
            w.U16(static_cast<uint16_t>(m.who.size()));
            for (const auto& [pid, n] : m.who) { w.I32(pid); w.U32(n); }
        }
        w.U32(p.dayId); w.U8(p.attendCount); w.U8(p.attendTier);
        w.I32(p.claimantId); w.U32(p.claimEndsAt);
        w.U16(static_cast<uint16_t>(p.members.size()));
        for (const auto& m : p.members)
        {
            w.I32(m.playerId); w.U32(m.joinedAt); w.U32(m.contribWeek); w.U32(m.contribTotal); w.U16(m.activeWeek); w.U16(m.activeToday);
            w.U32(m.lastActiveMin); w.U32(m.expToday); w.U8(m.attendedToday);
            w.U8(static_cast<uint8_t>(std::min<size_t>(m.bought.size(), 255)));
            for (size_t i = 0; i < m.bought.size() && i < 255; ++i) { w.U16(m.bought[i].first); w.U8(m.bought[i].second); }
        }
        return std::move(w.out);
    }

    bool Deserialize(std::span<const uint8_t> bytes, GuildProgress& out)
    {
        Reader r{ bytes };
        if (r.U8() != kVersion || r.bad)
            return false;
        GuildProgress p;
        p.level = r.U8(); p.exp = r.U32(); p.title = r.U8(); p.recruiting = r.U8(); p.recruitMinLevel = r.U8(); p.intro = r.Str();
        p.permsSet = r.U8(); for (uint8_t& v : p.perms) v = r.U8();
        p.kills = r.U32(); p.bossKills = r.U32(); p.coopKills = r.U32(); p.missionsDone = r.U32(); p.attendDays = r.U32(); p.peakMembers = r.U8();
        for (size_t i = 0, n = r.Count(6); i < n && !r.bad; ++i)
        {
            const uint16_t id = r.U16();
            p.achievements.emplace_back(id, r.U32());
        }
        p.weekId = r.U32();
        for (size_t i = 0, n = r.Count(9); i < n && !r.bad; ++i)
        {
            GuildMissionProgress m;
            m.id = r.U16(); m.progress = r.U32(); m.done = r.U8();
            for (size_t k = 0, c = r.Count(8); k < c && !r.bad; ++k)
            {
                const int32_t pid = r.I32();
                m.who.emplace_back(pid, r.U32());
            }
            p.missions.push_back(std::move(m));
        }
        p.dayId = r.U32(); p.attendCount = r.U8(); p.attendTier = r.U8();
        p.claimantId = r.I32(); p.claimEndsAt = r.U32();
        for (size_t i = 0, n = r.Count(30); i < n && !r.bad; ++i)   // 한 줄 최소 30바이트(산 것 0개)
        {
            GuildMemberProgress m;
            m.playerId = r.I32(); m.joinedAt = r.U32(); m.contribWeek = r.U32(); m.contribTotal = r.U32(); m.activeWeek = r.U16(); m.activeToday = r.U16();
            m.lastActiveMin = r.U32(); m.expToday = r.U32(); m.attendedToday = r.U8();
            const size_t nb = r.U8();
            for (size_t k = 0; k < nb && !r.bad; ++k)
            {
                const uint16_t id = r.U16();
                m.bought.emplace_back(id, r.U8());
            }
            p.members.push_back(std::move(m));
        }
        if (r.bad || r.pos != bytes.size())
            return false;
        if (p.level == 0) p.level = 1;
        out = std::move(p);
        return true;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 결사원
    // ─────────────────────────────────────────────────────────────────────────
    GuildMemberProgress* Member(GuildProgress& p, int32_t playerId)
    {
        for (auto& m : p.members)
            if (m.playerId == playerId) return &m;
        return nullptr;
    }
    const GuildMemberProgress* Member(const GuildProgress& p, int32_t playerId)
    {
        for (const auto& m : p.members)
            if (m.playerId == playerId) return &m;
        return nullptr;
    }
    GuildMemberProgress& EnsureMember(GuildProgress& p, int32_t playerId, uint32_t now)
    {
        if (GuildMemberProgress* m = Member(p, playerId))
            return *m;
        GuildMemberProgress m;
        m.playerId = playerId;
        m.joinedAt = now;
        p.members.push_back(std::move(m));
        return p.members.back();
    }
    void RemoveMember(GuildProgress& p, int32_t playerId)
    {
        p.members.erase(std::remove_if(p.members.begin(), p.members.end(), [&](const GuildMemberProgress& m) { return m.playerId == playerId; }), p.members.end());
        if (p.claimantId == playerId)
        {
            p.claimantId = 0;
            p.claimEndsAt = 0;
        }
    }
    Departed Depart(const GuildProgress& p, int32_t playerId)
    {
        Departed d;
        d.dayId = p.dayId;
        d.weekId = p.weekId;
        if (const GuildMemberProgress* m = Member(p, playerId))
            d.m = *m;
        return d;
    }
    void Rejoin(const GuildProgress& p, GuildMemberProgress& m, const Departed& d)
    {
        if (d.m.playerId != m.playerId)
            return;
        if (d.dayId == p.dayId)
        {
            m.expToday = std::max(m.expToday, d.m.expToday);
            m.activeToday = std::max(m.activeToday, d.m.activeToday);
            m.lastActiveMin = std::max(m.lastActiveMin, d.m.lastActiveMin);
            m.attendedToday = std::max(m.attendedToday, d.m.attendedToday);
        }
        if (d.weekId == p.weekId)
            for (const auto& [id, n] : d.m.bought)
                for (uint8_t i = BoughtThisWeek(m, id); i < n; ++i)
                    AddBought(m, id);
    }
    void SyncMembers(GuildProgress& p, std::span<const int32_t> playerIds, uint32_t now)
    {
        p.members.erase(std::remove_if(p.members.begin(), p.members.end(), [&](const GuildMemberProgress& m)
        {
            return std::find(playerIds.begin(), playerIds.end(), m.playerId) == playerIds.end();
        }), p.members.end());
        for (int32_t pid : playerIds)
            (void)EnsureMember(p, pid, now);
        p.peakMembers = static_cast<uint8_t>(std::max<size_t>(p.peakMembers, std::min<size_t>(p.members.size(), 255)));
    }

    uint8_t PermsOf(const GuildProgress& p, const GuildGrowth::ConstRow& c, uint8_t rank) noexcept
    {
        if (rank == 0) return GuildGrowth::kAllPerms;
        if (rank > 3) return 0;
        if (p.permsSet) return p.perms[rank];
        return rank == 1 ? c.permOfficer : rank == 2 ? c.permElite : c.permMember;
    }
    void PermsArray(const GuildProgress& p, const GuildGrowth::ConstRow& c, uint8_t (&out)[4]) noexcept
    {
        for (uint8_t r = 0; r < 4; ++r)
            out[r] = PermsOf(p, c, r);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 주 · 날
    // ─────────────────────────────────────────────────────────────────────────
    int Roll(GuildProgress& p, uint32_t guildId, int64_t now, const Tables& t)
    {
        int rolled = 0;
        const uint32_t day = GuildGrowth::PeriodId(now, t.c.dayPeriodSec, t.c.dayOffsetSec);
        if (day != p.dayId)
        {
            p.dayId = day;
            p.attendCount = 0;
            p.attendTier = 0;
            for (auto& m : p.members)
            {
                m.activeToday = 0;
                m.expToday = 0;
                m.attendedToday = 0;
            }
            rolled |= 1;
        }
        const uint32_t week = GuildGrowth::PeriodId(now, t.c.weekPeriodSec, t.c.weekOffsetSec);
        if (week != p.weekId || p.missions.empty())
        {
            const bool newWeek = week != p.weekId;
            p.weekId = week;
            if (newWeek)
                for (auto& m : p.members)
                {
                    m.contribWeek = 0;
                    m.activeWeek = 0;
                    m.bought.clear();
                }
            uint16_t ids[GuildGrowth::kMissionSlotsMax]{};
            const size_t slots = std::min<size_t>(t.c.missionSlots, GuildGrowth::kMissionSlotsMax);
            const size_t n = GuildGrowth::PickMissions(t.missions, p.level, guildId, week, slots, std::span<uint16_t>(ids, slots));
            p.missions.clear();
            for (size_t i = 0; i < n; ++i)
            {
                GuildMissionProgress m;
                m.id = ids[i];
                p.missions.push_back(std::move(m));
            }
            rolled |= newWeek ? 2 : 0;
        }
        return rolled;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 규칙 적용
    // ─────────────────────────────────────────────────────────────────────────
    namespace
    {
        const GuildGrowth::MissionRow* MissionOf(const Tables& t, uint16_t id)
        {
            for (const auto& m : t.missions)
                if (m.id == id) return &m;
            return nullptr;
        }

        void Contribute(GuildProgress& p, int32_t playerId, uint32_t amount, Effects& fx)
        {
            if (amount == 0) return;
            GuildMemberProgress* m = Member(p, playerId);
            if (m == nullptr) return;
            m->contribWeek += amount;
            m->contribTotal += amount;
            for (auto& [pid, n] : fx.contrib)
                if (pid == playerId) { n += amount; return; }
            fx.contrib.emplace_back(playerId, amount);
        }

        bool HasAch(const GuildProgress& p, uint16_t id)
        {
            return std::any_of(p.achievements.begin(), p.achievements.end(), [&](const auto& a) { return a.first == id; });
        }

        void CompleteMission(GuildProgress& p, GuildMissionProgress& mp, const GuildGrowth::MissionRow& row, int64_t now, const Tables& t, Effects& fx)
        {
            mp.done = 1;
            ++p.missionsDone;
            fx.missionsDone.push_back(row.id);
            const uint32_t people = static_cast<uint32_t>(mp.who.size());
            const uint32_t exp = GuildGrowth::WithBonus(row.exp, people, t.c.missionBonusMembers, t.c.missionBonusPct);
            const uint32_t seals = GuildGrowth::WithBonus(row.seals, people, t.c.missionBonusMembers, t.c.missionBonusPct);
            if (seals > 0)
                fx.seals.push_back({ static_cast<int32_t>(seals), GuildLogKind::SealsIn, static_cast<int32_t>(row.id), row.name });
            for (const auto& [pid, n] : mp.who)
                Contribute(p, pid, row.contrib, fx);
            AddExp(p, exp, now, t, fx);
        }

        // 진행 += amount — 참여한 사람마다 몫 amount를 적고(달성 보상의 기여도를 받는다), 진행은 한 번만 오른다
        // (여럿이 함께 쓰러뜨린 한 마리 = 1). 달성하면 보상까지.
        void Advance(GuildProgress& p, GuildMissionProgress& mp, const GuildGrowth::MissionRow& row, std::span<const int32_t> players, uint32_t amount,
            int64_t now, const Tables& t, Effects& fx)
        {
            if (mp.done || amount == 0) return;
            for (int32_t pid : players)
            {
                auto it = std::find_if(mp.who.begin(), mp.who.end(), [&](const auto& w) { return w.first == pid; });
                if (it == mp.who.end()) mp.who.emplace_back(pid, amount);
                else it->second += amount;
            }
            mp.progress = static_cast<uint32_t>(std::min<uint64_t>(static_cast<uint64_t>(mp.progress) + amount, row.target));
            fx.missionProgress = true;
            if (mp.progress >= row.target)
                CompleteMission(p, mp, row, now, t, fx);
        }

        template <class Pred>
        void AdvanceKind(GuildProgress& p, GuildMissionKind kind, std::span<const int32_t> players, uint32_t amount, int64_t now, const Tables& t, Effects& fx, Pred&& accept)
        {
            for (auto& mp : p.missions)
            {
                const GuildGrowth::MissionRow* row = MissionOf(t, mp.id);
                if (row == nullptr || row->kind != kind || mp.done || !accept(*row)) continue;
                Advance(p, mp, *row, players, amount, now, t, fx);
            }
        }

        // 활동 임무(sub분 이상 활동한 결사원 수) — 활동이 늘 때마다 다시 센다
        void RecountActive(GuildProgress& p, int32_t playerId, int64_t now, const Tables& t, Effects& fx)
        {
            for (auto& mp : p.missions)
            {
                const GuildGrowth::MissionRow* row = MissionOf(t, mp.id);
                if (row == nullptr || row->kind != GuildMissionKind::Active || mp.done) continue;
                uint32_t count = 0;
                for (const auto& m : p.members)
                    if (m.activeWeek >= row->sub) ++count;
                const GuildMemberProgress* me = Member(p, playerId);
                if (me != nullptr && me->activeWeek >= row->sub
                    && std::none_of(mp.who.begin(), mp.who.end(), [&](const auto& w) { return w.first == playerId; }))
                    mp.who.emplace_back(playerId, me->activeWeek);
                if (count != mp.progress)
                {
                    mp.progress = std::min(count, row->target);
                    fx.missionProgress = true;
                    if (mp.progress >= row->target)
                        CompleteMission(p, mp, *row, now, t, fx);
                }
            }
        }

        // 활동 1분(같은 분은 한 번) — 출석 · 활동 임무
        void Activity(GuildProgress& p, int32_t playerId, int64_t now, const Tables& t, Effects& fx)
        {
            GuildMemberProgress* m = Member(p, playerId);
            if (m == nullptr) return;
            const uint32_t minute = static_cast<uint32_t>(now / 60);
            if (m->lastActiveMin == minute) return;
            m->lastActiveMin = minute;
            if (m->activeToday < 0xFFFF) ++m->activeToday;
            if (m->activeWeek < 0xFFFF) ++m->activeWeek;
            RecountActive(p, playerId, now, t, fx);
            if (m->attendedToday || m->activeToday < t.c.attendActiveMin) return;
            m->attendedToday = 1;
            if (p.attendCount < 255) ++p.attendCount;
            const uint8_t tier = GuildGrowth::AttendTierFor(t.attend, p.attendCount);
            while (p.attendTier < tier)
            {
                ++p.attendTier;
                if (p.attendTier == 1) ++p.attendDays;
                fx.attendTiers.push_back(p.attendTier);
                for (const auto& row : t.attend)
                {
                    if (row.tier != p.attendTier) continue;
                    if (row.seals > 0)
                        fx.seals.push_back({ static_cast<int32_t>(row.seals), GuildLogKind::SealsIn, row.tier, "출석" });
                    for (const auto& mm : p.members)
                        if (mm.attendedToday) Contribute(p, mm.playerId, row.contrib, fx);
                    AddExp(p, row.exp, now, t, fx);
                }
            }
        }

        void Unlock(uint32_t bitsBefore, const GuildProgress& p, const Tables& t, Effects& fx)
        {
            const uint32_t now = TitlesUnlocked(p, t);
            for (uint8_t id = 1; id <= GuildGrowth::kTitleIdMax; ++id)
                if ((now & (1u << id)) && !(bitsBefore & (1u << id))
                    && std::find(fx.titlesUnlocked.begin(), fx.titlesUnlocked.end(), id) == fx.titlesUnlocked.end())
                    fx.titlesUnlocked.push_back(id);
        }
    }

    void AddExp(GuildProgress& p, uint32_t exp, int64_t now, const Tables& t, Effects& fx)
    {
        if (exp == 0) return;
        const uint32_t titlesBefore = TitlesUnlocked(p, t);
        if (fx.levelBefore == 0) fx.levelBefore = p.level;
        p.exp = static_cast<uint32_t>(std::min<uint64_t>(static_cast<uint64_t>(p.exp) + exp, 0xFFFFFFF0ull));
        fx.exp += exp;
        const uint8_t lv = GuildGrowth::LevelForExp(t.levels, p.exp);
        if (lv > p.level)
            p.level = lv;
        fx.levelAfter = p.level;
        Unlock(titlesBefore, p, t, fx);
        CheckAchievements(p, now, 0, 0, t, fx);
    }

    void OnKill(GuildProgress& p, const KillIn& k, int64_t now, const Tables& t, Effects& fx)
    {
        if (k.players.empty()) return;
        ++p.kills;
        if (k.boss) ++p.bossKills;
        // 협력: 둘 이상 함께 · 그중 새 결사원
        bool coop = false;
        if (k.players.size() >= 2)
            for (int32_t pid : k.players)
                if (const GuildMemberProgress* m = Member(p, pid); m != nullptr && GuildGrowth::IsNewMember(m->joinedAt, now, t.c.coopNewDays))
                    coop = true;
        if (coop) ++p.coopKills;
        // 기여 · 경험치(한 마리에 한 번 — 함께 때린 사람끼리 나누고, 한 사람의 하루 한도까지만)
        const uint32_t contrib = (k.boss ? t.c.bossContrib : t.c.killContrib) + (coop ? t.c.coopContribBonus : 0u);
        const uint32_t pool = static_cast<uint32_t>(static_cast<uint64_t>(k.monsterExp) * t.c.killExpPct / 100);
        const uint32_t each = k.players.empty() ? 0 : pool / static_cast<uint32_t>(k.players.size());
        uint32_t remainder = pool - each * static_cast<uint32_t>(k.players.size());
        uint32_t gained = 0;
        for (int32_t pid : k.players)
        {
            GuildMemberProgress* m = Member(p, pid);
            if (m == nullptr) continue;
            uint32_t share = each + (remainder > 0 ? 1u : 0u);
            if (remainder > 0) --remainder;
            const uint32_t room = t.c.memberDailyExpCap > m->expToday ? t.c.memberDailyExpCap - m->expToday : 0u;
            share = std::min(share, room);
            m->expToday += share;
            gained += share;
            Contribute(p, pid, contrib, fx);
        }
        // 임무(한 마리 = 1 · 함께 때린 사람마다 몫 1)
        std::vector<int32_t> members;
        members.reserve(k.players.size());
        for (int32_t pid : k.players)
            if (Member(p, pid) != nullptr) members.push_back(pid);
        AdvanceKind(p, GuildMissionKind::Kill, members, 1, now, t, fx, [&](const GuildGrowth::MissionRow& r) { return k.monsterLevel >= r.sub; });
        if (k.boss)
            AdvanceKind(p, GuildMissionKind::Boss, members, 1, now, t, fx, [](const GuildGrowth::MissionRow&) { return true; });
        for (int32_t pid : k.players)
            Activity(p, pid, now, t, fx);
        AddExp(p, gained, now, t, fx);
        CheckAchievements(p, now, 0, 0, t, fx);
    }

    void OnLoot(GuildProgress& p, int32_t playerId, uint16_t itemId, uint16_t count, bool enchantMaterial, bool equipment, uint8_t rarity,
        int64_t now, const Tables& t, Effects& fx)
    {
        if (Member(p, playerId) == nullptr || count == 0) return;
        const int32_t who[1] = { playerId };
        AdvanceKind(p, GuildMissionKind::Collect, who, count, now, t, fx, [&](const GuildGrowth::MissionRow& r)
        {
            return r.sub == 0 ? enchantMaterial : r.sub == itemId;
        });
        if (equipment)
            AdvanceKind(p, GuildMissionKind::Gear, who, 1, now, t, fx, [&](const GuildGrowth::MissionRow& r) { return rarity >= r.sub; });
    }

    void OnQuest(GuildProgress& p, int32_t playerId, int64_t now, const Tables& t, Effects& fx)
    {
        if (Member(p, playerId) == nullptr) return;
        const int32_t who[1] = { playerId };
        AdvanceKind(p, GuildMissionKind::Quest, who, 1, now, t, fx, [](const GuildGrowth::MissionRow&) { return true; });
    }

    uint32_t AchProgress(const GuildProgress& p, GuildAchKind kind, int64_t now, int64_t createdAt) noexcept
    {
        switch (kind)
        {
        case GuildAchKind::Kills:     return p.kills;
        case GuildAchKind::BossKills: return p.bossKills;
        case GuildAchKind::Missions:  return p.missionsDone;
        case GuildAchKind::Coop:      return p.coopKills;
        case GuildAchKind::Days:      return (createdAt > 0 && now > createdAt) ? static_cast<uint32_t>((now - createdAt) / 86400) : 0u;
        case GuildAchKind::Attend:    return p.attendDays;
        case GuildAchKind::Level:     return p.level;
        case GuildAchKind::Members:   return p.peakMembers;
        }
        return 0;
    }

    void CheckAchievements(GuildProgress& p, int64_t now, int64_t createdAt, uint8_t memberCount, const Tables& t, Effects& fx)
    {
        if (memberCount > p.peakMembers) p.peakMembers = memberCount;
        for (const auto& a : t.achs)
        {
            if (HasAch(p, a.id)) continue;
            // Days는 createdAt을 아는 부름(서비스의 날 넘김 · 접속)에서만 — 0이면 건너뛴다
            if (a.kind == GuildAchKind::Days && createdAt <= 0) continue;
            if (AchProgress(p, a.kind, now, createdAt) < a.target) continue;
            const uint32_t titlesBefore = TitlesUnlocked(p, t);
            p.achievements.emplace_back(a.id, static_cast<uint32_t>(now));
            fx.achievementsDone.push_back(a.id);
            if (a.seals > 0)
                fx.seals.push_back({ static_cast<int32_t>(a.seals), GuildLogKind::SealsIn, static_cast<int32_t>(a.id), a.name });
            Unlock(titlesBefore, p, t, fx);
            AddExp(p, a.exp, now, t, fx);   // 레벨이 오르면 안에서 다시 확인(이룬 것은 건너뛴다)
        }
    }

    uint32_t TitlesUnlocked(const GuildProgress& p, const Tables& t) noexcept
    {
        uint32_t bits = 0;
        for (const auto& l : t.levels)
            if (l.titleId != 0 && p.level >= l.level && l.titleId <= GuildGrowth::kTitleIdMax) bits |= 1u << l.titleId;
        for (const auto& a : t.achs)
            if (a.titleId != 0 && a.titleId <= GuildGrowth::kTitleIdMax && HasAch(p, a.id)) bits |= 1u << a.titleId;
        return bits;
    }

    uint8_t BoughtThisWeek(const GuildMemberProgress& m, uint16_t shopId) noexcept
    {
        for (const auto& [id, n] : m.bought)
            if (id == shopId) return n;
        return 0;
    }
    void AddBought(GuildMemberProgress& m, uint16_t shopId)
    {
        for (auto& [id, n] : m.bought)
            if (id == shopId) { if (n < 255) ++n; return; }
        m.bought.emplace_back(shopId, 1);
    }
}
