#include "pch.h"
#include "Content/Transfer/TransferHelper.h"
#include "Content/Transfer/TransferPlan.h"
#include "Content/ServiceCommon.h"
#include "Content/Field/Sector.h"
#include "Database/DBThread.h"
#include "Worker/TimerThread.h"
#include "User/UserHelper.h"
#include "Content/Social/SocialHelper.h"
#include "Content/Achieve/AchieveHelper.h"
#include <iomanip>
#include <sstream>

struct PlayerTransfer
{
    UInt64 id = 0;
    UInt32 revision = 1, touchedAt = 0;
    ZoneId zone = InvalidZoneId;
    TransferKind kind = TransferKind::Exchange;
    TransferPhase phase = TransferPhase::Invited;
    std::array<UserWeakPtr, 2> users;
    std::array<std::vector<DB_ITEM_SLOT_SAVE>, 2> offers;
    UInt32 price = 0;
    UInt8 ready = 0, confirmed = 0;
    bool logout[2]{}, barriersOk = true;
    int barriers = 0;
    DB_TRANSFER_SAVE save;
};

namespace
{
    // Zone별 상태 — 다른 Zone의 CUser/인벤토리를 읽지 않는다.
    thread_local std::unordered_map<UInt64, std::shared_ptr<PlayerTransfer>> transfers;
    std::atomic<UInt64> nextId{1};
    const ObjID tickId(EnumCategory::eNone, 0x5452414e53464552ull);
    UInt32 Setting(const char* name, UInt32 fallback, UInt32 max)
    {
        const char* value = std::getenv(name);
        if (!value) return fallback;
        char* end = nullptr;
        const auto n = std::strtoul(value, &end, 10);
        return end != value && *end == '\0' && n >= 1 && n <= max ? static_cast<UInt32>(n) : fallback;
    }
    const UInt32 inviteMs = Setting("HARVEN_TRANSFER_INVITE_MS", 30'000, 300'000);
    const UInt32 editMs = Setting("HARVEN_TRANSFER_EDIT_MS", 120'000, 600'000);
    const UInt32 requestMs = Setting("HARVEN_TRANSFER_REQUEST_MS", 2'000, 60'000);
    const int distance = Setting("HARVEN_TRANSFER_DISTANCE", 3, 20);

    bool Available(const UserSharedPtr& u)
    {
        return u && u->GetZoneId() == LCurrentZoneId && !u->IsTransferring() &&
            !u->GetStat()->IsDead() && u->GetGameSession() && u->GetGameSession()->m_eState == ST_INGAME;
    }
    bool Near(const UserSharedPtr& a, const UserSharedPtr& b)
    {
        return Available(a) && Available(b) && std::max(std::abs(int(a->GetX()) - b->GetX()), std::abs(int(a->GetY()) - b->GetY())) <= distance;
    }
    bool Busy(const UserSharedPtr& u)
    {
        return u->GetInventory()->IsBusy() || !u->m_PendingAuctionSeqs.empty() || u->m_PendingGuildEconomy != 0;
    }
    void Ack(const UserSharedPtr& u, const TRANSFER_REQ_PACKET& req, TransferResult result,
        const std::shared_ptr<PlayerTransfer>& t = {})
    {
        TRANSFER_ACK_PACKET p{}; Service::Init(p, PacketType::TRANSFER_ACK);
        p.op = req.op; p.seq = req.seq; p.result = static_cast<UInt8>(result);
        p.transferId = t ? t->id : req.transferId; p.revision = t ? t->revision : 0;
        if (auto s = u->GetGameSession()) s->PostSend(p);
    }
    void Publish(const std::shared_ptr<PlayerTransfer>& t, TransferResult result = TransferResult::Ok)
    {
        for (int side = 0; side < 2; ++side)
        {
            const auto u = t->users[side].lock(), peer = t->users[1-side].lock();
            if (!u || !peer) continue;
            TRANSFER_STATE_PACKET p{}; Service::Init(p, PacketType::TRANSFER_STATE);
            p.transferId = t->id; p.revision = t->revision; p.kind = static_cast<UInt8>(t->kind);
            p.phase = static_cast<UInt8>(t->phase); p.result = static_cast<UInt8>(result); p.side = side;
            p.peer = peer->GetObjID(); std::memcpy(p.peerName, peer->GetName().data(), std::min(peer->GetName().size(), sizeof(p.peerName)-1));
            p.price = t->price; p.readyMask = t->ready; p.confirmMask = t->confirmed;
            p.itemCounts[0] = t->offers[0].size(); p.itemCounts[1] = t->offers[1].size();
            const UInt32 limit = t->phase == TransferPhase::Invited ? inviteMs : editMs;
            p.remainingMs = limit - std::min(limit, GetNowTime() - t->touchedAt);
            Service::SendTo(u->GetGameSession(), p);
            for (int offerSide = 0; offerSide < 2; ++offerSide)
                for (size_t i = 0; i < t->offers[offerSide].size(); ++i)
                {
                    const auto& item = t->offers[offerSide][i];
                    TRANSFER_ITEM_PACKET q{}; Service::Init(q, PacketType::TRANSFER_ITEM);
                    q.transferId = t->id; q.revision = t->revision; q.side = offerSide; q.index = i;
                    q.slotIndex = item.slotIndex; q.itemId = item.itemId; q.count = item.count; q.enchant = item.enchant; q.extra = item.extra;
                    Service::SendTo(u->GetGameSession(), q);
                }
        }
    }
    void End(std::shared_ptr<PlayerTransfer> t, TransferResult result, bool committed = false)
    {
        t->phase = committed ? TransferPhase::Completed : TransferPhase::Cancelled;
        t->revision++;
        for (int i = 0; i < 2; ++i)
            if (auto u = t->users[i].lock())
            {
                if (u->GetInventory()->GetPendingTx() == CInventory::PendingTx::Transfer)
                    u->GetInventory()->FinishTransfer(committed ? &t->save.players[i] : nullptr);
                u->m_Transfer.reset();
                if (committed)
                    CAchieveHelper::OnTrade(u);   // 개인 업적(교환 · 직거래 성립 — 두 사람 모두, 이 Zone 스레드가 둘 다 가졌다)
            }
        Publish(t, result);
        transfers.erase(t->id);
        // 끊긴 사람의 나가기는 이 Zone 큐 뒤로 넘긴다 — 여기서 바로 부르면 End를 부른 쪽(LeaveWorld · 사망 · Zone 이동의 Cancel)이
        // 이미 지워진 CUser로 남은 일을 이어 가고(LeaveWorld 두 번 · 지운 사람의 소지품 떨굼 · 지운 객체의 Zone 등록), 계정이 그 사이
        // 풀려 다시 들어온 접속이 그 뒤의 저장보다 먼저 DB를 읽을 수 있었다. LeaveWorld는 가진 Zone으로 알아서 넘기고 없으면 아무것도 않는다.
        for (int i = 0; i < 2; ++i)
            if (auto u = t->users[i].lock(); u && (t->logout[i] || !u->GetGameSession() || u->GetGameSession()->m_eState != ST_INGAME))
            {
                const ObjID id = u->GetObjID();
                GZoneManager->EnqueueByZone(t->zone, [id]() { CUserHelper::LeaveWorld(id); });
            }
    }
    void Changed(const std::shared_ptr<PlayerTransfer>& t)
    {
        t->revision++; t->touchedAt = GetNowTime(); t->ready = t->confirmed = 0; t->phase = TransferPhase::Editing;
    }
    void Barrier(const std::shared_ptr<PlayerTransfer>& t, bool ok)
    {
        t->barriersOk &= ok;
        if (++t->barriers != 2) return;
        if (!t->barriersOk) return End(t, TransferResult::StorageError);
        GDBThread->RequestTransfer(t->save, [t](bool committed)
        {
            GZoneManager->EnqueueByZone(t->zone, [t, committed]()
            { End(t, committed ? TransferResult::Ok : TransferResult::StorageError, committed); });
        });
    }
    TransferResult Commit(const std::shared_ptr<PlayerTransfer>& t)
    {
        std::array<TransferPlan::Bag, 2> bags;
        std::array<UInt32, 2> wallets;
        for (int i = 0; i < 2; ++i)
        {
            const auto u = t->users[i].lock();
            if (!Available(u)) return TransferResult::Unavailable;
            const auto inv = u->GetInventory();
            if (!u->m_PendingAuctionSeqs.empty() || u->m_PendingGuildEconomy != 0) return TransferResult::Busy;
            wallets[i] = inv->GetAdena(); t->save.playerIds[i] = u->GetPlayerId();
            for (UInt16 slot = 0; slot < MAX_INVENTORY_SLOTS; ++slot) bags[i][slot] = inv->DescribeSlot(slot);
        }
        const auto result = TransferPlan::Build(t->kind, bags, t->offers, wallets, t->price, t->save);
        if (result != TransferResult::Ok) return result;
        t->save.offers = t->offers;
        // 프로세스 nonce + 단조 증가 번호. receipt는 재시작을 포함해 충돌하지 않도록 128비트.
        static const UInt64 nonce = (UInt64(std::random_device{}()) << 32) | std::random_device{}();
        std::ostringstream key; key << std::hex << std::setfill('0') << std::setw(16) << nonce << std::setw(16) << t->id;
        t->save.receipt = key.str(); t->save.kind = static_cast<UInt8>(t->kind);
        t->phase = TransferPhase::Committing; t->revision++;
        for (int i = 0; i < 2; ++i) t->users[i].lock()->GetInventory()->FreezeTransfer(t->save.players[i].adenaAfter);
        Publish(t);
        // 두 DB 큐의 이전 저장을 배출한 뒤 한 커넥션에서 양쪽을 커밋한다. 워커를 기다리게 하지 않는다.
        for (const int pid : t->save.playerIds)
            GDBThread->RequestTx(pid, "TransferBarrier", [](CDBConnection&) { return true; }, [t](bool ok)
            { GZoneManager->EnqueueByZone(t->zone, [t, ok]() { Barrier(t, ok); }); });
        return TransferResult::Ok;
    }
}

namespace CTransferHelper
{
    void HandleRequest(const UserSharedPtr& u, const TRANSFER_REQ_PACKET& req)
    {
        if (!u) return;
        if (req.op > static_cast<UInt8>(TransferOp::Cancel) || req.kind > static_cast<UInt8>(TransferKind::Sale))
            return Ack(u, req, TransferResult::Invalid);
        if (!req.seq || req.seq <= u->m_LastTransferSeq) return Ack(u, req, TransferResult::Duplicate, u->m_Transfer);
        u->m_LastTransferSeq = req.seq;
        const auto op = static_cast<TransferOp>(req.op);
        auto t = u->m_Transfer;
        if (op == TransferOp::Request)
        {
            if (!Available(u) || req.transferId != 0 || req.revision != 0 ||
                (req.target != ObjID::npos && (req.target.GetCategory<EnumCategory>() != EnumCategory::eUser || req.target == u->GetObjID())))
                return Ack(u, req, TransferResult::Invalid);
            if (t || Busy(u)) return Ack(u, req, TransferResult::Busy);
            const UInt32 now = GetNowTime();
            if (u->m_LastTransferRequestAt && now - u->m_LastTransferRequestAt < requestMs) return Ack(u, req, TransferResult::TooFast);
            u->m_LastTransferRequestAt = now;
            // 조회표의 Zone부터 검사한다 — 남의 Zone의 CUser 필드는 읽지 않는다.
            UserSharedPtr peer;
            if (req.target != ObjID::npos)
            {
                if (GZoneManager->GetObjectZone(req.target) != LCurrentZoneId) return Ack(u, req, TransferResult::TooFar);
                peer = ::GetGameObject<CUser>(req.target);
            }
            else
            {
                const auto length = ::strnlen(req.targetName, sizeof(req.targetName));
                if (length == 0 || length == sizeof(req.targetName)) return Ack(u, req, TransferResult::Invalid);
                const std::string name(req.targetName, length);
                GSector->ForEachUserInZone([&](const SubjectSharedPtr& o)
                {
                    if (o->GetName() == name) peer = static_pointer_cast<CUser>(o);
                });
                if (peer == u) return Ack(u, req, TransferResult::Invalid);
            }
            if (!Near(u, peer)) return Ack(u, req, TransferResult::TooFar);
            if (CSocialHelper::Blocked(u->GetPlayerId(),peer->GetPlayerId())) return Ack(u,req,TransferResult::Unavailable);
            if (peer->m_Transfer || Busy(peer)) return Ack(u, req, TransferResult::Busy);
            t = std::make_shared<PlayerTransfer>(); t->id = nextId.fetch_add(1); t->zone = LCurrentZoneId;
            t->kind = static_cast<TransferKind>(req.kind); t->users = {u, peer}; t->touchedAt = now;
            u->m_Transfer = peer->m_Transfer = t; transfers.emplace(t->id, t);
            Ack(u, req, TransferResult::Ok, t); Publish(t); return;
        }
        if (!t || req.transferId != t->id || req.kind != static_cast<UInt8>(t->kind)) return Ack(u, req, TransferResult::Invalid, t);
        const int side = t->users[0].lock() == u ? 0 : 1;
        if (t->phase == TransferPhase::Committing) return Ack(u, req, TransferResult::Busy, t);
        if (op == TransferOp::Cancel)
        { Ack(u, req, TransferResult::Ok, t); End(t, TransferResult::Declined); return; }
        const auto other=t->users[1-side].lock();
        if(other && CSocialHelper::Blocked(u->GetPlayerId(),other->GetPlayerId()))
        { Ack(u,req,TransferResult::Unavailable,t); End(t,TransferResult::Unavailable); return; }
        if (req.revision != t->revision) return Ack(u, req, TransferResult::Stale, t);
        if (!Near(t->users[0].lock(), t->users[1].lock()))
        { Ack(u, req, TransferResult::TooFar, t); End(t, TransferResult::TooFar); return; }
        if (t->phase == TransferPhase::Invited)
        {
            if (op != TransferOp::Accept || side != 1) return Ack(u, req, TransferResult::Invalid, t);
            for (const auto& w : t->users) if (Busy(w.lock())) return Ack(u, req, TransferResult::Busy, t);
            for (const auto& w : t->users) ASSERT_CRASH(w.lock()->GetInventory()->BeginTransfer());
            Changed(t); Ack(u, req, TransferResult::Ok, t); Publish(t); return;
        }
        if (op == TransferOp::SetItem)
        {
            if (side == 1 && t->kind != TransferKind::Exchange) return Ack(u, req, TransferResult::Invalid, t);
            if (req.slotIndex >= MAX_INVENTORY_SLOTS) return Ack(u, req, TransferResult::BadItem, t);
            auto& offers = t->offers[side];
            auto it = std::find_if(offers.begin(), offers.end(), [&](const auto& x) { return x.slotIndex == req.slotIndex; });
            if (req.count == 0)
            { if (it == offers.end()) return Ack(u, req, TransferResult::BadItem, t); offers.erase(it); }
            else
            {
                auto item = u->GetInventory()->DescribeSlot(req.slotIndex);
                const auto* row = ItemTable::Find(item.itemId);
                if (!item.hasItem || !row || row->type == ItemType::eCurrency) return Ack(u, req, TransferResult::BadItem, t);
                if (item.equipped) return Ack(u, req, TransferResult::Equipped, t);
                if (ItemRules::IsBound(*row, item.extra)) return Ack(u, req, TransferResult::Bound, t);
                if (req.count > item.count) return Ack(u, req, TransferResult::BadCount, t);
                item.count = req.count;
                if (it != offers.end()) *it = item;
                else { if (offers.size() >= kTransferMaxItems) return Ack(u, req, TransferResult::BadCount, t); offers.push_back(item); }
            }
            std::vector<UInt16> locked; for (const auto& item : offers) locked.push_back(item.slotIndex);
            u->GetInventory()->SetTransferSlots(locked); Changed(t);
        }
        else if (op == TransferOp::SetPrice)
        {
            if (t->kind != TransferKind::Sale || side != 0 || req.price == 0 || req.price > TransferPlan::kMaxAdena)
                return Ack(u, req, TransferResult::Invalid, t);
            t->price = req.price; Changed(t);
        }
        else if (op == TransferOp::Ready)
        {
            if (t->offers[0].empty() || (t->kind == TransferKind::Exchange && t->offers[1].empty()) || (t->kind == TransferKind::Sale && !t->price))
                return Ack(u, req, TransferResult::BadItem, t);
            t->ready |= UInt8(1 << side); t->confirmed = 0;
            if (t->ready == 3) t->phase = TransferPhase::Ready;
        }
        else if (op == TransferOp::Confirm)
        {
            if (t->ready != 3 || t->phase != TransferPhase::Ready) return Ack(u, req, TransferResult::NotReady, t);
            // 판매자는 구매자가 확인한 뒤에만 최종 확인한다.
            if (t->kind == TransferKind::Sale && side == 0 && !(t->confirmed & 2)) return Ack(u, req, TransferResult::NotReady, t);
            t->confirmed |= UInt8(1 << side);
            if (t->confirmed == 3)
            {
                const auto r = Commit(t);
                Ack(u, req, r, t);
                if (r != TransferResult::Ok) { Changed(t); Publish(t, r); }
                return;
            }
        }
        else return Ack(u, req, TransferResult::Invalid, t);
        t->touchedAt = GetNowTime(); Ack(u, req, TransferResult::Ok, t); Publish(t);
    }
    void Cancel(const UserSharedPtr& u, TransferResult reason)
    {
        if (u && u->m_Transfer && u->m_Transfer->phase != TransferPhase::Committing) End(u->m_Transfer, reason);
    }
    void CancelWith(const UserSharedPtr& u,int playerId)
    {
        if(!u || !u->m_Transfer) return;
        for(const auto& weak:u->m_Transfer->users)
            if(const auto peer=weak.lock();peer && peer->GetPlayerId()==playerId) { Cancel(u,TransferResult::Unavailable); return; }
    }
    bool DeferLogout(const UserSharedPtr& u)
    {
        if (!u || !u->m_Transfer || u->m_Transfer->phase != TransferPhase::Committing) return false;
        auto& t = *u->m_Transfer; t.logout[t.users[0].lock() == u ? 0 : 1] = true; return true;
    }
    void InitForZone()
    { GTimerThread->ScheduleAfter(tickId, std::chrono::milliseconds(250), TIMER_EVENT_TYPE::EV_TRANSFER_TICK); }
    void Tick()
    {
        const auto now = GetNowTime();
        std::vector<std::pair<std::shared_ptr<PlayerTransfer>, TransferResult>> expired;
        for (const auto& [id, t] : transfers)
        {
            if (t->phase == TransferPhase::Committing) continue;
            if (!Near(t->users[0].lock(), t->users[1].lock())) expired.push_back({t, TransferResult::Unavailable});
            else if (now - t->touchedAt >= (t->phase == TransferPhase::Invited ? inviteMs : editMs)) expired.push_back({t, TransferResult::Expired});
        }
        for (const auto& [t, reason] : expired) End(t, reason);
        InitForZone();
    }
}
