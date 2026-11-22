#include "pch.h"
#include "Content/Party/PartyService.h"

#include <algorithm>

#include "Content/Party/PartyHelper.h"
#include "Content/Social/SocialHelper.h"
#include "Database/GameData/PartyRules.h"
#include "Network/GameSessionManager.h"

std::shared_ptr<CPartyService> GPartyService;

bool CPartyService::CanSocialJoin(const Party& p,int playerId) const
{
    return std::none_of(p.members.begin(),p.members.end(),[&](const Member& m)
        { return CSocialHelper::Blocked(playerId,m.playerId); });
}
void CPartyService::OnBlock(int owner,int target)
{
    for(auto it=m_Invites.begin();it!=m_Invites.end();)
        if((it->first==owner && it->second.inviterId==target) || (it->first==target && it->second.inviterId==owner)) it=m_Invites.erase(it);
        else ++it;
    Party* p=PartyOf(owner);
    if(p && std::any_of(p->members.begin(),p->members.end(),[&](const Member& m){return m.playerId==target;}))
        if(const auto* ctx=OnlineCtx(owner)) { const auto copy=*ctx; Leave(copy,0); }
}

namespace
{
    void CopyStr(char* dst, size_t cap, const std::string& src)
    {
        ::strncpy_s(dst, cap, src.c_str(), _TRUNCATE);
    }

    [[nodiscard]] bool Expired(UInt32 now, UInt32 at) noexcept { return static_cast<Int32>(now - at) >= 0; }
}

// ─────────────────────────────────────────────────────────────────────────────
// 서비스 뼈대
// ─────────────────────────────────────────────────────────────────────────────
CPartyService::CPartyService() = default;

void CPartyService::Run()
{
    Service::LServiceId = Service::kPartyServiceId;
    m_Loop.Run();
}

void CPartyService::Post(Task task)
{
    m_Loop.Enqueue(std::move(task));
}

std::string CPartyService::NameKey(const std::string& name)
{
    std::string key = name;
    for (char& c : key)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return key;
}

CPartyService::Party* CPartyService::PartyOf(int playerId)
{
    const auto it = m_PartyOf.find(playerId);
    if (it == m_PartyOf.end())
        return nullptr;
    const auto p = m_Parties.find(it->second);
    return p == m_Parties.end() ? nullptr : &p->second;
}

CPartyService::Member* CPartyService::FindMember(Party& p, int playerId)
{
    for (auto& m : p.members)
        if (m.playerId == playerId) return &m;
    return nullptr;
}

CPartyService::Member* CPartyService::FindMemberByName(Party& p, const std::string& name)
{
    const std::string key = NameKey(name);
    for (auto& m : p.members)
        if (NameKey(m.name) == key) return &m;
    return nullptr;
}

int CPartyService::SlotOf(const Party& p, int playerId)
{
    for (size_t i = 0; i < p.members.size(); ++i)
        if (p.members[i].playerId == playerId) return static_cast<int>(i);
    return -1;
}

UInt8 CPartyService::MemberFlags(const Party& p, const Member& m)
{
    UInt8 flags = static_cast<UInt8>(m.state.flags & (PARTY_MEMBER_DEAD | PARTY_MEMBER_COMBAT));
    if (m.online) flags |= PARTY_MEMBER_ONLINE;
    else flags &= static_cast<UInt8>(~PARTY_MEMBER_COMBAT);
    if (m.playerId == p.leaderId) flags |= PARTY_MEMBER_LEADER;
    return flags;
}

bool CPartyService::CanInvite(const Party& p, int playerId) noexcept
{
    return p.leaderId == playerId || p.invitePolicy == static_cast<UInt8>(PartyInvitePolicy::Anyone);
}

const Service::Ctx* CPartyService::OnlineCtx(int playerId) const
{
    const auto it = m_Online.find(playerId);
    return it == m_Online.end() ? nullptr : &it->second;
}

// ─────────────────────────────────────────────────────────────────────────────
// 보내기
// ─────────────────────────────────────────────────────────────────────────────
void CPartyService::Ack(const Service::Ctx& ctx, PartyOp op, PartyResult r, UInt32 seq, UInt32 param) const
{
    PARTY_ACK_PACKET p;
    Service::Init(p, PacketType::PARTY_ACK);
    p.op = static_cast<UInt8>(op);
    p.result = static_cast<UInt8>(r);
    p.seq = seq;
    p.param = param;
    Service::SendTo(ctx.session, p);
}

void CPartyService::SendRoster(const SessionWeakPtr& session, const Party* p) const
{
    PARTY_ROSTER_INF_PACKET pk;
    Service::Init(pk, PacketType::PARTY_INF);
    pk.kind = static_cast<UInt8>(PartyInfKind::Roster);
    pk.mark = ObjID::npos;
    if (p == nullptr)
    {
        Service::SendTo(session, pk);   // 파티 없음(창을 비운다)
        return;
    }
    pk.partyId = p->id;
    pk.version = p->version;
    pk.leader = static_cast<UInt8>(std::max(0, SlotOf(*p, p->leaderId)));
    pk.invitePolicy = p->invitePolicy;
    pk.maxMembers = PartyRule::MaxMembers();
    pk.total = static_cast<UInt8>(p->members.size());
    pk.recruitId = p->recruitId;
    pk.mark = p->mark;
    // 4줄씩 — 8명이면 두 패킷(빈 파티는 없다: 마지막 사람이 나가면 해산)
    for (size_t first = 0; first < p->members.size(); first += PartyRule::kRosterRowsPerPacket)
    {
        pk.first = static_cast<UInt8>(first);
        pk.count = 0;
        std::memset(pk.rows, 0, sizeof(pk.rows));
        for (size_t i = first; i < p->members.size() && pk.count < PartyRule::kRosterRowsPerPacket; ++i)
        {
            const Member& m = p->members[i];
            PartyRosterRow& row = pk.rows[pk.count++];
            CopyStr(row.name, sizeof(row.name), m.name);
            row.obj = m.online ? m.obj : ObjID::npos;
            row.level = m.state.level;
            row.classId = m.classId;
            row.flags = MemberFlags(*p, m);
            row.zone = m.state.zone;
            row.hp = m.state.hp;
            row.maxHp = m.state.maxHp;
            row.mp = m.state.mp;
            row.maxMp = m.state.maxMp;
        }
        Service::SendTo(session, pk);
    }
}

void CPartyService::BroadcastRoster(const Party& p) const
{
    for (const auto& m : p.members)
        if (m.online)
            SendRoster(m.session, &p);
}

void CPartyService::SendMembers(const Party& p, UInt8 slotMask) const
{
    if (slotMask == 0)
        return;
    PARTY_MEMBERS_INF_PACKET pk;
    Service::Init(pk, PacketType::PARTY_INF);
    pk.kind = static_cast<UInt8>(PartyInfKind::Members);
    pk.partyId = p.id;
    pk.version = p.version;
    for (size_t i = 0; i < p.members.size() && i < static_cast<size_t>(PartyRule::kMaxMembers); ++i)
    {
        if ((slotMask & (1u << i)) == 0)
            continue;
        const Member& m = p.members[i];
        PartyMemberRow& row = pk.rows[pk.count++];
        row.slot = static_cast<UInt8>(i);
        row.level = m.state.level;
        row.flags = MemberFlags(p, m);
        row.zone = m.state.zone;
        row.x = m.state.x;
        row.y = m.state.y;
        row.hp = m.state.hp;
        row.maxHp = m.state.maxHp;
        row.mp = m.state.mp;
        row.maxMp = m.state.maxMp;
    }
    for (const auto& m : p.members)
        if (m.online)
            Service::SendTo(m.session, pk);
}

PARTY_NFY_PACKET CPartyService::MakeNfy(PartyEvent ev, const std::string& who, const std::string& by)
{
    PARTY_NFY_PACKET n;
    Service::Init(n, PacketType::PARTY_NFY);
    n.event = static_cast<UInt8>(ev);
    n.obj = ObjID::npos;
    n.target = ObjID::npos;
    CopyStr(n.who, sizeof(n.who), who);
    CopyStr(n.by, sizeof(n.by), by);
    return n;
}

void CPartyService::Notify(const Party& p, const PARTY_NFY_PACKET& n, int exceptPlayerId) const
{
    for (const auto& m : p.members)
        if (m.online && m.playerId != exceptPlayerId)
            Service::SendTo(m.session, n);
}

void CPartyService::SendRecruitList(const Service::Ctx& ctx, UInt8 purpose, UInt32 page) const
{
    // 글은 많아야 수백 — 요청마다 거르고 정렬해도 싸다(요청은 사람마다 0.5초에 한 번, 소유 Zone이 거른다)
    struct Item { const Recruit* r; UInt8 members; UInt8 seats; };
    std::vector<Item> list;
    list.reserve(m_Recruits.size());
    const UInt8 cap = PartyRule::MaxMembers();
    for (const auto& [id, r] : m_Recruits)
    {
        if (purpose != PartyRule::kAnyPurpose && r.purpose != purpose)
            continue;
        const auto pi = m_Parties.find(r.partyId);
        if (pi == m_Parties.end())
            continue;
        const UInt8 members = static_cast<UInt8>(pi->second.members.size());
        const UInt8 goal = std::min(r.slots, cap);
        list.push_back({ &r, members, static_cast<UInt8>(goal > members ? goal - members : 0) });
    }
    // 빈자리 있는 글 먼저 · 최근에 움직인 글 먼저 · 새 글 먼저
    std::sort(list.begin(), list.end(), [](const Item& a, const Item& b)
    {
        if ((a.seats > 0) != (b.seats > 0)) return a.seats > 0;
        if (a.r->activeAt != b.r->activeAt) return static_cast<Int32>(a.r->activeAt - b.r->activeAt) > 0;
        return a.r->id > b.r->id;
    });
    const int per = PartyRule::kRecruitRowsPerPage;
    const int pages = std::max(1, (static_cast<int>(list.size()) + per - 1) / per);
    const int pg = static_cast<int>(std::min<UInt32>(page, static_cast<UInt32>(pages - 1)));

    const auto applied = m_AppliedTo.find(ctx.playerId);
    const auto mine = m_PartyOf.find(ctx.playerId);
    PARTY_RECRUIT_LIST_INF_PACKET pk;
    Service::Init(pk, PacketType::PARTY_INF);
    pk.kind = static_cast<UInt8>(PartyInfKind::RecruitList);
    pk.purpose = purpose;
    pk.page = static_cast<UInt16>(pg);
    pk.pages = static_cast<UInt16>(std::min(pages, 0xFFFF));
    pk.total = static_cast<UInt16>(std::min<size_t>(list.size(), 0xFFFF));
    for (int i = pg * per; i < static_cast<int>(list.size()) && pk.count < per; ++i)
    {
        const Item& it = list[static_cast<size_t>(i)];
        const Recruit& r = *it.r;
        PartyRecruitRow& row = pk.rows[pk.count++];
        row.id = r.id;
        row.purpose = r.purpose;
        row.minLevel = r.minLevel;
        row.maxLevel = r.maxLevel;
        row.members = it.members;
        row.slots = std::min(r.slots, PartyRule::MaxMembers());
        row.flags = static_cast<UInt8>((r.autoJoin ? 1 : 0)
            | (mine != m_PartyOf.end() && mine->second == r.partyId ? 2 : 0)
            | (applied != m_AppliedTo.end() && applied->second == r.id ? 4 : 0));
        if (const auto pi = m_Parties.find(r.partyId); pi != m_Parties.end())
            for (const auto& m : pi->second.members)
                if (m.playerId == pi->second.leaderId) CopyStr(row.leader, sizeof(row.leader), m.name);
        CopyStr(row.title, sizeof(row.title), r.title);
    }
    Service::SendTo(ctx.session, pk);
}

void CPartyService::SendApplicants(const Service::Ctx& ctx, const Recruit& r) const
{
    PARTY_APPLICANTS_INF_PACKET pk;
    Service::Init(pk, PacketType::PARTY_INF);
    pk.kind = static_cast<UInt8>(PartyInfKind::Applicants);
    pk.recruitId = r.id;
    for (const auto& a : r.applicants)
    {
        if (pk.count >= PartyRule::kMaxApplicants)
            break;
        PartyApplicantRow& row = pk.rows[pk.count++];
        CopyStr(row.name, sizeof(row.name), a.name);
        row.level = a.level;
        row.classId = a.classId;
    }
    Service::SendTo(ctx.session, pk);
}

void CPartyService::ApplyPartyId(const ObjID& obj, UInt32 partyId)
{
    if (obj == ObjID::npos)
        return;
    const UInt32 seq = ++m_ApplySeq;
    Service::PostToOwner(obj, [partyId, seq](const UserSharedPtr& user)
    {
        if (user != nullptr)
            CPartyHelper::ApplyPartyId(user, partyId, seq);
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// 구조 변경 — 파티 만들기 · 들어옴 · 나감 · 파티장 · 해산 · 모집 글 닫기
// ─────────────────────────────────────────────────────────────────────────────
CPartyService::Party& CPartyService::CreateParty(const Service::Ctx& leader)
{
    Party p;
    p.id = m_NextPartyId++;
    p.leaderId = leader.playerId;
    p.invitePolicy = static_cast<UInt8>(PartyInvitePolicy::LeaderOnly);
    p.mark = ObjID::npos;
    Party& stored = m_Parties.emplace(p.id, std::move(p)).first->second;
    AddMember(stored, leader);
    return stored;
}

void CPartyService::AddMember(Party& p, const Service::Ctx& ctx)
{
    m_Invites.erase(ctx.playerId);
    Withdraw(ctx.playerId);

    Member m;
    m.playerId = ctx.playerId;
    m.name = ctx.name;
    m.obj = ctx.obj;
    m.session = ctx.session;
    m.classId = ctx.classId;
    m.online = m_Online.count(ctx.playerId) > 0;
    m.state.playerId = ctx.playerId;
    m.state.obj = ctx.obj;
    m.state.level = ctx.level;
    if (!m.online)
    {
        m.obj = ObjID::npos;
        m.offlineAt = GetNowTime();
    }
    p.members.push_back(std::move(m));
    ++p.version;
    m_PartyOf[ctx.playerId] = p.id;

    PARTY_NFY_PACKET n = MakeNfy(PartyEvent::Joined, ctx.name);
    n.obj = ctx.obj;
    Notify(p, n, ctx.playerId);
    BroadcastRoster(p);
    if (p.members.back().online)
        ApplyPartyId(ctx.obj, p.id);
    TouchRecruit(p);
}

void CPartyService::RemoveMember(Party& p, int playerId, PartyEvent reason, const std::string& by)
{
    const int slot = SlotOf(p, playerId);
    if (slot < 0)
        return;
    const Member gone = p.members[static_cast<size_t>(slot)];
    p.members.erase(p.members.begin() + slot);
    ++p.version;
    m_PartyOf.erase(playerId);

    // 나간 사람에게: 알림 · 빈 구성(창을 비운다) · Zone 사본 0
    if (gone.online)
    {
        PARTY_NFY_PACKET self = MakeNfy(reason, gone.name, by);
        Service::SendTo(gone.session, self);
        SendRoster(gone.session, nullptr);
        ApplyPartyId(gone.obj, 0);
    }
    if (p.members.empty())
    {
        Disband(p);
        return;
    }
    Notify(p, MakeNfy(reason, gone.name, by));
    if (p.leaderId == playerId)
        PassLeadership(p);
    BroadcastRoster(p);
}

void CPartyService::PassLeadership(Party& p)
{
    // 접속 중인 사람 중 먼저 들어온 사람(없으면 먼저 들어온 사람)
    const Member* next = nullptr;
    for (const auto& m : p.members)
        if (m.online) { next = &m; break; }
    if (next == nullptr && !p.members.empty())
        next = &p.members.front();
    if (next == nullptr || next->playerId == p.leaderId)
        return;
    p.leaderId = next->playerId;
    p.mark = ObjID::npos;   // 집중 표시는 표시한 파티장의 것
    Notify(p, MakeNfy(PartyEvent::LeaderChanged, next->name));
}

void CPartyService::Disband(Party& p)
{
    const UInt32 id = p.id;
    if (p.recruitId != 0)
        CloseRecruit(p.recruitId, PartyRecruitCloseReason::Disbanded);
    const PARTY_NFY_PACKET n = MakeNfy(PartyEvent::Disbanded);
    for (const auto& m : p.members)
    {
        m_PartyOf.erase(m.playerId);
        if (!m.online)
            continue;
        Service::SendTo(m.session, n);
        SendRoster(m.session, nullptr);
        ApplyPartyId(m.obj, 0);
    }
    // 이 파티로 가는 초대는 받아들일 때 InviteExpired가 된다(파티가 없다)
    m_Parties.erase(id);
}

void CPartyService::CloseRecruit(UInt32 recruitId, PartyRecruitCloseReason reason)
{
    const auto it = m_Recruits.find(recruitId);
    if (it == m_Recruits.end())
        return;
    Recruit r = std::move(it->second);
    m_Recruits.erase(it);

    PARTY_NFY_PACKET n = MakeNfy(PartyEvent::RecruitClosed);
    n.arg32 = r.id;
    n.arg8 = static_cast<UInt8>(reason);
    for (const auto& a : r.applicants)
    {
        m_AppliedTo.erase(a.playerId);
        if (const Service::Ctx* c = OnlineCtx(a.playerId))
            Service::SendTo(c->session, n);
    }
    if (const auto pi = m_Parties.find(r.partyId); pi != m_Parties.end())
    {
        Party& p = pi->second;
        p.recruitId = 0;
        if (reason != PartyRecruitCloseReason::Disbanded)
        {
            if (const Service::Ctx* c = OnlineCtx(p.leaderId))
                Service::SendTo(c->session, n);
            BroadcastRoster(p);   // 구성의 recruitId가 0으로
        }
    }
}

void CPartyService::Withdraw(int playerId)
{
    const auto it = m_AppliedTo.find(playerId);
    if (it == m_AppliedTo.end())
        return;
    if (const auto ri = m_Recruits.find(it->second); ri != m_Recruits.end())
    {
        auto& list = ri->second.applicants;
        list.erase(std::remove_if(list.begin(), list.end(), [playerId](const Applicant& a) { return a.playerId == playerId; }), list.end());
    }
    m_AppliedTo.erase(it);
}

void CPartyService::TouchRecruit(const Party& p)
{
    if (p.recruitId == 0)
        return;
    if (const auto it = m_Recruits.find(p.recruitId); it != m_Recruits.end())
        it->second.activeAt = GetNowTime();
}

// ─────────────────────────────────────────────────────────────────────────────
// 접속 · 틱 · 상태
// ─────────────────────────────────────────────────────────────────────────────
void CPartyService::OnLogin(const Service::Ctx& ctx)
{
    m_Online[ctx.playerId] = ctx;
    Party* p = PartyOf(ctx.playerId);
    if (p == nullptr)
    {
        SendRoster(ctx.session, nullptr);   // 파티 없음(창 초기화)
        return;
    }
    Member* m = FindMember(*p, ctx.playerId);
    if (m == nullptr)
        return;
    m->online = true;
    m->obj = ctx.obj;
    m->session = ctx.session;
    m->name = ctx.name;
    m->classId = ctx.classId;
    m->state = PartySyncRow{};
    m->state.playerId = ctx.playerId;
    m->state.obj = ctx.obj;
    m->state.level = ctx.level;
    ApplyPartyId(ctx.obj, p->id);
    PARTY_NFY_PACKET n = MakeNfy(PartyEvent::Online, ctx.name);
    n.obj = ctx.obj;
    Notify(*p, n, ctx.playerId);
    // 아무도 접속해 있지 않던 파티의 파티장이 끊겨 있으면 들어온 사람이 이어받는다
    if (const Member* leader = FindMember(*p, p->leaderId); leader == nullptr || !leader->online)
        PassLeadership(*p);
    BroadcastRoster(*p);
}

void CPartyService::OnLogout(int playerId, const ObjID& obj)
{
    if (const auto it = m_Online.find(playerId); it != m_Online.end() && it->second.obj == obj)
        m_Online.erase(it);
    else if (it != m_Online.end())
        return;   // 같은 계정이 이미 다시 들어왔다 — 옛 접속의 끝은 버린다
    m_Invites.erase(playerId);
    Withdraw(playerId);

    Party* p = PartyOf(playerId);
    if (p == nullptr)
        return;
    Member* m = FindMember(*p, playerId);
    if (m == nullptr || !m->online)
        return;
    m->online = false;
    m->obj = ObjID::npos;
    m->session.reset();
    m->offlineAt = GetNowTime();
    m->state.flags &= static_cast<UInt8>(~PARTY_MEMBER_COMBAT);
    Notify(*p, MakeNfy(PartyEvent::Offline, m->name), playerId);
    if (p->leaderId == playerId)
        PassLeadership(*p);
    BroadcastRoster(*p);
}

void CPartyService::Tick()
{
    const UInt32 now = GetNowTime();
    for (auto it = m_Invites.begin(); it != m_Invites.end();)
    {
        if (Expired(now, it->second.expiresAt))
            it = m_Invites.erase(it);
        else
            ++it;
    }

    // 오래 끊긴 파티원 — 모은 뒤 뺀다(빼는 동안 파티가 해산될 수 있다)
    const UInt32 keepMs = PartyRule::OfflineKeepSec() * 1000u;
    std::vector<std::pair<UInt32, int>> timedOut;
    for (const auto& [id, p] : m_Parties)
        for (const auto& m : p.members)
            if (!m.online && now - m.offlineAt >= keepMs)
                timedOut.emplace_back(id, m.playerId);
    for (const auto& [partyId, playerId] : timedOut)
        if (const auto pi = m_Parties.find(partyId); pi != m_Parties.end())
            RemoveMember(pi->second, playerId, PartyEvent::MemberTimeout);

    const UInt32 idleMs = PartyRule::RecruitIdleSec() * 1000u;
    std::vector<UInt32> idle;
    for (const auto& [id, r] : m_Recruits)
        if (now - r.activeAt >= idleMs)
            idle.push_back(id);
    for (const UInt32 id : idle)
        CloseRecruit(id, PartyRecruitCloseReason::Idle);
}

void CPartyService::OnSync(const std::vector<PartySyncRow>& rows)
{
    // 한 묶음(한 Zone의 한 틱)에 바뀐 파티원 — 파티마다 한 패킷으로
    std::vector<std::pair<UInt32, UInt8>> dirty;
    for (const auto& row : rows)
    {
        Party* p = PartyOf(row.playerId);
        if (p == nullptr)
            continue;
        const int slot = SlotOf(*p, row.playerId);
        if (slot < 0 || slot >= PartyRule::kMaxMembers)
            continue;
        Member& m = p->members[static_cast<size_t>(slot)];
        if (!m.online || m.obj != row.obj)
            continue;   // 끊긴 · 옛 접속의 줄
        m.state = row;
        auto it = std::find_if(dirty.begin(), dirty.end(), [p](const auto& d) { return d.first == p->id; });
        if (it == dirty.end())
            dirty.emplace_back(p->id, static_cast<UInt8>(1u << slot));
        else
            it->second = static_cast<UInt8>(it->second | (1u << slot));
    }
    for (const auto& [partyId, mask] : dirty)
        if (const auto pi = m_Parties.find(partyId); pi != m_Parties.end())
            SendMembers(pi->second, mask);
}

void CPartyService::OnDied(int playerId, const ObjID& obj, UInt16 zone, Int16 x, Int16 y)
{
    Party* p = PartyOf(playerId);
    if (p == nullptr)
        return;
    const int slot = SlotOf(*p, playerId);
    if (slot < 0 || slot >= PartyRule::kMaxMembers)
        return;
    Member& m = p->members[static_cast<size_t>(slot)];
    if (!m.online || m.obj != obj)
        return;
    m.state.flags |= PARTY_MEMBER_DEAD;
    m.state.hp = 0;
    m.state.zone = zone;
    m.state.x = x;
    m.state.y = y;
    PARTY_NFY_PACKET n = MakeNfy(PartyEvent::Died, m.name);
    n.obj = obj;
    n.arg16 = zone;
    n.arg32 = (static_cast<UInt32>(static_cast<UInt16>(x)) << 16) | static_cast<UInt16>(y);
    Notify(*p, n, playerId);
    SendMembers(*p, static_cast<UInt8>(1u << slot));
}

void CPartyService::OnRevived(int playerId, const ObjID& obj)
{
    Party* p = PartyOf(playerId);
    if (p == nullptr)
        return;
    Member* m = FindMember(*p, playerId);
    if (m == nullptr || !m->online || m->obj != obj)
        return;
    m->state.flags &= static_cast<UInt8>(~PARTY_MEMBER_DEAD);
    PARTY_NFY_PACKET n = MakeNfy(PartyEvent::Revived, m->name);
    n.obj = obj;
    Notify(*p, n, playerId);
    // HP · 위치는 다음 Zone 틱(살아나 섹터에 다시 들어갔다)이 보낸다
}

void CPartyService::RelayChat(int playerId, const ObjID& senderObj, const std::string& senderName, const std::string& body)
{
    const Party* p = PartyOf(playerId);
    if (p == nullptr)
        return;
    SC_CHAT_PACKET pk;
    Service::Init(pk, PacketType::SC_CHAT);
    pk.channel = static_cast<UInt8>(ChatChannel::Party);
    pk.senderId = senderObj;
    CopyStr(pk.senderName, sizeof(pk.senderName), senderName);
    ::strncpy_s(pk.mess, CHAT_SIZE, body.c_str(), _TRUNCATE);
    for (const auto& m : p->members)
        if (m.online && !CSocialHelper::Blocked(playerId,m.playerId))
            Service::SendTo(m.session, pk);
}

// ─────────────────────────────────────────────────────────────────────────────
// 요청(서비스 스레드) — 소유 Zone이 형식 · 멱등 · 도배를 거른 뒤에 온다
// ─────────────────────────────────────────────────────────────────────────────
void CPartyService::Handle(const Service::Ctx& ctx, const PARTY_REQ_PACKET& p)
{
    char target[NAME_SIZE + 1]{};
    ::memcpy(target, p.target, NAME_SIZE);
    char text[sizeof(p.text) + 1]{};
    ::memcpy(text, p.text, sizeof(p.text));

    const auto op = static_cast<PartyOp>(p.op);
    switch (op)
    {
    case PartyOp::Info:
        SendRoster(ctx.session, PartyOf(ctx.playerId));
        break;
    case PartyOp::Create:
        if (m_PartyOf.count(ctx.playerId))
            return Ack(ctx, op, PartyResult::AlreadyInParty, p.seq);
        Ack(ctx, op, PartyResult::Ok, p.seq, CreateParty(ctx).id);
        break;
    case PartyOp::Invite:            DoInvite(ctx, p.seq, target); break;
    case PartyOp::InviteReply:       Reply(ctx, p.seq, p.arg8 != 0, p.arg32); break;
    case PartyOp::Leave:             Leave(ctx, p.seq); break;
    case PartyOp::Kick:              Kick(ctx, p.seq, target); break;
    case PartyOp::Transfer:          Transfer(ctx, p.seq, target); break;
    case PartyOp::Disband:           DoDisband(ctx, p.seq); break;
    case PartyOp::Settings:          Settings(ctx, p.seq, p.arg8); break;
    case PartyOp::Target:            ShareTarget(ctx, p.obj); break;
    case PartyOp::Mark:              SetMark(ctx, p.seq, p.obj); break;
    case PartyOp::RecruitList:       SendRecruitList(ctx, p.arg8, p.arg32); break;
    case PartyOp::RecruitPost:       RecruitPost(ctx, p, text); break;
    case PartyOp::RecruitClose:      RecruitClose(ctx, p.seq); break;
    case PartyOp::RecruitApply:      RecruitApply(ctx, p.seq, p.arg32); break;
    case PartyOp::RecruitAccept:
    case PartyOp::RecruitReject:     RecruitDecide(ctx, op, p.seq, target); break;
    case PartyOp::RecruitApplicants: RecruitApplicants(ctx, p.seq); break;
    case PartyOp::RecruitCancel:     RecruitCancel(ctx, p.seq); break;
    default:                         Ack(ctx, op, PartyResult::Invalid, p.seq); break;
    }
}

void CPartyService::DoInvite(const Service::Ctx& ctx, UInt32 seq, const std::string& target)
{
    constexpr PartyOp op = PartyOp::Invite;
    const Party* p = PartyOf(ctx.playerId);
    if (p != nullptr)
    {
        if (!CanInvite(*p, ctx.playerId))
            return Ack(ctx, op, PartyResult::NoPermission, seq);
        if (p->members.size() >= PartyRule::MaxMembers())
            return Ack(ctx, op, PartyResult::Full, seq);
    }

    const auto session = GSessionManager->FindByName(target);
    const auto user = session != nullptr ? session->GetOwner() : nullptr;
    if (user == nullptr || session->m_eState != SOCKET_STATE::ST_INGAME)
        return Ack(ctx, op, PartyResult::TargetNotFound, seq);
    const int targetId = user->GetPlayerId();   // 로그인 때 한 번 정해지는 값
    if (targetId == ctx.playerId)
        return Ack(ctx, op, PartyResult::Invalid, seq);
    if (m_PartyOf.count(targetId))
        return Ack(ctx, op, PartyResult::TargetInParty, seq);
    if(CSocialHelper::Blocked(ctx.playerId,targetId) || (p && !CanSocialJoin(*p,targetId)))
        return Ack(ctx,op,PartyResult::NoPermission,seq);

    Invite inv;
    inv.id = m_NextInviteId++;
    inv.inviterId = ctx.playerId;
    inv.inviterName = ctx.name;
    inv.partyId = p != nullptr ? p->id : 0;
    inv.expiresAt = GetNowTime() + PartyRule::InviteExpireMs();
    m_Invites[targetId] = inv;

    PARTY_NFY_PACKET n = MakeNfy(PartyEvent::InviteFrom, ctx.name);
    n.obj = ctx.obj;
    n.arg32 = inv.id;
    n.arg8 = static_cast<UInt8>(p != nullptr ? p->members.size() : 0);
    Service::SendTo(session, n);
    Ack(ctx, op, PartyResult::Ok, seq, inv.id);
}

void CPartyService::Reply(const Service::Ctx& ctx, UInt32 seq, bool accept, UInt32 inviteId)
{
    constexpr PartyOp op = PartyOp::InviteReply;
    const auto it = m_Invites.find(ctx.playerId);
    if (it == m_Invites.end() || it->second.id != inviteId || Expired(GetNowTime(), it->second.expiresAt))
    {
        if (it != m_Invites.end() && it->second.id == inviteId) m_Invites.erase(it);
        return Ack(ctx, op, PartyResult::InviteExpired, seq);
    }
    const Invite inv = it->second;
    m_Invites.erase(it);

    if (!accept)
    {
        if (const Service::Ctx* inviter = OnlineCtx(inv.inviterId))
            Service::SendTo(inviter->session, MakeNfy(PartyEvent::InviteDeclined, ctx.name));
        return Ack(ctx, op, PartyResult::Ok, seq);
    }
    if (m_PartyOf.count(ctx.playerId))
        return Ack(ctx, op, PartyResult::AlreadyInParty, seq);

    // 들어갈 파티: 초대할 때의 파티 → (그때 없었으면) 지금 초대한 사람의 파티(초대할 수 있을 때) → 초대한 사람이 파티장인 새 파티
    Party* party = nullptr;
    if (inv.partyId != 0)
    {
        const auto pi = m_Parties.find(inv.partyId);
        if (pi == m_Parties.end())
            return Ack(ctx, op, PartyResult::InviteExpired, seq);   // 그 사이 해산
        // 초대는 초대한 사람의 지금 권한으로만 — 그 사이 나갔거나 · 쫓겨났거나 · 초대 권한을 잃었으면(설정 · 파티장 넘김) 만료.
        // (예전: 쫓겨난 파티원이 남긴 초대로 파티장이 모르는 사람이 들어왔다)
        if (FindMember(pi->second, inv.inviterId) == nullptr || !CanInvite(pi->second, inv.inviterId))
            return Ack(ctx, op, PartyResult::InviteExpired, seq);
        party = &pi->second;
    }
    else if (Party* now = PartyOf(inv.inviterId))
    {
        if (!CanInvite(*now, inv.inviterId))
            return Ack(ctx, op, PartyResult::InviteExpired, seq);
        party = now;
    }
    else
    {
        const Service::Ctx* inviter = OnlineCtx(inv.inviterId);
        if (inviter == nullptr)
            return Ack(ctx, op, PartyResult::InviteExpired, seq);   // 초대한 사람이 나갔다
        party = &CreateParty(*inviter);
    }
    if (party->members.size() >= PartyRule::MaxMembers())
        return Ack(ctx, op, PartyResult::Full, seq);
    if(CSocialHelper::Blocked(ctx.playerId,inv.inviterId) || !CanSocialJoin(*party,ctx.playerId))
        return Ack(ctx,op,PartyResult::NoPermission,seq);
    AddMember(*party, ctx);
    Ack(ctx, op, PartyResult::Ok, seq, party->id);
}

void CPartyService::Leave(const Service::Ctx& ctx, UInt32 seq)
{
    Party* p = PartyOf(ctx.playerId);
    if (p == nullptr)
        return Ack(ctx, PartyOp::Leave, PartyResult::NotInParty, seq);
    Ack(ctx, PartyOp::Leave, PartyResult::Ok, seq);
    RemoveMember(*p, ctx.playerId, PartyEvent::Left);
}

void CPartyService::Kick(const Service::Ctx& ctx, UInt32 seq, const std::string& target)
{
    constexpr PartyOp op = PartyOp::Kick;
    Party* p = PartyOf(ctx.playerId);
    if (p == nullptr)
        return Ack(ctx, op, PartyResult::NotInParty, seq);
    if (p->leaderId != ctx.playerId)
        return Ack(ctx, op, PartyResult::NoPermission, seq);
    const Member* m = FindMemberByName(*p, target);
    if (m == nullptr)
        return Ack(ctx, op, PartyResult::TargetNotFound, seq);
    if (m->playerId == ctx.playerId)
        return Ack(ctx, op, PartyResult::Invalid, seq);
    Ack(ctx, op, PartyResult::Ok, seq);
    RemoveMember(*p, m->playerId, PartyEvent::Kicked, ctx.name);
}

void CPartyService::Transfer(const Service::Ctx& ctx, UInt32 seq, const std::string& target)
{
    constexpr PartyOp op = PartyOp::Transfer;
    Party* p = PartyOf(ctx.playerId);
    if (p == nullptr)
        return Ack(ctx, op, PartyResult::NotInParty, seq);
    if (p->leaderId != ctx.playerId)
        return Ack(ctx, op, PartyResult::NoPermission, seq);
    const Member* m = FindMemberByName(*p, target);
    if (m == nullptr || !m->online)
        return Ack(ctx, op, PartyResult::TargetNotFound, seq);
    if (m->playerId == ctx.playerId)
        return Ack(ctx, op, PartyResult::Invalid, seq);
    p->leaderId = m->playerId;
    p->mark = ObjID::npos;
    Ack(ctx, op, PartyResult::Ok, seq);
    Notify(*p, MakeNfy(PartyEvent::LeaderChanged, m->name, ctx.name));
    BroadcastRoster(*p);
    TouchRecruit(*p);
}

void CPartyService::DoDisband(const Service::Ctx& ctx, UInt32 seq)
{
    Party* p = PartyOf(ctx.playerId);
    if (p == nullptr)
        return Ack(ctx, PartyOp::Disband, PartyResult::NotInParty, seq);
    if (p->leaderId != ctx.playerId)
        return Ack(ctx, PartyOp::Disband, PartyResult::NoPermission, seq);
    Ack(ctx, PartyOp::Disband, PartyResult::Ok, seq);
    Disband(*p);
}

void CPartyService::Settings(const Service::Ctx& ctx, UInt32 seq, UInt8 policy)
{
    Party* p = PartyOf(ctx.playerId);
    if (p == nullptr)
        return Ack(ctx, PartyOp::Settings, PartyResult::NotInParty, seq);
    if (p->leaderId != ctx.playerId)
        return Ack(ctx, PartyOp::Settings, PartyResult::NoPermission, seq);
    p->invitePolicy = policy;
    Ack(ctx, PartyOp::Settings, PartyResult::Ok, seq);
    PARTY_NFY_PACKET n = MakeNfy(PartyEvent::SettingsChanged, "", ctx.name);
    n.arg8 = policy;
    Notify(*p, n);
    BroadcastRoster(*p);
}

void CPartyService::ShareTarget(const Service::Ctx& ctx, const ObjID& target)
{
    // 대상 공유는 잦다 — 응답 없이 알림만(파티가 없으면 버린다)
    const Party* p = PartyOf(ctx.playerId);
    if (p == nullptr)
        return;
    PARTY_NFY_PACKET n = MakeNfy(PartyEvent::Target, ctx.name);
    n.obj = ctx.obj;
    n.target = target;
    Notify(*p, n, ctx.playerId);
}

void CPartyService::SetMark(const Service::Ctx& ctx, UInt32 seq, const ObjID& target)
{
    Party* p = PartyOf(ctx.playerId);
    if (p == nullptr)
        return Ack(ctx, PartyOp::Mark, PartyResult::NotInParty, seq);
    if (p->leaderId != ctx.playerId)
        return Ack(ctx, PartyOp::Mark, PartyResult::NoPermission, seq);
    p->mark = target;
    Ack(ctx, PartyOp::Mark, PartyResult::Ok, seq);
    PARTY_NFY_PACKET n = MakeNfy(PartyEvent::Mark, ctx.name);
    n.obj = ctx.obj;
    n.target = target;
    Notify(*p, n);
}

// ── 모집 게시판 ────────────────────────────────────────────────────────────
void CPartyService::RecruitPost(const Service::Ctx& ctx, const PARTY_REQ_PACKET& req, const std::string& title)
{
    constexpr PartyOp op = PartyOp::RecruitPost;
    Party* p = PartyOf(ctx.playerId);
    if (p != nullptr && p->leaderId != ctx.playerId)
        return Ack(ctx, op, PartyResult::NoPermission, req.seq);
    if (req.slots < 2 || req.slots > PartyRule::MaxMembers())
        return Ack(ctx, op, PartyResult::Invalid, req.seq);
    if (p != nullptr && p->members.size() >= req.slots)
        return Ack(ctx, op, PartyResult::Full, req.seq);   // 이미 목표 인원이다
    if (p == nullptr)
        p = &CreateParty(ctx);

    const bool fresh = p->recruitId == 0;
    if (fresh)
    {
        Recruit r;
        r.id = m_NextRecruitId++;
        r.partyId = p->id;
        p->recruitId = r.id;
        m_Recruits.emplace(r.id, std::move(r));
    }
    Recruit& r = m_Recruits[p->recruitId];
    r.purpose = req.purpose;
    r.minLevel = req.minLevel;
    r.maxLevel = req.maxLevel;
    r.slots = req.slots;
    r.autoJoin = req.arg8 != 0;
    r.title = title;
    r.activeAt = GetNowTime();   // 올림 · 고침 = 끌어올림
    Ack(ctx, op, PartyResult::Ok, req.seq, r.id);
    if (fresh)
        BroadcastRoster(*p);
}

void CPartyService::RecruitClose(const Service::Ctx& ctx, UInt32 seq)
{
    const Party* p = PartyOf(ctx.playerId);
    if (p == nullptr)
        return Ack(ctx, PartyOp::RecruitClose, PartyResult::NotInParty, seq);
    if (p->leaderId != ctx.playerId)
        return Ack(ctx, PartyOp::RecruitClose, PartyResult::NoPermission, seq);
    if (p->recruitId == 0)
        return Ack(ctx, PartyOp::RecruitClose, PartyResult::RecruitNotFound, seq);
    Ack(ctx, PartyOp::RecruitClose, PartyResult::Ok, seq);
    CloseRecruit(p->recruitId, PartyRecruitCloseReason::Leader);
}

void CPartyService::RecruitApply(const Service::Ctx& ctx, UInt32 seq, UInt32 recruitId)
{
    constexpr PartyOp op = PartyOp::RecruitApply;
    if (m_PartyOf.count(ctx.playerId))
        return Ack(ctx, op, PartyResult::AlreadyInParty, seq);
    const auto ri = m_Recruits.find(recruitId);
    if (ri == m_Recruits.end())
        return Ack(ctx, op, PartyResult::RecruitNotFound, seq);
    Recruit& r = ri->second;
    const auto pi = m_Parties.find(r.partyId);
    if (pi == m_Parties.end())
        return Ack(ctx, op, PartyResult::RecruitNotFound, seq);
    Party& party = pi->second;
    if(!CanSocialJoin(party,ctx.playerId)) return Ack(ctx,op,PartyResult::NoPermission,seq);
    if (ctx.level < r.minLevel || ctx.level > r.maxLevel)
        return Ack(ctx, op, PartyResult::LevelOutOfRange, seq, (static_cast<UInt32>(r.minLevel) << 8) | r.maxLevel);
    if (party.members.size() >= std::min(r.slots, PartyRule::MaxMembers()))
        return Ack(ctx, op, PartyResult::Full, seq);
    if (const auto a = m_AppliedTo.find(ctx.playerId); a != m_AppliedTo.end() && a->second == recruitId)
        return Ack(ctx, op, PartyResult::AlreadyApplied, seq);

    if (r.autoJoin)
    {
        if(!CanSocialJoin(party,ctx.playerId)) return Ack(ctx,op,PartyResult::NoPermission,seq);
        AddMember(party, ctx);   // 다른 글에 낸 신청 · 받은 초대는 AddMember가 거둔다
        return Ack(ctx, op, PartyResult::Ok, seq, 1);
    }
    if (r.applicants.size() >= static_cast<size_t>(PartyRule::kMaxApplicants))
        return Ack(ctx, op, PartyResult::Busy, seq);   // 승인 대기 자리가 없다
    Withdraw(ctx.playerId);   // 한 사람에 신청 하나 — 다른 글에 낸 것은 거둔다
    r.applicants.push_back({ ctx.playerId, ctx.name, ctx.level, ctx.classId });
    m_AppliedTo[ctx.playerId] = r.id;
    Ack(ctx, op, PartyResult::Ok, seq, 0);

    if (const Service::Ctx* leader = OnlineCtx(party.leaderId))
    {
        PARTY_NFY_PACKET n = MakeNfy(PartyEvent::RecruitApplied, ctx.name);
        n.obj = ctx.obj;
        n.arg8 = ctx.level;
        n.arg16 = ctx.classId;
        n.arg32 = r.id;
        Service::SendTo(leader->session, n);
    }
}

void CPartyService::RecruitDecide(const Service::Ctx& ctx, PartyOp op, UInt32 seq, const std::string& target)
{
    Party* p = PartyOf(ctx.playerId);
    if (p == nullptr)
        return Ack(ctx, op, PartyResult::NotInParty, seq);
    if (p->leaderId != ctx.playerId)
        return Ack(ctx, op, PartyResult::NoPermission, seq);
    const auto ri = m_Recruits.find(p->recruitId);
    if (ri == m_Recruits.end())
        return Ack(ctx, op, PartyResult::RecruitNotFound, seq);
    Recruit& r = ri->second;
    const std::string key = NameKey(target);
    const auto ai = std::find_if(r.applicants.begin(), r.applicants.end(), [&key](const Applicant& a) { return NameKey(a.name) == key; });
    if (ai == r.applicants.end())
        return Ack(ctx, op, PartyResult::TargetNotFound, seq);
    // 가득 찬 동안의 승인은 신청을 그대로 둔다(대기) — 예전엔 목록에서 지우고 신청자에게 알리지 않아 클라는 「신청 중」에 머물렀다
    if (op == PartyOp::RecruitAccept && p->members.size() >= std::min(r.slots, PartyRule::MaxMembers()))
        return Ack(ctx, op, PartyResult::Full, seq);
    // 차단 관계면 승인하지 않는다(11exchange 친구 · 차단). 가득 참과 같이 지우기 전에 — 지우고 알리지 않으면 신청자는 「신청 중」에 머문다. 거절은 그대로 된다.
    if (op == PartyOp::RecruitAccept && !CanSocialJoin(*p, ai->playerId))
        return Ack(ctx, op, PartyResult::NoPermission, seq);
    const Applicant who = *ai;
    r.applicants.erase(ai);
    m_AppliedTo.erase(who.playerId);
    r.activeAt = GetNowTime();
    const Service::Ctx* applicant = OnlineCtx(who.playerId);

    if (op == PartyOp::RecruitReject)
    {
        Ack(ctx, op, PartyResult::Ok, seq);
        if (applicant != nullptr)
        {
            PARTY_NFY_PACKET n = MakeNfy(PartyEvent::RecruitRejected, ctx.name);
            n.arg32 = r.id;
            Service::SendTo(applicant->session, n);
        }
        return;
    }
    if (applicant == nullptr)
        return Ack(ctx, op, PartyResult::TargetNotFound, seq);   // 그 사이 나갔다
    if (m_PartyOf.count(who.playerId))
        return Ack(ctx, op, PartyResult::TargetInParty, seq);
    PARTY_NFY_PACKET n = MakeNfy(PartyEvent::RecruitAccepted, ctx.name);
    n.arg32 = r.id;
    Service::SendTo(applicant->session, n);
    const Service::Ctx joiner = *applicant;   // AddMember가 m_Online을 바꾸지 않지만 사본으로
    AddMember(*p, joiner);
    Ack(ctx, op, PartyResult::Ok, seq);
}

void CPartyService::RecruitApplicants(const Service::Ctx& ctx, UInt32 seq)
{
    const Party* p = PartyOf(ctx.playerId);
    if (p == nullptr)
        return Ack(ctx, PartyOp::RecruitApplicants, PartyResult::NotInParty, seq);
    if (p->leaderId != ctx.playerId)
        return Ack(ctx, PartyOp::RecruitApplicants, PartyResult::NoPermission, seq);
    const auto ri = m_Recruits.find(p->recruitId);
    if (ri == m_Recruits.end())
        return Ack(ctx, PartyOp::RecruitApplicants, PartyResult::RecruitNotFound, seq);
    SendApplicants(ctx, ri->second);
}

void CPartyService::RecruitCancel(const Service::Ctx& ctx, UInt32 seq)
{
    if (!m_AppliedTo.count(ctx.playerId))
        return Ack(ctx, PartyOp::RecruitCancel, PartyResult::NotApplied, seq);
    Withdraw(ctx.playerId);
    Ack(ctx, PartyOp::RecruitCancel, PartyResult::Ok, seq);
}
