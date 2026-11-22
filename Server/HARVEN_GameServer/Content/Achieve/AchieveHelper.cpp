#include "pch.h"
#include "Content/Achieve/AchieveHelper.h"

#include <algorithm>
#include <string>

#include "Base/Core/CoreTLS.h"
#include "Content/Chat/ChatHelper.h"
#include "Content/Economy/EconomyLog.h"
#include "Content/Field/ZoneManager.h"
#include "Content/Inventory/Inventory.h"
#include "Content/ServiceCommon.h"
#include "Database/GameData/AchieveRules.h"
#include "Database/GameData/Item/ItemTable.h"
#include "Database/GameData/ItemRules.h"
#include "Database/GameData/WorldData.h"
#include "User/User.h"
#include "User/UserHelper.h"
#include "Worker/TimerThread.h"

namespace CAchieveHelper
{
    namespace
    {
        constexpr UInt32 kMaxAdena = 2'000'000'000u;   // CInventory::AddAdena와 같은 지갑 상한(DB INT)
        using AchieveRule::Catalog;
        using AchieveRule::StatKey;

        void Ack(const UserSharedPtr& user, AchieveOp op, AchieveResult r, UInt32 seq, UInt16 achId = 0, UInt16 claimed = 0,
                 UInt32 adena = 0, UInt16 itemId = 0, UInt16 itemCount = 0)
        {
            ACHIEVE_ACK_PACKET p;
            Service::Init(p, PacketType::ACHIEVE_ACK);
            p.op = static_cast<UInt8>(op);
            p.result = static_cast<UInt8>(r);
            p.seq = seq;
            p.achId = achId;
            p.claimed = claimed;
            p.adena = adena;
            p.itemId = itemId;
            p.itemCount = itemCount;
            if (const auto s = user->GetGameSession()) s->PostSend(p);
        }

        void FillSummary(ACHIEVE_INF_PACKET& p, const AchieveBook::Summary& s)
        {
            p.points = s.points;
            p.done = s.done;
            p.total = s.total;
            p.claimable = s.claimable;
        }

        [[nodiscard]] ACHIEVE_ENTRY Entry(const AchieveBook& book, const AchieveRule::Row& row)
        {
            ACHIEVE_ENTRY e{};
            e.id = row.id;
            e.progress = book.Progress(row);
            e.state = book.State(row.id);
            return e;
        }

        // 줄 목록을 30줄씩(kind = Page · Claimed). 줄이 없어도 한 장(count 0)
        void SendPages(const UserSharedPtr& user, AchieveInfKind kind, const std::vector<ACHIEVE_ENTRY>& entries)
        {
            const auto session = user->GetGameSession();
            if (session == nullptr)
                return;
            const AchieveBook::Summary sum = user->m_Achieve.Sum(AchieveRule::Get());
            const size_t per = static_cast<size_t>(AchieveRule::kPageEntries);
            const size_t pages = std::max<size_t>(1, (entries.size() + per - 1) / per);
            for (size_t pg = 0; pg < pages; ++pg)
            {
                ACHIEVE_INF_PACKET p;
                Service::Init(p, PacketType::ACHIEVE_INF);
                p.kind = static_cast<UInt8>(kind);
                FillSummary(p, sum);
                p.page = static_cast<UInt8>(std::min<size_t>(pg, 255));
                p.pageCount = static_cast<UInt8>(std::min<size_t>(pages, 255));
                const size_t from = pg * per;
                const size_t n = std::min(per, entries.size() - std::min(entries.size(), from));
                p.count = static_cast<UInt8>(n);
                for (size_t i = 0; i < n; ++i)
                    p.entries[i] = entries[from + i];
                session->PostSend(p);
            }
        }

        void SendSummary(const UserSharedPtr& user)
        {
            const auto session = user->GetGameSession();
            if (session == nullptr)
                return;
            ACHIEVE_INF_PACKET p;
            Service::Init(p, PacketType::ACHIEVE_INF);
            p.kind = static_cast<UInt8>(AchieveInfKind::Summary);
            FillSummary(p, user->m_Achieve.Sum(AchieveRule::Get()));
            session->PostSend(p);
        }

        void ScheduleSave(const UserSharedPtr& user)
        {
            if (user->m_AchieveSaveQueued)
                return;
            user->m_AchieveSaveQueued = true;
            GTimerThread->ScheduleAfter(user->GetObjID(), std::chrono::milliseconds(AchieveRule::kSaveDelayMs), TIMER_EVENT_TYPE::EV_ACHIEVE_SAVE);
        }

        // 사건 뒤: 새로 달성한 업적마다 Completed INF + 시스템 채팅 → 바로 저장. 달성이 없고 진척만 바뀌었으면 60초 저장 예약.
        void Settle(const UserSharedPtr& user, const AchieveBook::DoneList& done)
        {
            if (done.empty())
            {
                if (user->m_Achieve.dirty)
                    ScheduleSave(user);
                return;
            }
            const Catalog& cat = AchieveRule::Get();
            const auto session = user->GetGameSession();
            const AchieveBook::Summary sum = user->m_Achieve.Sum(cat);   // 이 사건으로 달성한 것을 모두 담은 값
            for (const UInt16 id : done)
            {
                const AchieveRule::Row* row = cat.Find(id);
                if (row == nullptr)
                    continue;
                if (session != nullptr)
                {
                    ACHIEVE_INF_PACKET p;
                    Service::Init(p, PacketType::ACHIEVE_INF);
                    p.kind = static_cast<UInt8>(AchieveInfKind::Completed);
                    FillSummary(p, sum);
                    p.pageCount = 1;
                    p.count = 1;
                    p.entries[0] = Entry(user->m_Achieve, *row);
                    session->PostSend(p);
                }
                std::string text = "업적 달성: ";
                text += row->name;
                CChatHelper::SendSystemTo(user, text.c_str());
            }
            CUserHelper::SaveProgress(user);
        }

        // 보상 한 업적 — 다 들어갈 때만 지급(아무것도 받지 않거나 모두 받는다). 차례가 계약이다:
        //   (a) 드라이런 — 지갑 상한 · 가방 자리(CInventory::CanAddItem). 안 들어가면 아무것도 바꾸지 않고 false(BagFull)
        //   (b) 받음 비트  (c) 진행 기록 저장을 **먼저** 줄 세운다 — 같은 사람의 GDB 샤드(playerId % 8)는 줄 선 차례대로 반영한다
        //   (d) 그 뒤에 아이템(TryAddItem — 드라이런이 보장했으니 실패는 불변 조건 위반) · 그로시(AddAdena)
        // 서버가 (c)와 (d)의 반영 사이에서 죽으면 받음만 남고 보상은 없다(경제 로그로 운영이 메운다). 거꾸로 — 보상은 DB에 있는데
        // 받음 비트가 없어 다시 받는 일 — 은 생기지 않는다: 크래시는 보상을 잃게 할 수는 있어도 두 번 주게 하지는 않는다.
        [[nodiscard]] bool Grant(const UserSharedPtr& user, const AchieveRule::Row& row)
        {
            const auto inv = user->GetInventory();
            // (a)
            if (row.rewardAdena > 0 && inv->GetAdena() > kMaxAdena - std::min(kMaxAdena, row.rewardAdena))
                return false;   // 지갑 상한 — 넘치는 그로시를 버리지 않는다
            const bool hasItem = row.rewardItem != ITEM_TABLE_ID_NONE && row.rewardCount > 0;
            const ItemTableRow* item = hasItem ? ItemTable::Find(row.rewardItem) : nullptr;
            if (hasItem && (item == nullptr || !inv->CanAddItem(row.rewardItem, row.rewardCount)))
                return false;   // 가방 자리 없음(없는 아이템 = 검증 R140을 건너뛴 판)
            // (b) · (c)
            if (!user->m_Achieve.MarkClaimed(row.id))
                return false;   // 달성 전 · 이미 받음 · 얼린 책 — 호출자가 먼저 걸렀다
            CUserHelper::SaveProgress(user);
            // (d)
            if (hasItem)
            {
                UInt64 extra = 0;
                if (item->type == ItemType::eEquipment)
                {
                    auto rng = []() { return static_cast<UInt32>(LRng()); };
                    extra = ItemRules::RollInstance(*item, ItemRules::FloorRarity(*item), user->m_eClassId, rng).Pack();
                }
                extra = ItemRules::OnAcquire(*item, extra);   // 획득 귀속(장비 bit 8 · 겹치는 재료는 행으로)
                const bool added = inv->TryAddItem(row.rewardItem, row.rewardCount, nullptr, 0, extra);
                ASSERT_CRASH(added);   // (a)의 드라이런과 같은 순회 — 그 사이 가방을 바꾼 것이 없다
                if (item->type == ItemType::eEquipment)
                    CEconomyLog::OnGearIn(GearSource::Achievement, item->grade, row.rewardCount);
                else if (item->type == ItemType::eMaterial)
                    CEconomyLog::OnMaterialIn(MaterialSource::Achievement, item->id, row.rewardCount);
            }
            if (row.rewardAdena > 0)
                (void)inv->AddAdena(row.rewardAdena, AdenaReason::Achievement);   // 경제 로그(그로시 Achievement)는 AddAdena가
            return true;
        }

        [[nodiscard]] bool InventoryBusy(const UserSharedPtr& user)
        {
            return user->GetInventory()->IsBusy() || !user->m_PendingAuctionSeqs.empty() || user->m_PendingGuildEconomy != 0;
        }

        void HandleList(const UserSharedPtr& user, const ACHIEVE_REQ_PACKET& p)
        {
            const UInt32 now = GetNowTime();
            if (user->m_LastAchieveListAt != 0 && now - user->m_LastAchieveListAt < AchieveRule::kListIntervalMs)
                return Ack(user, AchieveOp::List, AchieveResult::TooFast, p.seq);
            user->m_LastAchieveListAt = now;
            if (p.category != AchieveRule::kAllCategories && !AchieveRule::ValidCategory(p.category))
                return Ack(user, AchieveOp::List, AchieveResult::Invalid, p.seq);
            const Catalog& cat = AchieveRule::Get();
            std::vector<ACHIEVE_ENTRY> entries;
            entries.reserve(cat.rows.size());
            for (const AchieveRule::Row& row : cat.rows)
                if (p.category == AchieveRule::kAllCategories || static_cast<UInt8>(row.category) == p.category)
                    entries.push_back(Entry(user->m_Achieve, row));
            SendPages(user, AchieveInfKind::Page, entries);
        }

        void HandleClaim(const UserSharedPtr& user, const ACHIEVE_REQ_PACKET& p)
        {
            const Catalog& cat = AchieveRule::Get();
            const AchieveRule::Row* row = cat.Find(p.achId);
            if (row == nullptr)
                return Ack(user, AchieveOp::Claim, AchieveResult::Invalid, p.seq, p.achId);
            if (!user->m_Achieve.IsDone(row->id))
                return Ack(user, AchieveOp::Claim, AchieveResult::NotDone, p.seq, p.achId);
            if (user->m_Achieve.IsClaimed(row->id))
                return Ack(user, AchieveOp::Claim, AchieveResult::AlreadyClaimed, p.seq, p.achId);
            if (InventoryBusy(user))
                return Ack(user, AchieveOp::Claim, AchieveResult::Busy, p.seq, p.achId);
            if (!Grant(user, *row))
                return Ack(user, AchieveOp::Claim, AchieveResult::BagFull, p.seq, p.achId);
            // 진행 기록은 Grant가 보상보다 먼저 줄 세웠다
            Ack(user, AchieveOp::Claim, AchieveResult::Ok, p.seq, p.achId, 1, row->rewardAdena, row->rewardItem, row->rewardCount);
            SendPages(user, AchieveInfKind::Claimed, { Entry(user->m_Achieve, *row) });
        }

        void HandleClaimAll(const UserSharedPtr& user, const ACHIEVE_REQ_PACKET& p)
        {
            const Catalog& cat = AchieveRule::Get();
            std::vector<const AchieveRule::Row*> todo;
            for (const AchieveRule::Row& row : cat.rows)
                if (user->m_Achieve.IsDone(row.id) && !user->m_Achieve.IsClaimed(row.id))
                    todo.push_back(&row);
            if (todo.empty())
                return Ack(user, AchieveOp::ClaimAll, AchieveResult::NothingToClaim, p.seq);
            if (InventoryBusy(user))
                return Ack(user, AchieveOp::ClaimAll, AchieveResult::Busy, p.seq);
            // id 순으로 들어가는 것만 받는다 — 안 들어가는 것(가방 · 지갑)은 건너뛰고 남긴다(뒤의 그로시만 · 겹쳐지는 보상은 받을 수 있다)
            std::vector<ACHIEVE_ENTRY> got;
            UInt64 adena = 0;
            UInt16 itemId = 0, itemCount = 0;
            for (const AchieveRule::Row* row : todo)
            {
                if (!Grant(user, *row))
                    continue;
                got.push_back(Entry(user->m_Achieve, *row));
                adena += row->rewardAdena;
                if (itemId == 0 && row->rewardItem != ITEM_TABLE_ID_NONE)
                {
                    itemId = row->rewardItem;
                    itemCount = row->rewardCount;
                }
            }
            if (got.empty())
                return Ack(user, AchieveOp::ClaimAll, AchieveResult::BagFull, p.seq);
            // 진행 기록은 받을 때마다 Grant가 그 보상보다 먼저 줄 세웠다(받은 만큼만 받음 비트가 켜진 기록)
            Ack(user, AchieveOp::ClaimAll, AchieveResult::Ok, p.seq, 0, static_cast<UInt16>(got.size()),
                static_cast<UInt32>(std::min<UInt64>(adena, 0xFFFFFFFFull)), itemId, itemCount);
            SendPages(user, AchieveInfKind::Claimed, got);
        }

        // 지금 가진 장비(가방 · 착용)의 가장 높은 강화 단계
        [[nodiscard]] UInt32 BestEnchant(const UserSharedPtr& user)
        {
            UInt32 best = 0;
            const auto inv = user->GetInventory();
            for (UInt16 s = 0; s < MAX_INVENTORY_SLOTS; ++s)
                best = std::max(best, inv->GetSlotEnchant(s));
            return best;
        }

        template <class Fn>
        void Apply(const UserSharedPtr& user, Fn&& fn)
        {
            if (user == nullptr)
                return;
            AchieveBook::DoneList done;
            fn(user->m_Achieve, AchieveRule::Get(), done);
            Settle(user, done);
        }
    }

    void HandleRequest(const UserSharedPtr& user, const ACHIEVE_REQ_PACKET& p)
    {
        if (user == nullptr)
            return;
        const auto op = static_cast<AchieveOp>(p.op);
        if (op == AchieveOp::List)
            return HandleList(user, p);   // 묻기 — 순번을 보지 않는다
        if (op != AchieveOp::Claim && op != AchieveOp::ClaimAll)
            return Ack(user, op, AchieveResult::Invalid, p.seq, p.achId);
        // 멱등: 같은(또는 지난) 순번은 한 번만 — 소유 Zone이 사람마다 기억한다(접속마다 새 CUser라 1부터 — PARTY.md 7장 「순번 계약」과 같다).
        // Duplicate의 claimed = 마지막으로 받은 순번(아래 16비트): 처음 보낸 요청이 Duplicate면 클라 순번이 뒤처진 것 — claimed + 1로 다시.
        if (p.seq == 0 || p.seq <= user->m_LastAchieveSeq)
            return Ack(user, op, AchieveResult::Duplicate, p.seq, p.achId, static_cast<UInt16>(user->m_LastAchieveSeq & 0xFFFF));
        user->m_LastAchieveSeq = p.seq;
        if (user->GetStat()->IsDead() || user->IsTransferring() || user->m_Achieve.Frozen())
            return Ack(user, op, AchieveResult::Invalid, p.seq, p.achId);   // 얼린 책(깨진 기록) — 받음 비트를 믿을 수 없다
        if (op == AchieveOp::Claim)
            HandleClaim(user, p);
        else
            HandleClaimAll(user, p);
    }

    void OnEnterWorld(const UserSharedPtr& user)
    {
        if (user == nullptr)
            return;
        AchieveBook& book = user->m_Achieve;
        if (book.Frozen())
        {
            // 깨진 업적 절 — 이 접속 동안 읽기 전용(받기 Invalid · 사건은 세지 않는다), 저장은 원본 절을 그대로 되쓴다(AchieveBook::Read)
            static constexpr const char* kWhy[] = { "-", "잘림", "비트 길이", "뒤에 남은 바이트", "새 판" };
            const auto why = static_cast<size_t>(book.FrozenWhy());
            cout << "[Achieve] " << user->GetName() << "(" << user->GetPlayerId() << ") 업적 기록이 깨져 읽기 전용 — "
                 << (why < std::size(kWhy) ? kWhy[why] : "?") << " · 판 " << static_cast<int>(user->m_LoadedProgressVersion)
                 << " · 원본 절을 그대로 되쓴다\n" << std::flush;
            SendSummary(user);
            return;
        }
        // 소급 — 지금 상태로 셀 수 있는 것 + 표가 바뀌어 이미 닿은 것(Recheck). 로그인 때 달성은 알림 없이(Completed · 채팅 없음) —
        // 받기는 그대로 해야 하고, Summary의 claimable이 빨간 점을 켠다.
        //   · 레벨 · 지금 지역: 접속마다 — 되돌릴 수 없는 값이다(레벨은 내려가지 않고, 지금 선 지역은 정말 들어가 본 곳)
        //   · 가진 장비의 최고 강화: 판 4 미만(업적 전 기록 · 새 캐릭터)을 처음 읽을 때 한 번만 — 판 4부터는 강화 성공 훅(OnEnchant)만 센다.
        //     접속마다 하면 남의 +9를 빌려 들고 다시 접속하는 것만으로 강화 업적을 채울 수 있다
        const Catalog& cat = AchieveRule::Get();
        AchieveBook::DoneList done;
        book.Recheck(cat, done);
        book.Raise(cat, StatKey{ AchKind::eLevel, 0 }, user->GetStat()->GetLevel(), done);
        if (user->m_LoadedProgressVersion < 4)
            book.Raise(cat, StatKey{ AchKind::eEnchant, 0 }, BestEnchant(user), done);
        (void)book.EnterRegion(cat, WorldData::RegionIdAt(user->GetX(), user->GetY()), done);
        if (book.dirty || !done.empty())
            CUserHelper::SaveProgress(user);
        SendSummary(user);
    }

    void OnKill(const UserSharedPtr& user, UInt16 species, bool elite, bool boss)
    {
        if (user == nullptr)
            return;
        const bool party = user->m_PartyId.load(std::memory_order_relaxed) != 0;
        Apply(user, [&](AchieveBook& b, const Catalog& c, AchieveBook::DoneList& d) { b.OnKill(c, species, elite, boss, party, d); });
    }

    void OnLevel(const UserSharedPtr& user)
    {
        if (user == nullptr)
            return;
        const UInt32 level = user->GetStat()->GetLevel();
        Apply(user, [&](AchieveBook& b, const Catalog& c, AchieveBook::DoneList& d) { b.Raise(c, StatKey{ AchKind::eLevel, 0 }, level, d); });
    }

    void OnDeath(const UserSharedPtr& user)
    {
        Apply(user, [](AchieveBook& b, const Catalog& c, AchieveBook::DoneList& d) { b.Add(c, StatKey{ AchKind::eDeath, 0 }, 1, d); });
    }

    void OnAdenaPickup(const UserSharedPtr& user, UInt32 amount)
    {
        if (amount == 0)
            return;
        Apply(user, [&](AchieveBook& b, const Catalog& c, AchieveBook::DoneList& d) { b.Add(c, StatKey{ AchKind::eAdena, 0 }, amount, d); });
    }

    void OnLoot(const UserSharedPtr& user, ItemTableId itemId, UInt16 count)
    {
        const ItemTableRow* row = ItemTable::Find(itemId);
        if (row == nullptr || count == 0)
            return;
        if (row->type == ItemType::eEquipment)
            OnGear(user, row->grade, count);
        else if (AchieveRule::IsHuntMaterial(itemId))
            Apply(user, [&](AchieveBook& b, const Catalog& c, AchieveBook::DoneList& d) { b.Add(c, StatKey{ AchKind::eEssence, 0 }, count, d); });
    }

    void OnGear(const UserSharedPtr& user, ItemGrade grade, UInt32 count)
    {
        Apply(user, [&](AchieveBook& b, const Catalog& c, AchieveBook::DoneList& d) { b.OnGear(c, static_cast<uint8_t>(grade), count, d); });
    }

    void OnEnchant(const UserSharedPtr& user, UInt32 level)
    {
        Apply(user, [&](AchieveBook& b, const Catalog& c, AchieveBook::DoneList& d) { b.Raise(c, StatKey{ AchKind::eEnchant, 0 }, level, d); });
    }

    void OnCraft(const UserSharedPtr& user, ItemGrade grade)
    {
        Apply(user, [&](AchieveBook& b, const Catalog& c, AchieveBook::DoneList& d)
        {
            b.Add(c, StatKey{ AchKind::eCraft, 0 }, 1, d);
            b.OnGear(c, static_cast<uint8_t>(grade), 1, d);
        });
    }

    void OnDismantle(const UserSharedPtr& user)
    {
        Apply(user, [](AchieveBook& b, const Catalog& c, AchieveBook::DoneList& d) { b.Add(c, StatKey{ AchKind::eDismantle, 0 }, 1, d); });
    }

    void OnQuest(const UserSharedPtr& user, UInt16 questId)
    {
        Apply(user, [&](AchieveBook& b, const Catalog& c, AchieveBook::DoneList& d) { b.OnQuest(c, questId, d); });
    }

    void OnGuildJoin(const UserSharedPtr& user)
    {
        Apply(user, [](AchieveBook& b, const Catalog& c, AchieveBook::DoneList& d) { b.Add(c, StatKey{ AchKind::eGuildJoin, 0 }, 1, d); });
    }

    void OnGuildPresent(const UserSharedPtr& user)
    {
        Apply(user, [](AchieveBook& b, const Catalog& c, AchieveBook::DoneList& d) { b.Raise(c, StatKey{ AchKind::eGuildJoin, 0 }, 1, d); });
    }

    void OnAuctionSold(const UserSharedPtr& user)
    {
        Apply(user, [](AchieveBook& b, const Catalog& c, AchieveBook::DoneList& d) { b.Add(c, StatKey{ AchKind::eAuctionSell, 0 }, 1, d); });
    }

    void OnAuctionBought(const UserSharedPtr& user)
    {
        Apply(user, [](AchieveBook& b, const Catalog& c, AchieveBook::DoneList& d) { b.Add(c, StatKey{ AchKind::eAuctionBuy, 0 }, 1, d); });
    }

    void OnTrade(const UserSharedPtr& user)
    {
        Apply(user, [](AchieveBook& b, const Catalog& c, AchieveBook::DoneList& d) { b.Add(c, StatKey{ AchKind::eTrade, 0 }, 1, d); });
    }

    void OnEnterRegion(const UserSharedPtr& user)
    {
        if (user == nullptr)
            return;
        const int region = WorldData::RegionIdAt(user->GetX(), user->GetY());
        Apply(user, [&](AchieveBook& b, const Catalog& c, AchieveBook::DoneList& d) { (void)b.EnterRegion(c, region, d); });
    }

    void HandleSaveTimer(const ObjID& playerId)
    {
        auto flush = [](const UserSharedPtr& u)
        {
            if (u == nullptr)
                return;   // 나갔다 — 로그아웃이 저장했다
            u->m_AchieveSaveQueued = false;
            if (u->m_Achieve.dirty)
                CUserHelper::SaveProgress(u);
        };
        const auto user = ::GetGameObject<CUser>(playerId);
        if (user == nullptr)
            return;
        // 예약은 조회표의 Zone으로 온다 — Zone을 옮기는 중이면 새 Zone(입장 처리 뒤)에서
        if (user->IsTransferring() || !GZoneManager->IsCurrentThreadOwner(playerId))
        {
            Service::PostToOwner(playerId, flush);
            return;
        }
        flush(user);
    }
}
