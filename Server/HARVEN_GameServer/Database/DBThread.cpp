#include "pch.h"
#include "Database/DBThread.h"
#include "Database/DBConnectionPool.h"
#include "User/UserHelper.h"
#include "User/UserManager.h"
#include "Content/Field/ZoneManager.h"
#include "Database/GameData/WorldData.h"

namespace
{
    class CScopedDBConnection
    {
    public:
        explicit CScopedDBConnection(const shared_ptr<CDBConnectionPool>& pool)
            : m_pPool(pool), m_pConnection(pool != nullptr ? pool->Pop() : nullptr) {}

        ~CScopedDBConnection()
        {
            if (m_pPool != nullptr && m_pConnection != nullptr)
                m_pPool->Push(m_pConnection);
        }

        [[nodiscard]] const DBConnectionSharedPtr& Get() const noexcept { return m_pConnection; }

    private:
        shared_ptr<CDBConnectionPool> m_pPool;
        DBConnectionSharedPtr     m_pConnection;
    };
}

bool CDBThread::DBEventCompare::operator()(const shared_ptr<DB_EVENT_BASE>& lhs, const shared_ptr<DB_EVENT_BASE>& rhs) const noexcept
{
    if (lhs->wakeupTime != rhs->wakeupTime)
        return lhs->wakeupTime > rhs->wakeupTime;
    return lhs->sequence > rhs->sequence;
}

int CDBThread::ShardForPlayerId(int playerId) noexcept
{
    // playerId는 DB auto-increment라 순차 정수다 — 나머지 연산만으로 샤드에
    // 거의 정확히 균등하게 흩어진다(해시를 따로 돌릴 이유가 없다).
    return static_cast<int>(static_cast<UInt32>(playerId) % kShardCount);
}

int CDBThread::NextRoundRobinShard() noexcept
{
    return static_cast<int>(m_RoundRobinCursor.fetch_add(1, std::memory_order_relaxed) % kShardCount);
}

bool CDBThread::IsWaitingCaller(const shared_ptr<DB_EVENT_BASE>& event) noexcept
{
    return std::dynamic_pointer_cast<DB_LOGIN_EVENT>(event) != nullptr || std::dynamic_pointer_cast<DB_TX_EVENT>(event) != nullptr;
}

void CDBThread::FailFast(const shared_ptr<DB_EVENT_BASE>& event)
{
    // 로그인은 실패로 알린다 — 안 그러면 세션이 로그인 중(ST_LOGIN)에 멈춰 다시 시도할 수 없다.
    if (const auto e = std::dynamic_pointer_cast<DB_LOGIN_EVENT>(event))
    {
        CUserHelper::HandleLoginFail(e->session);
        return;
    }
    // 결과를 기다리는 트랜잭션(Phase 5)은 실패로 알린다 — 서비스가 잠근 목록을 풀고 Zone이 예약을 되돌린다
    if (const auto e = std::dynamic_pointer_cast<DB_TX_EVENT>(event))
    {
        std::cout << "[DB] tx " << e->what << " failed (no DB connection)\n" << std::flush;
        if (e->done)
            e->done(false);
    }
}

void CDBThread::ScheduleOnShard(shared_ptr<DB_EVENT_BASE> event, int shardIndex)
{
    ASSERT_CRASH(shardIndex >= 0 && shardIndex < kShardCount);
    Shard& shard = m_Shards[shardIndex];
    const bool waitingCaller = IsWaitingCaller(event);

    {
        std::scoped_lock lock(shard.lock);
        event->sequence = m_NextSequence.fetch_add(1, std::memory_order_relaxed);
        shard.events.push(std::move(event));
        if (waitingCaller)
            ++shard.waitingCallers;
    }
    shard.cv.notify_one();
}

void CDBThread::RequestLogin(const SessionSharedPtr& session, const std::string& name, const std::string& password)
{
    auto event      = std::make_shared<DB_LOGIN_EVENT>();
    event->wakeupTime = Now();
    event->session  = session;
    event->name     = name;
    event->password = password;
    // 로그인은 아직 playerId를 모르는 상태에서 하는 조회 작업이고, 순서를 맞춰야
    // 할 다른 이벤트도 없다 — 부하만 고르게 흩어지도록 라운드로빈으로 보낸다.
    ScheduleOnShard(std::move(event), NextRoundRobinShard());
}

void CDBThread::RequestLoginSnapshot(const SessionSharedPtr& session, int playerId)
{
    // 이 플레이어의 강화 · 경매 커밋과 저장(같은 샤드)이 다 반영된 뒤에 UserID로 읽는다.
    // 하나라도 읽다 실패하면 로그인 실패 — 빈 가방 · 처음 진행으로 들어가면 다음 저장이 DB의 진짜 행을 덮어쓴다.
    // (행이 없는 것은 실패가 아니다: 저장한 적 없는 진행 · 기술 · 가호, 빈 가방)
    RequestTx(playerId, "LoginSnapshot", [playerId, session](CDBConnection& c)
    {
        DB_USER_INFO userInfo;
        bool found = false;
        if (!c.ExtractUserInfo(playerId, userInfo, found) || !found) return false;
        std::vector<DB_ITEM_INFO> items;
        if (!c.ExtractInventory(playerId, items)) return false;
        std::vector<DB_ENCHANT_GRACE> grace;
        if (!c.LoadEnchantGrace(playerId, grace)) return false;
        std::vector<UInt8> progress;
        if (!c.LoadPlayerProgress(playerId, progress)) return false;
        std::vector<DB_SKILL_INFO> skills;
        if (!c.LoadPlayerSkills(playerId, skills)) return false;
        GZoneManager->EnqueueByZone(CUserHelper::GetLoginZone(userInfo.m_X, userInfo.m_Y), [session, userInfo, items, progress = std::move(progress), skills = std::move(skills), grace = std::move(grace)]()
        {
            CUserHelper::HandleGetUserInfo(session, userInfo, items, progress, skills, grace);
        });
        return true;
    }, [session](bool ok) { if (!ok) CUserHelper::HandleLoginFail(session); });
}

void CDBThread::RequestSaveUser(const ObjID& subjectId, const DB_USER_INFO& info)
{
    auto event        = std::make_shared<DB_SAVE_EVENT>();
    event->wakeupTime = Now();
    event->subjectId  = subjectId;
    event->playerId   = info.m_PlayerId;
    event->x          = static_cast<short>(info.m_X);
    event->y          = static_cast<short>(info.m_Y);
    event->level      = info.m_Level;
    event->exp        = info.m_Exp;
    // 한 플레이어의 저장은 전부 playerId 샤드에 쌓는다 — 다시 로그인할 때의 읽기(LoginSnapshot)가 같은 샤드라
    // 끊긴 세션이 남긴 저장 뒤에 읽는다(CUserManager가 같은 계정 재접속을 그 순서로 잇는다).
    ScheduleOnShard(std::move(event), ShardForPlayerId(info.m_PlayerId));
}

void CDBThread::RequestSaveItem(int playerId, UInt16 slotIndex, UInt16 itemId, UInt16 count, bool equipped, UInt32 enchant, UInt64 extra)
{
    auto event        = std::make_shared<DB_ITEM_SAVE_EVENT>();
    event->wakeupTime = Now();
    event->playerId   = playerId;
    event->slotIndex  = slotIndex;
    event->itemId     = itemId;
    event->count      = count;
    event->equipped   = equipped;
    event->enchant    = enchant;
    event->extra      = extra;
    ScheduleOnShard(std::move(event), ShardForPlayerId(playerId));
}

void CDBThread::RequestDeleteItem(int playerId, UInt16 slotIndex)
{
    auto event        = std::make_shared<DB_ITEM_DELETE_EVENT>();
    event->wakeupTime = Now();
    event->playerId   = playerId;
    event->slotIndex  = slotIndex;
    ScheduleOnShard(std::move(event), ShardForPlayerId(playerId));
}

void CDBThread::RequestSaveTwoItems(int playerId, const DB_ITEM_SLOT_SAVE& a, const DB_ITEM_SLOT_SAVE& b)
{
    auto event        = std::make_shared<DB_ITEM_SAVE_TWO_EVENT>();
    event->wakeupTime = Now();
    event->playerId   = playerId;
    event->a          = a;
    event->b          = b;
    ScheduleOnShard(std::move(event), ShardForPlayerId(playerId));
}

void CDBThread::RequestSaveAdena(int playerId, UInt32 adena)
{
    auto event        = std::make_shared<DB_ADENA_SAVE_EVENT>();
    event->wakeupTime = Now();
    event->playerId   = playerId;
    event->adena      = adena;
    ScheduleOnShard(std::move(event), ShardForPlayerId(playerId));
}

void CDBThread::RequestShopTransaction(int playerId, UInt32 adena, const DB_ITEM_SLOT_SAVE& slot)
{
    auto event        = std::make_shared<DB_SHOP_TX_EVENT>();
    event->wakeupTime = Now();
    event->playerId   = playerId;
    event->adena      = adena;
    event->slot       = slot;
    ScheduleOnShard(std::move(event), ShardForPlayerId(playerId));
}

void CDBThread::RequestSaveAlignment(int playerId, Int16 alignment, UInt16 pkCount)
{
    auto event        = std::make_shared<DB_ALIGNMENT_SAVE_EVENT>();
    event->wakeupTime = Now();
    event->playerId   = playerId;
    event->alignment  = alignment;
    event->pkCount    = pkCount;
    ScheduleOnShard(std::move(event), ShardForPlayerId(playerId));
}

void CDBThread::RequestSaveProgress(int playerId, std::vector<UInt8> data)
{
    auto event        = std::make_shared<DB_PROGRESS_SAVE_EVENT>();
    event->wakeupTime = Now();
    event->playerId   = playerId;
    event->data       = std::move(data);
    ScheduleOnShard(std::move(event), ShardForPlayerId(playerId));
}

void CDBThread::RequestSaveSkill(int playerId, const DB_SKILL_INFO& skill)
{
    auto event        = std::make_shared<DB_SKILL_SAVE_EVENT>();
    event->wakeupTime = Now();
    event->playerId   = playerId;
    event->skill      = skill;
    ScheduleOnShard(std::move(event), ShardForPlayerId(playerId));
}

void CDBThread::RequestResetSkills(int playerId)
{
    auto event        = std::make_shared<DB_SKILL_RESET_EVENT>();
    event->wakeupTime = Now();
    event->playerId   = playerId;
    ScheduleOnShard(std::move(event), ShardForPlayerId(playerId));
}

void CDBThread::RequestTx(int playerId, const char* what, std::function<bool(CDBConnection&)> work, std::function<void(bool)> done)
{
    auto event        = std::make_shared<DB_TX_EVENT>();
    event->wakeupTime = Now();
    event->what       = what;
    event->work       = std::move(work);
    event->done       = std::move(done);
    ScheduleOnShard(std::move(event), ShardForPlayerId(playerId));
}

void CDBThread::RequestTxKeyed(Int64 key, const char* what, std::function<bool(CDBConnection&)> work, std::function<void(bool)> done)
{
    auto event        = std::make_shared<DB_TX_EVENT>();
    event->wakeupTime = Now();
    event->what       = what;
    event->work       = std::move(work);
    event->done       = std::move(done);
    ScheduleOnShard(std::move(event), static_cast<int>(static_cast<UInt64>(key) % kShardCount));
}

void CDBThread::RequestTransfer(const DB_TRANSFER_SAVE& save, std::function<void(bool)> done)
{
    auto event = std::make_shared<DB_TRANSFER_EVENT>();
    event->save = save; event->done = std::move(done); event->wakeupTime = Now();
    ScheduleOnShard(std::move(event), ShardForPlayerId(save.playerIds[0]));
}

CDBThread::EventResult CDBThread::ProcessEvent(const shared_ptr<DB_EVENT_BASE>& event)
{
    CScopedDBConnection scopedConnection(GDBConnectionPool);
    const auto& connection = scopedConnection.Get();
    if (connection == nullptr)
    {
        // 결과를 기다리는 호출(로그인 · 트랜잭션)은 예전처럼 바로 실패로 알린다.
        if (IsWaitingCaller(event))
        {
            FailFast(event);
            return EventResult::Done;
        }
        // 저장은 버리지 않는다 — 이 샤드가 같은 이벤트를 다시 시도한다(RunEvent).
        return EventResult::RetryLater;
    }

    if (const auto e = std::dynamic_pointer_cast<DB_TRANSFER_EVENT>(event))
    {
        const auto result = connection->SaveTransfer(e->save);
        if (result == DB_TRANSFER_RESULT::Retry) return EventResult::RetryLater;
        if (e->done) e->done(result == DB_TRANSFER_RESULT::Committed);
        return EventResult::Done;
    }

    if (const auto e = std::dynamic_pointer_cast<DB_LOGIN_EVENT>(event))
    {
        const auto session = e->session;
        if (session == nullptr)
            return EventResult::Done;

        // 찾음 · 없음 · 오류를 나눈다. 오류는 로그인 실패 — 있는 캐릭터를 새 캐릭터 길(초보자 꾸러미 ·
        // 그로시 · 진행 초기화)로 보내지 않는다.
        DB_USER_INFO identity;
        bool found = false;
        if (!connection->ExtractUserInfo(e->name, identity, found))
        {
            CUserHelper::HandleLoginFail(session);
            return EventResult::Done;
        }

        int  playerId = identity.m_PlayerId;
        bool created  = false;
        if (!found)
        {
            // playerId는 DB가 발급하는 값이라 CUser 객체를 만들기 전에 반드시 먼저 확보해야 한다 —
            // 그래서 계정 INSERT를 여기(DB 워커 스레드)에서 동기적으로 끝내고, 결과 playerId를 쥔 채로
            // Zone 스레드에 진입시킨다. created는 이 호출이 행을 넣었을 때만 true다 — 같은 새 이름으로
            // 동시에 들어온 다른 세션이 먼저 만들었으면 false(있는 캐릭터로 들어간다).
            if (!connection->AddUserInfoInDataBase(e->name, e->password, playerId, created) || playerId <= 0)
            {
                CUserHelper::HandleLoginFail(session);
                return EventResult::Done;
            }
        }

        session->SetPlayerId(playerId);
        if (!created)
        {
            // Skip password verification — dev prototype with no auth requirement
            // 계정 하나 = 세션 하나. 이미 접속 중이면 그 세션을 끊고, 그쪽 저장이 다 쌓인 뒤 UserID로 읽는다.
            GUserManager->Claim(playerId, session, [this, session, playerId]()
            {
                RequestLoginSnapshot(session, playerId);
            });
            return EventResult::Done;
        }

        // 신규 캐릭터는 리니지처럼 마을에서 시작한다. 부하 시험(--load-test)이면 Zone을 돌아가며 흩어 세운다.
        static std::atomic<UInt32> s_counter{ 0 };
        const ZoneId zoneId = GLoadTestMode
            ? static_cast<ZoneId>(s_counter.fetch_add(1, std::memory_order_relaxed) % CZoneLayout::ZoneCount)
            : static_cast<ZoneId>(WorldData::kTownZoneId);

        DB_USER_INFO info;
        info.m_PlayerId = playerId;
        info.m_Name     = e->name;
        info.m_Password = e->password;
        GUserManager->Claim(playerId, session, [session, info, zoneId]()
        {
            GZoneManager->EnqueueByZone(zoneId, [session, info]() { CUserHelper::HandleAddUserInfo(session, info); });
        });
        return EventResult::Done;
    }

    if (const auto e = std::dynamic_pointer_cast<DB_TX_EVENT>(event))
    {
        // Phase 5: 프로시저 하나 = 트랜잭션 하나. 결과는 콜백으로(받는 쪽 큐에 넣기만 한다)
        const bool ok = e->work ? e->work(*connection) : false;
        if (!ok)
            std::cout << "[DB] tx " << e->what << " failed\n" << std::flush;
        if (e->done)
            e->done(ok);
        return EventResult::Done;
    }

    // 저장(값은 전부 절대값 — 같은 이벤트가 두 번 반영돼도 결과가 같다)
    bool        ok       = true;
    const char* what     = "";
    int         playerId = 0;
    if (const auto e = std::dynamic_pointer_cast<DB_PROGRESS_SAVE_EVENT>(event))
    {
        what = "progress"; playerId = e->playerId;
        ok = connection->SavePlayerProgress(e->playerId, e->data);
    }
    else if (const auto e = std::dynamic_pointer_cast<DB_SAVE_EVENT>(event))
    {
        what = "user"; playerId = e->playerId;
        ok = connection->SaveUserInfo(e->playerId, e->x, e->y, e->level, e->exp);
    }
    else if (const auto e = std::dynamic_pointer_cast<DB_ITEM_SAVE_EVENT>(event))
    {
        what = "item"; playerId = e->playerId;
        ok = connection->SaveInventorySlot(e->playerId, e->slotIndex, e->itemId, e->count, e->equipped, e->enchant, e->extra);
    }
    else if (const auto e = std::dynamic_pointer_cast<DB_ITEM_DELETE_EVENT>(event))
    {
        what = "item delete"; playerId = e->playerId;
        ok = connection->DeleteInventorySlot(e->playerId, e->slotIndex);
    }
    else if (const auto e = std::dynamic_pointer_cast<DB_ITEM_SAVE_TWO_EVENT>(event))
    {
        what = "two items"; playerId = e->playerId;
        ok = connection->SaveTwoInventorySlots(e->playerId, e->a, e->b);
    }
    else if (const auto e = std::dynamic_pointer_cast<DB_ADENA_SAVE_EVENT>(event))
    {
        what = "adena"; playerId = e->playerId;
        ok = connection->SavePlayerAdena(e->playerId, e->adena);
    }
    else if (const auto e = std::dynamic_pointer_cast<DB_SHOP_TX_EVENT>(event))
    {
        what = "shop"; playerId = e->playerId;
        ok = connection->ShopTransaction(e->playerId, e->adena, e->slot);
    }
    else if (const auto e = std::dynamic_pointer_cast<DB_ALIGNMENT_SAVE_EVENT>(event))
    {
        what = "alignment"; playerId = e->playerId;
        ok = connection->SavePlayerAlignment(e->playerId, e->alignment, e->pkCount);
    }
    else if (const auto e = std::dynamic_pointer_cast<DB_SKILL_SAVE_EVENT>(event))
    {
        what = "skill"; playerId = e->playerId;
        ok = connection->SavePlayerSkill(e->playerId, e->skill);
    }
    else if (const auto e = std::dynamic_pointer_cast<DB_SKILL_RESET_EVENT>(event))
    {
        what = "skill reset"; playerId = e->playerId;
        ok = connection->ResetPlayerSkills(e->playerId);
    }

    if (ok)
        return EventResult::Done;
    // 연결이 끊겨서 실패했으면 같은 이벤트를 다시(다음 Pop이 새로 붙는다). 그 밖의 오류(제약 위반 등)는
    // 다시 해도 같으므로 찍고 넘어간다 — 샤드를 영원히 막지 않는다.
    if (connection->LostConnection())
        return EventResult::RetryNow;
    std::cout << "[DB] save " << what << " playerId=" << playerId << " failed — dropped\n" << std::flush;
    return EventResult::Done;
}

void CDBThread::RunEvent(Shard& shard, const shared_ptr<DB_EVENT_BASE>& event)
{
    Duration delay = kRetryFirst;
    bool retried = false;
    bool reconnected = false;
    for (EventResult result; (result = ProcessEvent(event)) != EventResult::Done;)
    {
        // 쓰던 커넥션이 끊겼으면(DB 재시작 뒤 풀에 남은 커넥션 등) 한 번은 바로 다시 — 다음 Pop이 새로 붙는다.
        if (result == EventResult::RetryNow && !reconnected)
        {
            reconnected = true;
            continue;
        }
        retried = true;
        if (!m_bOutage.exchange(true))
            std::cout << "[DB] no database connection — holding writes in order, retrying every "
                << std::chrono::duration_cast<std::chrono::milliseconds>(kRetryFirst).count() << "ms to "
                << std::chrono::duration_cast<std::chrono::milliseconds>(kRetryMax).count() << "ms\n" << std::flush;
        WaitBeforeRetry(shard, delay);
        delay = (std::min)(delay * 2, kRetryMax);
    }
    if (retried && m_bOutage.exchange(false))
        std::cout << "[DB] database connection back — held writes resumed\n" << std::flush;
}

void CDBThread::WaitBeforeRetry(Shard& shard, Duration delay)
{
    const auto retryAt = Now() + delay;
    std::unique_lock lock(shard.lock);
    while (true)
    {
        // 막힌 저장 뒤에 줄 선 로그인 · 트랜잭션은 기다리게 두지 않고 지금 실패로 알린다(쓰지 않으므로 순서와 무관).
        // 나머지(저장)는 넣은 순서 그대로 큐에 남는다.
        if (shard.waitingCallers > 0)
        {
            std::vector<shared_ptr<DB_EVENT_BASE>> waiting, keep;
            while (!shard.events.empty())
            {
                auto next = shard.events.top();
                shard.events.pop();
                (IsWaitingCaller(next) ? waiting : keep).push_back(std::move(next));
            }
            for (auto& next : keep)
                shard.events.push(std::move(next));
            shard.waitingCallers = 0;

            lock.unlock();   // 콜백이 다른(또는 이) 샤드에 새 이벤트를 넣을 수 있다
            for (const auto& next : waiting)
                FailFast(next);
            lock.lock();
            continue;
        }
        if (Now() >= retryAt)
            return;
        // 다시 시도할 때나 새 이벤트가 들어올 때(notify) 깬다 — 바쁜 대기 없이, 샤드 락은 기다리는 동안 놓는다.
        shard.cv.wait_until(lock, retryAt);
    }
}

void CDBThread::DoDataBase(int shardIndex)
{
    ASSERT_CRASH(shardIndex >= 0 && shardIndex < kShardCount);
    Shard& shard = m_Shards[shardIndex];

    std::unique_lock lock(shard.lock);

    while (true) {
        shard.cv.wait(lock, [&shard]() { return !shard.events.empty(); });

        while (!shard.events.empty()) {
            const auto nextWakeup = shard.events.top()->wakeupTime;
            const bool rescheduledEarlierEvent = shard.cv.wait_until(lock, nextWakeup, [&shard, nextWakeup]()
            {
                return shard.events.empty() || shard.events.top()->wakeupTime < nextWakeup;
            });

            if (shard.events.empty())
                break;
            if (rescheduledEarlierEvent)
                continue;
            if (shard.events.top()->wakeupTime > Now())
                continue;

            auto event = shard.events.top();
            shard.events.pop();
            if (shard.waitingCallers > 0 && IsWaitingCaller(event))
                --shard.waitingCallers;

            // 이 샤드는 스레드 하나가 전담하므로, 여기서 락을 놓고 ODBC 왕복을
            // 하는 동안 같은 샤드의 다음 이벤트를 다른 스레드가 먼저 처리해버리는
            // 일이 없다 — 그래서 같은 플레이어의 저장이 항상 넣은 순서대로 커밋된다.
            // 연결이 없으면 RunEvent가 이 이벤트를 끝낼 때까지 다시 시도한다(다음 이벤트를 먼저 꺼내지 않는다).
            lock.unlock();
            RunEvent(shard, event);
            lock.lock();
        }
    }
}
