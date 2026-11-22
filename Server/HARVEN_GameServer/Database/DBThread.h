#pragma once

class CDBConnectionPool;
class CGameSession;

// DB 작업을 게임 로직 스레드에서 떼어내 비동기로 처리한다.
//
// 큐 하나를 여러 스레드가 나눠 갖는 대신, "샤드(shard)"를 kShardCount개 두고
// 스레드 하나가 샤드 하나를 전담한다. 그리고 같은 행(row)을 건드리는 이벤트는
// 항상 같은 샤드로 보낸다(한 플레이어의 저장 · 로그인 읽기는 전부 playerId = User.UserID 기준).
//
// 왜 이렇게 하는가 — 예전처럼 공용 큐를 여러 스레드가 나눠 가지면, 큐에서 꺼내는
// 순서는 맞아도 꺼낸 뒤 각자 ODBC 왕복을 하므로 "먼저 꺼낸 것이 먼저 커밋된다"는
// 보장이 없다. 같은 플레이어가 장착 -> 해제를 빠르게 하면 두 저장이 서로 다른
// 스레드로 가서 순서가 뒤바뀐 채 커밋될 수 있고, 그 상태로 서버가 죽으면 DB에는
// 옛날 상태가 영구히 남는다. 샤드를 고정하면 같은 플레이어의 쓰기는 한 스레드가
// 넣은 순서 그대로 하나씩 처리하므로 이 역전이 구조적으로 불가능해진다.
//
// 병렬성은 그대로다 — 스레드도 커넥션도 여전히 kShardCount개가 전부 돌고, 서로
// 다른 플레이어끼리는 예전처럼 완전히 병렬로 처리된다. playerId가 DB
// auto-increment 정수라 나머지 연산만으로도 샤드에 거의 균등하게 흩어진다.
//
// DB 연결이 없을 때(RDS 재시작 · 네트워크 끊김) 저장 이벤트는 버리지 않는다 — 그 샤드는 같은
// 이벤트를 200ms부터 2초까지 늘려 가며 다시 시도하고, 성공하기 전에는 큐의 다음 이벤트를 꺼내지
// 않는다(쓰기가 빠지거나 순서가 바뀌지 않는다). 결과를 기다리는 호출(로그인 · DB_TX_EVENT)은 예전처럼
// 바로 실패로 알린다 — 막힌 저장 뒤에 줄 서 있던 것도 기다리는 동안 꺼내 실패로 알린다.
class CDBThread
{
public:
    using Clock    = std::chrono::steady_clock;
    using Duration = Clock::duration;

    // 샤드 수 = DB 워커 스레드 수. GameServer.cpp의 스레드 생성 루프와 반드시
    // 같아야 하므로(샤드에 담당 스레드가 없으면 그 샤드 이벤트는 영원히 처리되지
    // 않는다) 그쪽에서도 이 상수를 그대로 쓴다.
    static constexpr int kShardCount = 8;

    CDBThread() = default;
    ~CDBThread() = default;

    // shardIndex 샤드만 전담해서 처리한다. 스레드 하나당 하나씩, 0 ~ kShardCount-1을
    // 빠짐없이 한 번씩 맡겨야 한다.
    void DoDataBase(int shardIndex);

    void RequestLogin(const SessionSharedPtr& session, const std::string& name, const std::string& password);
    // 계정을 잡은 뒤(CUserManager) 로그인에 필요한 것을 UserID로 한 번에 읽어 Zone에 넘긴다.
    // 캐릭터 행 · 소지품 · 가호 · 진행 · 기술 중 하나라도 읽다 실패하면 로그인 실패(행이 없는 것은 괜찮다).
    void RequestLoginSnapshot(const SessionSharedPtr& session, int playerId);
    void RequestSaveUser(const ObjID& subjectId, const DB_USER_INFO& info);

    // 인벤토리 슬롯 하나를 즉시(디바운스 없이) 저장/삭제한다. 호출한 존 스레드를
    // 막지 않는 비동기 enqueue — 실제 ODBC 호출은 DB 워커 스레드가 처리한다.
    // playerId는 로그인 시 CUser에 캐싱해둔 정수 PK를 그대로 넘긴다(name 조회 없음).
    // 같은 playerId의 요청은 항상 같은 샤드로 가므로 넣은 순서대로 반영된다.
    void RequestSaveItem(int playerId, UInt16 slotIndex, UInt16 itemId, UInt16 count, bool equipped, UInt32 enchant, UInt64 extra = 0);
    void RequestDeleteItem(int playerId, UInt16 slotIndex);

    // 두 슬롯을 하나의 트랜잭션으로 함께 반영한다(장착 시 이전 장비 자동 탈착,
    // 슬롯 교체처럼 "논리적으로 한 동작"인 경우 반드시 이걸 쓴다 — Inventory.cpp 참고).
    void RequestSaveTwoItems(int playerId, const DB_ITEM_SLOT_SAVE& a, const DB_ITEM_SLOT_SAVE& b);

    // ── 리니지풍 컨텐츠 확장 ──
    // 셋 다 playerId 샤드로 간다(인벤토리 슬롯 저장과 같은 샤드) — 한 플레이어의
    // 그로시/슬롯/성향치 변경이 넣은 순서 그대로 커밋된다. 값은 절대값(최신 상태)이다.
    void RequestSaveAdena(int playerId, UInt32 adena);
    void RequestShopTransaction(int playerId, UInt32 adena, const DB_ITEM_SLOT_SAVE& slot);
    void RequestSaveAlignment(int playerId, Int16 alignment, UInt16 pkCount);
    void RequestSaveProgress(int playerId, std::vector<UInt8> data);
    // Phase 2-D 기술 성장(UserSkill) — 같은 playerId 샤드라 넣은 순서대로 커밋된다
    void RequestSaveSkill(int playerId, const DB_SKILL_INFO& skill);
    void RequestResetSkills(int playerId);

    // Phase 5 결사 · 경매장: 결과가 필요한 트랜잭션 하나(DB_TX_EVENT). playerId 샤드 — 그 사람의 그로시 · 슬롯 저장과 한 줄로.
    // done(ok)은 DB 스레드에서 불린다(받는 쪽 큐에 작업만 넣을 것). 결사 행처럼 사람이 아닌 것은 RequestTxKeyed(번호 샤드).
    void RequestTx(int playerId, const char* what, std::function<bool(CDBConnection&)> work, std::function<void(bool)> done);
    void RequestTxKeyed(Int64 key, const char* what, std::function<bool(CDBConnection&)> work, std::function<void(bool)> done);
    // 호출자는 양쪽 인벤토리를 동결하고 두 playerId 큐의 barrier를 완료한 뒤 부른다.
    // 연결 오류/응답 유실은 동일 receipt로 재시도한다. 결과가 확정되기 전에는 done을 부르지 않는다.
    void RequestTransfer(const DB_TRANSFER_SAVE& save, std::function<void(bool)> done);

    [[nodiscard]] static Clock::time_point Now() noexcept { return Clock::now(); }

private:
    struct DBEventCompare
    {
        bool operator()(const shared_ptr<DB_EVENT_BASE>& lhs, const shared_ptr<DB_EVENT_BASE>& rhs) const noexcept;
    };

    using EventQueue = std::priority_queue<shared_ptr<DB_EVENT_BASE>, std::vector<shared_ptr<DB_EVENT_BASE>>, DBEventCompare>;

    struct Shard
    {
        EventQueue              events;
        std::mutex              lock;
        std::condition_variable cv;
        size_t                  waitingCallers = 0;   // events 안의 로그인 · DB_TX_EVENT 수(연결이 없을 때 바로 실패로 꺼낸다)
    };

    // Done = 끝(성공이든 실패든). 못 한 저장은 같은 이벤트를 다시 시도한다:
    // RetryNow = 쓰던 커넥션이 끊김(한 번은 새 커넥션으로 바로) · RetryLater = 풀이 커넥션을 못 줌(기다렸다가)
    enum class EventResult { Done, RetryNow, RetryLater };

    // 연결이 없을 때 다시 시도하는 간격: 처음 200ms, 두 배씩 2초까지
    static constexpr Duration kRetryFirst = std::chrono::milliseconds(200);
    static constexpr Duration kRetryMax   = std::chrono::seconds(2);

    void ScheduleOnShard(shared_ptr<DB_EVENT_BASE> event, int shardIndex);
    EventResult ProcessEvent(const shared_ptr<DB_EVENT_BASE>& event);
    // event를 끝낼 때까지 처리한다(RetryLater면 기다렸다가 같은 이벤트를 다시). 샤드 락 없이 부른다.
    void RunEvent(Shard& shard, const shared_ptr<DB_EVENT_BASE>& event);
    // 다시 시도할 때까지 샤드 cv에서 기다린다 — 그 사이 들어온 로그인 · 트랜잭션은 바로 실패로 알린다.
    void WaitBeforeRetry(Shard& shard, Duration delay);

    [[nodiscard]] static bool IsWaitingCaller(const shared_ptr<DB_EVENT_BASE>& event) noexcept;
    static void FailFast(const shared_ptr<DB_EVENT_BASE>& event);

    // 같은 행을 건드리는 이벤트가 항상 같은 샤드로 가게 하는 매핑 — 한 플레이어의 User · 소지품 ·
    // 진행 · 기술 저장과 다음 로그인의 읽기가 모두 그 playerId(User.UserID) 샤드에 한 줄로 선다.
    [[nodiscard]] static int ShardForPlayerId(int playerId) noexcept;
    // 순서를 맞출 대상이 없는 이벤트(로그인: 아직 playerId를 모르는 조회 작업)용.
    // 아무 샤드에나 가도 되므로 부하만 고르게 흩어준다.
    [[nodiscard]] int NextRoundRobinShard() noexcept;

private:
    std::array<Shard, kShardCount> m_Shards;
    // 샤드별 락과 무관하게 전역으로 증가한다 — 같은 wakeupTime을 가진 이벤트끼리
    // "먼저 넣은 것이 먼저"를 판정하는 tie-break 용도라 전역 단조 증가면 충분하다.
    std::atomic<UInt64> m_NextSequence{ 0 };
    std::atomic<UInt32> m_RoundRobinCursor{ 0 };
    // DB 연결이 없는 동안 true — 끊김 · 회복을 한 번씩만 찍는다(샤드 여럿이 함께 다시 시도한다).
    std::atomic<bool>     m_bOutage{ false };
};
