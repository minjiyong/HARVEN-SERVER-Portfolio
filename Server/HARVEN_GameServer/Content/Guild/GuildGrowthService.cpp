#include "pch.h"
// ─────────────────────────────────────────────────────────────────────────────
// 결사 성장 — CGuildService(서비스 스레드)의 성장 쪽 · CGuildHelper(소유 Zone)의 성장 요청 · 사냥 훅.
// 설계 Shared/Document/GUILD.md, 규칙 Database/GameData/GuildGrowth.h · Content/Guild/GuildProgress.h(GuildModel).
// ─────────────────────────────────────────────────────────────────────────────
#include "Content/Guild/GuildService.h"

#include <algorithm>

#include "Content/Chat/ChatHelper.h"
#include "Content/Inventory/Inventory.h"
#include "Database/DBThread.h"
#include "Database/GameData/GuildGrowth.h"
#include "Database/GameData/Item/ItemTable.h"
#include "Database/GameData/ItemRules.h"
#include "Network/GameSessionManager.h"

namespace GuildModel
{
    Tables FromSheet()
    {
        Tables t;
        t.c = GuildGrowth::Const();
        t.levels = GuildGrowth::Levels();
        t.missions = GuildGrowth::Missions();
        t.achs = GuildGrowth::Achievements();
        t.attend = GuildGrowth::AttendTiers();
        return t;
    }
}

namespace
{
    using GuildRule::Rank;

    void Copy(char* dst, size_t cap, const std::string& src)
    {
        ::strncpy_s(dst, cap, src.c_str(), _TRUNCATE);
    }
    [[nodiscard]] UInt32 Now32(Int64 unix) noexcept { return static_cast<UInt32>(std::max<Int64>(unix, 0)); }

    std::string ItemName(UInt16 itemId)
    {
        const ItemTableRow* row = ItemTable::Find(itemId);
        if (row == nullptr) return {};
        return (row->displayName != nullptr && row->displayName[0] != '\0') ? row->displayName : row->name;
    }
    std::string TitleName(UInt8 id)
    {
        const GuildGrowth::TitleRow* t = GuildGrowth::FindTitle(id);
        return t != nullptr ? t->name : std::string();
    }
    // 기록 거르기(GuildLogFilter)
    [[nodiscard]] bool LogMatches(UInt8 kind, UInt8 filter) noexcept
    {
        const auto k = static_cast<GuildLogKind>(kind);
        switch (static_cast<GuildLogFilter>(filter))
        {
        case GuildLogFilter::Members:
            return k == GuildLogKind::Joined || k == GuildLogKind::Left || k == GuildLogKind::Kicked || k == GuildLogKind::Rank || k == GuildLogKind::Leader
                || k == GuildLogKind::ClaimStart || k == GuildLogKind::ClaimCancel || k == GuildLogKind::ClaimDone || k == GuildLogKind::Perms
                || k == GuildLogKind::Notice || k == GuildLogKind::Intro || k == GuildLogKind::Emblem || k == GuildLogKind::Accepted;
        case GuildLogFilter::Treasury:
            return k == GuildLogKind::SealsIn || k == GuildLogKind::SealsOut;
        case GuildLogFilter::Growth:
            return k == GuildLogKind::LevelUp || k == GuildLogKind::Mission || k == GuildLogKind::Achievement || k == GuildLogKind::Attend || k == GuildLogKind::Title;
        default:
            return true;
        }
    }
    [[nodiscard]] UInt8 Pages(size_t n, int per) noexcept
    {
        return static_cast<UInt8>(std::min<size_t>(255, std::max<size_t>(1, (n + per - 1) / per)));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 바탕 — 인원 한도 · 권한 · 불러오기 · 저장 · 기록 · 금고 · 알림
// ─────────────────────────────────────────────────────────────────────────────
UInt8 CGuildService::MaxMembers(const Guild& g) noexcept
{
    if (const GuildGrowth::LevelRow* r = GuildGrowth::LevelRowOf(GuildGrowth::Levels(), g.grow.p.level))
        return r->maxMembers;
    return static_cast<UInt8>(GuildRule::kMaxMembers);
}

bool CGuildService::HasPerm(const Guild& g, UInt8 rank, GuildPerm perm) noexcept
{
    if (rank == static_cast<UInt8>(Rank::Leader)) return true;
    return (GuildModel::PermsOf(g.grow.p, GuildGrowth::Const(), rank) & static_cast<UInt8>(perm)) != 0;
}

bool CGuildService::LoadGrowth(CDBConnection& db)
{
    std::vector<DB_GUILD_PROGRESS_ROW> rows;
    std::vector<DB_GUILD_LOG_ROW> logs;
    std::vector<DB_GUILD_POST_ROW> posts;
    Int64 maxPost = 0;
    if (!db.LoadGuildProgress(rows) || !db.LoadGuildLogs(logs, GuildGrowth::Const().logKeep) || !db.LoadGuildPosts(posts) || !db.GuildPostMaxId(maxPost))
        return false;
    m_NextPostId = maxPost + 1;
    const Int64 now = Service::UnixNow();
    int broken = 0;
    for (const auto& row : rows)
    {
        const auto it = m_Guilds.find(static_cast<UInt32>(row.guildId));
        if (it == m_Guilds.end())
            continue;
        Guild& g = it->second;
        g.grow.seals = row.seals;
        g.grow.createdAt = row.createdAt;
        bool fresh = row.data.empty();
        if (!fresh && !GuildModel::Deserialize(row.data, g.grow.p))
        {
            ++broken;   // 모르는 판 · 깨진 덩어리 — 기본값으로 시작(금고 · 기록 · 게시판은 따로라 남는다)
            fresh = true;
        }
        InitGrowth(g, row.createdAt > 0 ? row.createdAt : now, fresh);
    }
    for (auto& [id, g] : m_Guilds)
        if (g.grow.p.missions.empty() && g.grow.p.weekId == 0)
            InitGrowth(g, now, true);   // 진행 줄이 없던 결사(LoadGuildProgress는 결사마다 한 줄이지만 — 지켜 둔다)
    for (auto& l : logs)
        if (const auto it = m_Guilds.find(static_cast<UInt32>(l.guildId)); it != m_Guilds.end())
            it->second.grow.logs.push_back(std::move(l));
    for (auto& p : posts)
        if (const auto it = m_Guilds.find(static_cast<UInt32>(p.guildId)); it != m_Guilds.end())
            it->second.grow.posts.push_back(std::move(p));
    std::cout << "[GuildGrowth] guilds=" << m_Guilds.size() << " logs=" << logs.size() << " posts=" << posts.size()
        << " broken=" << broken << " nextPost=" << m_NextPostId << "\n" << std::flush;
    return true;
}

void CGuildService::InitGrowth(Guild& g, Int64 joinedDefault, bool fresh)
{
    const Int64 now = Service::UnixNow();
    std::vector<int32_t> ids;
    ids.reserve(g.members.size());
    for (const auto& m : g.members) ids.push_back(m.playerId);
    GuildModel::SyncMembers(g.grow.p, ids, Now32(joinedDefault));
    const GuildModel::Tables t = GuildModel::FromSheet();
    if (GuildModel::Roll(g.grow.p, g.id, now, t) != 0 || fresh)
        MarkDirty(g, fresh);
}

void CGuildService::MarkDirty(Guild& g, bool urgent)
{
    if (!g.grow.dirty)
        g.grow.dirtyAt = GetNowTime();
    g.grow.dirty = true;
    g.grow.urgent = g.grow.urgent || urgent;
}

void CGuildService::SaveGrowth(Guild& g)
{
    if (g.grow.saving)
        return;
    std::vector<UInt8> bytes = GuildModel::Serialize(g.grow.p);
    if (bytes.size() > GuildModel::kMaxBytes)
    {
        std::cout << "[GuildGrowth] progress too big guild=" << g.id << " bytes=" << bytes.size() << "\n" << std::flush;
        g.grow.dirty = false;
        return;
    }
    g.grow.saving = true;
    g.grow.dirty = false;
    g.grow.urgent = false;
    const UInt32 guildId = g.id;
    GDBThread->RequestTxKeyed(guildId, "GuildProgressSave",
        [guildId, bytes = std::move(bytes)](CDBConnection& c) { return c.GuildProgressSave(static_cast<int>(guildId), bytes); },
        [guildId](bool ok)
        {
            GGuildService->Post([guildId, ok]()
            {
                auto& svc = *GGuildService;
                const auto it = svc.m_Guilds.find(guildId);
                if (it == svc.m_Guilds.end()) return;
                it->second.grow.saving = false;
                if (!ok) svc.MarkDirty(it->second);   // 다음 틱에 다시
            });
        });
}

void CGuildService::AddLog(Guild& g, GuildLogKind kind, Int32 value, const std::string& who, const std::string& what, bool persist)
{
    DB_GUILD_LOG_ROW row;
    row.guildId = static_cast<int>(g.id);
    row.at = Service::UnixNow();
    row.kind = static_cast<UInt8>(kind);
    row.value = value;
    row.who = who;
    row.what = what;
    if (persist)
        GDBThread->RequestTxKeyed(g.id, "GuildLog", [row](CDBConnection& c) { return c.GuildLogAdd(row); }, nullptr);
    g.grow.logs.push_back(std::move(row));
    const size_t keep = std::max<size_t>(1, GuildGrowth::Const().logKeep);
    if (g.grow.logs.size() > keep)
        g.grow.logs.erase(g.grow.logs.begin(), g.grow.logs.begin() + static_cast<std::ptrdiff_t>(g.grow.logs.size() - keep));
}

void CGuildService::AddSeals(Guild& g, const GuildModel::Seal& seal)
{
    if (seal.amount == 0) return;
    DB_GUILD_LOG_ROW log;
    log.guildId = static_cast<int>(g.id);
    log.at = Service::UnixNow();
    log.kind = static_cast<UInt8>(seal.kind);
    log.value = seal.amount;
    log.what = seal.what;
    const UInt32 guildId = g.id;
    const Int32 amount = seal.amount;
    GDBThread->RequestTxKeyed(guildId, "GuildSeals",
        [guildId, amount, log](CDBConnection& c) { return c.GuildSealsAdd(static_cast<int>(guildId), amount, log); },
        [guildId, amount, log](bool ok)
        {
            GGuildService->Post([guildId, amount, log, ok]()
            {
                auto& svc = *GGuildService;
                const auto it = svc.m_Guilds.find(guildId);
                if (it == svc.m_Guilds.end()) return;
                Guild& gg = it->second;
                if (!ok)
                {
                    std::cout << "[GuildGrowth] seals add failed guild=" << guildId << " amount=" << amount << "\n" << std::flush;
                    return;
                }
                gg.grow.seals = static_cast<UInt32>(std::max<Int64>(0, static_cast<Int64>(gg.grow.seals) + amount));
                gg.grow.logs.push_back(log);
                const size_t keep = std::max<size_t>(1, GuildGrowth::Const().logKeep);
                if (gg.grow.logs.size() > keep) gg.grow.logs.erase(gg.grow.logs.begin());
                svc.NotifyEx(gg, GuildExEvent::Seals, amount, static_cast<Int32>(gg.grow.seals), "", log.what);
            });
        });
}

void CGuildService::ApplyEffects(Guild& g, GuildModel::Effects& fx)
{
    for (const auto& seal : fx.seals)
        AddSeals(g, seal);
    if (fx.Leveled())
    {
        NotifyEx(g, GuildExEvent::LevelUp, fx.levelAfter, MaxMembers(g), "", "");
        AddLog(g, GuildLogKind::LevelUp, fx.levelAfter, "", "");
        for (const auto& m : g.members)
            if (m.online) SendOverview(m.session, &g, m.playerId);
        std::cout << "[GuildGrowth] level guild=" << g.id << " level=" << static_cast<int>(fx.levelAfter) << "\n" << std::flush;
    }
    for (UInt16 id : fx.missionsDone)
    {
        const GuildGrowth::MissionRow* row = GuildGrowth::FindMission(id);
        const std::string name = row != nullptr ? row->name : "";
        Int32 seals = 0;
        for (const auto& s : fx.seals)
            if (s.value == id && s.what == name) seals = s.amount;
        NotifyEx(g, GuildExEvent::MissionDone, id, seals, "", name);
        AddLog(g, GuildLogKind::Mission, id, "", name);
        g.grow.missionDirty = true;
    }
    for (UInt16 id : fx.achievementsDone)
    {
        const GuildGrowth::AchRow* row = GuildGrowth::FindAch(id);
        const std::string name = row != nullptr ? row->name : "";
        NotifyEx(g, GuildExEvent::AchievementDone, id, row != nullptr ? row->titleId : 0, "", name);
        AddLog(g, GuildLogKind::Achievement, id, "", name);
    }
    for (UInt8 tier : fx.attendTiers)
    {
        NotifyEx(g, GuildExEvent::AttendTier, tier, g.grow.p.attendCount, "", "");
        AddLog(g, GuildLogKind::Attend, tier, "", "");
    }
    for (UInt8 id : fx.titlesUnlocked)
        NotifyEx(g, GuildExEvent::TitleUnlocked, id, 0, "", TitleName(id));
    for (const auto& [pid, n] : fx.contrib)
        g.grow.pulse.insert(pid);
    if (fx.missionProgress)
        g.grow.missionDirty = true;
    MarkDirty(g);
}

void CGuildService::NotifyOne(const SessionWeakPtr& session, GuildExEvent ev, Int32 value, Int32 value2, const std::string& who, const std::string& text) const
{
    GUILD_EX_NFY_PACKET p;
    Service::Init(p, PacketType::GUILD_EX_NFY);
    p.event = static_cast<UInt8>(ev);
    p.value = value;
    p.value2 = value2;
    Copy(p.who, sizeof(p.who), who);
    Copy(p.text, sizeof(p.text), text);
    Service::SendTo(session, p);
}

void CGuildService::NotifyEx(const Guild& g, GuildExEvent ev, Int32 value, Int32 value2, const std::string& who, const std::string& text,
    UInt8 permFilter, int except) const
{
    GUILD_EX_NFY_PACKET p;
    Service::Init(p, PacketType::GUILD_EX_NFY);
    p.event = static_cast<UInt8>(ev);
    p.value = value;
    p.value2 = value2;
    Copy(p.who, sizeof(p.who), who);
    Copy(p.text, sizeof(p.text), text);
    for (const auto& m : g.members)
    {
        if (!m.online || m.playerId == except) continue;
        if (permFilter != 0 && !HasPerm(g, m.rank, static_cast<GuildPerm>(permFilter))) continue;
        Service::SendTo(m.session, p);
    }
}

void CGuildService::AckEx(const Service::Ctx& ctx, GuildExOp op, GuildExResult r, UInt32 seq, UInt32 param) const
{
    if (op == GuildExOp::Buy)
        Service::PostToOwner(ctx.obj, [](const UserSharedPtr& u) { if (u && u->m_PendingGuildEconomy) --u->m_PendingGuildEconomy; });
    GUILD_EX_ACK_PACKET p;
    Service::Init(p, PacketType::GUILD_EX_ACK);
    p.op = static_cast<UInt8>(op);
    p.result = static_cast<UInt8>(r);
    p.seq = seq;
    p.param = param;
    Service::SendTo(ctx.session, p);
}

// ─────────────────────────────────────────────────────────────────────────────
// 틱(1초) — 주 · 날 넘김 · 위임 청구 · 신청 만료 · 진행 알림 · 저장
// ─────────────────────────────────────────────────────────────────────────────
void CGuildService::GrowthTick(UInt32 nowMs, Int64 nowUnix)
{
    const GuildModel::Tables t = GuildModel::FromSheet();
    const UInt32 applyExpire = GuildGrowth::Const().applyExpireSec;
    for (auto& [id, g] : m_Guilds)
    {
        Growth& gr = g.grow;
        const int rolled = GuildModel::Roll(gr.p, g.id, nowUnix, t);
        if (rolled & 2)
            gr.departed.clear();   // 지난주에 나간 사람의 한도 몫은 뜻이 없다
        if (rolled != 0)
        {
            GuildModel::Effects fx;
            GuildModel::CheckAchievements(gr.p, nowUnix, gr.createdAt, static_cast<uint8_t>(g.members.size()), t, fx);
            ApplyEffects(g, fx);
            gr.missionDirty = true;
            MarkDirty(g, true);
            for (const auto& m : g.members)
                if (m.online) SendOverview(m.session, &g, m.playerId);
        }
        if (gr.p.claimantId != 0 && nowUnix >= static_cast<Int64>(gr.p.claimEndsAt) && !gr.claimBusy && !g.busy)
            FinishClaim(g);
        for (auto it = gr.applicants.begin(); it != gr.applicants.end();)
        {
            if (static_cast<Int64>(it->at) + applyExpire > nowUnix) { ++it; continue; }
            NotifyOne(it->ctx.session, GuildExEvent::ApplyResult, 2, static_cast<Int32>(g.id), "", g.name);
            m_ApplyOf.erase(it->ctx.playerId);
            it = gr.applicants.erase(it);
        }
        if (gr.missionDirty && nowMs - gr.missionSentAt >= 2000)
        {
            for (const auto& mp : gr.p.missions)
                NotifyEx(g, GuildExEvent::MissionProgress, mp.id, static_cast<Int32>(mp.progress), "", "");
            gr.missionDirty = false;
            gr.missionSentAt = nowMs;
        }
        if (nowMs - gr.pulseAt >= 3000 && (!gr.pulse.empty() || gr.pulseExp != gr.p.exp))
        {
            const bool expChanged = gr.pulseExp != gr.p.exp;
            for (const auto& m : g.members)
            {
                if (!m.online || !(expChanged || gr.pulse.count(m.playerId))) continue;
                const GuildMemberProgress* mp = GuildModel::Member(gr.p, m.playerId);
                NotifyOne(m.session, GuildExEvent::Pulse, static_cast<Int32>(gr.p.exp), mp != nullptr ? static_cast<Int32>(mp->contribWeek) : 0, "", "");
            }
            gr.pulse.clear();
            gr.pulseExp = gr.p.exp;
            gr.pulseAt = nowMs;
        }
        if (gr.dirty && !gr.saving && (gr.urgent || nowMs - gr.dirtyAt >= 10000))
            SaveGrowth(g);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 사냥 · 전리품 · 퀘스트(Zone이 Post)
// ─────────────────────────────────────────────────────────────────────────────
void CGuildService::OnGuildKill(UInt32 guildId, const GuildModel::KillIn& kill)
{
    const auto it = m_Guilds.find(guildId);
    if (it == m_Guilds.end()) return;
    Guild& g = it->second;
    GuildModel::KillIn k = kill;
    k.players.erase(std::remove_if(k.players.begin(), k.players.end(), [&](int32_t pid) { return FindMember(g, pid) == nullptr; }), k.players.end());
    if (k.players.empty()) return;
    GuildModel::Effects fx;
    GuildModel::OnKill(g.grow.p, k, Service::UnixNow(), GuildModel::FromSheet(), fx);
    ApplyEffects(g, fx);
}

void CGuildService::OnGuildLoot(UInt32 guildId, int playerId, UInt16 itemId, UInt16 count, bool material, bool equipment, UInt8 rarity)
{
    const auto it = m_Guilds.find(guildId);
    if (it == m_Guilds.end() || FindMember(it->second, playerId) == nullptr) return;
    GuildModel::Effects fx;
    GuildModel::OnLoot(it->second.grow.p, playerId, itemId, count, material, equipment, rarity, Service::UnixNow(), GuildModel::FromSheet(), fx);
    if (fx.missionProgress || !fx.missionsDone.empty())
        ApplyEffects(it->second, fx);
}

void CGuildService::OnGuildQuest(UInt32 guildId, int playerId)
{
    const auto it = m_Guilds.find(guildId);
    if (it == m_Guilds.end() || FindMember(it->second, playerId) == nullptr) return;
    GuildModel::Effects fx;
    GuildModel::OnQuest(it->second.grow.p, playerId, Service::UnixNow(), GuildModel::FromSheet(), fx);
    if (fx.missionProgress || !fx.missionsDone.empty())
        ApplyEffects(it->second, fx);
}

// ─────────────────────────────────────────────────────────────────────────────
// 보내기
// ─────────────────────────────────────────────────────────────────────────────
void CGuildService::SendOverview(const SessionWeakPtr& session, const Guild* g, int playerId) const
{
    GUILD_EX_OVERVIEW_INF_PACKET p;
    Service::Init(p, PacketType::GUILD_EX_INF);
    p.kind = static_cast<UInt8>(GuildExInfKind::Overview);
    if (g == nullptr)
    {
        Service::SendTo(session, p);
        return;
    }
    const GuildProgress& gp = g->grow.p;
    const GuildGrowth::ConstRow& c = GuildGrowth::Const();
    const auto levels = GuildGrowth::Levels();
    const Int64 now = Service::UnixNow();
    p.guildId = g->id;
    p.level = gp.level;
    if (const GuildGrowth::LevelRow* r = GuildGrowth::LevelRowOf(levels, gp.level))
    {
        p.frame = r->frame;
        p.expLevel = r->exp;
    }
    if (const GuildGrowth::LevelRow* n = GuildGrowth::LevelRowOf(levels, static_cast<uint8_t>(gp.level + 1)))
        p.expNext = n->exp;
    p.title = gp.title;
    p.members = static_cast<UInt8>(std::min<size_t>(g->members.size(), 255));
    p.maxMembers = MaxMembers(*g);
    p.recruiting = gp.recruiting;
    p.recruitMinLevel = gp.recruitMinLevel;
    p.exp = gp.exp;
    p.seals = g->grow.seals;
    GuildModel::PermsArray(gp, c, p.perms);
    p.weekId = gp.weekId;
    p.weekLeft = GuildGrowth::PeriodLeft(now, c.weekPeriodSec, c.weekOffsetSec);
    p.dayLeft = GuildGrowth::PeriodLeft(now, c.dayPeriodSec, c.dayOffsetSec);
    p.attendToday = gp.attendCount;
    p.attendTier = gp.attendTier;
    for (const auto& a : GuildGrowth::AttendTiers())
        if (a.tier == gp.attendTier + 1) p.attendNext = a.members;
    p.attendActiveMin = c.attendActiveMin;
    p.expCapDaily = c.memberDailyExpCap;
    const GuildModel::Tables t = GuildModel::FromSheet();
    p.titlesUnlocked = GuildModel::TitlesUnlocked(gp, t);
    p.achDone = static_cast<UInt8>(std::min<size_t>(gp.achievements.size(), 255));
    p.achTotal = static_cast<UInt8>(std::min<size_t>(GuildGrowth::Achievements().size(), 255));
    p.missionsDone = static_cast<UInt16>(std::min<uint32_t>(gp.missionsDone, 0xFFFF));
    p.kills = gp.kills;
    p.bossKills = gp.bossKills;
    p.coopKills = gp.coopKills;
    p.ageDays = static_cast<UInt16>(std::min<uint32_t>(GuildModel::AchProgress(gp, GuildAchKind::Days, now, g->grow.createdAt), 0xFFFF));
    p.attendDays = static_cast<UInt16>(std::min<uint32_t>(gp.attendDays, 0xFFFF));
    for (const auto& m : g->members)
    {
        if (m.playerId == playerId) p.myRank = m.rank;
        if (m.playerId == gp.claimantId) Copy(p.claimant, sizeof(p.claimant), m.name);
    }
    if (const GuildMemberProgress* me = GuildModel::Member(gp, playerId))
    {
        p.myContribWeek = me->contribWeek;
        p.myContribTotal = me->contribTotal;
        p.myJoinedAt = me->joinedAt;
        p.myExpToday = me->expToday;
        p.myActiveToday = me->activeToday;
        p.myAttended = me->attendedToday;
    }
    if (gp.claimantId != 0 && static_cast<Int64>(gp.claimEndsAt) > now)
        p.claimLeft = static_cast<UInt32>(gp.claimEndsAt - now);
    p.claimIn = 0xFFFFFFFFu;
    for (const auto& m : g->members)
        if (m.playerId == g->leaderId && !m.online && m.lastSeenMin > 0)
        {
            const Int64 absent = now - static_cast<Int64>(m.lastSeenMin) * 60;
            p.claimIn = absent >= static_cast<Int64>(c.leaderAbsentSec) ? 0u : static_cast<UInt32>(c.leaderAbsentSec - absent);
        }
    Service::SendTo(session, p);
}

void CGuildService::SendMissions(const Service::Ctx& ctx, const Guild& g) const
{
    const GuildProgress& gp = g.grow.p;
    const GuildGrowth::ConstRow& c = GuildGrowth::Const();
    GUILD_EX_MISSIONS_INF_PACKET p;
    Service::Init(p, PacketType::GUILD_EX_INF);
    p.kind = static_cast<UInt8>(GuildExInfKind::Missions);
    p.weekId = gp.weekId;
    p.weekLeft = GuildGrowth::PeriodLeft(Service::UnixNow(), c.weekPeriodSec, c.weekOffsetSec);
    p.bonusMembers = c.missionBonusMembers;
    p.bonusPct = c.missionBonusPct;
    const GuildMemberProgress* me = GuildModel::Member(gp, ctx.playerId);
    for (const auto& mp : gp.missions)
    {
        if (p.count >= GuildGrowth::kMissionSlotsMax) break;
        const GuildGrowth::MissionRow* row = GuildGrowth::FindMission(mp.id);
        if (row == nullptr) continue;
        GuildMissionRow& r = p.rows[p.count++];
        r.id = row->id;
        r.kind = static_cast<UInt8>(row->kind);
        r.done = mp.done;
        r.target = row->target;
        r.progress = mp.progress;
        r.sub = row->sub;
        r.exp = row->exp;
        r.seals = row->seals;
        r.contrib = row->contrib;
        r.contributors = static_cast<UInt8>(std::min<size_t>(mp.who.size(), 255));
        r.bonus = (c.missionBonusMembers > 0 && mp.who.size() >= c.missionBonusMembers) ? 1 : 0;
        if (row->kind == GuildMissionKind::Active)
            r.mine = me != nullptr ? me->activeWeek : 0;
        else
            for (const auto& [pid, n] : mp.who)
                if (pid == ctx.playerId) r.mine = n;
        Copy(r.name, sizeof(r.name), row->name);
    }
    Service::SendTo(ctx.session, p);
}

void CGuildService::SendStats(const Service::Ctx& ctx, const Guild& g, UInt8 page, UInt8 sort) const
{
    const GuildProgress& gp = g.grow.p;
    struct RowRef { const Member* m; const GuildMemberProgress* p; };
    std::vector<RowRef> order;
    order.reserve(g.members.size());
    for (const auto& m : g.members)
        order.push_back({ &m, GuildModel::Member(gp, m.playerId) });
    std::sort(order.begin(), order.end(), [sort](const RowRef& a, const RowRef& b)
    {
        const GuildMemberProgress z{};
        const GuildMemberProgress& x = a.p ? *a.p : z;
        const GuildMemberProgress& y = b.p ? *b.p : z;
        switch (sort)
        {
        case 1: if (x.contribTotal != y.contribTotal) return x.contribTotal > y.contribTotal; break;
        case 2: if (x.joinedAt != y.joinedAt) return x.joinedAt < y.joinedAt; break;
        case 3: if (x.activeWeek != y.activeWeek) return x.activeWeek > y.activeWeek; break;
        default: if (x.contribWeek != y.contribWeek) return x.contribWeek > y.contribWeek; break;
        }
        return a.m->name < b.m->name;
    });
    const int per = GuildGrowth::kStatsPerPage;
    GUILD_EX_MEMBER_STATS_INF_PACKET p;
    Service::Init(p, PacketType::GUILD_EX_INF);
    p.kind = static_cast<UInt8>(GuildExInfKind::MemberStats);
    p.sort = sort;
    p.pages = Pages(order.size(), per);
    p.page = std::min<UInt8>(page, static_cast<UInt8>(p.pages - 1));
    const Int64 now = Service::UnixNow();
    const uint8_t newDays = GuildGrowth::Const().coopNewDays;
    for (size_t i = static_cast<size_t>(p.page) * per; i < order.size() && p.count < per; ++i)
    {
        GuildStatRow& r = p.rows[p.count++];
        Copy(r.name, sizeof(r.name), order[i].m->name);
        if (const GuildMemberProgress* mp = order[i].p)
        {
            r.joinedDay = static_cast<UInt16>(mp->joinedAt / 86400);
            r.contribWeek = mp->contribWeek;
            r.contribTotal = mp->contribTotal;
            r.activeWeek = mp->activeWeek;
            r.flags = static_cast<UInt8>((mp->attendedToday ? 1 : 0) | (GuildGrowth::IsNewMember(mp->joinedAt, now, newDays) ? 2 : 0));
        }
    }
    Service::SendTo(ctx.session, p);
}

void CGuildService::SendLog(const Service::Ctx& ctx, const Guild& g, UInt8 page, UInt8 filter) const
{
    std::vector<const DB_GUILD_LOG_ROW*> rows;
    rows.reserve(g.grow.logs.size());
    for (auto it = g.grow.logs.rbegin(); it != g.grow.logs.rend(); ++it)
        if (LogMatches(it->kind, filter)) rows.push_back(&*it);
    const int per = GuildGrowth::kLogPerPage;
    GUILD_EX_LOG_INF_PACKET p;
    Service::Init(p, PacketType::GUILD_EX_INF);
    p.kind = static_cast<UInt8>(GuildExInfKind::Log);
    p.filter = filter;
    p.pages = Pages(rows.size(), per);
    p.page = std::min<UInt8>(page, static_cast<UInt8>(p.pages - 1));
    for (size_t i = static_cast<size_t>(p.page) * per; i < rows.size() && p.count < per; ++i)
    {
        GuildLogRow& r = p.rows[p.count++];
        r.at = Now32(rows[i]->at);
        r.kind = rows[i]->kind;
        r.value = rows[i]->value;
        Copy(r.who, sizeof(r.who), rows[i]->who);
        Copy(r.what, sizeof(r.what), rows[i]->what);
    }
    Service::SendTo(ctx.session, p);
}

void CGuildService::SendBoard(const Service::Ctx& ctx, const Guild& g, UInt8 page, UInt8 category) const
{
    std::vector<const DB_GUILD_POST_ROW*> rows;
    for (const auto& post : g.grow.posts)
        if (category == 0xFF || post.category == category) rows.push_back(&post);
    std::sort(rows.begin(), rows.end(), [](const DB_GUILD_POST_ROW* a, const DB_GUILD_POST_ROW* b)
    {
        if (a->pinned != b->pinned) return a->pinned > b->pinned;
        return a->postId > b->postId;
    });
    const int per = GuildGrowth::kBoardPerPage;
    GUILD_EX_BOARD_INF_PACKET p;
    Service::Init(p, PacketType::GUILD_EX_INF);
    p.kind = static_cast<UInt8>(GuildExInfKind::Board);
    p.category = category;
    p.pages = Pages(rows.size(), per);
    p.page = std::min<UInt8>(page, static_cast<UInt8>(p.pages - 1));
    for (size_t i = static_cast<size_t>(p.page) * per; i < rows.size() && p.count < per; ++i)
    {
        GuildPostRow& r = p.rows[p.count++];
        r.postId = static_cast<UInt32>(rows[i]->postId);
        r.at = Now32(rows[i]->at);
        r.category = rows[i]->category;
        r.pinned = rows[i]->pinned;
        Copy(r.author, sizeof(r.author), rows[i]->author);
        Copy(r.title, sizeof(r.title), rows[i]->title);
    }
    Service::SendTo(ctx.session, p);
}

void CGuildService::SendPost(const Service::Ctx& ctx, const Guild& g, UInt32 postId) const
{
    GUILD_EX_POST_INF_PACKET p;
    Service::Init(p, PacketType::GUILD_EX_INF);
    p.kind = static_cast<UInt8>(GuildExInfKind::Post);
    for (const auto& post : g.grow.posts)
    {
        if (post.postId != static_cast<Int64>(postId)) continue;
        p.postId = postId;
        p.at = Now32(post.at);
        p.category = post.category;
        p.pinned = post.pinned;
        UInt8 rank = static_cast<UInt8>(Rank::Member);
        for (const auto& m : g.members)
            if (m.playerId == ctx.playerId) rank = m.rank;
        p.canDelete = (post.authorId == ctx.playerId || HasPerm(g, rank, GuildPerm::Board)) ? 1 : 0;
        Copy(p.author, sizeof(p.author), post.author);
        Copy(p.title, sizeof(p.title), post.title);
        Copy(p.body, sizeof(p.body), post.body);
        break;
    }
    Service::SendTo(ctx.session, p);
}

void CGuildService::SendPublic(const Service::Ctx& ctx, UInt32 guildId) const
{
    GUILD_EX_PUBLIC_INF_PACKET p;
    Service::Init(p, PacketType::GUILD_EX_INF);
    p.kind = static_cast<UInt8>(GuildExInfKind::Public);
    if (const auto it = m_Guilds.find(guildId); it != m_Guilds.end())
    {
        const Guild& g = it->second;
        const GuildProgress& gp = g.grow.p;
        p.guildId = g.id;
        p.emblem = g.emblem;
        p.level = gp.level;
        if (const GuildGrowth::LevelRow* r = GuildGrowth::LevelRowOf(GuildGrowth::Levels(), gp.level)) p.frame = r->frame;
        p.title = gp.title;
        p.members = static_cast<UInt8>(std::min<size_t>(g.members.size(), 255));
        p.maxMembers = MaxMembers(g);
        p.recruiting = gp.recruiting;
        p.minLevel = gp.recruitMinLevel;
        const auto ap = m_ApplyOf.find(ctx.playerId);
        p.applied = (ap != m_ApplyOf.end() && ap->second == g.id) ? 1 : 0;
        Copy(p.name, sizeof(p.name), g.name);
        for (const auto& m : g.members)
            if (m.playerId == g.leaderId) Copy(p.leader, sizeof(p.leader), m.name);
        Copy(p.titleName, sizeof(p.titleName), TitleName(gp.title));
        Copy(p.intro, sizeof(p.intro), gp.intro);
    }
    Service::SendTo(ctx.session, p);
}

void CGuildService::SendRecruit(const Service::Ctx& ctx, UInt8 page) const
{
    std::vector<const Guild*> rows;
    for (const auto& [id, g] : m_Guilds)
        if (g.grow.p.recruiting) rows.push_back(&g);
    std::sort(rows.begin(), rows.end(), [](const Guild* a, const Guild* b)
    {
        if (a->grow.p.level != b->grow.p.level) return a->grow.p.level > b->grow.p.level;
        if (a->members.size() != b->members.size()) return a->members.size() > b->members.size();
        return a->id < b->id;
    });
    const int per = GuildGrowth::kRecruitPerPage;
    GUILD_EX_RECRUIT_INF_PACKET p;
    Service::Init(p, PacketType::GUILD_EX_INF);
    p.kind = static_cast<UInt8>(GuildExInfKind::Recruit);
    p.pages = Pages(rows.size(), per);
    p.page = std::min<UInt8>(page, static_cast<UInt8>(p.pages - 1));
    const auto ap = m_ApplyOf.find(ctx.playerId);
    for (size_t i = static_cast<size_t>(p.page) * per; i < rows.size() && p.count < per; ++i)
    {
        const Guild& g = *rows[i];
        GuildRecruitRow& r = p.rows[p.count++];
        r.guildId = g.id;
        r.emblem = g.emblem;
        r.level = g.grow.p.level;
        if (const GuildGrowth::LevelRow* lr = GuildGrowth::LevelRowOf(GuildGrowth::Levels(), g.grow.p.level)) r.frame = lr->frame;
        r.members = static_cast<UInt8>(std::min<size_t>(g.members.size(), 255));
        r.maxMembers = MaxMembers(g);
        r.minLevel = g.grow.p.recruitMinLevel;
        r.applied = (ap != m_ApplyOf.end() && ap->second == g.id) ? 1 : 0;
        Copy(r.name, sizeof(r.name), g.name);
    }
    Service::SendTo(ctx.session, p);
}

void CGuildService::SendApplicants(const Service::Ctx& ctx, const Guild& g) const
{
    GUILD_EX_APPLICANTS_INF_PACKET p;
    Service::Init(p, PacketType::GUILD_EX_INF);
    p.kind = static_cast<UInt8>(GuildExInfKind::Applicants);
    for (const auto& a : g.grow.applicants)
    {
        if (p.count >= GuildGrowth::kApplicantsMax) break;
        GuildApplicantRow& r = p.rows[p.count++];
        Copy(r.name, sizeof(r.name), a.ctx.name);
        r.level = a.ctx.level;
        r.classId = a.ctx.classId;
        r.at = a.at;
    }
    Service::SendTo(ctx.session, p);
}

void CGuildService::SendShop(const Service::Ctx& ctx, const Guild& g, UInt8 page) const
{
    const auto shop = GuildGrowth::Shop();
    const int per = GuildGrowth::kShopPerPage;
    GUILD_EX_SHOP_INF_PACKET p;
    Service::Init(p, PacketType::GUILD_EX_INF);
    p.kind = static_cast<UInt8>(GuildExInfKind::Shop);
    p.seals = g.grow.seals;
    p.pages = Pages(shop.size(), per);
    p.page = std::min<UInt8>(page, static_cast<UInt8>(p.pages - 1));
    const GuildMemberProgress* me = GuildModel::Member(g.grow.p, ctx.playerId);
    for (size_t i = static_cast<size_t>(p.page) * per; i < shop.size() && p.count < per; ++i)
    {
        GuildShopRow& r = p.rows[p.count++];
        r.id = shop[i].id;
        r.itemId = shop[i].itemId;
        r.count = shop[i].count;
        r.price = shop[i].price;
        r.minLevel = shop[i].minLevel;
        r.weeklyLimit = shop[i].weeklyLimit;
        r.bought = me != nullptr ? GuildModel::BoughtThisWeek(*me, shop[i].id) : 0;
    }
    Service::SendTo(ctx.session, p);
}

void CGuildService::SendAch(const Service::Ctx& ctx, const Guild& g, UInt8 page) const
{
    const auto achs = GuildGrowth::Achievements();
    const int per = GuildGrowth::kAchPerPage;
    const Int64 now = Service::UnixNow();
    GUILD_EX_ACH_INF_PACKET p;
    Service::Init(p, PacketType::GUILD_EX_INF);
    p.kind = static_cast<UInt8>(GuildExInfKind::Achievements);
    p.pages = Pages(achs.size(), per);
    p.page = std::min<UInt8>(page, static_cast<UInt8>(p.pages - 1));
    for (size_t i = static_cast<size_t>(p.page) * per; i < achs.size() && p.count < per; ++i)
    {
        const GuildGrowth::AchRow& a = achs[i];
        GuildAchRow& r = p.rows[p.count++];
        r.id = a.id;
        r.kind = static_cast<UInt8>(a.kind);
        r.target = a.target;
        r.progress = std::min(GuildModel::AchProgress(g.grow.p, a.kind, now, g.grow.createdAt), a.target);
        r.seals = a.seals;
        r.exp = a.exp;
        r.titleId = a.titleId;
        for (const auto& [id, at] : g.grow.p.achievements)
            if (id == a.id) { r.done = 1; r.at = at; r.progress = a.target; }
        Copy(r.name, sizeof(r.name), a.name);
    }
    Service::SendTo(ctx.session, p);
}

void CGuildService::SendTitles(const Service::Ctx& ctx, const Guild& g, UInt8 page) const
{
    const auto titles = GuildGrowth::Titles();
    const int per = GuildGrowth::kTitlesPerPage;
    const uint32_t bits = GuildModel::TitlesUnlocked(g.grow.p, GuildModel::FromSheet());
    GUILD_EX_TITLES_INF_PACKET p;
    Service::Init(p, PacketType::GUILD_EX_INF);
    p.kind = static_cast<UInt8>(GuildExInfKind::Titles);
    p.active = g.grow.p.title;
    p.pages = Pages(titles.size(), per);
    p.page = std::min<UInt8>(page, static_cast<UInt8>(p.pages - 1));
    for (size_t i = static_cast<size_t>(p.page) * per; i < titles.size() && p.count < per; ++i)
    {
        GuildTitleRow& r = p.rows[p.count++];
        r.id = titles[i].id;
        r.unlocked = (titles[i].id <= GuildGrowth::kTitleIdMax && (bits & (1u << titles[i].id))) ? 1 : 0;
        Copy(r.name, sizeof(r.name), titles[i].name);
    }
    Service::SendTo(ctx.session, p);
}

// ─────────────────────────────────────────────────────────────────────────────
// 요청(서비스 스레드)
// ─────────────────────────────────────────────────────────────────────────────
void CGuildService::HandleEx(const Service::Ctx& ctx, const GUILD_EX_REQ_PACKET& p)
{
    const auto op = static_cast<GuildExOp>(p.op);
    char target[NAME_SIZE + 1]{};
    ::memcpy(target, p.target, NAME_SIZE);
    char title[sizeof(p.title) + 1]{};
    ::memcpy(title, p.title, sizeof(p.title));
    char body[sizeof(p.body) + 1]{};
    ::memcpy(body, p.body, sizeof(p.body));

    // 결사가 없어도 되는 것
    switch (op)
    {
    case GuildExOp::RecruitList: return SendRecruit(ctx, p.arg8);
    case GuildExOp::RecruitView: return SendPublic(ctx, p.arg32);
    case GuildExOp::Apply:       return Apply(ctx, p.seq, p.arg32);
    case GuildExOp::ApplyCancel: return ApplyCancel(ctx, p.seq);
    case GuildExOp::Overview:    return SendOverview(ctx.session, GuildOf(ctx.playerId), ctx.playerId);
    default: break;
    }
    const Guild* g = GuildOf(ctx.playerId);
    if (g == nullptr)
        return AckEx(ctx, op, GuildExResult::NotInGuild, p.seq);
    switch (op)
    {
    case GuildExOp::Missions:     SendMissions(ctx, *g); break;
    case GuildExOp::MemberStats:  SendStats(ctx, *g, p.arg8, static_cast<UInt8>(p.arg32)); break;
    case GuildExOp::Log:          SendLog(ctx, *g, p.arg8, static_cast<UInt8>(p.arg32)); break;
    case GuildExOp::Board:        SendBoard(ctx, *g, p.arg8, static_cast<UInt8>(p.arg32)); break;
    case GuildExOp::PostRead:     SendPost(ctx, *g, p.arg32); break;
    case GuildExOp::Applicants:
    {
        const Member* me = FindMember(const_cast<Guild&>(*g), ctx.playerId);
        if (me == nullptr || !HasPerm(*g, me->rank, GuildPerm::Approve))
            return AckEx(ctx, op, GuildExResult::NoPermission, p.seq);
        SendApplicants(ctx, *g);
        break;
    }
    case GuildExOp::Shop:         SendShop(ctx, *g, p.arg8); break;
    case GuildExOp::Achievements: SendAch(ctx, *g, p.arg8); break;
    case GuildExOp::Titles:       SendTitles(ctx, *g, p.arg8); break;
    case GuildExOp::PostWrite:    PostWrite(ctx, p.seq, p.arg8, title, body); break;
    case GuildExOp::PostDelete:   PostDelete(ctx, p.seq, p.arg32); break;
    case GuildExOp::PostPin:      PostPin(ctx, p.seq, p.arg32, p.arg8 != 0); break;
    case GuildExOp::Intro:        SetIntro(ctx, p.seq, body, p.arg8 != 0, static_cast<UInt8>(std::clamp<uint32_t>(p.arg32, 1, 255))); break;
    case GuildExOp::ApplyReply:   ApplyReply(ctx, p.seq, target, p.arg8 != 0); break;
    case GuildExOp::Perms:        SetPerms(ctx, p.seq, p.arg8, static_cast<UInt8>(p.arg32)); break;
    case GuildExOp::Title:        SetTitle(ctx, p.seq, p.arg8); break;
    case GuildExOp::Buy:          Buy(ctx, p.seq, static_cast<UInt16>(p.arg32)); break;
    case GuildExOp::Claim:        Claim(ctx, p.seq); break;
    default:                      AckEx(ctx, op, GuildExResult::Invalid, p.seq); break;
    }
}

void CGuildService::PostWrite(const Service::Ctx& ctx, UInt32 seq, UInt8 category, const std::string& title, const std::string& body)
{
    Guild* g = GuildOf(ctx.playerId);
    const Member* me = g != nullptr ? FindMember(*g, ctx.playerId) : nullptr;
    if (me == nullptr)
        return AckEx(ctx, GuildExOp::PostWrite, GuildExResult::NotInGuild, seq);
    if (category >= static_cast<UInt8>(GuildPostCategory::Count))
        return AckEx(ctx, GuildExOp::PostWrite, GuildExResult::Invalid, seq);
    // 공지 분류는 공지 권한(결사장이 공지를 쓰고 결사원이 본다)
    if (category == static_cast<UInt8>(GuildPostCategory::Notice) && !HasPerm(*g, me->rank, GuildPerm::Notice))
        return AckEx(ctx, GuildExOp::PostWrite, GuildExResult::NoPermission, seq);
    Growth& gr = g->grow;
    // 한도: 넘으면 고정하지 않은 가장 오래된 글을 같은 트랜잭션에서 내린다(모두 고정이면 거절)
    Int64 drop = 0;
    const size_t limit = std::max<size_t>(1, GuildGrowth::Const().postMax);
    if (gr.posts.size() + static_cast<size_t>(gr.pendingPosts) >= limit)
    {
        for (const auto& post : gr.posts)
            if (!post.pinned && (drop == 0 || post.postId < drop)) drop = post.postId;
        if (drop == 0 || gr.pendingPosts > 0)
            return AckEx(ctx, GuildExOp::PostWrite, GuildExResult::TooMany, seq);
    }
    DB_GUILD_POST_ROW row;
    row.postId = m_NextPostId++;
    row.guildId = static_cast<int>(g->id);
    row.authorId = ctx.playerId;
    row.author = ctx.name;
    row.category = category;
    row.title = title;
    row.body = body;
    row.at = Service::UnixNow();
    ++gr.pendingPosts;
    const UInt32 guildId = g->id;
    GDBThread->RequestTxKeyed(guildId, "GuildPost",
        [row, drop](CDBConnection& c) { return c.GuildPostAdd(row, drop); },
        [ctx, seq, guildId, row, drop](bool ok)
        {
            GGuildService->Post([ctx, seq, guildId, row, drop, ok]()
            {
                auto& svc = *GGuildService;
                const auto it = svc.m_Guilds.find(guildId);
                if (it == svc.m_Guilds.end())
                    return svc.AckEx(ctx, GuildExOp::PostWrite, GuildExResult::Invalid, seq);
                Growth& gg = it->second.grow;
                gg.pendingPosts = std::max(0, gg.pendingPosts - 1);
                if (!ok)
                    return svc.AckEx(ctx, GuildExOp::PostWrite, GuildExResult::Invalid, seq);
                if (drop != 0)
                    gg.posts.erase(std::remove_if(gg.posts.begin(), gg.posts.end(), [&](const DB_GUILD_POST_ROW& p) { return p.postId == drop; }), gg.posts.end());
                gg.posts.push_back(row);
                svc.AckEx(ctx, GuildExOp::PostWrite, GuildExResult::Ok, seq, static_cast<UInt32>(row.postId));
                svc.NotifyEx(it->second, GuildExEvent::Posted, row.category, static_cast<Int32>(row.postId), row.author, row.title, 0, ctx.playerId);
            });
        });
}

void CGuildService::PostDelete(const Service::Ctx& ctx, UInt32 seq, UInt32 postId)
{
    Guild* g = GuildOf(ctx.playerId);
    const Member* me = g != nullptr ? FindMember(*g, ctx.playerId) : nullptr;
    if (me == nullptr)
        return AckEx(ctx, GuildExOp::PostDelete, GuildExResult::NotInGuild, seq);
    const auto it = std::find_if(g->grow.posts.begin(), g->grow.posts.end(), [&](const DB_GUILD_POST_ROW& p) { return p.postId == static_cast<Int64>(postId); });
    if (it == g->grow.posts.end())
        return AckEx(ctx, GuildExOp::PostDelete, GuildExResult::NotFound, seq);
    if (it->authorId != ctx.playerId && !HasPerm(*g, me->rank, GuildPerm::Board))
        return AckEx(ctx, GuildExOp::PostDelete, GuildExResult::NoPermission, seq);
    const UInt32 guildId = g->id;
    GDBThread->RequestTxKeyed(guildId, "GuildPostDelete",
        [guildId, postId](CDBConnection& c) { return c.GuildPostDelete(static_cast<int>(guildId), postId); },
        [ctx, seq, guildId, postId](bool ok)
        {
            GGuildService->Post([ctx, seq, guildId, postId, ok]()
            {
                auto& svc = *GGuildService;
                const auto it = svc.m_Guilds.find(guildId);
                if (it == svc.m_Guilds.end() || !ok)
                    return svc.AckEx(ctx, GuildExOp::PostDelete, GuildExResult::NotFound, seq);
                auto& posts = it->second.grow.posts;
                posts.erase(std::remove_if(posts.begin(), posts.end(), [&](const DB_GUILD_POST_ROW& p) { return p.postId == static_cast<Int64>(postId); }), posts.end());
                svc.AckEx(ctx, GuildExOp::PostDelete, GuildExResult::Ok, seq, postId);
            });
        });
}

void CGuildService::PostPin(const Service::Ctx& ctx, UInt32 seq, UInt32 postId, bool pin)
{
    Guild* g = GuildOf(ctx.playerId);
    const Member* me = g != nullptr ? FindMember(*g, ctx.playerId) : nullptr;
    if (me == nullptr)
        return AckEx(ctx, GuildExOp::PostPin, GuildExResult::NotInGuild, seq);
    if (!HasPerm(*g, me->rank, GuildPerm::Board))
        return AckEx(ctx, GuildExOp::PostPin, GuildExResult::NoPermission, seq);
    const UInt32 guildId = g->id;
    GDBThread->RequestTxKeyed(guildId, "GuildPostPin",
        [guildId, postId, pin](CDBConnection& c) { return c.GuildPostPin(static_cast<int>(guildId), postId, pin); },
        [ctx, seq, guildId, postId, pin](bool ok)
        {
            GGuildService->Post([ctx, seq, guildId, postId, pin, ok]()
            {
                auto& svc = *GGuildService;
                const auto it = svc.m_Guilds.find(guildId);
                if (it == svc.m_Guilds.end() || !ok)
                    return svc.AckEx(ctx, GuildExOp::PostPin, GuildExResult::NotFound, seq);
                for (auto& p : it->second.grow.posts)
                    if (p.postId == static_cast<Int64>(postId)) p.pinned = pin ? 1 : 0;
                svc.AckEx(ctx, GuildExOp::PostPin, GuildExResult::Ok, seq, postId);
            });
        });
}

void CGuildService::SetIntro(const Service::Ctx& ctx, UInt32 seq, const std::string& text, bool recruiting, UInt8 minLevel)
{
    Guild* g = GuildOf(ctx.playerId);
    const Member* me = g != nullptr ? FindMember(*g, ctx.playerId) : nullptr;
    if (me == nullptr)
        return AckEx(ctx, GuildExOp::Intro, GuildExResult::NotInGuild, seq);
    if (!HasPerm(*g, me->rank, GuildPerm::Notice))
        return AckEx(ctx, GuildExOp::Intro, GuildExResult::NoPermission, seq);
    GuildProgress& gp = g->grow.p;
    gp.intro = text;
    gp.recruiting = recruiting ? 1 : 0;
    gp.recruitMinLevel = std::max<UInt8>(1, minLevel);
    MarkDirty(*g, true);
    AddLog(*g, GuildLogKind::Intro, gp.recruiting, ctx.name, "");
    AckEx(ctx, GuildExOp::Intro, GuildExResult::Ok, seq);
    NotifyEx(*g, GuildExEvent::IntroChanged, gp.recruiting, gp.recruitMinLevel, ctx.name, "");
}

void CGuildService::Apply(const Service::Ctx& ctx, UInt32 seq, UInt32 guildId)
{
    if (m_GuildOf.count(ctx.playerId))
        return AckEx(ctx, GuildExOp::Apply, GuildExResult::InGuild, seq);
    const auto it = m_Guilds.find(guildId);
    if (it == m_Guilds.end())
        return AckEx(ctx, GuildExOp::Apply, GuildExResult::NotFound, seq);
    Guild& g = it->second;
    if (!g.grow.p.recruiting)
        return AckEx(ctx, GuildExOp::Apply, GuildExResult::NotRecruiting, seq);
    if (ctx.level < g.grow.p.recruitMinLevel)
        return AckEx(ctx, GuildExOp::Apply, GuildExResult::LevelTooLow, seq, g.grow.p.recruitMinLevel);
    if (static_cast<int>(g.members.size()) + g.pendingJoins >= MaxMembers(g))
        return AckEx(ctx, GuildExOp::Apply, GuildExResult::Full, seq);
    if (const auto ap = m_ApplyOf.find(ctx.playerId); ap != m_ApplyOf.end())
        return AckEx(ctx, GuildExOp::Apply, GuildExResult::AlreadyApplied, seq, ap->second);
    if (g.grow.applicants.size() >= std::max<size_t>(1, GuildGrowth::Const().applyMax))
        return AckEx(ctx, GuildExOp::Apply, GuildExResult::TooMany, seq);
    g.grow.applicants.push_back({ ctx, Now32(Service::UnixNow()) });
    m_ApplyOf[ctx.playerId] = guildId;
    AckEx(ctx, GuildExOp::Apply, GuildExResult::Ok, seq, guildId);
    NotifyEx(g, GuildExEvent::Applied, ctx.level, ctx.classId, ctx.name, "", static_cast<UInt8>(GuildPerm::Approve));
}

void CGuildService::ApplyCancel(const Service::Ctx& ctx, UInt32 seq)
{
    if (!m_ApplyOf.count(ctx.playerId))
        return AckEx(ctx, GuildExOp::ApplyCancel, GuildExResult::NotFound, seq);
    WithdrawApply(ctx.playerId);
    AckEx(ctx, GuildExOp::ApplyCancel, GuildExResult::Ok, seq);
}

void CGuildService::ApplyReply(const Service::Ctx& ctx, UInt32 seq, const std::string& target, bool accept)
{
    Guild* g = GuildOf(ctx.playerId);
    const Member* me = g != nullptr ? FindMember(*g, ctx.playerId) : nullptr;
    if (me == nullptr)
        return AckEx(ctx, GuildExOp::ApplyReply, GuildExResult::NotInGuild, seq);
    if (!HasPerm(*g, me->rank, GuildPerm::Approve))
        return AckEx(ctx, GuildExOp::ApplyReply, GuildExResult::NoPermission, seq);
    auto& list = g->grow.applicants;
    const std::string key = NameKey(target);
    const auto ai = std::find_if(list.begin(), list.end(), [&](const Applicant& a) { return NameKey(a.ctx.name) == key; });
    if (ai == list.end())
        return AckEx(ctx, GuildExOp::ApplyReply, GuildExResult::NotFound, seq);
    const Applicant a = *ai;
    if (!accept)
    {
        list.erase(ai);
        m_ApplyOf.erase(a.ctx.playerId);
        NotifyOne(a.ctx.session, GuildExEvent::ApplyResult, 0, static_cast<Int32>(g->id), "", g->name);
        return AckEx(ctx, GuildExOp::ApplyReply, GuildExResult::Ok, seq);
    }
    if (m_GuildOf.count(a.ctx.playerId))
    {
        list.erase(ai);
        m_ApplyOf.erase(a.ctx.playerId);
        return AckEx(ctx, GuildExOp::ApplyReply, GuildExResult::InGuild, seq);
    }
    if (m_BusyPlayers.count(a.ctx.playerId))
        return AckEx(ctx, GuildExOp::ApplyReply, GuildExResult::Busy, seq);
    if (static_cast<int>(g->members.size()) + g->pendingJoins >= MaxMembers(*g))
        return AckEx(ctx, GuildExOp::ApplyReply, GuildExResult::Full, seq);
    list.erase(ai);
    m_ApplyOf.erase(a.ctx.playerId);
    ++g->pendingJoins;
    m_BusyPlayers.insert(a.ctx.playerId);
    const UInt32 guildId = g->id;
    const int pid = a.ctx.playerId;
    GDBThread->RequestTxKeyed(guildId, "GuildAccept",
        [guildId, pid](CDBConnection& c) { return c.GuildMemberSet(static_cast<int>(guildId), pid, static_cast<UInt8>(Rank::Member)); },
        [ctx, seq, guildId, a](bool ok)
        {
            GGuildService->Post([ctx, seq, guildId, a, ok]()
            {
                auto& svc = *GGuildService;
                svc.m_BusyPlayers.erase(a.ctx.playerId);
                const auto it = svc.m_Guilds.find(guildId);
                if (it == svc.m_Guilds.end())
                    return svc.AckEx(ctx, GuildExOp::ApplyReply, GuildExResult::Invalid, seq);
                Guild& gg = it->second;
                gg.pendingJoins = std::max(0, gg.pendingJoins - 1);
                if (!ok)
                    return svc.AckEx(ctx, GuildExOp::ApplyReply, GuildExResult::Invalid, seq);
                svc.AddMember(gg, a.ctx);
                svc.AddLog(gg, GuildLogKind::Accepted, 0, a.ctx.name, ctx.name);
                svc.NotifyOne(svc.CurrentCtx(a.ctx).session, GuildExEvent::ApplyResult, 1, static_cast<Int32>(gg.id), "", gg.name);
                svc.AckEx(ctx, GuildExOp::ApplyReply, GuildExResult::Ok, seq);
            });
        });
}

void CGuildService::SetPerms(const Service::Ctx& ctx, UInt32 seq, UInt8 rank, UInt8 mask)
{
    Guild* g = GuildOf(ctx.playerId);
    if (g == nullptr)
        return AckEx(ctx, GuildExOp::Perms, GuildExResult::NotInGuild, seq);
    if (g->leaderId != ctx.playerId)
        return AckEx(ctx, GuildExOp::Perms, GuildExResult::NoPermission, seq);
    if (rank < 1 || rank > 3)
        return AckEx(ctx, GuildExOp::Perms, GuildExResult::Invalid, seq);
    GuildProgress& gp = g->grow.p;
    if (!gp.permsSet)
    {
        uint8_t now[4]{};
        GuildModel::PermsArray(gp, GuildGrowth::Const(), now);
        for (int r = 1; r < 4; ++r) gp.perms[r] = now[r];
        gp.permsSet = 1;
    }
    gp.perms[rank] = static_cast<uint8_t>(mask & GuildGrowth::kAllPerms);
    MarkDirty(*g, true);
    AddLog(*g, GuildLogKind::Perms, (static_cast<Int32>(rank) << 8) | gp.perms[rank], ctx.name, "");
    AckEx(ctx, GuildExOp::Perms, GuildExResult::Ok, seq);
    NotifyEx(*g, GuildExEvent::PermsChanged, rank, gp.perms[rank], ctx.name, "");
    for (const auto& m : g->members)
        if (m.online) SendOverview(m.session, g, m.playerId);   // 할 수 있는 일이 바뀌었다
}

void CGuildService::SetTitle(const Service::Ctx& ctx, UInt32 seq, UInt8 titleId)
{
    Guild* g = GuildOf(ctx.playerId);
    if (g == nullptr)
        return AckEx(ctx, GuildExOp::Title, GuildExResult::NotInGuild, seq);
    if (g->leaderId != ctx.playerId)
        return AckEx(ctx, GuildExOp::Title, GuildExResult::NoPermission, seq);
    if (titleId != 0)
    {
        const uint32_t bits = GuildModel::TitlesUnlocked(g->grow.p, GuildModel::FromSheet());
        if (titleId > GuildGrowth::kTitleIdMax || !(bits & (1u << titleId)))
            return AckEx(ctx, GuildExOp::Title, GuildExResult::NotFound, seq);
    }
    g->grow.p.title = titleId;
    MarkDirty(*g, true);
    const std::string name = TitleName(titleId);
    AddLog(*g, GuildLogKind::Title, titleId, ctx.name, name);
    AckEx(ctx, GuildExOp::Title, GuildExResult::Ok, seq, titleId);
    NotifyEx(*g, GuildExEvent::TitleChanged, titleId, 0, ctx.name, name);
}

void CGuildService::Buy(const Service::Ctx& ctx, UInt32 seq, UInt16 shopId)
{
    Guild* g = GuildOf(ctx.playerId);
    const Member* me = g != nullptr ? FindMember(*g, ctx.playerId) : nullptr;
    if (me == nullptr)
        return AckEx(ctx, GuildExOp::Buy, GuildExResult::NotInGuild, seq);
    if (!HasPerm(*g, me->rank, GuildPerm::Shop))
        return AckEx(ctx, GuildExOp::Buy, GuildExResult::NoPermission, seq);
    const GuildGrowth::ShopRow* row = GuildGrowth::FindShop(shopId);
    if (row == nullptr)
        return AckEx(ctx, GuildExOp::Buy, GuildExResult::NotFound, seq);
    Growth& gr = g->grow;
    if (gr.p.level < row->minLevel)
        return AckEx(ctx, GuildExOp::Buy, GuildExResult::LevelTooLow, seq, row->minLevel);
    GuildMemberProgress& mp = GuildModel::EnsureMember(gr.p, ctx.playerId, Now32(Service::UnixNow()));
    if (row->weeklyLimit != 0 && GuildModel::BoughtThisWeek(mp, shopId) >= row->weeklyLimit)
        return AckEx(ctx, GuildExOp::Buy, GuildExResult::LimitReached, seq, row->weeklyLimit);
    if (gr.shopBusy.count(ctx.playerId))
        return AckEx(ctx, GuildExOp::Buy, GuildExResult::Busy, seq);
    if (static_cast<UInt64>(gr.sealsReserved) + row->price > gr.seals)
        return AckEx(ctx, GuildExOp::Buy, GuildExResult::NoSeals, seq, row->price);
    gr.sealsReserved += row->price;
    gr.shopBusy.insert(ctx.playerId);
    const UInt32 guildId = g->id;
    const UInt16 itemId = row->itemId, count = row->count;
    const UInt32 price = row->price;
    const Int64 at = Service::UnixNow();
    const std::string itemName = ItemName(itemId);
    Service::PostToOwner(ctx.obj, [ctx, seq, guildId, shopId, itemId, count, price, at, itemName](const UserSharedPtr& user)
    {
        auto abort = [=](GuildExResult r)
        {
            GGuildService->Post([=]()
            {
                auto& svc = *GGuildService;
                if (const auto it = svc.m_Guilds.find(guildId); it != svc.m_Guilds.end())
                {
                    it->second.grow.sealsReserved -= std::min(it->second.grow.sealsReserved, price);
                    it->second.grow.shopBusy.erase(ctx.playerId);
                }
                svc.AckEx(ctx, GuildExOp::Buy, r, seq);
            });
        };
        if (user == nullptr)
            return abort(GuildExResult::Invalid);
        auto inv = user->GetInventory();
        const UInt16 slot = inv->FindSlotForIncoming(itemId, count);
        if (slot >= MAX_INVENTORY_SLOTS)
            return abort(GuildExResult::InventoryFull);
        const ItemTableRow* row = ItemTable::Find(itemId);
        const CInventory::TakenItem item{ itemId, count, 0, row != nullptr ? ItemRules::OnAcquire(*row, 0) : 0 };   // 획득 귀속 장비면 bit 8
        DB_ITEM_SLOT_SAVE out;
        if (!inv->PlaceIncoming(slot, item, out))
            return abort(GuildExResult::InventoryFull);
        DB_GUILD_LOG_ROW log;
        log.guildId = static_cast<int>(guildId);
        log.at = at;
        log.kind = static_cast<UInt8>(GuildLogKind::SealsOut);
        log.value = static_cast<Int32>(price);
        log.who = ctx.name;
        log.what = itemName;
        const int pid = ctx.playerId;
        const ObjID obj = ctx.obj;
        GDBThread->RequestTx(pid, "GuildShopBuy",
            [pid, guildId, price, out, log](CDBConnection& c) { return c.GuildShopBuy(pid, static_cast<int>(guildId), price, out, log); },
            [=](bool ok)
            {
                if (!ok)
                    Service::PostToOwner(obj, [slot, item](const UserSharedPtr& u)
                    {
                        if (u != nullptr) u->GetInventory()->UndoIncoming(slot, item);
                    });
                GGuildService->Post([=]() { GGuildService->FinishBuy(ok, ctx, seq, guildId, shopId, price, at); });
            });
    });
}

void CGuildService::FinishBuy(bool ok, const Service::Ctx& ctx, UInt32 seq, UInt32 guildId, UInt16 shopId, UInt32 price, Int64 at)
{
    const auto it = m_Guilds.find(guildId);
    if (it == m_Guilds.end())
        return AckEx(ctx, GuildExOp::Buy, GuildExResult::Invalid, seq);
    Guild& g = it->second;
    Growth& gr = g.grow;
    gr.sealsReserved -= std::min(gr.sealsReserved, price);
    gr.shopBusy.erase(ctx.playerId);
    if (!ok)
        return AckEx(ctx, GuildExOp::Buy, GuildExResult::NoSeals, seq, price);
    gr.seals -= std::min(gr.seals, price);
    const GuildGrowth::ShopRow* row = GuildGrowth::FindShop(shopId);
    const UInt16 itemId = row != nullptr ? row->itemId : 0;
    const UInt16 count = row != nullptr ? row->count : 0;
    const std::string itemName = ItemName(itemId);
    if (GuildMemberProgress* mp = GuildModel::Member(gr.p, ctx.playerId))
        GuildModel::AddBought(*mp, shopId);
    else if (const auto d = gr.departed.find(ctx.playerId); d != gr.departed.end() && d->second.m.playerId == ctx.playerId)
        GuildModel::AddBought(d->second.m, shopId);   // 사는 사이 나갔다 — 다시 들어오면 이 구매도 주 한도에 센다
    (void)at;
    AddLog(g, GuildLogKind::SealsOut, static_cast<Int32>(price), ctx.name, itemName, false);   // DB 줄은 구매 트랜잭션이 넣었다
    MarkDirty(g);
    AckEx(ctx, GuildExOp::Buy, GuildExResult::Ok, seq, price);
    NotifyEx(g, GuildExEvent::Seals, -static_cast<Int32>(price), static_cast<Int32>(gr.seals), ctx.name, itemName);
    NotifyEx(g, GuildExEvent::Bought, itemId, count, ctx.name, itemName);
}

void CGuildService::Claim(const Service::Ctx& ctx, UInt32 seq)
{
    Guild* g = GuildOf(ctx.playerId);
    const Member* me = g != nullptr ? FindMember(*g, ctx.playerId) : nullptr;
    if (me == nullptr)
        return AckEx(ctx, GuildExOp::Claim, GuildExResult::NotInGuild, seq);
    if (g->leaderId == ctx.playerId)
        return AckEx(ctx, GuildExOp::Claim, GuildExResult::Invalid, seq);
    GuildProgress& gp = g->grow.p;
    const Int64 now = Service::UnixNow();
    if (gp.claimantId != 0)
        return AckEx(ctx, GuildExOp::Claim, GuildExResult::Busy, seq, gp.claimEndsAt > now ? static_cast<UInt32>(gp.claimEndsAt - now) : 0u);
    const Member* leader = FindMember(*g, g->leaderId);
    const GuildGrowth::ConstRow& c = GuildGrowth::Const();
    if (leader == nullptr || leader->online)
        return AckEx(ctx, GuildExOp::Claim, GuildExResult::NotYet, seq, 0xFFFFFFFFu);
    const Int64 lastSeen = static_cast<Int64>(leader->lastSeenMin) * 60;
    if (!GuildGrowth::LeaderAbsent(leader->online, lastSeen, now, c.leaderAbsentSec))
        return AckEx(ctx, GuildExOp::Claim, GuildExResult::NotYet, seq,
            lastSeen > 0 ? static_cast<UInt32>(std::max<Int64>(0, static_cast<Int64>(c.leaderAbsentSec) - (now - lastSeen))) : c.leaderAbsentSec);
    // 결사장을 뺀 가장 높은 직위만
    UInt8 best = static_cast<UInt8>(Rank::Count);
    for (const auto& m : g->members)
        if (m.playerId != g->leaderId) best = std::min(best, m.rank);
    if (me->rank != best)
        return AckEx(ctx, GuildExOp::Claim, GuildExResult::NoPermission, seq);
    gp.claimantId = ctx.playerId;
    gp.claimEndsAt = Now32(now + c.claimGraceSec);
    MarkDirty(*g, true);
    AddLog(*g, GuildLogKind::ClaimStart, static_cast<Int32>(c.claimGraceSec), ctx.name, "");
    AckEx(ctx, GuildExOp::Claim, GuildExResult::Ok, seq, c.claimGraceSec);
    NotifyEx(*g, GuildExEvent::ClaimStarted, static_cast<Int32>(c.claimGraceSec), 0, ctx.name, "");
    std::cout << "[GuildGrowth] claim guild=" << g->id << " by=" << ctx.playerId << " grace=" << c.claimGraceSec << "\n" << std::flush;
}

void CGuildService::CancelClaim(Guild& g, const std::string& by)
{
    if (g.grow.p.claimantId == 0) return;
    g.grow.p.claimantId = 0;
    g.grow.p.claimEndsAt = 0;
    MarkDirty(g, true);
    AddLog(g, GuildLogKind::ClaimCancel, 0, by, "");
    NotifyEx(g, GuildExEvent::ClaimCancelled, 0, 0, by, "");
}

void CGuildService::FinishClaim(Guild& g)
{
    GuildProgress& gp = g.grow.p;
    const Member* leader = FindMember(g, g.leaderId);
    const Member* claimant = FindMember(g, gp.claimantId);
    UInt8 best = static_cast<UInt8>(Rank::Count);
    for (const auto& m : g.members)
        if (m.playerId != g.leaderId) best = std::min(best, m.rank);
    if (leader == nullptr || claimant == nullptr || leader->online || claimant->rank != best)
        return CancelClaim(g, leader != nullptr ? leader->name : std::string());
    g.busy = true;
    g.grow.claimBusy = true;
    const UInt32 guildId = g.id;
    const int fromId = g.leaderId, toId = gp.claimantId;
    GDBThread->RequestTxKeyed(guildId, "GuildClaim",
        [guildId, fromId, toId](CDBConnection& c) { return c.GuildTransfer(static_cast<int>(guildId), fromId, toId); },
        [guildId, fromId, toId](bool ok)
        {
            GGuildService->Post([guildId, fromId, toId, ok]()
            {
                auto& svc = *GGuildService;
                const auto it = svc.m_Guilds.find(guildId);
                if (it == svc.m_Guilds.end()) return;
                Guild& gg = it->second;
                gg.busy = false;
                gg.grow.claimBusy = false;
                Member* from = FindMember(gg, fromId);
                Member* to = FindMember(gg, toId);
                if (!ok || from == nullptr || to == nullptr)
                    return svc.CancelClaim(gg, "");
                from->rank = static_cast<UInt8>(Rank::Officer);
                to->rank = static_cast<UInt8>(Rank::Leader);
                gg.leaderId = toId;
                gg.grow.p.claimantId = 0;
                gg.grow.p.claimEndsAt = 0;
                svc.MarkDirty(gg, true);
                svc.AddLog(gg, GuildLogKind::ClaimDone, 0, to->name, from->name);
                svc.NotifyEx(gg, GuildExEvent::ClaimDone, 0, 0, to->name, "");
                svc.NotifyMembers(gg, GuildEvent::LeaderChanged, to->name, from->name, static_cast<UInt8>(Rank::Leader));
                for (const auto& mm : gg.members)
                    if (mm.online)
                    {
                        svc.SendInfo(mm.session, &gg, mm.playerId);
                        svc.SendOverview(mm.session, &gg, mm.playerId);
                    }
                if (from->online) ApplyTag(from->obj, gg.id, gg.emblem, from->rank);
                if (to->online) ApplyTag(to->obj, gg.id, gg.emblem, to->rank);
                std::cout << "[GuildGrowth] claim done guild=" << guildId << " leader=" << toId << "\n" << std::flush;
            });
        });
}

// ─────────────────────────────────────────────────────────────────────────────
// CGuildHelper — 소유 Zone 스레드(검사 · 멱등 · 요청 간격 → 서비스 큐) · 사냥 훅
// ─────────────────────────────────────────────────────────────────────────────
namespace CGuildHelper
{
    namespace
    {
        [[nodiscard]] bool IsMutatingEx(GuildExOp op) noexcept
        {
            switch (op)
            {
            case GuildExOp::PostWrite: case GuildExOp::PostDelete: case GuildExOp::PostPin: case GuildExOp::Intro:
            case GuildExOp::Apply: case GuildExOp::ApplyCancel: case GuildExOp::ApplyReply: case GuildExOp::Perms:
            case GuildExOp::Title: case GuildExOp::Buy: case GuildExOp::Claim:
                return true;
            default:
                return false;
            }
        }
        void ZoneAckEx(const UserSharedPtr& user, GuildExOp op, GuildExResult r, UInt32 seq, UInt32 param = 0)
        {
            GUILD_EX_ACK_PACKET p;
            Service::Init(p, PacketType::GUILD_EX_ACK);
            p.op = static_cast<UInt8>(op);
            p.result = static_cast<UInt8>(r);
            p.seq = seq;
            p.param = param;
            if (auto s = user->GetGameSession()) s->PostSend(p);
        }
        [[nodiscard]] bool TextOk(const char* raw, size_t cap, int minChars, int maxChars)
        {
            char text[256]{};
            ::memcpy(text, raw, std::min(cap, sizeof(text) - 1));
            const size_t n = ::strnlen(text, cap);
            if (n >= cap) return false;   // 널 없음
            if (!CChatHelper::IsValidUtf8(text, n)) return false;
            return GuildGrowth::CheckText(text, minChars, maxChars) == GuildGrowth::TextCheck::Ok;
        }
    }

    void HandleExRequest(const UserSharedPtr& user, const GUILD_EX_REQ_PACKET& p)
    {
        if (user == nullptr)
            return;
        const auto op = static_cast<GuildExOp>(p.op);
        if (op == GuildExOp::Buy && user->m_Transfer) return ZoneAckEx(user, op, GuildExResult::Busy, p.seq);
        if (GGuildService == nullptr)
            return ZoneAckEx(user, op, GuildExResult::Busy, p.seq);
        const UInt32 now = static_cast<UInt32>(GetNowTime());
        if (IsMutatingEx(op))
        {
            if (p.seq <= user->m_LastGuildExSeq)   // param = 마지막으로 받은 순번(GUILD.md 5장 「순번 계약」)
                return ZoneAckEx(user, op, GuildExResult::Duplicate, p.seq, user->m_LastGuildExSeq);
            user->m_LastGuildExSeq = p.seq;
        }
        else
        {
            if (user->m_LastGuildExQueryAt != 0 && now - user->m_LastGuildExQueryAt < GuildGrowth::kQueryMs)
                return ZoneAckEx(user, op, GuildExResult::Busy, p.seq);
            user->m_LastGuildExQueryAt = now;
        }
        switch (op)
        {
        case GuildExOp::PostWrite:
            if (p.arg8 >= static_cast<UInt8>(GuildPostCategory::Count)
                || !TextOk(p.title, sizeof(p.title), GuildGrowth::kPostTitleMinChars, GuildGrowth::kPostTitleMaxChars)
                || !TextOk(p.body, sizeof(p.body), GuildGrowth::kPostBodyMinChars, GuildGrowth::kPostBodyMaxChars))
                return ZoneAckEx(user, op, GuildExResult::BadText, p.seq);
            if (user->m_LastGuildPostAt != 0 && now - user->m_LastGuildPostAt < GuildGrowth::kPostMs)
                return ZoneAckEx(user, op, GuildExResult::Busy, p.seq);
            user->m_LastGuildPostAt = now;
            break;
        case GuildExOp::Intro:
            if (!TextOk(p.body, sizeof(p.body), 0, GuildGrowth::kIntroMaxChars) || ::strnlen(p.body, sizeof(p.body)) >= static_cast<size_t>(GuildGrowth::kIntroBytes))
                return ZoneAckEx(user, op, GuildExResult::BadText, p.seq);
            break;
        case GuildExOp::ApplyReply:
            if (p.target[0] == '\0')
                return ZoneAckEx(user, op, GuildExResult::NotFound, p.seq);
            break;
        default:
            if (p.op > static_cast<UInt8>(GuildExOp::Claim))
                return ZoneAckEx(user, op, GuildExResult::Invalid, p.seq);
            break;
        }
        const Service::Ctx ctx = Service::MakeCtx(user);
        const GUILD_EX_REQ_PACKET copy = p;
        if (op == GuildExOp::Buy) ++user->m_PendingGuildEconomy;
        GGuildService->Post([ctx, copy]() { GGuildService->HandleEx(ctx, copy); });
    }

    void OnKill(std::span<const UserSharedPtr> damagers, UInt32 monsterExp, UInt8 monsterLevel, bool boss)
    {
        if (GGuildService == nullptr)
            return;
        // 결사마다 묶는다(함께 때린 사람은 많아야 몇 — 작은 선형 탐색)
        std::vector<std::pair<UInt32, GuildModel::KillIn>> groups;
        for (const UserSharedPtr& u : damagers)
        {
            if (u == nullptr) continue;
            const UInt32 gid = u->m_GuildId.load(std::memory_order_relaxed);
            if (gid == 0) continue;
            auto it = std::find_if(groups.begin(), groups.end(), [gid](const auto& g) { return g.first == gid; });
            if (it == groups.end())
            {
                GuildModel::KillIn k;
                k.monsterExp = monsterExp;
                k.monsterLevel = monsterLevel;
                k.boss = boss;
                groups.emplace_back(gid, std::move(k));
                it = groups.end() - 1;
            }
            it->second.players.push_back(u->GetPlayerId());
        }
        for (auto& [gid, k] : groups)
            GGuildService->Post([gid = gid, k = std::move(k)]() { GGuildService->OnGuildKill(gid, k); });
    }

    void OnLoot(const UserSharedPtr& user, UInt16 itemId, UInt16 count, UInt64 extra)
    {
        if (user == nullptr || GGuildService == nullptr)
            return;
        const UInt32 gid = user->m_GuildId.load(std::memory_order_relaxed);
        if (gid == 0)
            return;
        const ItemTableRow* row = ItemTable::Find(itemId);
        if (row == nullptr)
            return;
        const bool material = row->effect == ItemEffect::eEnchantWeapon;
        const bool equipment = row->type == ItemType::eEquipment;
        const UInt8 rarity = equipment ? static_cast<UInt8>(ItemRules::EffectiveRarity(*row, extra)) : 0;
        const int pid = user->GetPlayerId();
        GGuildService->Post([gid, pid, itemId, count, material, equipment, rarity]()
        {
            GGuildService->OnGuildLoot(gid, pid, itemId, count, material, equipment, rarity);
        });
    }

    void OnQuestDone(const UserSharedPtr& user)
    {
        if (user == nullptr || GGuildService == nullptr)
            return;
        const UInt32 gid = user->m_GuildId.load(std::memory_order_relaxed);
        if (gid == 0)
            return;
        const int pid = user->GetPlayerId();
        GGuildService->Post([gid, pid]() { GGuildService->OnGuildQuest(gid, pid); });
    }
}
