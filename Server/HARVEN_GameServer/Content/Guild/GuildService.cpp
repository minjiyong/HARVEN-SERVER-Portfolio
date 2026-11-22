#include "pch.h"
#include "Content/Social/SocialHelper.h"
#include "Content/Guild/GuildService.h"

#include <algorithm>
#include <shared_mutex>

#include "Content/Chat/ChatHelper.h"
#include "Database/GameData/GuildAuction.h"
#include "Database/GameData/GuildGrowth.h"
#include "Network/GameSessionManager.h"
#include "Content/Npc/NpcHelper.h"
#include "Content/Field/Sector.h"
#include "Entity/Subject/SubjectHelper.h"
#include "Database/DBThread.h"
#include "User/UserHelper.h"
#include "Content/Achieve/AchieveHelper.h"

std::shared_ptr<CGuildService> GGuildService;

namespace
{
    using GuildRule::Rank;

    [[nodiscard]] Rank R(UInt8 r) noexcept { return static_cast<Rank>(r < static_cast<UInt8>(Rank::Count) ? r : 3); }

    void CopyStr(char* dst, size_t cap, const std::string& src)
    {
        ::strncpy_s(dst, cap, src.c_str(), _TRUNCATE);
    }

    // 결사 번호 → 이름 · 문장(머리 위 표시에 Zone 스레드들이 읽는다). 쓰는 쪽은 서비스뿐 — 세우기 · 문장 · 해산 때만.
    struct NameCache
    {
        std::shared_mutex lock;
        std::unordered_map<UInt32, std::pair<std::string, UInt8>> byId;
    };
    NameCache& Names()
    {
        static NameCache cache;
        return cache;
    }
    void CacheName(UInt32 id, const std::string& name, UInt8 emblem)
    {
        std::unique_lock lock(Names().lock);
        Names().byId[id] = { name, emblem };
    }
    void UncacheName(UInt32 id)
    {
        std::unique_lock lock(Names().lock);
        Names().byId.erase(id);
    }
    bool LookupName(UInt32 id, char (&out)[25])
    {
        std::shared_lock lock(Names().lock);
        const auto it = Names().byId.find(id);
        if (it == Names().byId.end())
            return false;
        CopyStr(out, sizeof(out), it->second.first);
        return true;
    }

    [[nodiscard]] UInt32 UnixMinutes(Int64 unixSec) noexcept { return static_cast<UInt32>(unixSec / 60); }
}

// ─────────────────────────────────────────────────────────────────────────────
// 서비스 뼈대
// ─────────────────────────────────────────────────────────────────────────────
CGuildService::CGuildService() = default;

void CGuildService::Run()
{
    Service::LServiceId = Service::kGuildServiceId;
    m_Loop.Run();
}

void CGuildService::Post(Task task)
{
    m_Loop.Enqueue(std::move(task));
}

std::string CGuildService::NameKey(const std::string& name)
{
    std::string key = name;
    for (char& c : key)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return key;
}

bool CGuildService::LoadFromDB(CDBConnection& db, int maxGuildId)
{
    std::vector<DB_GUILD_ROW> guilds;
    std::vector<DB_GUILD_MEMBER_ROW> members;
    if (!db.LoadGuilds(guilds) || !db.LoadGuildMembers(members))
        return false;
    for (const auto& row : guilds)
    {
        Guild g;
        g.id = static_cast<UInt32>(row.guildId);
        g.name = row.name;
        g.key = NameKey(row.name);
        g.emblem = GuildRule::ValidEmblem(row.emblem) ? row.emblem : 0;
        g.notice = row.notice;
        g.leaderId = row.leaderId;
        m_ByKey[g.key] = g.id;
        CacheName(g.id, g.name, g.emblem);
        m_Guilds.emplace(g.id, std::move(g));
    }
    for (const auto& row : members)
    {
        const auto it = m_Guilds.find(static_cast<UInt32>(row.guildId));
        if (it == m_Guilds.end())
            continue;
        Member m;
        m.playerId = row.playerId;
        m.name = row.name;
        m.rank = row.rank;
        m.level = row.level;
        m.lastSeenMin = row.lastSeenMin;
        it->second.members.push_back(std::move(m));
        m_GuildOf[row.playerId] = it->second.id;
    }
    m_NextGuildId = static_cast<UInt32>(std::max(maxGuildId, 0)) + 1;
    return LoadGrowth(db);   // 결사 성장(진행 · 기록 · 게시판) — 못 읽으면 결사 서비스를 켜지 않는다
}

CGuildService::Guild* CGuildService::GuildOf(int playerId)
{
    const auto it = m_GuildOf.find(playerId);
    if (it == m_GuildOf.end())
        return nullptr;
    const auto g = m_Guilds.find(it->second);
    return g == m_Guilds.end() ? nullptr : &g->second;
}

CGuildService::Member* CGuildService::FindMember(Guild& g, int playerId)
{
    for (auto& m : g.members)
        if (m.playerId == playerId) return &m;
    return nullptr;
}

CGuildService::Member* CGuildService::FindMemberByName(Guild& g, const std::string& name)
{
    const std::string key = NameKey(name);
    for (auto& m : g.members)
        if (NameKey(m.name) == key) return &m;
    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// 보내기
// ─────────────────────────────────────────────────────────────────────────────
void CGuildService::Ack(const Service::Ctx& ctx, GuildOp op, GuildResult r, UInt32 seq, UInt32 param) const
{
    if (op == GuildOp::Create)
        Service::PostToOwner(ctx.obj, [](const UserSharedPtr& u) { if (u && u->m_PendingGuildEconomy) --u->m_PendingGuildEconomy; });
    GUILD_ACK_PACKET p;
    Service::Init(p, PacketType::GUILD_ACK);
    p.op = static_cast<UInt8>(op);
    p.result = static_cast<UInt8>(r);
    p.seq = seq;
    p.param = param;
    Service::SendTo(ctx.session, p);
}

void CGuildService::SendInfo(const SessionWeakPtr& session, const Guild* g, int playerId) const
{
    GUILD_INFO_INF_PACKET p;
    Service::Init(p, PacketType::GUILD_INF);
    p.kind = static_cast<UInt8>(GuildInfKind::Info);
    if (g != nullptr)
    {
        p.guildId = g->id;
        p.emblem = g->emblem;
        p.members = static_cast<UInt8>(g->members.size());
        for (const auto& m : g->members)
        {
            if (m.online) ++p.online;
            if (m.playerId == playerId) p.myRank = m.rank;
            if (m.playerId == g->leaderId) CopyStr(p.leader, sizeof(p.leader), m.name);
        }
        CopyStr(p.name, sizeof(p.name), g->name);
        CopyStr(p.notice, sizeof(p.notice), g->notice);
    }
    Service::SendTo(session, p);
}

void CGuildService::SendMembers(const Service::Ctx& ctx, const Guild& g, UInt8 page, UInt8 sort) const
{
    // 정렬: 0 접속 중 → 직위 → 이름(기본) · 1 레벨 높은 순 · 2 이름 · 3 최근 접속 순
    std::vector<const Member*> order;
    order.reserve(g.members.size());
    for (const auto& m : g.members) order.push_back(&m);
    std::sort(order.begin(), order.end(), [sort](const Member* a, const Member* b)
    {
        switch (sort)
        {
        case 1: if (a->level != b->level) return a->level > b->level; break;
        case 2: break;
        case 3:
            if (a->online != b->online) return a->online;
            if (a->lastSeenMin != b->lastSeenMin) return a->lastSeenMin > b->lastSeenMin;
            break;
        default:
            if (a->online != b->online) return a->online;
            if (a->rank != b->rank) return a->rank < b->rank;
            break;
        }
        return a->name < b->name;
    });
    const int per = GuildRule::kMembersPerPage;
    const int pages = std::max(1, (static_cast<int>(order.size()) + per - 1) / per);
    const int pg = std::min<int>(page, pages - 1);

    GUILD_MEMBERS_INF_PACKET p;
    Service::Init(p, PacketType::GUILD_INF);
    p.kind = static_cast<UInt8>(GuildInfKind::Members);
    p.page = static_cast<UInt8>(pg);
    p.pages = static_cast<UInt8>(pages);
    for (int i = pg * per; i < static_cast<int>(order.size()) && p.count < per; ++i)
    {
        const Member& m = *order[static_cast<size_t>(i)];
        GuildMemberRow& row = p.rows[p.count++];
        CopyStr(row.name, sizeof(row.name), m.name);
        row.rank = m.rank;
        row.level = m.level;
        row.classId = m.classId;
        row.online = m.online ? 1 : 0;
        row.lastSeenMin = m.online ? 0 : m.lastSeenMin;
    }
    Service::SendTo(ctx.session, p);
}

void CGuildService::SendName(const Service::Ctx& ctx, UInt32 guildId) const
{
    GUILD_NAME_INF_PACKET p;
    Service::Init(p, PacketType::GUILD_INF);
    p.kind = static_cast<UInt8>(GuildInfKind::Name);
    p.guildId = guildId;
    if (const auto it = m_Guilds.find(guildId); it != m_Guilds.end())
    {
        p.emblem = it->second.emblem;
        CopyStr(p.name, sizeof(p.name), it->second.name);
    }
    Service::SendTo(ctx.session, p);
}

void CGuildService::SendTop(const Service::Ctx& ctx) const
{
    // 결사 수는 많아야 수백 — 요청마다 부분 정렬해도 싸다(요청은 사람마다 2초에 한 번, 소유 Zone이 거른다)
    std::vector<const Guild*> all;
    all.reserve(m_Guilds.size());
    for (const auto& [id, g] : m_Guilds)
        all.push_back(&g);
    const size_t n = std::min(all.size(), static_cast<size_t>(GuildRule::kHallTop));
    std::partial_sort(all.begin(), all.begin() + static_cast<std::ptrdiff_t>(n), all.end(), [](const Guild* a, const Guild* b)
    {
        if (a->members.size() != b->members.size()) return a->members.size() > b->members.size();
        return a->id < b->id;
    });
    GUILD_TOP_INF_PACKET p;
    Service::Init(p, PacketType::GUILD_INF);
    p.kind = static_cast<UInt8>(GuildInfKind::Top);
    p.count = static_cast<UInt8>(n);
    for (size_t i = 0; i < n; ++i)
    {
        p.rows[i].guildId = all[i]->id;
        p.rows[i].emblem = all[i]->emblem;
        p.rows[i].members = static_cast<UInt8>(std::min<size_t>(all[i]->members.size(), 255));
        CopyStr(p.rows[i].name, sizeof(p.rows[i].name), all[i]->name);
    }
    Service::SendTo(ctx.session, p);
}

void CGuildService::NotifyMembers(const Guild& g, GuildEvent ev, const std::string& who, const std::string& by, UInt8 rank, int except) const
{
    GUILD_NFY_PACKET p;
    Service::Init(p, PacketType::GUILD_NFY);
    p.event = static_cast<UInt8>(ev);
    p.emblem = g.emblem;
    p.rank = rank;
    p.guildId = g.id;
    CopyStr(p.who, sizeof(p.who), who);
    CopyStr(p.by, sizeof(p.by), by);
    CopyStr(p.name, sizeof(p.name), g.name);
    for (const auto& m : g.members)
        if (m.online && m.playerId != except)
            Service::SendTo(m.session, p);
}

void CGuildService::ApplyTag(const ObjID& obj, UInt32 guildId, UInt8 emblem, UInt8 rank)
{
    Service::PostToOwner(obj, [guildId, emblem, rank](const UserSharedPtr& user)
    {
        if (user == nullptr)
            return;
        user->m_GuildId.store(guildId, std::memory_order_relaxed);
        user->m_GuildEmblem.store(emblem, std::memory_order_relaxed);
        user->m_GuildRank = rank;
        CGuildHelper::BroadcastTag(user);
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// 접속 · 틱
// ─────────────────────────────────────────────────────────────────────────────
void CGuildService::OnLogin(const Service::Ctx& ctx)
{
    m_Online[ctx.playerId] = ctx;
    Guild* g = GuildOf(ctx.playerId);
    if (g == nullptr)
    {
        SendInfo(ctx.session, nullptr, ctx.playerId);   // 결사 없음(창 초기화)
        SendOverview(ctx.session, nullptr, ctx.playerId);
        // 가입 신청을 해 둔 사람 — 새 세션으로(승인되면 이 값으로 결사원이 된다)
        if (const auto ap = m_ApplyOf.find(ctx.playerId); ap != m_ApplyOf.end())
            if (const auto gi = m_Guilds.find(ap->second); gi != m_Guilds.end())
                for (auto& a : gi->second.grow.applicants)
                    if (a.ctx.playerId == ctx.playerId) a.ctx = ctx;
        return;
    }
    Member* m = FindMember(*g, ctx.playerId);
    if (m == nullptr)
        return;
    m->online = true;
    m->obj = ctx.obj;
    m->session = ctx.session;
    m->level = ctx.level;
    m->classId = ctx.classId;
    m->name = ctx.name;
    SendInfo(ctx.session, g, ctx.playerId);
    NotifyMembers(*g, GuildEvent::Online, ctx.name, "", m->rank, ctx.playerId);
    ApplyTag(ctx.obj, g->id, g->emblem, m->rank);
    // 개인 업적: 결사원이면 「결사 가입」은 적어도 1 — 나가 있는 동안 승인된 사람 · 업적 전부터 결사원이던 사람(최댓값이라 두 번 세지 않는다)
    Service::PostToOwner(ctx.obj, [](const UserSharedPtr& u) { if (u != nullptr) CAchieveHelper::OnGuildPresent(u); });
    // 결사장이 돌아왔다 — 위임 청구는 취소
    if (g->leaderId == ctx.playerId && g->grow.p.claimantId != 0)
        CancelClaim(*g, ctx.name);
    SendOverview(ctx.session, g, ctx.playerId);
}

void CGuildService::OnLogout(int playerId, Int64 nowUnix)
{
    m_Online.erase(playerId);
    m_Invites.erase(playerId);
    Guild* g = GuildOf(playerId);
    if (g == nullptr)
        return;
    Member* m = FindMember(*g, playerId);
    if (m == nullptr || !m->online)
        return;
    m->online = false;
    m->session.reset();
    m->lastSeenMin = UnixMinutes(nowUnix);
    NotifyMembers(*g, GuildEvent::Offline, m->name, "", m->rank, playerId);
    // 마지막 접속 — 사람 샤드(그 사람의 다른 저장과 한 줄). 결과는 기다리지 않는다
    GDBThread->RequestTx(playerId, "GuildMemberSeen",
        [playerId, nowUnix](CDBConnection& c) { return c.GuildMemberSeen(playerId, nowUnix); }, nullptr);
}

void CGuildService::OnLevel(int playerId, UInt8 level)
{
    if (Guild* g = GuildOf(playerId))
        if (Member* m = FindMember(*g, playerId))
            m->level = level;
}

void CGuildService::Tick()
{
    const UInt32 now = GetNowTime();
    for (auto it = m_Invites.begin(); it != m_Invites.end();)
    {
        if (now >= it->second.expiresAt)
            it = m_Invites.erase(it);
        else
            ++it;
    }
    GrowthTick(now, Service::UnixNow());
}

void CGuildService::RelayChat(int playerId, const ObjID& senderObj, const std::string& senderName, const std::string& body)
{
    const Guild* g = GuildOf(playerId);
    if (g == nullptr)
        return;
    SC_CHAT_PACKET p;
    Service::Init(p, PacketType::SC_CHAT);
    p.channel = static_cast<UInt8>(ChatChannel::Guild);
    p.senderId = senderObj;
    CopyStr(p.senderName, sizeof(p.senderName), senderName);
    ::strncpy_s(p.mess, CHAT_SIZE, body.c_str(), _TRUNCATE);
    for (const auto& m : g->members)
        if (m.online && !CSocialHelper::Blocked(playerId,m.playerId))
            Service::SendTo(m.session, p);
}

// ─────────────────────────────────────────────────────────────────────────────
// 요청(서비스 스레드) — 검사 → DB(결과 콜백) → 메모리 반영 · 알림
// ─────────────────────────────────────────────────────────────────────────────
void CGuildService::Handle(const Service::Ctx& ctx, const GUILD_REQ_PACKET& p)
{
    char target[NAME_SIZE + 1]{};
    ::memcpy(target, p.target, NAME_SIZE);
    char text[sizeof(p.text) + 1]{};
    ::memcpy(text, p.text, sizeof(p.text));

    switch (static_cast<GuildOp>(p.op))
    {
    case GuildOp::Info:
        SendInfo(ctx.session, GuildOf(ctx.playerId), ctx.playerId);
        break;
    case GuildOp::Members:
        if (const Guild* g = GuildOf(ctx.playerId)) SendMembers(ctx, *g, p.arg8, static_cast<UInt8>(p.arg32));
        else Ack(ctx, GuildOp::Members, GuildResult::NotInGuild, p.seq);
        break;
    case GuildOp::Name:        SendName(ctx, p.arg32); break;
    case GuildOp::Top:         SendTop(ctx); break;
    case GuildOp::Create:      Create(ctx, p.seq, text, p.arg8); break;
    case GuildOp::Invite:      DoInvite(ctx, p.seq, target); break;
    case GuildOp::InviteReply: Reply(ctx, p.seq, p.arg8 != 0, p.arg32); break;
    case GuildOp::Leave:       Leave(ctx, p.seq); break;
    case GuildOp::Kick:        Kick(ctx, p.seq, target); break;
    case GuildOp::SetRank:     SetRank(ctx, p.seq, target, p.arg8); break;
    case GuildOp::Transfer:    Transfer(ctx, p.seq, target); break;
    case GuildOp::Notice:      SetNotice(ctx, p.seq, text); break;
    case GuildOp::Disband:     Disband(ctx, p.seq); break;
    case GuildOp::Emblem:      SetEmblem(ctx, p.seq, p.arg8); break;
    default:                   Ack(ctx, static_cast<GuildOp>(p.op), GuildResult::Invalid, p.seq); break;
    }
}

void CGuildService::Create(const Service::Ctx& ctx, UInt32 seq, const std::string& name, UInt8 emblem)
{
    if (m_GuildOf.count(ctx.playerId))
        return Ack(ctx, GuildOp::Create, GuildResult::AlreadyInGuild, seq);
    if (m_BusyPlayers.count(ctx.playerId))
        return Ack(ctx, GuildOp::Create, GuildResult::Busy, seq);
    const std::string key = NameKey(name);
    if (m_ByKey.count(key) || m_PendingKeys.count(key))
        return Ack(ctx, GuildOp::Create, GuildResult::NameTaken, seq);

    const UInt32 id = m_NextGuildId++;
    m_PendingKeys.insert(key);
    m_BusyPlayers.insert(ctx.playerId);

    // 그로시는 만드는 사람의 Zone이 뺀다 → 그 사람 샤드에서 한 트랜잭션(그로시 · 결사 행 · 결사장 행)
    Service::PostToOwner(ctx.obj, [ctx, seq, id, name, key, emblem](const UserSharedPtr& user)
    {
        auto abort = [=](GuildResult r, UInt32 param)
        {
            GGuildService->Post([=]()
            {
                GGuildService->m_PendingKeys.erase(key);
                GGuildService->m_BusyPlayers.erase(ctx.playerId);
                GGuildService->Ack(ctx, GuildOp::Create, r, seq, param);
            });
        };
        if (user == nullptr)
            return abort(GuildResult::Invalid, 0);
        if (!user->GetInventory()->ReserveAdena(GuildRule::kCreateCost, AdenaReason::GuildCreate))
            return abort(GuildResult::NoAdena, GuildRule::kCreateCost);
        const UInt32 adenaAfter = user->GetInventory()->GetAdena();
        const int playerId = ctx.playerId;
        GDBThread->RequestTx(playerId, "GuildCreate",
            [=](CDBConnection& c) { return c.GuildCreate(playerId, adenaAfter, static_cast<int>(id), name, key, emblem); },
            [=](bool ok)
            {
                if (!ok)
                    Service::PostToOwner(ctx.obj, [](const UserSharedPtr& u)
                    {
                        if (u != nullptr) u->GetInventory()->ReturnAdena(GuildRule::kCreateCost, AdenaReason::Refund);
                    });
                GGuildService->Post([=]() { GGuildService->FinishCreate(ok, ctx, seq, id, name, key, emblem); });
            });
    });
}

void CGuildService::FinishCreate(bool ok, const Service::Ctx& ctx, UInt32 seq, UInt32 id, const std::string& name, const std::string& key, UInt8 emblem)
{
    m_PendingKeys.erase(key);
    m_BusyPlayers.erase(ctx.playerId);
    if (!ok)
        return Ack(ctx, GuildOp::Create, GuildResult::NameTaken, seq);

    const Service::Ctx& cur = CurrentCtx(ctx);   // 세우는 사이 다시 접속했으면 새 세션으로
    Guild g;
    g.id = id;
    g.name = name;
    g.key = key;
    g.emblem = emblem;
    g.leaderId = ctx.playerId;
    Member m;
    m.playerId = ctx.playerId;
    m.name = cur.name;
    m.rank = static_cast<UInt8>(Rank::Leader);
    m.level = cur.level;
    m.classId = cur.classId;
    m.online = m_Online.count(ctx.playerId) > 0;
    m.obj = cur.obj;
    m.session = cur.session;
    g.members.push_back(std::move(m));
    m_ByKey[key] = id;
    m_GuildOf[ctx.playerId] = id;
    CacheName(id, name, emblem);
    auto& stored = m_Guilds.emplace(id, std::move(g)).first->second;
    // 세우기 전에 다른 결사에 내 둔 가입 신청은 거둔다(예전: 남아서 그 결사의 승인 목록에 보이고 · 나중에 승인되면 끼어들었다)
    WithdrawApply(ctx.playerId);

    stored.grow.createdAt = Service::UnixNow();
    InitGrowth(stored, stored.grow.createdAt, true);
    Ack(ctx, GuildOp::Create, GuildResult::Ok, seq, id);
    SendInfo(cur.session, &stored, ctx.playerId);
    SendOverview(cur.session, &stored, ctx.playerId);
    ApplyTag(cur.obj, id, emblem, static_cast<UInt8>(Rank::Leader));
    Service::PostToOwner(cur.obj, [](const UserSharedPtr& u) { if (u != nullptr) CAchieveHelper::OnGuildJoin(u); });   // 개인 업적(결사 창설)

    // 월드 공지 — 모두에게(드문 일)
    GUILD_NFY_PACKET n;
    Service::Init(n, PacketType::GUILD_NFY);
    n.event = static_cast<UInt8>(GuildEvent::Created);
    n.emblem = emblem;
    n.guildId = id;
    CopyStr(n.who, sizeof(n.who), ctx.name);
    CopyStr(n.name, sizeof(n.name), name);
    for (const auto& s : GSessionManager->SnapshotSessions())
        Service::SendTo(s, n);
    std::cout << "[Guild] created id=" << id << " leader=" << ctx.playerId << "\n" << std::flush;
}

void CGuildService::DoInvite(const Service::Ctx& ctx, UInt32 seq, const std::string& target)
{
    Guild* g = GuildOf(ctx.playerId);
    if (g == nullptr)
        return Ack(ctx, GuildOp::Invite, GuildResult::NotInGuild, seq);
    const Member* me = FindMember(*g, ctx.playerId);
    if (me == nullptr || !HasPerm(*g, me->rank, GuildPerm::Invite))
        return Ack(ctx, GuildOp::Invite, GuildResult::NoPermission, seq);
    if (static_cast<int>(g->members.size()) + g->pendingJoins >= MaxMembers(*g))
        return Ack(ctx, GuildOp::Invite, GuildResult::Full, seq);

    const auto session = GSessionManager->FindByName(target);
    const auto user = session != nullptr ? session->GetOwner() : nullptr;
    if (user == nullptr || session->m_eState != SOCKET_STATE::ST_INGAME)
        return Ack(ctx, GuildOp::Invite, GuildResult::TargetNotFound, seq);
    const int targetId = user->GetPlayerId();   // 로그인 때 한 번 정해지는 값
    if(CSocialHelper::Blocked(ctx.playerId,targetId)) return Ack(ctx,GuildOp::Invite,GuildResult::NoPermission,seq);
    if (targetId == ctx.playerId)
        return Ack(ctx, GuildOp::Invite, GuildResult::Invalid, seq);
    if (m_GuildOf.count(targetId))
        return Ack(ctx, GuildOp::Invite, GuildResult::TargetInGuild, seq);
    if (m_BusyPlayers.count(targetId))
        return Ack(ctx, GuildOp::Invite, GuildResult::Busy, seq);

    Invite inv;
    inv.guildId = g->id;
    inv.inviterId = ctx.playerId;
    inv.inviterName = ctx.name;
    inv.expiresAt = GetNowTime() + GuildRule::kInviteMs;
    m_Invites[targetId] = inv;

    GUILD_NFY_PACKET n;
    Service::Init(n, PacketType::GUILD_NFY);
    n.event = static_cast<UInt8>(GuildEvent::InviteFrom);
    n.emblem = g->emblem;
    n.guildId = g->id;
    CopyStr(n.who, sizeof(n.who), ctx.name);
    CopyStr(n.name, sizeof(n.name), g->name);
    Service::SendTo(session, n);
    Ack(ctx, GuildOp::Invite, GuildResult::Ok, seq);
}

void CGuildService::Reply(const Service::Ctx& ctx, UInt32 seq, bool accept, UInt32 guildId)
{
    const auto it = m_Invites.find(ctx.playerId);
    if (it == m_Invites.end() || it->second.guildId != guildId || GetNowTime() >= it->second.expiresAt)
    {
        if (it != m_Invites.end() && it->second.guildId == guildId) m_Invites.erase(it);
        return Ack(ctx, GuildOp::InviteReply, GuildResult::InviteExpired, seq);
    }
    const Invite inv = it->second;
    m_Invites.erase(it);

    const auto gi = m_Guilds.find(guildId);
    if (gi == m_Guilds.end())
        return Ack(ctx, GuildOp::InviteReply, GuildResult::InviteExpired, seq);
    Guild& g = gi->second;
    if (!accept)
    {
        // 초대한 사람에게 "거절했다"
        if (const Member* inviter = FindMember(g, inv.inviterId); inviter != nullptr && inviter->online)
        {
            GUILD_NFY_PACKET n;
            Service::Init(n, PacketType::GUILD_NFY);
            n.event = static_cast<UInt8>(GuildEvent::InviteDeclined);
            n.guildId = g.id;
            n.emblem = g.emblem;
            CopyStr(n.who, sizeof(n.who), ctx.name);
            CopyStr(n.name, sizeof(n.name), g.name);
            Service::SendTo(inviter->session, n);
        }
        return Ack(ctx, GuildOp::InviteReply, GuildResult::Ok, seq);
    }
    if (m_GuildOf.count(ctx.playerId))
        return Ack(ctx, GuildOp::InviteReply, GuildResult::AlreadyInGuild, seq);
    if(CSocialHelper::Blocked(ctx.playerId,inv.inviterId)) return Ack(ctx,GuildOp::InviteReply,GuildResult::NoPermission,seq);
    if (m_BusyPlayers.count(ctx.playerId))
        return Ack(ctx, GuildOp::InviteReply, GuildResult::Busy, seq);
    if (static_cast<int>(g.members.size()) + g.pendingJoins >= MaxMembers(g))
        return Ack(ctx, GuildOp::InviteReply, GuildResult::Full, seq);

    ++g.pendingJoins;
    m_BusyPlayers.insert(ctx.playerId);
    const int playerId = ctx.playerId;
    GDBThread->RequestTxKeyed(guildId, "GuildJoin",
        [=](CDBConnection& c) { return c.GuildMemberSet(static_cast<int>(guildId), playerId, static_cast<UInt8>(Rank::Member)); },
        [=](bool ok) { GGuildService->Post([=]() { GGuildService->FinishJoin(ok, ctx, seq, guildId); }); });
}

void CGuildService::FinishJoin(bool ok, const Service::Ctx& ctx, UInt32 seq, UInt32 guildId)
{
    m_BusyPlayers.erase(ctx.playerId);
    const auto gi = m_Guilds.find(guildId);
    if (gi == m_Guilds.end())
        return Ack(ctx, GuildOp::InviteReply, GuildResult::InviteExpired, seq);
    Guild& g = gi->second;
    g.pendingJoins = std::max(0, g.pendingJoins - 1);
    if (!ok)
        return Ack(ctx, GuildOp::InviteReply, GuildResult::Invalid, seq);

    Ack(ctx, GuildOp::InviteReply, GuildResult::Ok, seq, g.id);
    AddMember(g, ctx);
}

const Service::Ctx& CGuildService::CurrentCtx(const Service::Ctx& who) const
{
    const auto it = m_Online.find(who.playerId);
    return it != m_Online.end() ? it->second : who;
}

void CGuildService::WithdrawApply(int playerId)
{
    const auto ap = m_ApplyOf.find(playerId);
    if (ap == m_ApplyOf.end())
        return;
    if (const auto gi = m_Guilds.find(ap->second); gi != m_Guilds.end())
    {
        auto& v = gi->second.grow.applicants;
        v.erase(std::remove_if(v.begin(), v.end(), [playerId](const Applicant& a) { return a.ctx.playerId == playerId; }), v.end());
    }
    m_ApplyOf.erase(ap);
}

CGuildService::Member& CGuildService::AddMember(Guild& g, const Service::Ctx& request)
{
    // 들어옴은 DB를 기다린 뒤 — 그 사이 다시 접속했으면 요청 때의 세션 · ObjID가 아니라 지금 것으로(예전: 접속 중인데 옛 세션이 적혀 알림 · 머리 위 표시를 못 받았다)
    const Service::Ctx& who = CurrentCtx(request);
    Member m;
    m.playerId = who.playerId;
    m.name = who.name;
    m.rank = static_cast<UInt8>(Rank::Member);
    m.level = who.level;
    m.classId = who.classId;
    m.online = m_Online.count(who.playerId) > 0;
    m.obj = who.obj;
    m.session = who.session;
    g.members.push_back(std::move(m));
    m_GuildOf[who.playerId] = g.id;
    WithdrawApply(who.playerId);   // 다른 결사에 넣어 둔 가입 신청은 거둔다

    NotifyMembers(g, GuildEvent::Joined, who.name, "", static_cast<UInt8>(Rank::Member));
    SendInfo(who.session, &g, who.playerId);
    ApplyTag(who.obj, g.id, g.emblem, static_cast<UInt8>(Rank::Member));
    // 개인 업적(결사 가입) — 그 사람의 소유 Zone에서. 나가 있으면(승인 · 접속 중이 아님) 다음 로그인의 OnGuildPresent가 센다
    if (m_Online.count(who.playerId) > 0)
        Service::PostToOwner(who.obj, [](const UserSharedPtr& u) { if (u != nullptr) CAchieveHelper::OnGuildJoin(u); });
    // 결사 성장: 들어온 날 · 인원 업적 · 기록
    const Int64 now = Service::UnixNow();
    GuildMemberProgress& mp = GuildModel::EnsureMember(g.grow.p, who.playerId, static_cast<uint32_t>(now));
    if (const auto d = g.grow.departed.find(who.playerId); d != g.grow.departed.end())
    {
        GuildModel::Rejoin(g.grow.p, mp, d->second);   // 이번 주에 나갔다 다시 — 오늘 · 이번 주 한도 몫을 잇는다
        g.grow.departed.erase(d);
    }
    GuildModel::Effects fx;
    GuildModel::CheckAchievements(g.grow.p, now, g.grow.createdAt, static_cast<uint8_t>(std::min<size_t>(g.members.size(), 255)), GuildModel::FromSheet(), fx);
    ApplyEffects(g, fx);
    AddLog(g, GuildLogKind::Joined, 0, who.name, "");
    MarkDirty(g, true);
    SendOverview(who.session, &g, who.playerId);
    return g.members.back();
}

void CGuildService::Leave(const Service::Ctx& ctx, UInt32 seq)
{
    Guild* g = GuildOf(ctx.playerId);
    if (g == nullptr)
        return Ack(ctx, GuildOp::Leave, GuildResult::NotInGuild, seq);
    if (g->leaderId == ctx.playerId)
        return Ack(ctx, GuildOp::Leave, GuildResult::LeaderMustTransfer, seq);
    if (g->busy || m_BusyPlayers.count(ctx.playerId))
        return Ack(ctx, GuildOp::Leave, GuildResult::Busy, seq);
    g->busy = true;
    m_BusyPlayers.insert(ctx.playerId);
    const UInt32 guildId = g->id;
    const int playerId = ctx.playerId;
    GDBThread->RequestTxKeyed(guildId, "GuildLeave",
        [=](CDBConnection& c) { return c.GuildMemberRemove(static_cast<int>(guildId), playerId); },
        [=](bool ok) { GGuildService->Post([=]() { GGuildService->FinishRemove(ok, ctx, seq, guildId, playerId, false); }); });
}

void CGuildService::Kick(const Service::Ctx& ctx, UInt32 seq, const std::string& target)
{
    Guild* g = GuildOf(ctx.playerId);
    if (g == nullptr)
        return Ack(ctx, GuildOp::Kick, GuildResult::NotInGuild, seq);
    const Member* me = FindMember(*g, ctx.playerId);
    Member* victim = FindMemberByName(*g, target);
    if (victim == nullptr)
        return Ack(ctx, GuildOp::Kick, GuildResult::TargetNotFound, seq);
    if (me == nullptr || victim->playerId == ctx.playerId || !HasPerm(*g, me->rank, GuildPerm::Kick)
        || !GuildGrowth::Outranks(R(me->rank), R(victim->rank)))
        return Ack(ctx, GuildOp::Kick, GuildResult::NoPermission, seq);
    if (g->busy || m_BusyPlayers.count(victim->playerId))
        return Ack(ctx, GuildOp::Kick, GuildResult::Busy, seq);
    g->busy = true;
    m_BusyPlayers.insert(victim->playerId);
    const UInt32 guildId = g->id;
    const int victimId = victim->playerId;
    GDBThread->RequestTxKeyed(guildId, "GuildKick",
        [=](CDBConnection& c) { return c.GuildMemberRemove(static_cast<int>(guildId), victimId); },
        [=](bool ok) { GGuildService->Post([=]() { GGuildService->FinishRemove(ok, ctx, seq, guildId, victimId, true); }); });
}

void CGuildService::FinishRemove(bool ok, const Service::Ctx& ctx, UInt32 seq, UInt32 guildId, int targetId, bool kicked)
{
    m_BusyPlayers.erase(targetId);
    const auto gi = m_Guilds.find(guildId);
    if (gi == m_Guilds.end())
        return Ack(ctx, kicked ? GuildOp::Kick : GuildOp::Leave, GuildResult::Invalid, seq);
    Guild& g = gi->second;
    g.busy = false;
    if (!ok)
        return Ack(ctx, kicked ? GuildOp::Kick : GuildOp::Leave, GuildResult::Invalid, seq);

    const auto mi = std::find_if(g.members.begin(), g.members.end(), [&](const Member& m) { return m.playerId == targetId; });
    if (mi == g.members.end())
        return Ack(ctx, kicked ? GuildOp::Kick : GuildOp::Leave, GuildResult::Invalid, seq);
    const Member gone = *mi;
    g.members.erase(mi);
    m_GuildOf.erase(targetId);
    // 오늘 · 이번 주 한도 몫은 남겨 두었다가 다시 들어오면 잇는다(예전: 나갔다 들어오면 하루 경험치 한도 · 출석 · 주 상점 한도가 새로 시작했다)
    g.grow.departed[targetId] = GuildModel::Depart(g.grow.p, targetId);
    GuildModel::RemoveMember(g.grow.p, targetId);   // 기여도는 결사의 것 — 나가면 사라진다(위임 청구자였으면 청구도)
    AddLog(g, kicked ? GuildLogKind::Kicked : GuildLogKind::Left, 0, gone.name, kicked ? ctx.name : std::string());
    MarkDirty(g, true);

    Ack(ctx, kicked ? GuildOp::Kick : GuildOp::Leave, GuildResult::Ok, seq);
    NotifyMembers(g, kicked ? GuildEvent::Kicked : GuildEvent::Left, gone.name, kicked ? ctx.name : std::string());
    // 나간 사람에게도(창을 닫고 머리 위 표시를 지운다)
    if (gone.online)
    {
        GUILD_NFY_PACKET n;
        Service::Init(n, PacketType::GUILD_NFY);
        n.event = static_cast<UInt8>(kicked ? GuildEvent::Kicked : GuildEvent::Left);
        n.guildId = g.id;
        n.emblem = g.emblem;
        CopyStr(n.who, sizeof(n.who), gone.name);
        CopyStr(n.by, sizeof(n.by), kicked ? ctx.name : std::string());
        CopyStr(n.name, sizeof(n.name), g.name);
        Service::SendTo(gone.session, n);
        SendInfo(gone.session, nullptr, targetId);
        SendOverview(gone.session, nullptr, targetId);   // 성장 창(개요)도 비운다 — 들어올 때 · 로그인 때처럼 둘 다
        ApplyTag(gone.obj, 0, 0, static_cast<UInt8>(Rank::Member));
    }
}

void CGuildService::SetRank(const Service::Ctx& ctx, UInt32 seq, const std::string& target, UInt8 rank)
{
    Guild* g = GuildOf(ctx.playerId);
    if (g == nullptr)
        return Ack(ctx, GuildOp::SetRank, GuildResult::NotInGuild, seq);
    const Member* me = FindMember(*g, ctx.playerId);
    Member* who = FindMemberByName(*g, target);
    if (who == nullptr)
        return Ack(ctx, GuildOp::SetRank, GuildResult::TargetNotFound, seq);
    uint8_t masks[4]{};
    GuildModel::PermsArray(g->grow.p, GuildGrowth::Const(), masks);
    if (me == nullptr || who->playerId == ctx.playerId || rank >= static_cast<UInt8>(Rank::Count)
        || !GuildGrowth::CanSetRank(R(me->rank), R(who->rank), R(rank), masks))
        return Ack(ctx, GuildOp::SetRank, GuildResult::NoPermission, seq);
    if (who->rank == rank)
        return Ack(ctx, GuildOp::SetRank, GuildResult::Ok, seq);
    if (g->busy)
        return Ack(ctx, GuildOp::SetRank, GuildResult::Busy, seq);
    g->busy = true;
    const UInt32 guildId = g->id;
    const int targetId = who->playerId;
    GDBThread->RequestTxKeyed(guildId, "GuildRank",
        [=](CDBConnection& c) { return c.GuildMemberSet(static_cast<int>(guildId), targetId, rank); },
        [=](bool ok)
        {
            GGuildService->Post([=]()
            {
                auto& svc = *GGuildService;
                const auto gi = svc.m_Guilds.find(guildId);
                if (gi == svc.m_Guilds.end())
                    return svc.Ack(ctx, GuildOp::SetRank, GuildResult::Invalid, seq);
                Guild& gg = gi->second;
                gg.busy = false;
                Member* m = FindMember(gg, targetId);
                if (!ok || m == nullptr)
                    return svc.Ack(ctx, GuildOp::SetRank, GuildResult::Invalid, seq);
                m->rank = rank;
                svc.AddLog(gg, GuildLogKind::Rank, rank, m->name, ctx.name);
                svc.Ack(ctx, GuildOp::SetRank, GuildResult::Ok, seq);
                svc.NotifyMembers(gg, GuildEvent::RankChanged, m->name, ctx.name, rank);
                if (m->online)
                {
                    svc.SendInfo(m->session, &gg, m->playerId);   // 바뀐 사람의 창(내 직위 · 할 수 있는 일)
                    svc.SendOverview(m->session, &gg, m->playerId);
                    ApplyTag(m->obj, gg.id, gg.emblem, rank);
                }
            });
        });
}

void CGuildService::Transfer(const Service::Ctx& ctx, UInt32 seq, const std::string& target)
{
    Guild* g = GuildOf(ctx.playerId);
    if (g == nullptr)
        return Ack(ctx, GuildOp::Transfer, GuildResult::NotInGuild, seq);
    if (g->leaderId != ctx.playerId)
        return Ack(ctx, GuildOp::Transfer, GuildResult::NoPermission, seq);
    Member* who = FindMemberByName(*g, target);
    if (who == nullptr || who->playerId == ctx.playerId)
        return Ack(ctx, GuildOp::Transfer, GuildResult::TargetNotFound, seq);
    if (g->busy)
        return Ack(ctx, GuildOp::Transfer, GuildResult::Busy, seq);
    g->busy = true;
    const UInt32 guildId = g->id;
    const int fromId = ctx.playerId;
    const int toId = who->playerId;
    GDBThread->RequestTxKeyed(guildId, "GuildTransfer",
        [=](CDBConnection& c) { return c.GuildTransfer(static_cast<int>(guildId), fromId, toId); },
        [=](bool ok)
        {
            GGuildService->Post([=]()
            {
                auto& svc = *GGuildService;
                const auto gi = svc.m_Guilds.find(guildId);
                if (gi == svc.m_Guilds.end())
                    return svc.Ack(ctx, GuildOp::Transfer, GuildResult::Invalid, seq);
                Guild& gg = gi->second;
                gg.busy = false;
                Member* from = FindMember(gg, fromId);
                Member* to = FindMember(gg, toId);
                if (!ok || from == nullptr || to == nullptr)
                    return svc.Ack(ctx, GuildOp::Transfer, GuildResult::Invalid, seq);
                from->rank = static_cast<UInt8>(Rank::Officer);
                to->rank = static_cast<UInt8>(Rank::Leader);
                gg.leaderId = toId;
                svc.AddLog(gg, GuildLogKind::Leader, 0, to->name, ctx.name);
                if (gg.grow.p.claimantId != 0)
                {
                    gg.grow.p.claimantId = 0;   // 결사장이 직접 넘겼다 — 청구는 뜻이 없다
                    gg.grow.p.claimEndsAt = 0;
                }
                svc.MarkDirty(gg, true);
                svc.Ack(ctx, GuildOp::Transfer, GuildResult::Ok, seq);
                svc.NotifyMembers(gg, GuildEvent::LeaderChanged, to->name, ctx.name, static_cast<UInt8>(Rank::Leader));
                for (const auto& mm : gg.members)
                    if (mm.online)
                    {
                        svc.SendInfo(mm.session, &gg, mm.playerId);   // 결사장 이름 · 두 사람의 직위
                        svc.SendOverview(mm.session, &gg, mm.playerId);
                    }
                if (from->online) ApplyTag(from->obj, gg.id, gg.emblem, from->rank);
                if (to->online) ApplyTag(to->obj, gg.id, gg.emblem, to->rank);
            });
        });
}

void CGuildService::SetNotice(const Service::Ctx& ctx, UInt32 seq, const std::string& text)
{
    Guild* g = GuildOf(ctx.playerId);
    if (g == nullptr)
        return Ack(ctx, GuildOp::Notice, GuildResult::NotInGuild, seq);
    const Member* me = FindMember(*g, ctx.playerId);
    if (me == nullptr || !HasPerm(*g, me->rank, GuildPerm::Notice))
        return Ack(ctx, GuildOp::Notice, GuildResult::NoPermission, seq);
    if (g->busy)
        return Ack(ctx, GuildOp::Notice, GuildResult::Busy, seq);
    g->busy = true;
    const UInt32 guildId = g->id;
    const UInt8 emblem = g->emblem;
    GDBThread->RequestTxKeyed(guildId, "GuildNotice",
        [=](CDBConnection& c) { return c.GuildSetInfo(static_cast<int>(guildId), emblem, text); },
        [=](bool ok)
        {
            GGuildService->Post([=]()
            {
                auto& svc = *GGuildService;
                const auto gi = svc.m_Guilds.find(guildId);
                if (gi == svc.m_Guilds.end())
                    return svc.Ack(ctx, GuildOp::Notice, GuildResult::Invalid, seq);
                Guild& gg = gi->second;
                gg.busy = false;
                if (!ok)
                    return svc.Ack(ctx, GuildOp::Notice, GuildResult::Invalid, seq);
                gg.notice = text;
                svc.AddLog(gg, GuildLogKind::Notice, 0, ctx.name, "");
                svc.Ack(ctx, GuildOp::Notice, GuildResult::Ok, seq);
                svc.NotifyMembers(gg, GuildEvent::NoticeChanged, "", ctx.name);
                for (const auto& m : gg.members)
                    if (m.online) svc.SendInfo(m.session, &gg, m.playerId);
            });
        });
}

void CGuildService::SetEmblem(const Service::Ctx& ctx, UInt32 seq, UInt8 emblem)
{
    Guild* g = GuildOf(ctx.playerId);
    if (g == nullptr)
        return Ack(ctx, GuildOp::Emblem, GuildResult::NotInGuild, seq);
    if (g->leaderId != ctx.playerId)
        return Ack(ctx, GuildOp::Emblem, GuildResult::NoPermission, seq);
    if (!GuildRule::ValidEmblem(emblem))
        return Ack(ctx, GuildOp::Emblem, GuildResult::Invalid, seq);
    if (g->busy)
        return Ack(ctx, GuildOp::Emblem, GuildResult::Busy, seq);
    g->busy = true;
    const UInt32 guildId = g->id;
    const std::string notice = g->notice;
    GDBThread->RequestTxKeyed(guildId, "GuildEmblem",
        [=](CDBConnection& c) { return c.GuildSetInfo(static_cast<int>(guildId), emblem, notice); },
        [=](bool ok)
        {
            GGuildService->Post([=]()
            {
                auto& svc = *GGuildService;
                const auto gi = svc.m_Guilds.find(guildId);
                if (gi == svc.m_Guilds.end())
                    return svc.Ack(ctx, GuildOp::Emblem, GuildResult::Invalid, seq);
                Guild& gg = gi->second;
                gg.busy = false;
                if (!ok)
                    return svc.Ack(ctx, GuildOp::Emblem, GuildResult::Invalid, seq);
                gg.emblem = emblem;
                CacheName(gg.id, gg.name, emblem);
                svc.AddLog(gg, GuildLogKind::Emblem, emblem, ctx.name, "");
                svc.Ack(ctx, GuildOp::Emblem, GuildResult::Ok, seq);
                svc.NotifyMembers(gg, GuildEvent::EmblemChanged, "", ctx.name);
                for (const auto& m : gg.members)
                    if (m.online)
                    {
                        svc.SendInfo(m.session, &gg, m.playerId);
                        ApplyTag(m.obj, gg.id, emblem, m.rank);
                    }
            });
        });
}

void CGuildService::Disband(const Service::Ctx& ctx, UInt32 seq)
{
    Guild* g = GuildOf(ctx.playerId);
    if (g == nullptr)
        return Ack(ctx, GuildOp::Disband, GuildResult::NotInGuild, seq);
    if (g->leaderId != ctx.playerId)
        return Ack(ctx, GuildOp::Disband, GuildResult::NoPermission, seq);
    if (g->members.size() > 1 || g->pendingJoins > 0)
        return Ack(ctx, GuildOp::Disband, GuildResult::NotEmpty, seq);
    if (g->busy || m_BusyPlayers.count(ctx.playerId))
        return Ack(ctx, GuildOp::Disband, GuildResult::Busy, seq);
    g->busy = true;
    m_BusyPlayers.insert(ctx.playerId);
    const UInt32 guildId = g->id;
    GDBThread->RequestTxKeyed(guildId, "GuildDisband",
        [=](CDBConnection& c) { return c.GuildDisband(static_cast<int>(guildId)); },
        [=](bool ok)
        {
            GGuildService->Post([=]()
            {
                auto& svc = *GGuildService;
                svc.m_BusyPlayers.erase(ctx.playerId);
                const auto gi = svc.m_Guilds.find(guildId);
                if (gi == svc.m_Guilds.end())
                    return svc.Ack(ctx, GuildOp::Disband, GuildResult::Invalid, seq);
                Guild& gg = gi->second;
                gg.busy = false;
                if (!ok)
                    return svc.Ack(ctx, GuildOp::Disband, GuildResult::Invalid, seq);
                svc.NotifyMembers(gg, GuildEvent::Disbanded, ctx.name, ctx.name);
                for (const auto& m : gg.members)
                {
                    svc.m_GuildOf.erase(m.playerId);
                    if (m.online)
                    {
                        svc.SendInfo(m.session, nullptr, m.playerId);
                        svc.SendOverview(m.session, nullptr, m.playerId);
                        ApplyTag(m.obj, 0, 0, static_cast<UInt8>(Rank::Member));
                    }
                }
                for (auto it = svc.m_Invites.begin(); it != svc.m_Invites.end();)
                    it = it->second.guildId == guildId ? svc.m_Invites.erase(it) : std::next(it);
                // 대기 중인 가입 신청자에게 「끝남」(ApplyResult 2 — 만료와 같다). 예전: 알림 없이 지워 클라가 「신청 중」에 머물렀다
                for (const auto& a : gg.grow.applicants)
                {
                    svc.m_ApplyOf.erase(a.ctx.playerId);
                    svc.NotifyOne(a.ctx.session, GuildExEvent::ApplyResult, 2, static_cast<Int32>(guildId), "", gg.name);
                }
                svc.m_ByKey.erase(gg.key);
                UncacheName(guildId);
                svc.m_Guilds.erase(gi);
                svc.Ack(ctx, GuildOp::Disband, GuildResult::Ok, seq);
                std::cout << "[Guild] disbanded id=" << guildId << "\n" << std::flush;
            });
        });
}

// ─────────────────────────────────────────────────────────────────────────────
// CGuildHelper — 요청한 사람의 소유 Zone 스레드
// ─────────────────────────────────────────────────────────────────────────────
namespace CGuildHelper
{
    namespace
    {
        [[nodiscard]] bool IsMutating(GuildOp op) noexcept
        {
            return op != GuildOp::Info && op != GuildOp::Members && op != GuildOp::Name && op != GuildOp::Top;
        }

        void ZoneAck(const UserSharedPtr& user, GuildOp op, GuildResult r, UInt32 seq, UInt32 param = 0)
        {
            GUILD_ACK_PACKET p;
            Service::Init(p, PacketType::GUILD_ACK);
            p.op = static_cast<UInt8>(op);
            p.result = static_cast<UInt8>(r);
            p.seq = seq;
            p.param = param;
            if (auto s = user->GetGameSession()) s->PostSend(p);
        }

        void FillTag(GUILD_NFY_PACKET& p, const UserSharedPtr& user)
        {
            Service::Init(p, PacketType::GUILD_NFY);
            p.event = static_cast<UInt8>(GuildEvent::Tag);
            p.obj = user->GetObjID();
            p.guildId = user->m_GuildId.load(std::memory_order_relaxed);
            p.emblem = user->m_GuildEmblem.load(std::memory_order_relaxed);
            p.rank = user->m_GuildRank;
            if (p.guildId != 0)
                (void)LookupName(p.guildId, p.name);
        }
    }

    void HandleRequest(const UserSharedPtr& user, const GUILD_REQ_PACKET& p)
    {
        if (user == nullptr)
            return;
        const auto op = static_cast<GuildOp>(p.op);
        if (op == GuildOp::Create && user->m_Transfer) return ZoneAck(user, op, GuildResult::Busy, p.seq);
        if (GGuildService == nullptr)
            return ZoneAck(user, op, GuildResult::Busy, p.seq);   // 켤 때 DB를 못 읽었다(서버 로그 [Phase5])
        if (IsMutating(op))
        {
            // 멱등: 같은(또는 지난) 순번은 한 번만 — 소유 Zone이 사람마다 기억한다
            // Duplicate의 param = 마지막으로 받은 순번(클라가 순번을 맞춘다 — GUILD.md 5장 「순번 계약」, 파티와 같다)
            if (p.seq <= user->m_LastGuildSeq)
                return ZoneAck(user, op, GuildResult::Duplicate, p.seq, user->m_LastGuildSeq);
            user->m_LastGuildSeq = p.seq;
        }

        switch (op)
        {
        case GuildOp::Create:
        {
            char name[sizeof(p.text) + 1]{};
            ::memcpy(name, p.text, sizeof(p.text));
            if (::strnlen(name, sizeof(name)) >= static_cast<size_t>(GuildRule::kNameBytes) || GuildRule::CheckName(name) != GuildRule::NameCheck::Ok)
                return ZoneAck(user, op, GuildResult::NameInvalid, p.seq);
            if (!GuildRule::ValidEmblem(p.arg8))
                return ZoneAck(user, op, GuildResult::Invalid, p.seq);
            if (!CNpcHelper::IsNear(GuildRule::kHallNpcId, user->GetX(), user->GetY()))
                return ZoneAck(user, op, GuildResult::NotAtNpc, p.seq);
            if (user->GetStat()->GetLevel() < GuildRule::kMinLevel)
                return ZoneAck(user, op, GuildResult::LevelTooLow, p.seq, GuildRule::kMinLevel);
            if (user->GetInventory()->GetAdena() < GuildRule::kCreateCost)   // 미리 거른다(실제 차감은 이름 확인 뒤)
                return ZoneAck(user, op, GuildResult::NoAdena, p.seq, GuildRule::kCreateCost);
            break;
        }
        case GuildOp::Emblem:
            if (!GuildRule::ValidEmblem(p.arg8))
                return ZoneAck(user, op, GuildResult::Invalid, p.seq);
            if (!CNpcHelper::IsNear(GuildRule::kHallNpcId, user->GetX(), user->GetY()))
                return ZoneAck(user, op, GuildResult::NotAtNpc, p.seq);
            break;
        case GuildOp::Notice:
        {
            char text[sizeof(p.text) + 1]{};
            ::memcpy(text, p.text, sizeof(p.text));
            const size_t n = ::strnlen(text, sizeof(text));
            if (n >= static_cast<size_t>(GuildRule::kNoticeBytes) || !CChatHelper::IsValidUtf8(text, n))
                return ZoneAck(user, op, GuildResult::Invalid, p.seq);
            break;
        }
        case GuildOp::Invite: case GuildOp::Kick: case GuildOp::SetRank: case GuildOp::Transfer:
            if (p.target[0] == '\0')
                return ZoneAck(user, op, GuildResult::TargetNotFound, p.seq);
            break;
        case GuildOp::Top:
        {
            // 깃발 뜰 순위 — 사람마다 2초에 한 번(넘치면 버린다. 클라는 마을에 있을 때 20초마다 · 결사 사건 뒤에 묻는다)
            const UInt32 now = static_cast<UInt32>(GetNowTime());
            if (user->m_LastGuildTopAt != 0 && now - user->m_LastGuildTopAt < GuildRule::kTopQueryMs)
                return;
            user->m_LastGuildTopAt = now;
            break;
        }
        case GuildOp::Info: case GuildOp::Members: case GuildOp::Name:
        case GuildOp::InviteReply: case GuildOp::Leave: case GuildOp::Disband:
            break;
        default:
            return ZoneAck(user, op, GuildResult::Invalid, p.seq);
        }

        const Service::Ctx ctx = Service::MakeCtx(user);
        const GUILD_REQ_PACKET copy = p;
        if (op == GuildOp::Create) ++user->m_PendingGuildEconomy;
        GGuildService->Post([ctx, copy]() { GGuildService->Handle(ctx, copy); });
    }

    void OnEnterWorld(const UserSharedPtr& user)
    {
        if (user == nullptr || GGuildService == nullptr)
            return;
        const Service::Ctx ctx = Service::MakeCtx(user);
        GGuildService->Post([ctx]() { GGuildService->OnLogin(ctx); });
    }

    void OnLeaveWorld(const UserSharedPtr& user)
    {
        if (user == nullptr || GGuildService == nullptr)
            return;
        const int playerId = user->GetPlayerId();
        const Int64 now = Service::UnixNow();
        GGuildService->Post([playerId, now]() { GGuildService->OnLogout(playerId, now); });
    }

    void OnLevelUp(const UserSharedPtr& user)
    {
        if (user == nullptr || GGuildService == nullptr || user->m_GuildId.load(std::memory_order_relaxed) == 0)
            return;
        const int playerId = user->GetPlayerId();
        const UInt8 level = user->GetStat()->GetLevel();
        GGuildService->Post([playerId, level]() { GGuildService->OnLevel(playerId, level); });
    }

    void RelayChat(const UserSharedPtr& sender, const char* body)
    {
        if (sender == nullptr || GGuildService == nullptr)
            return;
        const int playerId = sender->GetPlayerId();
        const ObjID obj = sender->GetObjID();
        std::string name = sender->GetName();
        std::string text = body;
        GGuildService->Post([playerId, obj, name = std::move(name), text = std::move(text)]()
        {
            GGuildService->RelayChat(playerId, obj, name, text);
        });
    }

    void SendTag(const UserSharedPtr& viewer, const UserSharedPtr& target)
    {
        if (viewer == nullptr || target == nullptr || target->m_GuildId.load(std::memory_order_relaxed) == 0)
            return;
        auto s = viewer->GetGameSession();
        if (s == nullptr)
            return;
        GUILD_NFY_PACKET p;
        FillTag(p, target);
        s->PostSend(p);
    }

    void BroadcastTag(const UserSharedPtr& user)
    {
        GUILD_NFY_PACKET p;
        FillTag(p, user);
        if (auto s = user->GetGameSession(); s != nullptr)
            s->PostSend(p);
        if (user->GetSectorX() < 0 || user->IsTransferring())
            return;   // 섹터에 없으면(죽음 · 이동 중) 주변도 없다 — 들어갈 때 시야 입장이 표시를 보낸다
        const ObjID self = user->GetObjID();
        GSector->ForEachNeighborObject(user->GetSectorX(), user->GetSectorY(), EnumCategory::eUser, [&](const SubjectSharedPtr& object)
        {
            if (object->GetObjID() == self || object->GetObjID().GetCategory<EnumCategory>() != EnumCategory::eUser)
                return;
            auto viewer = static_pointer_cast<CUser>(object);
            auto s = viewer->GetGameSession();
            if (s == nullptr || s->m_eState != SOCKET_STATE::ST_INGAME)
                return;
            if (CSubjectHelper::CanSee(object, user))
                s->PostSend(p);
        });
    }
}
