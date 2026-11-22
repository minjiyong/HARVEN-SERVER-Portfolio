#include "pch.h"
#include "Content/Auction/AuctionService.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "Database/GameData/GuildAuction.h"
#include "Database/GameData/ItemRules.h"
#include "Database/GameData/WorldData.h"
#include "Database/GameData/Item/ItemTable.h"
#include "Content/Npc/NpcHelper.h"
#include "User/UserHelper.h"
#include "Content/Achieve/AchieveHelper.h"
#include "Database/DBThread.h"

std::shared_ptr<CAuctionService> GAuctionService;

namespace
{
    constexpr UInt32 kMaxAdena = 2'000'000'000u;   // CInventory::AddAdena와 같은 상한(DB INT)
    constexpr Int64  kPriceLookbackSec = 14 * 24 * 3600;

    [[nodiscard]] AuctionResult FromTake(CInventory::TakeResult r) noexcept
    {
        switch (r)
        {
        case CInventory::TakeResult::Ok:       return AuctionResult::Ok;
        case CInventory::TakeResult::Equipped: return AuctionResult::Equipped;
        case CInventory::TakeResult::Bound:    return AuctionResult::Bound;
        default:                               return AuctionResult::BadItem;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 서비스 뼈대 · 불러오기
// ─────────────────────────────────────────────────────────────────────────────
CAuctionService::CAuctionService() = default;

void CAuctionService::Run()
{
    Service::LServiceId = Service::kAuctionServiceId;
    m_Loop.Run();
}

void CAuctionService::Post(Task task)
{
    m_Loop.Enqueue(std::move(task));
}

UInt32 CAuctionService::HourSec() noexcept
{
    static const UInt32 sec = []()
    {
        if (const char* v = std::getenv("HARVEN_AUCTION_HOUR_SEC"))
        {
            const long n = std::atol(v);
            if (n >= 1 && n <= 86400) return static_cast<UInt32>(n);
        }
        return AuctionRule::kHourSecDefault;
    }();
    return sec;
}

size_t CAuctionService::SettlementCount() const noexcept
{
    size_t n = 0;
    for (const auto& [pid, v] : m_Settlements) n += v.size();
    return n;
}

void CAuctionService::Index(Listing& l) const
{
    const ItemTableRow* row = ItemTable::Find(l.row.itemId);
    l.unit = l.row.price / std::max<UInt32>(1, l.row.count);
    if (row == nullptr)
        return;
    l.cat = static_cast<UInt8>(AuctionRule::CategoryOf(*row));
    l.sub = static_cast<UInt8>(AuctionRule::SubOf(*row));
    l.rarity = static_cast<UInt8>(ItemRules::EffectiveRarity(*row, l.row.extra));
    l.reqLevel = row->reqLevel;
    l.name = row->displayName != nullptr ? row->displayName : "";
}

bool CAuctionService::LoadFromDB(CDBConnection& db, Int64 maxListingId, Int64 maxSettlementId)
{
    std::vector<DB_LISTING_ROW> listings;
    std::vector<DB_SETTLEMENT_ROW> settles;
    std::vector<DB_PRICE_ROW> prices;
    if (!db.LoadAuctionListings(listings) || !db.LoadAuctionSettlements(settles)
        || !db.LoadAuctionPrices(prices, Service::UnixNow() - kPriceLookbackSec))
        return false;
    for (const auto& r : listings)
    {
        if (r.status != 0)
            continue;
        Listing l;
        l.row = r;
        Index(l);
        ++m_PerSeller[r.sellerId];
        m_ByCat[l.cat].insert(static_cast<UInt32>(r.listingId));
        m_Listings.emplace(static_cast<UInt32>(r.listingId), std::move(l));
    }
    for (const auto& r : settles)
    {
        Settlement s;
        s.row = r;
        m_Settlements[r.playerId].push_back(std::move(s));
    }
    for (auto& [pid, v] : m_Settlements)
        std::sort(v.begin(), v.end(), [](const Settlement& a, const Settlement& b) { return a.row.settlementId < b.row.settlementId; });
    std::sort(prices.begin(), prices.end(), [](const DB_PRICE_ROW& a, const DB_PRICE_ROW& b) { return a.soldAt < b.soldAt; });
    for (const auto& p : prices)
        RecordPrice(p.itemId, p.price);
    m_NextListingId = static_cast<UInt32>(std::max<Int64>(maxListingId, 0)) + 1;
    m_NextSettlementId = std::max<Int64>(maxSettlementId, 0) + 1;
    return true;
}

UInt16 CAuctionService::MinutesLeft(const DB_LISTING_ROW& r, Int64 now) const noexcept
{
    // 표시용 "게임 분" — 기간(시간) 단위와 같은 배율(시험에서 한 시간을 몇 초로 줄여도 12 · 24 · 48시간으로 보인다)
    const Int64 left = r.expiresAt - now;
    if (left <= 0)
        return 0;
    const Int64 minutes = left * 60 / HourSec();
    return static_cast<UInt16>(std::clamp<Int64>(minutes, 1, 65535));
}

int CAuctionService::UnclaimedCount(int playerId) const
{
    const auto it = m_Settlements.find(playerId);
    return it == m_Settlements.end() ? 0 : static_cast<int>(it->second.size());
}

void CAuctionService::RecordPrice(UInt16 itemId, UInt32 unit)
{
    auto& q = m_Prices[itemId];
    q.push_back(unit);
    while (q.size() > static_cast<size_t>(AuctionRule::kPriceHistory))
        q.pop_front();
}

// ─────────────────────────────────────────────────────────────────────────────
// 보내기
// ─────────────────────────────────────────────────────────────────────────────
void CAuctionService::Ack(const Service::Ctx& ctx, AuctionOp op, AuctionResult r, UInt32 seq, UInt32 id,
    UInt32 adena, Int32 delta, UInt16 itemId, UInt16 count) const
{
    AUCTION_ACK_PACKET p;
    Service::Init(p, PacketType::AUCTION_ACK);
    p.op = static_cast<UInt8>(op);
    p.result = static_cast<UInt8>(r);
    p.seq = seq;
    p.id = id;
    p.adena = adena;
    p.delta = delta;
    p.itemId = itemId;
    p.count = count;
    Service::SendTo(ctx.session, p);
    if (op == AuctionOp::Register || op == AuctionOp::Buy || op == AuctionOp::Cancel || op == AuctionOp::Claim)
        Service::PostToOwner(ctx.obj, [seq](const UserSharedPtr& u) { if (u) u->m_PendingAuctionSeqs.erase(seq); });
}

void CAuctionService::SendTo(int playerId, const AUCTION_NFY_PACKET& p) const
{
    const auto it = m_Online.find(playerId);
    if (it != m_Online.end())
        Service::SendTo(it->second.session, p);
}

void CAuctionService::NotifySettleCount(int playerId) const
{
    AUCTION_NFY_PACKET p;
    Service::Init(p, PacketType::AUCTION_NFY);
    p.event = static_cast<UInt8>(AuctionEvent::SettleCount);
    p.settleCount = static_cast<UInt16>(UnclaimedCount(playerId));
    SendTo(playerId, p);
}

// ─────────────────────────────────────────────────────────────────────────────
// 접속 · 틱
// ─────────────────────────────────────────────────────────────────────────────
void CAuctionService::OnLogin(const Service::Ctx& ctx)
{
    m_Online[ctx.playerId] = Online{ ctx.obj, ctx.session };
    NotifySettleCount(ctx.playerId);   // 0이어도 보낸다(메뉴 막대 점 초기화)
}

void CAuctionService::OnLogout(int playerId)
{
    m_Online.erase(playerId);
}

void CAuctionService::Tick()
{
    // 만료 확인: 기본 1분마다. 시험 배율(한 시간을 짧게)에서는 "게임 1분"마다(최소 1초)
    const UInt32 now = GetNowTime();
    const UInt32 interval = std::clamp<UInt32>(HourSec() * 1000u / 60u, 1000u, AuctionRule::kExpireTickMs);
    if (m_LastExpireAt != 0 && now - m_LastExpireAt < interval)
        return;
    m_LastExpireAt = now;
    const Int64 unixNow = Service::UnixNow();
    int budget = 64;   // 한 번에 몰리지 않게(남은 것은 다음 틱)
    std::vector<UInt32> due;
    for (const auto& [id, l] : m_Listings)
    {
        if (l.state == State::Selling && l.row.expiresAt <= unixNow)
        {
            due.push_back(id);
            if (--budget == 0) break;
        }
    }
    for (const UInt32 id : due)
        Close(id, 3, std::nullopt, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// 요청(서비스 스레드)
// ─────────────────────────────────────────────────────────────────────────────
void CAuctionService::Handle(const Service::Ctx& ctx, const AUCTION_REQ_PACKET& p)
{
    switch (static_cast<AuctionOp>(p.op))
    {
    case AuctionOp::Search:      Search(ctx, p); break;
    case AuctionOp::Mine:        SendMine(ctx, p.page); break;
    case AuctionOp::Settlements: SendSettlements(ctx, p.page); break;
    case AuctionOp::PriceHint:   PriceHint(ctx, p.itemId, p.minRarity); break;
    case AuctionOp::Register:    Register(ctx, p); break;
    case AuctionOp::Buy:         Buy(ctx, p.seq, p.id, p.price); break;
    case AuctionOp::Cancel:      Cancel(ctx, p.seq, p.id); break;
    case AuctionOp::Claim:       Claim(ctx, p.seq, p.id); break;
    default:                     Ack(ctx, static_cast<AuctionOp>(p.op), AuctionResult::Invalid, p.seq); break;
    }
}

void CAuctionService::Search(const Service::Ctx& ctx, const AUCTION_REQ_PACKET& p) const
{
    char query[sizeof(p.query) + 1]{};
    ::memcpy(query, p.query, sizeof(p.query));
    const size_t qlen = ::strnlen(query, sizeof(query));
    const UInt8 wantOption = p.option;

    std::vector<const Listing*> hits;
    // 분류를 골랐으면 그 분류의 색인만 훑는다(전체면 모두)
    auto consider = [&](const Listing& l)
    {
        if (l.state == State::Closing) return;
        if (p.sub != 0 && l.sub != p.sub) return;
        if (l.rarity < p.minRarity) return;
        if (l.row.enchant < p.minEnchant) return;
        if (l.reqLevel < p.minLevel) return;
        if (p.maxLevel != 0 && l.reqLevel > p.maxLevel) return;
        if (wantOption != 0)
        {
            const ItemInstance inst = ItemInstance::Unpack(l.row.extra);
            bool has = false;
            for (const auto& o : inst.opts) if (static_cast<UInt8>(o.kind) == wantOption) has = true;
            if (!has) return;
        }
        if (qlen > 0 && std::strstr(l.name, query) == nullptr) return;
        hits.push_back(&l);
    };
    if (p.cat != 0 && p.cat < static_cast<UInt8>(AuctionRule::Cat::Count))
    {
        for (const UInt32 id : m_ByCat[p.cat])
            if (const auto it = m_Listings.find(id); it != m_Listings.end()) consider(it->second);
    }
    else
        for (const auto& [id, l] : m_Listings) consider(l);
    switch (static_cast<AuctionRule::Sort>(p.sort))
    {
    case AuctionRule::Sort::PriceDesc:
        std::sort(hits.begin(), hits.end(), [](const Listing* a, const Listing* b) { return a->unit != b->unit ? a->unit > b->unit : a->row.listingId < b->row.listingId; });
        break;
    case AuctionRule::Sort::TimeLeft:
        std::sort(hits.begin(), hits.end(), [](const Listing* a, const Listing* b) { return a->row.expiresAt != b->row.expiresAt ? a->row.expiresAt < b->row.expiresAt : a->row.listingId < b->row.listingId; });
        break;
    case AuctionRule::Sort::RarityDesc:
        std::sort(hits.begin(), hits.end(), [](const Listing* a, const Listing* b)
        {
            if (a->rarity != b->rarity) return a->rarity > b->rarity;
            if (a->row.enchant != b->row.enchant) return a->row.enchant > b->row.enchant;
            return a->unit != b->unit ? a->unit < b->unit : a->row.listingId < b->row.listingId;
        });
        break;
    default:
        std::sort(hits.begin(), hits.end(), [](const Listing* a, const Listing* b) { return a->unit != b->unit ? a->unit < b->unit : a->row.listingId < b->row.listingId; });
        break;
    }

    const int per = AuctionRule::kPageRows;
    const int total = static_cast<int>(hits.size());
    const int pages = std::max(1, (total + per - 1) / per);
    const int page = std::min<int>(p.page, pages - 1);
    const Int64 now = Service::UnixNow();

    AUCTION_LIST_INF_PACKET out;
    Service::Init(out, PacketType::AUCTION_LIST_INF);
    out.kind = static_cast<UInt8>(AuctionListKind::Search);
    out.page = static_cast<UInt16>(page);
    out.pages = static_cast<UInt16>(pages);
    out.total = static_cast<UInt32>(total);
    for (int i = page * per; i < total && out.count < per; ++i)
    {
        const Listing& l = *hits[static_cast<size_t>(i)];
        AuctionRow& row = out.rows[out.count++];
        row.id = static_cast<UInt32>(l.row.listingId);
        row.itemId = l.row.itemId;
        row.count = l.row.count;
        row.enchant = EnchantRule::LegacyLevel(l.row.enchant);
        row.extra = l.row.extra;
        row.price = l.row.price;
        row.minutesLeft = MinutesLeft(l.row, now);
        row.flags = static_cast<UInt8>((l.row.sellerId == ctx.playerId ? 1 : 0) | (l.state == State::Buying ? 2 : 0));
    }
    Service::SendTo(ctx.session, out);
    for (UInt8 i = 0; i < out.count; ++i)
    {
        const auto& row = out.rows[i];
        AUCTION_ENCHANT_LEVEL_INF_PACKET p{};
        Service::Init(p, PacketType::AUCTION_ENCHANT_LEVEL_INF);
        p.id = row.id; p.op = out.kind;
        const auto li = m_Listings.find(row.id);
            if (li != m_Listings.end()) p.level = li->second.row.enchant;
        Service::SendTo(ctx.session, p);
    }
}

void CAuctionService::SendMine(const Service::Ctx& ctx, UInt16 page) const
{
    std::vector<const Listing*> mine;
    for (const auto& [id, l] : m_Listings)
        if (l.row.sellerId == ctx.playerId) mine.push_back(&l);
    std::sort(mine.begin(), mine.end(), [](const Listing* a, const Listing* b) { return a->row.listingId > b->row.listingId; });
    const int per = AuctionRule::kPageRows;
    const int total = static_cast<int>(mine.size());
    const int pages = std::max(1, (total + per - 1) / per);
    const int pg = std::min<int>(page, pages - 1);
    const Int64 now = Service::UnixNow();

    AUCTION_LIST_INF_PACKET out;
    Service::Init(out, PacketType::AUCTION_LIST_INF);
    out.kind = static_cast<UInt8>(AuctionListKind::Mine);
    out.page = static_cast<UInt16>(pg);
    out.pages = static_cast<UInt16>(pages);
    out.total = static_cast<UInt32>(total);
    for (int i = pg * per; i < total && out.count < per; ++i)
    {
        const Listing& l = *mine[static_cast<size_t>(i)];
        AuctionRow& row = out.rows[out.count++];
        row.id = static_cast<UInt32>(l.row.listingId);
        row.itemId = l.row.itemId;
        row.count = l.row.count;
        row.enchant = EnchantRule::LegacyLevel(l.row.enchant);
        row.extra = l.row.extra;
        row.price = l.row.price;
        row.minutesLeft = MinutesLeft(l.row, now);
        row.flags = static_cast<UInt8>(1 | (l.state != State::Selling ? 2 : 0));
    }
    Service::SendTo(ctx.session, out);
    for (UInt8 i = 0; i < out.count; ++i)
    {
        const auto& row = out.rows[i];
        AUCTION_ENCHANT_LEVEL_INF_PACKET p{};
        Service::Init(p, PacketType::AUCTION_ENCHANT_LEVEL_INF);
        p.id = row.id; p.op = out.kind;
        const auto li = m_Listings.find(row.id);
            if (li != m_Listings.end()) p.level = li->second.row.enchant;
        Service::SendTo(ctx.session, p);
    }
}

void CAuctionService::SendSettlements(const Service::Ctx& ctx, UInt16 page) const
{
    AUCTION_LIST_INF_PACKET out;
    Service::Init(out, PacketType::AUCTION_LIST_INF);
    out.kind = static_cast<UInt8>(AuctionListKind::Settlements);
    const auto it = m_Settlements.find(ctx.playerId);
    const int total = it == m_Settlements.end() ? 0 : static_cast<int>(it->second.size());
    const int per = AuctionRule::kPageRows;
    const int pages = std::max(1, (total + per - 1) / per);
    const int pg = std::min<int>(page, pages - 1);
    out.page = static_cast<UInt16>(pg);
    out.pages = static_cast<UInt16>(pages);
    out.total = static_cast<UInt32>(total);
    for (int i = pg * per; i < total && out.count < per; ++i)
    {
        const Settlement& s = it->second[static_cast<size_t>(i)];
        AuctionRow& row = out.rows[out.count++];
        row.id = static_cast<UInt32>(s.row.settlementId);
        row.itemId = s.row.kind == 2 ? s.row.itemId : 0;
        row.count = s.row.kind == 2 ? s.row.count : 0;
        row.enchant = EnchantRule::LegacyLevel(s.row.enchant);
        row.extra = s.row.extra;
        row.price = s.row.kind == 1 ? s.row.adena : 0;
        row.minutesLeft = s.row.reason;          // 정산함: 사유(AuctionSettleReason)
        row.flags = s.claiming ? 2 : 0;
    }
    Service::SendTo(ctx.session, out);
    for (UInt8 i = 0; i < out.count; ++i)
    {
        const auto& row = out.rows[i];
        AUCTION_ENCHANT_LEVEL_INF_PACKET p{};
        Service::Init(p, PacketType::AUCTION_ENCHANT_LEVEL_INF);
        p.id = row.id; p.op = out.kind;
        const auto si = m_Settlements.find(ctx.playerId);
            if (si != m_Settlements.end()) for (const auto& entry : si->second) if (static_cast<UInt32>(entry.row.settlementId) == row.id) p.level = entry.row.enchant;
        Service::SendTo(ctx.session, p);
    }
}

void CAuctionService::PriceHint(const Service::Ctx& ctx, UInt16 itemId, UInt8 minRarity) const
{
    AUCTION_NFY_PACKET p;
    Service::Init(p, PacketType::AUCTION_NFY);
    p.event = static_cast<UInt8>(AuctionEvent::PriceHint);
    p.itemId = itemId;
    for (const auto& [id, l] : m_Listings)
        if (l.row.itemId == itemId && l.state == State::Selling && l.rarity >= minRarity && (p.lowest == 0 || l.unit < p.lowest))
            p.lowest = l.unit;
    if (const auto it = m_Prices.find(itemId); it != m_Prices.end() && !it->second.empty())
    {
        UInt64 sum = 0;
        for (const UInt32 v : it->second) sum += v;
        p.lastSold = it->second.back();
        p.soldCount = static_cast<UInt16>(it->second.size());
        p.avgSold = static_cast<UInt32>(sum / it->second.size());
    }
    Service::SendTo(ctx.session, p);
}

// ── 등록 ──
void CAuctionService::Register(const Service::Ctx& ctx, const AUCTION_REQ_PACKET& p)
{
    const UInt32 seq = p.seq;
    if (m_Inflight.count(ctx.playerId))
        return Ack(ctx, AuctionOp::Register, AuctionResult::Busy, seq);
    if (m_PerSeller[ctx.playerId] >= AuctionRule::kMaxListingsPerSeller)
        return Ack(ctx, AuctionOp::Register, AuctionResult::TooMany, seq);

    const Int64 now = Service::UnixNow();
    DB_LISTING_ROW row;
    row.listingId = m_NextListingId++;
    row.sellerId = ctx.playerId;
    row.price = p.price;
    row.deposit = AuctionRule::Deposit(p.price);
    row.createdAt = now;
    row.expiresAt = now + static_cast<Int64>(AuctionRule::kDurationHours[p.duration % AuctionRule::kDurationCount]) * HourSec();
    ++m_PerSeller[ctx.playerId];
    m_Inflight.insert(ctx.playerId);

    const UInt16 slot = p.slot;
    const UInt16 count = p.count;
    const UInt16 expectItem = p.itemId;
    Service::PostToOwner(ctx.obj, [ctx, seq, row, slot, count, expectItem](const UserSharedPtr& user)
    {
        auto abort = [=](AuctionResult r)
        {
            GAuctionService->Post([=]()
            {
                auto& svc = *GAuctionService;
                --svc.m_PerSeller[ctx.playerId];
                svc.m_Inflight.erase(ctx.playerId);
                svc.Ack(ctx, AuctionOp::Register, r, seq);
            });
        };
        if (user == nullptr)
            return abort(AuctionResult::Invalid);
        auto inv = user->GetInventory();
        if (inv->GetAdena() < row.deposit)
            return abort(AuctionResult::NoAdena);
        const auto& slots = inv->GetSlots();
        const auto si = slots.find(slot);
        if (si == slots.end() || si->second->GetItemTableId() != expectItem)
            return abort(AuctionResult::BadItem);   // 그 사이 칸이 바뀌었다

        CInventory::TakenItem taken;
        DB_ITEM_SLOT_SAVE remain;
        const auto tr = inv->TakeForListing(slot, count, taken, remain);
        if (tr == CInventory::TakeResult::Bound)
            CUserHelper::SendSystem(user, SystemMessageCode::ItemBound, static_cast<Int32>(expectItem), slot);
        if (tr != CInventory::TakeResult::Ok)
            return abort(FromTake(tr));
        (void)inv->ReserveAdena(row.deposit, AdenaReason::AuctionDeposit);   // 위에서 확인했다
        const UInt32 adenaAfter = inv->GetAdena();

        DB_LISTING_ROW full = row;
        full.itemId = taken.itemId;
        full.count = taken.count;
        full.enchant = taken.enchant;
        full.extra = taken.extra;
        const int sellerId = ctx.playerId;
        const ObjID obj = ctx.obj;
        GDBThread->RequestTx(sellerId, "AuctionRegister",
            [=](CDBConnection& c) { return c.AuctionRegister(sellerId, adenaAfter, remain, full); },
            [=](bool ok)
            {
                if (!ok)
                    Service::PostToOwner(obj, [=](const UserSharedPtr& u)
                    {
                        if (u == nullptr) return;   // 나갔다 — 커밋되지 않았으니 DB는 그대로
                        u->GetInventory()->RestoreTaken(slot, taken);
                        u->GetInventory()->ReturnAdena(full.deposit, AdenaReason::Refund);
                    });
                GAuctionService->Post([=]() { GAuctionService->FinishRegister(ok, ctx, seq, full, adenaAfter); });
            });
    });
}

void CAuctionService::FinishRegister(bool ok, const Service::Ctx& ctx, UInt32 seq, const DB_LISTING_ROW& row, UInt32 adenaAfter)
{
    m_Inflight.erase(ctx.playerId);
    if (!ok)
    {
        --m_PerSeller[ctx.playerId];
        return Ack(ctx, AuctionOp::Register, AuctionResult::Invalid, seq);
    }
    Listing l;
    l.row = row;
    Index(l);
    m_ByCat[l.cat].insert(static_cast<UInt32>(row.listingId));
    m_Listings.emplace(static_cast<UInt32>(row.listingId), std::move(l));
    Ack(ctx, AuctionOp::Register, AuctionResult::Ok, seq, static_cast<UInt32>(row.listingId), adenaAfter,
        -static_cast<Int32>(row.deposit), row.itemId, row.count);
}

// ── 구매 ──
void CAuctionService::Buy(const Service::Ctx& ctx, UInt32 seq, UInt32 listingId, UInt32 price)
{
    if (m_Inflight.count(ctx.playerId))
        return Ack(ctx, AuctionOp::Buy, AuctionResult::Busy, seq, listingId);
    const auto it = m_Listings.find(listingId);
    if (it == m_Listings.end() || it->second.state == State::Closing)
        return Ack(ctx, AuctionOp::Buy, AuctionResult::NotFound, seq, listingId);
    Listing& l = it->second;
    if (l.state == State::Buying)
        return Ack(ctx, AuctionOp::Buy, AuctionResult::Busy, seq, listingId);   // 다른 사람이 사는 중
    if (l.row.sellerId == ctx.playerId || l.row.price != price)
        return Ack(ctx, AuctionOp::Buy, AuctionResult::Invalid, seq, listingId);
    // 경제 1차 전에 올라온 목록이 지금은 귀속인 물건(보스 세트 · 전설)이면 팔지 않는다 — 기간이 끝나면 판 사람에게 돌아간다
    if (const ItemTableRow* listed = ItemTable::Find(l.row.itemId); listed != nullptr && ItemRules::IsBound(*listed, l.row.extra))
        return Ack(ctx, AuctionOp::Buy, AuctionResult::Bound, seq, listingId);

    // 잠금 — 이 순간부터 같은 목록의 다른 구매는 위에서 걸러진다(진 쪽은 그로시가 빠지지 않는다)
    l.state = State::Buying;
    l.buyer = ctx.playerId;
    m_Inflight.insert(ctx.playerId);
    const Int64 buyerSettle = m_NextSettlementId++;
    const Int64 sellerSettle = m_NextSettlementId++;
    const UInt32 sellerGets = AuctionRule::SellerGets(price);
    const Int64 now = Service::UnixNow();
    // 산 장비는 거래 이력(인스턴스 bit 9 — 분해량 traded_dismantle_percent)을 남긴다 — 구매자 정산 줄의 extra에(DB · 메모리 같게)
    const ItemTableRow* bought = ItemTable::Find(l.row.itemId);
    const UInt64 tradedBit = bought != nullptr ? ItemRules::TradedBitFor(*bought) : 0;

    Service::PostToOwner(ctx.obj, [=](const UserSharedPtr& user)
    {
        auto unlock = [=](AuctionResult r)
        {
            GAuctionService->Post([=]()
            {
                auto& svc = *GAuctionService;
                svc.m_Inflight.erase(ctx.playerId);
                if (const auto li = svc.m_Listings.find(listingId); li != svc.m_Listings.end() && li->second.state == State::Buying && li->second.buyer == ctx.playerId)
                    li->second.state = State::Selling;
                svc.Ack(ctx, AuctionOp::Buy, r, seq, listingId);
            });
        };
        if (user == nullptr)
            return unlock(AuctionResult::Invalid);
        if (!user->GetInventory()->ReserveAdena(price, AdenaReason::AuctionBuy))
            return unlock(AuctionResult::NoAdena);
        const UInt32 adenaAfter = user->GetInventory()->GetAdena();
        const int buyerId = ctx.playerId;
        const ObjID obj = ctx.obj;
        GDBThread->RequestTx(buyerId, "AuctionBuy",
            [=](CDBConnection& c) { return c.AuctionBuy(buyerId, adenaAfter, listingId, price, buyerSettle, sellerSettle, sellerGets, now, tradedBit); },
            [=](bool ok)
            {
                if (!ok)
                    Service::PostToOwner(obj, [price](const UserSharedPtr& u)
                    {
                        if (u != nullptr) u->GetInventory()->ReturnAdena(price, AdenaReason::Refund);
                    });
                else   // 개인 업적(경매 구매) — 커밋이 승부를 정했다. 구매자의 소유 Zone에서
                    Service::PostToOwner(obj, [](const UserSharedPtr& u) { if (u != nullptr) CAchieveHelper::OnAuctionBought(u); });
                GAuctionService->Post([=]() { GAuctionService->FinishBuy(ok, ctx, seq, listingId, buyerSettle, sellerSettle, adenaAfter, now); });
            });
    });
}

void CAuctionService::FinishBuy(bool ok, const Service::Ctx& ctx, UInt32 seq, UInt32 listingId, Int64 buyerSettle, Int64 sellerSettle,
    UInt32 adenaAfter, Int64 now)
{
    m_Inflight.erase(ctx.playerId);
    const auto it = m_Listings.find(listingId);
    if (it == m_Listings.end())
        return Ack(ctx, AuctionOp::Buy, AuctionResult::NotFound, seq, listingId);
    Listing& l = it->second;
    if (!ok)
    {
        if (l.state == State::Buying && l.buyer == ctx.playerId)
            l.state = State::Selling;
        return Ack(ctx, AuctionOp::Buy, AuctionResult::Invalid, seq, listingId);
    }

    // 커밋됐다: 목록을 닫고 정산함 두 줄(구매자 물건 · 판매자 대금), 시세
    const DB_LISTING_ROW row = l.row;
    m_ByCat[l.cat].erase(listingId);
    m_Listings.erase(it);
    if (--m_PerSeller[row.sellerId] <= 0)
        m_PerSeller.erase(row.sellerId);

    Settlement item;
    item.row.settlementId = buyerSettle;
    item.row.playerId = ctx.playerId;
    item.row.kind = 2;
    item.row.itemId = row.itemId;
    item.row.count = row.count;
    item.row.enchant = row.enchant;
    const ItemTableRow* bought = ItemTable::Find(row.itemId);
    item.row.extra = row.extra | (bought != nullptr ? ItemRules::TradedBitFor(*bought) : 0);   // 산 장비 — 거래 이력(bit 9, P_AUCTION_BUY와 같게)
    item.row.listingId = row.listingId;
    item.row.reason = static_cast<UInt8>(AuctionSettleReason::Bought);
    item.row.createdAt = now;
    m_Settlements[ctx.playerId].push_back(item);

    const UInt32 gets = AuctionRule::SellerGets(row.price);
    Settlement money;
    money.row.settlementId = sellerSettle;
    money.row.playerId = row.sellerId;
    money.row.kind = 1;
    money.row.adena = gets;
    money.row.listingId = row.listingId;
    money.row.reason = static_cast<UInt8>(AuctionSettleReason::Sold);
    money.row.createdAt = now;
    m_Settlements[row.sellerId].push_back(money);

    RecordPrice(row.itemId, row.price / std::max<UInt32>(1, row.count));

    Ack(ctx, AuctionOp::Buy, AuctionResult::Ok, seq, listingId, adenaAfter, -static_cast<Int32>(row.price), row.itemId, row.count);
    NotifySettleCount(ctx.playerId);

    AUCTION_NFY_PACKET sold;
    Service::Init(sold, PacketType::AUCTION_NFY);
    sold.event = static_cast<UInt8>(AuctionEvent::Sold);
    sold.id = listingId;
    sold.itemId = row.itemId;
    sold.price = gets;
    sold.settleCount = static_cast<UInt16>(UnclaimedCount(row.sellerId));
    SendTo(row.sellerId, sold);
    std::cout << "[Auction] sold listing=" << listingId << " buyer=" << ctx.playerId << " seller=" << row.sellerId << " price=" << row.price << "\n" << std::flush;
}

// ── 취소 · 만료 ──
void CAuctionService::Cancel(const Service::Ctx& ctx, UInt32 seq, UInt32 listingId)
{
    if (m_Inflight.count(ctx.playerId))
        return Ack(ctx, AuctionOp::Cancel, AuctionResult::Busy, seq, listingId);
    const auto it = m_Listings.find(listingId);
    if (it == m_Listings.end())
        return Ack(ctx, AuctionOp::Cancel, AuctionResult::NotFound, seq, listingId);
    if (it->second.row.sellerId != ctx.playerId)
        return Ack(ctx, AuctionOp::Cancel, AuctionResult::NotYours, seq, listingId);
    if (it->second.state != State::Selling)
        return Ack(ctx, AuctionOp::Cancel, AuctionResult::Busy, seq, listingId);   // 누가 사는 중
    m_Inflight.insert(ctx.playerId);
    Close(listingId, 2, ctx, seq);
}

void CAuctionService::Close(UInt32 listingId, UInt8 status, const std::optional<Service::Ctx>& ctx, UInt32 seq)
{
    const auto it = m_Listings.find(listingId);
    if (it == m_Listings.end())
        return;
    it->second.state = State::Closing;
    const Int64 settleId = m_NextSettlementId++;
    const Int64 now = Service::UnixNow();
    const int sellerId = it->second.row.sellerId;
    const std::optional<Service::Ctx> c2 = ctx;
    GDBThread->RequestTx(sellerId, status == 2 ? "AuctionCancel" : "AuctionExpire",
        [=](CDBConnection& c) { return c.AuctionClose(listingId, status, settleId, now); },
        [=](bool ok) { GAuctionService->Post([=]() { GAuctionService->FinishClose(ok, listingId, status, settleId, now, c2, seq); }); });
}

void CAuctionService::FinishClose(bool ok, UInt32 listingId, UInt8 status, Int64 settleId, Int64 now, const std::optional<Service::Ctx>& ctx, UInt32 seq)
{
    if (ctx)
        m_Inflight.erase(ctx->playerId);
    const auto it = m_Listings.find(listingId);
    if (it == m_Listings.end())
    {
        if (ctx) Ack(*ctx, AuctionOp::Cancel, AuctionResult::NotFound, seq, listingId);
        return;
    }
    if (!ok)
    {
        if (it->second.state == State::Closing) it->second.state = State::Selling;
        if (ctx) Ack(*ctx, AuctionOp::Cancel, AuctionResult::Invalid, seq, listingId);
        return;
    }
    const DB_LISTING_ROW row = it->second.row;
    m_ByCat[it->second.cat].erase(listingId);
    m_Listings.erase(it);
    if (--m_PerSeller[row.sellerId] <= 0)
        m_PerSeller.erase(row.sellerId);

    Settlement back;
    back.row.settlementId = settleId;
    back.row.playerId = row.sellerId;
    back.row.kind = 2;
    back.row.itemId = row.itemId;
    back.row.count = row.count;
    back.row.enchant = row.enchant;
    back.row.extra = row.extra;
    back.row.listingId = row.listingId;
    back.row.reason = static_cast<UInt8>(status == 2 ? AuctionSettleReason::Cancelled : AuctionSettleReason::Expired);
    back.row.createdAt = now;
    m_Settlements[row.sellerId].push_back(back);

    if (ctx)
        Ack(*ctx, AuctionOp::Cancel, AuctionResult::Ok, seq, listingId, 0, 0, row.itemId, row.count);
    if (status == 3)
    {
        AUCTION_NFY_PACKET p;
        Service::Init(p, PacketType::AUCTION_NFY);
        p.event = static_cast<UInt8>(AuctionEvent::Expired);
        p.id = listingId;
        p.itemId = row.itemId;
        p.settleCount = static_cast<UInt16>(UnclaimedCount(row.sellerId));
        SendTo(row.sellerId, p);
        std::cout << "[Auction] expired listing=" << listingId << " seller=" << row.sellerId << "\n" << std::flush;
    }
    NotifySettleCount(row.sellerId);
}

// ── 정산함 받기 ──
void CAuctionService::Claim(const Service::Ctx& ctx, UInt32 seq, UInt32 settlementId)
{
    if (m_Inflight.count(ctx.playerId))
        return Ack(ctx, AuctionOp::Claim, AuctionResult::Busy, seq, settlementId);
    const auto vi = m_Settlements.find(ctx.playerId);
    if (vi == m_Settlements.end())
        return Ack(ctx, AuctionOp::Claim, AuctionResult::NotFound, seq, settlementId);
    Settlement* s = nullptr;
    for (auto& e : vi->second)
        if (!e.claiming && (settlementId == 0 || e.row.settlementId == static_cast<Int64>(settlementId))) { s = &e; break; }
    if (s == nullptr)
        return Ack(ctx, AuctionOp::Claim, AuctionResult::NotFound, seq, settlementId);
    s->claiming = true;
    m_Inflight.insert(ctx.playerId);
    const DB_SETTLEMENT_ROW row = s->row;
    const Int64 now = Service::UnixNow();

    Service::PostToOwner(ctx.obj, [=](const UserSharedPtr& user)
    {
        const Int64 sid = row.settlementId;
        auto abort = [=](AuctionResult r)
        {
            GAuctionService->Post([=]()
            {
                auto& svc = *GAuctionService;
                svc.m_Inflight.erase(ctx.playerId);
                if (const auto v = svc.m_Settlements.find(ctx.playerId); v != svc.m_Settlements.end())
                    for (auto& e : v->second) if (e.row.settlementId == sid) e.claiming = false;
                svc.Ack(ctx, AuctionOp::Claim, r, seq, static_cast<UInt32>(sid));
            });
        };
        if (user == nullptr)
            return abort(AuctionResult::Invalid);
        auto inv = user->GetInventory();
        const int pid = ctx.playerId;
        const ObjID obj = ctx.obj;
        if (row.kind == 1)
        {
            if (inv->GetAdena() > kMaxAdena - std::min(kMaxAdena, row.adena))
                return abort(AuctionResult::InventoryFull);   // 지갑 상한
            inv->CreditAdenaNoSave(row.adena, AdenaReason::AuctionClaim);
            const UInt32 adenaAfter = inv->GetAdena();
            const DB_ITEM_SLOT_SAVE none{};
            const UInt32 amount = row.adena;
            // 개인 업적(경매 판매): 판매 대금 줄(Sold)을 받는 순간 센다 — 줄마다 한 번(받기는 한 번뿐) · 판 사람이 나가 있던 판매도 센다
            const bool sold = row.reason == static_cast<UInt8>(AuctionSettleReason::Sold);
            GDBThread->RequestTx(pid, "SettlementClaim",
                [=](CDBConnection& c) { return c.SettlementClaim(pid, sid, adenaAfter, none, now); },
                [=](bool ok)
                {
                    if (!ok)
                        Service::PostToOwner(obj, [amount](const UserSharedPtr& u)
                        {
                            if (u != nullptr) (void)u->GetInventory()->TrySpendAdena(amount, AdenaReason::Refund);
                        });
                    else if (sold)
                        Service::PostToOwner(obj, [](const UserSharedPtr& u) { if (u != nullptr) CAchieveHelper::OnAuctionSold(u); });
                    GAuctionService->Post([=]() { GAuctionService->FinishClaim(ok, ctx, seq, sid, adenaAfter); });
                });
            return;
        }
        const UInt16 slot = inv->FindSlotForIncoming(row.itemId, row.count);
        if (slot >= MAX_INVENTORY_SLOTS)
            return abort(AuctionResult::InventoryFull);
        const CInventory::TakenItem item{ row.itemId, row.count, row.enchant, row.extra };
        DB_ITEM_SLOT_SAVE out;
        if (!inv->PlaceIncoming(slot, item, out))
            return abort(AuctionResult::InventoryFull);
        const UInt32 adenaAfter = inv->GetAdena();
        GDBThread->RequestTx(pid, "SettlementClaim",
            [=](CDBConnection& c) { return c.SettlementClaim(pid, sid, adenaAfter, out, now); },
            [=](bool ok)
            {
                if (!ok)
                    Service::PostToOwner(obj, [slot, item](const UserSharedPtr& u)
                    {
                        if (u != nullptr) u->GetInventory()->UndoIncoming(slot, item);
                    });
                GAuctionService->Post([=]() { GAuctionService->FinishClaim(ok, ctx, seq, sid, adenaAfter); });
            });
    });
}

void CAuctionService::FinishClaim(bool ok, const Service::Ctx& ctx, UInt32 seq, Int64 settlementId, UInt32 adenaAfter)
{
    m_Inflight.erase(ctx.playerId);
    const auto vi = m_Settlements.find(ctx.playerId);
    if (vi == m_Settlements.end())
        return Ack(ctx, AuctionOp::Claim, AuctionResult::NotFound, seq, static_cast<UInt32>(settlementId));
    auto& v = vi->second;
    const auto it = std::find_if(v.begin(), v.end(), [&](const Settlement& s) { return s.row.settlementId == settlementId; });
    if (it == v.end())
        return Ack(ctx, AuctionOp::Claim, AuctionResult::NotFound, seq, static_cast<UInt32>(settlementId));
    if (!ok)
    {
        it->claiming = false;
        return Ack(ctx, AuctionOp::Claim, AuctionResult::Invalid, seq, static_cast<UInt32>(settlementId));
    }
    const DB_SETTLEMENT_ROW row = it->row;
    v.erase(it);
    if (v.empty())
        m_Settlements.erase(vi);
    Ack(ctx, AuctionOp::Claim, AuctionResult::Ok, seq, static_cast<UInt32>(settlementId), adenaAfter,
        row.kind == 1 ? static_cast<Int32>(row.adena) : 0, row.kind == 2 ? row.itemId : 0, row.kind == 2 ? row.count : 0);
    NotifySettleCount(ctx.playerId);
}

// ─────────────────────────────────────────────────────────────────────────────
// CAuctionHelper — 요청한 사람의 소유 Zone 스레드
// ─────────────────────────────────────────────────────────────────────────────
namespace CAuctionHelper
{
    namespace
    {
        void ZoneAck(const UserSharedPtr& user, AuctionOp op, AuctionResult r, UInt32 seq, UInt32 id = 0)
        {
            AUCTION_ACK_PACKET p;
            Service::Init(p, PacketType::AUCTION_ACK);
            p.op = static_cast<UInt8>(op);
            p.result = static_cast<UInt8>(r);
            p.seq = seq;
            p.id = id;
            p.adena = user->GetInventory()->GetAdena();
            if (auto s = user->GetGameSession()) s->PostSend(p);
        }
        [[nodiscard]] bool IsMutating(AuctionOp op) noexcept
        {
            return op == AuctionOp::Register || op == AuctionOp::Buy || op == AuctionOp::Cancel || op == AuctionOp::Claim;
        }
    }

    void HandleRequest(const UserSharedPtr& user, const AUCTION_REQ_PACKET& p)
    {
        if (user == nullptr)
            return;
        const auto op = static_cast<AuctionOp>(p.op);
        if (GAuctionService == nullptr)
            return ZoneAck(user, op, AuctionResult::Busy, p.seq, p.id);   // 켤 때 DB를 못 읽었다(서버 로그 [Phase5])
        const UInt32 now = GetNowTime();
        if (IsMutating(op))
        {
            if (user->GetInventory()->IsBusy())   // 강화 · 제작 · 분해 진행 중
                return ZoneAck(user, op, AuctionResult::Busy, p.seq, p.id);
            if (p.seq <= user->m_LastAuctionSeq)
                return ZoneAck(user, op, AuctionResult::Duplicate, p.seq, p.id);
            user->m_LastAuctionSeq = p.seq;
            if (!CNpcHelper::IsNear(AuctionRule::kAuctioneerNpcId, user->GetX(), user->GetY()))
                return ZoneAck(user, op, AuctionResult::NotAtNpc, p.seq, p.id);   // 거래(올리기 · 사기 · 내리기 · 받기)는 경매인 곁에서 — 상점과 같은 거리 규칙. 검색은 어디서나
            if (user->GetStat()->IsDead())
                return ZoneAck(user, op, AuctionResult::Invalid, p.seq, p.id);
        }
        else
        {
            // 검색 · 목록 · 시세: 너무 잦은 요청은 버린다(창을 넘기는 속도로 충분)
            if (user->m_LastAuctionQueryAt != 0 && now - user->m_LastAuctionQueryAt < 120)
                return;
            user->m_LastAuctionQueryAt = now;
        }

        switch (op)
        {
        case AuctionOp::Register:
        {
            if (!AuctionRule::ValidPrice(p.price))
                return ZoneAck(user, op, AuctionResult::BadPrice, p.seq);
            if (p.duration >= AuctionRule::kDurationCount || p.count == 0)
                return ZoneAck(user, op, AuctionResult::Invalid, p.seq);
            const auto& slots = user->GetInventory()->GetSlots();
            const auto si = slots.find(p.slot);
            if (si == slots.end() || si->second->GetItemTableId() != p.itemId || si->second->GetCount() < p.count)
                return ZoneAck(user, op, AuctionResult::BadItem, p.seq);
            const ItemTableRow* row = ItemTable::Find(p.itemId);
            if (row == nullptr || !AuctionRule::Listable(*row))
                return ZoneAck(user, op, AuctionResult::BadItem, p.seq);
            // 귀속(획득 귀속 행 — 장비 파편 같은 재료 포함 · 인스턴스 bit 8)은 올릴 수 없다 — 알림 + Bound
            if (ItemRules::IsBound(*row, user->GetInventory()->GetSlotExtra(p.slot)))
            {
                CUserHelper::SendSystem(user, SystemMessageCode::ItemBound, static_cast<Int32>(p.itemId), p.slot);
                return ZoneAck(user, op, AuctionResult::Bound, p.seq);
            }
            if (user->GetInventory()->GetAdena() < AuctionRule::Deposit(p.price))
                return ZoneAck(user, op, AuctionResult::NoAdena, p.seq);
            break;
        }
        case AuctionOp::Buy:
            if (p.id == 0 || !AuctionRule::ValidPrice(p.price))
                return ZoneAck(user, op, AuctionResult::Invalid, p.seq, p.id);
            if (user->GetInventory()->GetAdena() < p.price)
                return ZoneAck(user, op, AuctionResult::NoAdena, p.seq, p.id);
            break;
        case AuctionOp::Cancel:
            if (p.id == 0)
                return ZoneAck(user, op, AuctionResult::Invalid, p.seq);
            break;
        case AuctionOp::Claim:
        case AuctionOp::Search: case AuctionOp::Mine: case AuctionOp::Settlements: case AuctionOp::PriceHint:
            break;
        default:
            return ZoneAck(user, op, AuctionResult::Invalid, p.seq);
        }

        const Service::Ctx ctx = Service::MakeCtx(user);
        const AUCTION_REQ_PACKET copy = p;
        if (IsMutating(op)) user->m_PendingAuctionSeqs.insert(p.seq);
        GAuctionService->Post([ctx, copy]() { GAuctionService->Handle(ctx, copy); });
    }

    void OnEnterWorld(const UserSharedPtr& user)
    {
        if (user == nullptr || GAuctionService == nullptr)
            return;
        const Service::Ctx ctx = Service::MakeCtx(user);
        GAuctionService->Post([ctx]() { GAuctionService->OnLogin(ctx); });
    }

    void OnLeaveWorld(const UserSharedPtr& user)
    {
        if (user == nullptr || GAuctionService == nullptr)
            return;
        const int playerId = user->GetPlayerId();
        GAuctionService->Post([playerId]() { GAuctionService->OnLogout(playerId); });
    }
}
