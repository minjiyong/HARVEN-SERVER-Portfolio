#pragma once

// ── Standard library ──────────────────────────────────────────────────────────
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <concepts>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <future>
#include <iostream>
#include <locale>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <random>
#include <set>
#include <shared_mutex>
#include <stack>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// ── 네트워크(Boost.Asio) · 문자열 도우미 ────────────────────────────────────────────
#include <boost/asio.hpp>
#ifndef OUT
#define OUT
#endif
inline constexpr size_t _TRUNCATE = static_cast<size_t>(-1);
inline int strncpy_s(char* destination, size_t capacity, const char* source, size_t count)
{
    if (!destination || capacity == 0 || !source) return 22;
    const size_t limit = count == _TRUNCATE ? capacity - 1 : (std::min)(count, capacity - 1);
    size_t length = 0;
    while (length < limit && source[length] != '\0') ++length;
    std::memcpy(destination, source, length);
    destination[length] = '\0';
    return 0;
}
template<size_t N>
inline int strncpy_s(char (&destination)[N], const char* source, size_t count)
{
    return strncpy_s(destination, N, source, count);
}

using namespace std;

// ── 형 별칭(README "코드 컨벤션") — 정수 Int8 … UInt64 · 서버 객체 공유 포인터 UserSharedPtr … ──
#include "Base/Core/Types.h"
#include "GameServer_DefType.h"

// Sync primitives used by remaining Core headers
using Mutex     = std::mutex;
using RWLock    = std::shared_mutex;
using LockGuard = std::lock_guard<std::mutex>;
template<typename T>
using Atomic    = std::atomic<T>;

// ── Crash assertion (replaces Core/CoreMacro.h) ──────────────────────────────
#define ASSERT_CRASH(expr) \
    do { if (!(expr)) { volatile int* _p = nullptr; *_p = 0; } } while (false)

// ── DB user info (must come before Core\DBConnection.h which uses DB_USER_INFO) ──
struct DB_USER_INFO
{
    int      m_PlayerId = 0;  // User.UserID (DB가 발급한 정수 PK). 로그인/계정생성 시 한 번만 조회해 캐싱한다.
    string   m_Name;
    string   m_Password;
    int      m_X     = 0;
    int      m_Y     = 0;
    UInt8    m_Level = 1;
    UInt32   m_Exp   = 0;
    // 리니지풍 컨텐츠 확장 컬럼(User.Adena/Alignment/PkCount)
    UInt32 m_Adena     = 0;
    Int16  m_Alignment = 0;
    UInt16 m_PkCount   = 0;
};
using DB_PLAYER_INFO = DB_USER_INFO;

// ── DB item info (must come before Core\DBConnection.h which uses it) ──
// UserInventory 테이블 한 행. ItemTableRow.h를 끌어오지 않도록 itemId는
// ItemTableId가 아니라 그냥 UInt16로 둔다(DB_USER_INFO가 Stat.h 타입을
// 쓰지 않는 것과 같은 이유).
struct DB_ITEM_INFO
{
    UInt16 m_SlotIndex    = 0;
    UInt16 m_ItemId       = 0;
    UInt16   m_Count      = 0;
    bool     m_bEquipped  = false;
    UInt32  m_Enchant   = 0;   // UserInventory.Enchant (무기 강화 수치)
    UInt64 m_Extra     = 0;   // UserInventory.Extra (V2 희귀도·옵션 — Data/ItemRules.h ItemInstance)
};

// Phase 2-D 기술 성장 한 줄(UserSkill) — 레벨 1 · 갈래 없음은 행이 없다
struct DB_SKILL_INFO
{
    UInt16 m_SkillId = 0;
    UInt8  m_Level   = 1;
    UInt8  m_Pick1   = 0;
    UInt8  m_Pick2   = 0;
};

// 슬롯 두 개를 한 SQL 트랜잭션으로 함께 반영하기 위한 단위 데이터(CDBConnection::
// SaveTwoInventorySlots가 파라미터로 받으므로 그 선언보다 먼저 와야 한다).
// hasItem=false면 그 슬롯은 삭제 대상(itemId/count/equipped는 무시된다).
struct DB_ITEM_SLOT_SAVE
{
    UInt16   slotIndex = 0;
    bool     hasItem   = false;
    UInt16   itemId    = 0;
    UInt16   count     = 0;
    bool     equipped  = false;
    UInt32  enchant   = 0;
    UInt64 extra     = 0;   // V2 희귀도·옵션(ItemInstance)
};

struct DB_ENCHANT_GRACE
{
    UInt16 itemId = 0;
    UInt32 currentLevel = 0;
    UInt16 failures = 0;
};

struct DB_ENCHANT_SAVE
{
    std::array<DB_ITEM_SLOT_SAVE, 3> before{}, after{};
    UInt8 count = 0;
    UInt16 itemId = 0;
    UInt32 level = 0; // 성공한 +11 이상만 최초 기록
    std::string name;
    UInt16 graceBefore = 0, graceAfter = 0; // before[1].enchant 구간의 실패 횟수(CAS)
};

// 경제 1차 제작 · 분해: 여러 칸 + 그로시를 한 SQL 트랜잭션으로(CDBConnection::SaveItemBatch → P_ITEM_BATCH_TRADE).
// before[i] · after[i]는 같은 칸(칸 번호가 겹치지 않는다). before는 낙관 검사(지금 DB 행과 같아야 — 빈 칸이면 행이 없어야),
// after는 그 칸의 새 상태(hasItem=false면 지운다). 그로시는 adenaBefore ≠ adenaAfter일 때만 검사 · 저장한다.
struct DB_ITEM_BATCH_SAVE
{
    std::vector<DB_ITEM_SLOT_SAVE> before, after;
    UInt32 adenaBefore = 0, adenaAfter = 0;
};

// 양쪽 슬롯 + 잔액 + 고유 완료 기록을 한 번에 저장한다. Retry는 결과 불명확(멱등 재시도).
enum class DB_TRANSFER_RESULT { Committed, Rejected, Retry };
struct DB_TRANSFER_SAVE
{
    std::string receipt;
    UInt8 kind = 0;
    std::array<int, 2> playerIds{};
    std::array<DB_ITEM_BATCH_SAVE, 2> players;
    std::array<std::vector<DB_ITEM_SLOT_SAVE>, 2> offers; // 완료 감사 기록(변경량이 0인 동일 품목 교환도 기록)
};


// ── Phase 5 결사 · 경매장 DB 행 ─────────────
// CDBConnection의 새 프로시저가 쓰므로 그 선언보다 먼저. 번호(결사 · 목록 · 정산함)는 서비스가 정한다.
struct DB_GUILD_ROW
{
    int      guildId  = 0;
    string   name;                 // UTF-8
    UInt8    emblem   = 0;
    string   notice;               // UTF-8
    int      leaderId = 0;
};
struct DB_GUILD_MEMBER_ROW
{
    int      guildId  = 0;
    int      playerId = 0;
    UInt8    rank     = 3;         // GuildRule::Rank
    string   name;                 // User.Name(불러올 때 함께)
    UInt8    level    = 1;
    UInt32   lastSeenMin = 0;      // 마지막 저장 시각(유닉스 분) — 없으면 0
};
// ── 결사 성장(Content/Guild/GuildProgress · Shared/Document/GUILD.md) ─────────────
// 진행(레벨 · 경험치 · 기여도 · 임무 · 업적 …)은 버전 있는 바이너리 한 덩어리(GuildProgress.Data) — 기능이 늘 때마다 열을 늘리지 않는다.
// 금고(Seals)만 따로 열 — 늘 더하고 빼는 식으로만 쓴다(결사 번호 샤드의 보상과 사람 샤드의 상점 구매가 차례와 상관없이 맞는다).
struct DB_GUILD_PROGRESS_ROW
{
    int                 guildId   = 0;
    UInt32              seals     = 0;
    std::vector<UInt8>  data;              // 비었으면 아직 저장한 적 없는 결사(기본값으로 시작)
    Int64               createdAt = 0;     // Guild.CreatedAt(유닉스 초)
};
struct DB_GUILD_LOG_ROW
{
    int      guildId = 0;
    Int64    at      = 0;                  // 유닉스 초
    UInt8    kind    = 0;                  // GuildLogKind
    Int32    value   = 0;
    string   who;                          // 사람 이름(ASCII)
    string   what;                         // 덧말(UTF-8 30자 이하 — 사람 이름 · 아이템 · 임무 · 업적 · 칭호 이름)
};
struct DB_GUILD_POST_ROW
{
    Int64    postId   = 0;
    int      guildId  = 0;
    int      authorId = 0;
    string   author;
    UInt8    category = 0;                 // GuildPostCategory
    UInt8    pinned   = 0;
    string   title;                        // UTF-8, 한글 10자
    string   body;                         // UTF-8, 한글 50자
    Int64    at       = 0;
};
struct DB_LISTING_ROW
{
    Int64    listingId = 0;
    int      sellerId  = 0;
    UInt16   itemId    = 0;
    UInt16 count       = 0;
    UInt32  enchant   = 0;
    UInt64 extra     = 0;
    UInt32 price     = 0;
    UInt32 deposit   = 0;
    Int64  createdAt = 0;        // 유닉스 초
    Int64  expiresAt = 0;
    UInt8  status    = 0;        // 0 판매 중 · 1 팔림 · 2 취소 · 3 만료
};
struct DB_SETTLEMENT_ROW
{
    Int64    settlementId = 0;
    int      playerId  = 0;
    UInt8    kind      = 0;        // 1 그로시 · 2 물건
    UInt32   adena     = 0;
    UInt16 itemId      = 0;
    UInt16 count       = 0;
    UInt32  enchant   = 0;
    UInt64 extra     = 0;
    Int64  listingId = 0;
    UInt8  reason    = 0;        // AuctionSettleReason
    Int64  createdAt = 0;
};
struct DB_PRICE_ROW
{
    UInt16 itemId = 0;
    UInt32 price  = 0;           // 한 개 값(수량으로 나눈 값)
    Int64  soldAt = 0;
};

// ── Core headers still in use ─────────────────────────────────────────────────
#include "WorkerThread/ThreadManager.h"
#include "Database/DBConnectionPool.h"
#include "Database/DBConnection.h"

// ── Project headers ───────────────────────────────────────────────────────────
#include "Base/ObjID/ContentID.h"
#include "Base/ObjID/ObjID.h"
#include "Protocol/Protocol.h"
#include "Base/ServerGlobal.h"
#include "EnumCategory.h"

class CGameSession;

struct DB_EVENT_BASE : std::enable_shared_from_this<DB_EVENT_BASE>
{
    std::chrono::steady_clock::time_point wakeupTime;
    UInt64 sequence = 0;
    virtual ~DB_EVENT_BASE() = default;
};

struct DB_LOGIN_EVENT : DB_EVENT_BASE
{
    SessionSharedPtr session;
    string name;
    string password;
};

// 위치 · 레벨 · 경험치 저장(P_USER_UPSERT). playerId 샤드 — 그 사람의 다른 저장 · 다음 로그인 읽기와 한 줄로.
struct DB_SAVE_EVENT : DB_EVENT_BASE
{
    ObjID    subjectId;
    int      playerId = 0;
    short    x     = 0;
    short    y     = 0;
    UInt8    level = 1;
    UInt32   exp   = 0;
};

// 인벤토리 슬롯 하나를 즉시(디바운스 없이) 저장/삭제하기 위한 이벤트.
// CInventory의 각 mutating 메서드가 성공할 때마다 바로 Schedule된다.
// playerId는 로그인 시 한 번 캐싱된 정수 PK를 그대로 쓴다 — name(NVARCHAR) 기반
// 조회/조인이 DB 쪽에서 완전히 사라진다.
struct DB_ITEM_SAVE_EVENT : DB_EVENT_BASE
{
    int      playerId  = 0;
    UInt16   slotIndex = 0;
    UInt16 itemId      = 0;
    UInt16   count     = 0;
    bool     equipped  = false;
    UInt32  enchant   = 0;
    UInt64 extra     = 0;
};

struct DB_ITEM_DELETE_EVENT : DB_EVENT_BASE
{
    int      playerId  = 0;
    UInt16   slotIndex = 0;
};

// 두 슬롯이 "동시에" 바뀌는 동작(장착 시 이전 장비 자동 탈착, 슬롯 교체)을 위한
// 이벤트. 두 슬롯을 별도 이벤트 두 개로 나눠 보내면, DB 워커 스레드가 8개라
// 어느 한쪽만 먼저 반영된 채로 서버가 죽는 순간 DB에는 "두 개 다 장착됨" 같은
// 불변조건 위반 상태가 영구히 남을 수 있다 — 그래서 반드시 하나의 이벤트,
// 하나의 트랜잭션으로 묶는다(CDBConnection::SaveTwoInventorySlots 참고).
struct DB_ITEM_SAVE_TWO_EVENT : DB_EVENT_BASE
{
    int               playerId = 0;
    DB_ITEM_SLOT_SAVE a;
    DB_ITEM_SLOT_SAVE b;
};

// ── 리니지풍 컨텐츠 확장 DB 이벤트 ─────────────────────────────────────────────
// 전부 playerId 기준 샤드로 보낸다 — 인벤토리 슬롯 저장과 같은 샤드라서, 한
// 플레이어의 "그로시 변경"과 "슬롯 변경"이 넣은 순서 그대로 커밋된다.
// 값은 항상 델타가 아니라 절대값(메모리의 최신 상태)을 싣는다: 메모리가 권위이고
// DB는 그 저널이므로, 마지막으로 커밋된 이벤트가 곧 최신 상태가 된다.

// 그로시 획득(줍기) 등, 슬롯 변화 없이 지갑만 바뀐 경우.
struct DB_ADENA_SAVE_EVENT : DB_EVENT_BASE
{
    int      playerId = 0;
    UInt32   adena    = 0;
};

// 상점 구매/판매: 그로시와 슬롯 하나를 "하나의 SQL 트랜잭션"으로 반영한다.
// 따로 저장하면 그 사이에 서버가 죽었을 때 "돈은 나갔는데 물건이 없음" 또는
// "물건은 들어왔는데 돈이 그대로"(=복사) 상태가 DB에 영구히 남는다.
struct DB_SHOP_TX_EVENT : DB_EVENT_BASE
{
    int               playerId = 0;
    UInt32            adena    = 0;
    DB_ITEM_SLOT_SAVE slot;
};

struct DB_ALIGNMENT_SAVE_EVENT : DB_EVENT_BASE
{
    int      playerId  = 0;
    Int16    alignment = 0;
    UInt16   pkCount   = 0;
};

// V2 캐릭터 진행 데이터(직업·포인트·퀘스트…) 저장 — 인벤토리와 같은 playerId 샤드
struct DB_PROGRESS_SAVE_EVENT : DB_EVENT_BASE
{
    int                  playerId = 0;
    std::vector<UInt8>   data;
};

// Phase 2-D 기술 성장(UserSkill) — 인벤토리와 같은 playerId 샤드. 값은 절대값(바뀐 뒤 상태)이라
// 같은 요청이 두 번 반영돼도 결과가 같다. 초기화는 행을 모두 지운다(레벨 1 · 갈래 없음 = 행 없음).
struct DB_SKILL_SAVE_EVENT : DB_EVENT_BASE
{
    int           playerId = 0;
    DB_SKILL_INFO skill;
};

struct DB_SKILL_RESET_EVENT : DB_EVENT_BASE
{
    int playerId = 0;
};

// Phase 5: 결과가 필요한 트랜잭션(경매 등록 · 구매 · 취소 · 만료 · 정산 받기, 결사 세우기 · 변경).
// work는 DB 스레드에서 커넥션으로 프로시저 하나(= 트랜잭션 하나)를 부르고 성공 여부를 돌려준다.
// done은 그 결과로 DB 스레드에서 불린다 — 결과를 쓰는 쪽(서비스 · Zone)의 큐에 작업을 넣기만 한다(여기서 상태를 만지지 않는다).
// 샤드는 돈 · 물건 주인의 playerId(CDBThread::RequestTx) — 그 사람의 다른 그로시 · 슬롯 저장과 넣은 순서대로 커밋된다.
struct DB_TX_EVENT : DB_EVENT_BASE
{
    const char*                          what = "";
    std::function<bool(CDBConnection&)>  work;
    std::function<void(bool)>            done;
};

struct DB_TRANSFER_EVENT : DB_EVENT_BASE
{
    DB_TRANSFER_SAVE save;
    std::function<void(bool)> done;
};

// ── Timer event types ────────────────────────────────────────────────────────
enum TIMER_EVENT_TYPE
{
    EV_MONSTER_THINK,         // 몬스터 생각(행동 트리 한 번 — targetId.DatabaseID = 예약 번호). 예전 배회 · 추격 · 평타 · 패턴 · 보스 광역기 사슬을 하나로
    EV_MONSTER_RESPAWN,
    EV_MONSTER_AOE,           // 몬스터 장판 틱(subjectId = 주인, targetId.DatabaseID = 장판 핸들)
    EV_HEAL,
    EV_USER_RESPAWN,
    EV_ITEM_DESPAWN,
    // (EV_ITEM_LOOT_PRIORITY_EXPIRE 없앰 — 줍기 우선권은 주울 때 시각으로 판정한다, CItemHelper::CanLoot)
    // 리니지풍 컨텐츠 확장
    EV_PURPLE_EXPIRE,         // 적대 해제 확인
    EV_HASTE_EXPIRE,          // 가속 해제 확인
    // V2 전투
    EV_SKILL_IMPACT,          // 예고 뒤 스킬 판정(targetId.DatabaseID = 예약 번호)
    EV_STATUS_TICK,           // 지속 피해 틱
    // 살아 있는 마을
    EV_NPC_TICK,              // 마을 NPC 배회·혼잣말(마을 Zone 스레드, 0.25초마다)
    // 파티
    EV_PARTY_TICK,            // Zone마다 파티원 상태 모으기(그 Zone 스레드, 시트 PartyConst sync_interval_ms마다 — CPartyHelper::HandleZoneTick)
    EV_TRANSFER_TICK,
    // 개인 업적
    EV_ACHIEVE_SAVE,          // 달성 · 받기가 아닌 진척을 60초 뒤에 저장(사람마다 하나 — CAchieveHelper::HandleSaveTimer)
};

struct TIMER_EVENT
{
    ObjID subjectId;
    std::chrono::steady_clock::time_point wakeupTime;
    TIMER_EVENT_TYPE event;
    ObjID targetId;
    UInt64 sequence = 0;
    // 예약을 건 시점에 찍어두는 목적지 Zone. CTimerThread::Dispatch가 이 값으로
    // 곧장 큐에 넣어, 전역 조회표(CZoneManager::m_ObjectZones)를 뒤지지 않는다.
    // 자세한 조건은 CTimerThread::Schedule / Dispatch의 주석 참고.
    ZoneId zoneId = InvalidZoneId;
};

[[nodiscard]] inline UInt32 GetNowTime()
{
    return static_cast<UInt32>(chrono::duration_cast<chrono::milliseconds>(
        chrono::steady_clock::now().time_since_epoch()).count());
}

// ── IO / Socket state enums ───────────────────────────────────────────────────

// 세션 상태 — 접속(ST_ALLOC) → 로그인 요청(ST_LOGIN) → 게임 중(ST_INGAME) → 끊김(ST_CLOSED).
// 로그인 완료와 접속 끊김은 CGameSession::TryEnterGame / MarkClosed가 같은 락 안에서 바꿔 서로를 놓치지 않는다.
enum SOCKET_STATE
{
    ST_FREE,
    ST_ALLOC,
    ST_LOGIN,
    ST_INGAME,
    ST_CLOSED,
};

#include "Base/GameObjectManager.h"
