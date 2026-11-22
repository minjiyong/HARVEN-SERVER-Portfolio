#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <mutex>
#include <random>
#include <thread>
#include <unordered_map>
#include <vector>

using namespace std;
using namespace chrono;

#ifndef HARVEN_STRESS_MAX_BOTS
#define HARVEN_STRESS_MAX_BOTS 20000
#endif
const static int MAX_TEST        = HARVEN_STRESS_MAX_BOTS;   // 정적 배열 크기 — CMake HARVEN_STRESS_MAX_BOTS
const static int MAX_CLIENTS     = MAX_TEST * 2 + 10000;
const static int MAX_PACKET_SIZE = 255;
const static int MAX_BUFF_SIZE   = 255;

// 서버/클라이언트/스트레스테스트가 각자 프로토콜을 손으로 복제해서 들고
// 있으면 하나가 바뀔 때 나머지가 조용히 어긋난다(실제로 이 파일도 PacketType
// 순서와 SUBJECT_ATTACK_NFY_PACKET 필드가 실제 서버와 어긋나 있었다) — Client와
// 동일하게 GameServer의 Protocol.h를 직접 참조해 단일 소스로 통일한다.
// 찾는 곳은 include 뿌리(Shared · Server/HARVEN_GameServer · Libs — Tools/StressTest/CMakeLists.txt).
#include "Protocol/Protocol.h"
#include "NetworkModule.h"

// 받기는 epoll 하나를 워커 스레드들이 함께 기다린다(소켓마다 EPOLLONESHOT — 한 소켓은 한 번에 한 스레드만 읽는다).
// 보내기는 부른 스레드가 그 자리에서 쓴다(봇 요청은 255바이트 이하라 거의 한 번에 끝난다).
static int g_epoll = -1;

// 봇이 인벤토리를 다룰 때 쓰는 상수.
// 아이템 id는 기획 데이터(시트 Item_Table → Bin/GameData/Item.json)가 원본이다 — Protocol.h에는
// 없는 값이라 여기 옮겨 적는다(클라이언트의 ITEM_ASSET_TABLE도 같은 방식).
constexpr uint16_t INVALID_SLOT        = 0xFFFF;
constexpr uint16_t HEALTH_POTION_ID    = 1;  // 봇이 쓰는 회복 물약(약초 물약)

// 장비(무기) 아이템 id. 예전에는 "2번부터 전부 장비"였지만, 컨텐츠 확장으로 7번 이후에
// 그로시/물약/두루마리가 추가되면서 더 이상 성립하지 않는다(봇이 주운 두루마리를 장착하려 들면
// 서버는 실패만 돌려주지만, 봇이 진짜 무기 슬롯을 잃어버려 부하 패턴이 바뀐다).
// Bin/GameData/Item.json의 무기 행(equip_slot=Weapon: 2~6, 15~28)과 같아야 한다 — 이 프로젝트는
// Protocol.h만 참조해서 아이템 표 헤더를 직접 include하지 않는다. 방어구 · 장신구(29~)는 봇이 다루지 않는다
// (초보자 갑옷 · 장화는 서버가 입혀서 준다). 다른 직업 전용 무기를 주우면 장착이 실패할 뿐이다.
[[nodiscard]] constexpr bool IsEquipmentItemId(uint16_t itemId) noexcept
{
    return (itemId >= 2 && itemId <= 6) || (itemId >= 15 && itemId <= 28);
}

// 봇의 직업과 기술. 기술 id · 해금 레벨 · MP · 누르고 있기 시간은 기획 데이터(Bin/GameData/Class.json ·
// Skill.json · SkillAction.json · SkillKeyMap.json)가 원본이다 — Protocol.h에는 없는 값이라 옮겨 적는다.
// 서약기사는 1레벨에 범위 기술이 없다(회전 베기가 2레벨에 열린다). 그래서 5초마다 누르는 기술은
//   · 2레벨부터: 회전 베기 — 내 주변 1칸 원(예전 USER_SKILL_REQ의 십자베기처럼 "내 주변을 한 번에")
//   · 1레벨에서: 방패 강타 — 즉시 · 붙어 있는(1칸) 대상 하나. 대상이 없으면 누르지 않는다(InvalidTarget 응답만 돌아온다)
constexpr uint8_t  BOT_CLASS_ID           = 0;     // 서약기사(Knight) — Class.json 첫 행 = ClassId 0
constexpr uint16_t SKILL_BASIC_ATTACK     = 1;     // 검격(C): Basic · 부채꼴 1칸 3타 이어 치기 · 대상 없어도 나간다
constexpr uint16_t SKILL_SHIELD_BASH      = 2;     // 방패 강타(Q): 1레벨 · Normal(즉시) · 대상(Enemy) 1칸 · MP 10 · 6초
constexpr uint16_t SKILL_WHIRLWIND        = 3;     // 회전 베기(W): 2레벨 · Hold · 내 주변 원 1칸 · MP 14 · 5초
constexpr uint8_t  WHIRLWIND_UNLOCK_LEVEL = 2;
constexpr uint16_t SHIELD_BASH_MP         = 10;
constexpr uint16_t WHIRLWIND_MP           = 14;
constexpr int      WHIRLWIND_HOLD_MS      = 1500;  // SkillAction.hold_max_ms — 이 시간이 지나면 서버가 마무리 타격까지 한다

// 이동 응답(USER_MOVE_ACK)으로 지연을 재기 위해 "아직 응답이 안 온 이동"의 보낸 시각을 들고 있는 칸 수.
// 400ms마다 하나씩이므로 8칸이면 최소 3.2초어치다(2의 거듭제곱 — seq 아래 비트로 칸을 고른다).
constexpr int MOVE_RING_SIZE = 8;
static_assert((MOVE_RING_SIZE & (MOVE_RING_SIZE - 1)) == 0, "MOVE_RING_SIZE는 2의 거듭제곱이어야 한다");

// ConsumeReceivedBytes()는 MAX_PACKET_SIZE를 넘는 패킷을 받으면 그 클라이언트를
// 그냥 끊어버린다. 서버가 보내는 패킷 중 하나라도 버퍼보다 커지면 "봇이 그 패킷을
// 받는 순간 조용히 끊기는" 형태로 부하 테스트 전체가 망가진다(로그인 직후의
// ITEM_LIST_ACK가 대표적). 원인을 런타임에 헤매지 않도록 빌드 타임에 막는다 —
// Protocol.h가 서버→클라 패킷 전부의 최대 크기(MAX_SERVER_PACKET_SIZE)를 계산해 준다.
static_assert(MAX_SERVER_PACKET_SIZE <= static_cast<size_t>(MAX_PACKET_SIZE),
    "서버가 보내는 패킷이 봇 수신 버퍼보다 큽니다. MAX_PACKET_SIZE/MAX_BUFF_SIZE를 늘리세요.");
// 보내는 쪽도 요청 하나를 한 번에 쓴다.
static_assert(MAX_CLIENT_PACKET_SIZE <= static_cast<size_t>(MAX_BUFF_SIZE),
    "봇이 보내는 요청이 송신 버퍼보다 큽니다. MAX_BUFF_SIZE를 늘리세요.");

high_resolution_clock::time_point last_connect_time;

// 응답을 기다리는 이동 하나(보낸 순번 · 보낸 시각의 아래 32비트 ms). sentMs == 0이면 빈 칸.
struct PendingMove {
    uint16_t seq    = 0;
    uint32_t sentMs = 0;
};

struct CLIENT {
    ObjID    id{};
    short    x = 0;
    short    y = 0;
    uint16_t maxHp = PLAYER_MAX_HP;
    uint16_t hp    = PLAYER_MAX_HP;
    uint16_t mp    = 0;   // USER_RESOURCE_INF · SKILL_RESULT_ACK가 알려준다(로그인 직후 바로 온다)
    uint8_t  level = 1;
    uint32_t exp   = 0;
    int      visible_monsters = 0;
    int      visible_players  = 0;
    bool     dead        = false;
    bool     facing_left = false;
    int      zone_id     = -1;
    atomic_bool connected{ false };

    // --- 이동 순번 · 지연 측정 ---
    // 서버는 USER_MOVE_REQ마다 같은 seq의 USER_MOVE_ACK를 정확히 하나씩, 받은 순서대로
    // 돌려준다(거절이어도). 보낸 시각을 seq 아래 비트로 고른 칸에 적어 두었다가 응답에서 왕복 시간을 잰다.
    uint16_t    move_seq = 0;
    PendingMove pending_moves[MOVE_RING_SIZE]{};

    // --- 전투 ---
    uint16_t skill_seq         = 0;
    uint32_t basic_interval_ms = 0;   // 서버가 알려준 평타 간격(SKILL_RESULT_ACK Ok의 cooldownMs)
    uint32_t skill_interval_ms = 0;   // 5초 기술의 재사용 대기(같은 방식)
    // 조준할 몬스터 하나(가장 가까이에서 본 것). 몬스터 전체를 미러링하지 않고 이것만 든다 —
    // 평타(부채꼴 1칸)는 몬스터 쪽을 보고 눌러야 맞고, 방패 강타는 대상 id가 있어야 나간다.
    ObjID    focus_monster_id{};
    short    focus_x   = -1;
    short    focus_y   = -1;
    bool     has_focus = false;

    // --- 인벤토리 상태 ---
    // 슬롯 50칸(MAX_INVENTORY_SLOTS)을 통째로 미러링하면 클라이언트 수만큼 곱해져 메모리가 크게 늘어난다.
    // 봇은 "행동할 슬롯 하나"만 알면 충분하므로, 서버가 보내주는 슬롯 알림에서
    // 포션 슬롯과 장비 슬롯만 뽑아 들고 있는다.
    uint16_t potion_slot  = INVALID_SLOT;
    uint16_t potion_count = 0;
    uint16_t equip_slot   = INVALID_SLOT;
    bool     equipped     = false;
    int      visible_items = 0;
    // 지금 주우러 가는 필드 아이템(있으면 이동이 이쪽으로 유도된다).
    ObjID    target_item_id{};
    short    target_item_x  = -1;
    short    target_item_y  = -1;
    bool     has_target_item = false;

    int           client_socket = -1;
    std::mutex    send_lock;                       // 테스트 스레드 · 워커 스레드가 같은 소켓에 동시에 쓰지 않게
    unsigned char recv_buf[MAX_BUFF_SIZE]{};
    unsigned char packet_buf[MAX_PACKET_SIZE]{};
    int           buffered_bytes = 0;
    high_resolution_clock::time_point next_move_time{};
    high_resolution_clock::time_point next_attack_time{};
    high_resolution_clock::time_point next_skill_time{};
    high_resolution_clock::time_point next_chat_time{};
    high_resolution_clock::time_point next_pickup_time{};
    high_resolution_clock::time_point next_potion_time{};
    high_resolution_clock::time_point next_equip_time{};
    high_resolution_clock::time_point next_discard_time{};
};

array<CLIENT, MAX_CLIENTS> g_clients;

// --- Core counters (exposed via header) ---
atomic_int        num_connections   = 0;
atomic_int        active_clients    = 0;
atomic<int>       global_delay{ 0 };

// --- Extended stats (exposed via header) ---
atomic_int        dead_clients_count{ 0 };
atomic_int        delay_min_val{ INT_MAX };
atomic_int        delay_max_val{ 0 };
atomic_int        delay_avg_val{ 0 };
atomic<TestPhase> current_phase{ TestPhase::RAMP_UP };

// --- Graph history ring buffer ---
float g_delay_history[GRAPH_HISTORY_SIZE]{};
float g_client_history[GRAPH_HISTORY_SIZE]{};
int   g_history_index = 0;

// --- Per-client aggregate stats ---
atomic<float> g_avg_visible_monsters{ 0.0f };
atomic<float> g_avg_visible_players{ 0.0f };

// --- Zone player tracking (4x4 = 16 zones) ---
std::atomic<int> g_zone_player_count[ZONE_COUNT]{};

// --- Global monster state tracking ---
static std::mutex                              g_monsterMtx;
static std::unordered_map<uint64_t, uint8_t>  g_monsterMap;  // databaseID → 0:alive 1:dead
std::atomic_int g_monster_known{ 0 };
std::atomic_int g_monster_dead { 0 };

// --- Player dead tracking (all players: bots + human) ---
static std::mutex                              g_playerMtx;
static std::unordered_map<uint64_t, uint8_t>  g_playerMap;   // databaseID → 0:alive 1:dead
std::atomic_int g_player_dead{ 0 };

// --- Inventory activity (exposed via header) ---
std::atomic_int   g_items_picked{ 0 };
std::atomic_int   g_potions_used{ 0 };
std::atomic<float> g_avg_visible_items{ 0.0f };

// --- Server rejections (exposed via header) ---
std::atomic_int   g_move_too_fast{ 0 };    // USER_MOVE_ACK result=TooFast(토큰 버킷 초과)
std::atomic_int   g_skill_rejected{ 0 };   // SKILL_RESULT_ACK result!=Ok

// 접속할 서버 포트. 기본은 Protocol.h의 PORT_NUM, 환경 변수 HARVEN_STRESS_PORT가 있으면 그 값
// (샌드박스에서 다른 포트로 띄운 서버를 칠 때 — 기본 동작은 그대로).
static unsigned short g_server_port = static_cast<unsigned short>(PORT_NUM);

// --- Point cloud buffers ---
float point_cloud[MAX_TEST * 2];
int   point_states[MAX_TEST];

// --- Internal-only ---
atomic_int client_to_close = 0;
vector<thread*> worker_threads;
thread test_thread;

static int GetZoneId(short x, short y) noexcept
{
    int col = static_cast<int>(x) / 500;
    int row = static_cast<int>(y) / 500;
    if (col < 0) col = 0; else if (col > 3) col = 3;
    if (row < 0) row = 0; else if (row > 3) row = 3;
    return row * 4 + col;
}

namespace
{
    constexpr int DELAY_LIMIT  = 150;
    constexpr int DELAY_LIMIT2 = 250;
    constexpr int ACCEPT_DELAY = 5;    // ms per client slot during ramp-up
    constexpr int MAX_BATCH    = 100;  // max clients to connect in one tick

    [[nodiscard]] uint64_t NowMilliseconds() noexcept
    {
        return static_cast<uint64_t>(
            duration_cast<milliseconds>(high_resolution_clock::now().time_since_epoch()).count());
    }

    [[nodiscard]] int RandomRange(int minValue, int maxValue)
    {
        thread_local std::mt19937 generator{
            static_cast<uint32_t>(steady_clock::now().time_since_epoch().count())
            ^ static_cast<uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()))
        };
        std::uniform_int_distribution<int> distribution(minValue, maxValue);
        return distribution(generator);
    }

    // 각 행동의 간격은 서로 다른 서버 경로에 고르게 부하를 주도록 잡았다.
    // 이동/공격(0.4~0.6초)이 가장 잦고, 스킬(5초 쿨타임)과 포션(5초 쿨타임)은
    // 서버가 강제하는 쿨타임보다 살짝 길게 잡아 대부분의 요청이 실제로 통과하게
    // 한다. 줍기는 몬스터를 잡아 아이템이 떨어진 뒤에야 의미가 있으므로 중간
    // 빈도로, 장착/버리기는 실제 플레이에서도 드문 행동이라 훨씬 길게 잡았다.
    void ScheduleBehavior(CLIENT& client)
    {
        const auto now = high_resolution_clock::now();
        client.next_move_time    = now + milliseconds(RandomRange(400, 600));
        client.next_attack_time  = now + milliseconds(RandomRange(400, 600));
        client.next_skill_time   = now + milliseconds(RandomRange(5200, 7000));
        client.next_chat_time    = now + milliseconds(RandomRange(10000, 20000));
        client.next_pickup_time  = now + milliseconds(RandomRange(800, 1400));
        client.next_potion_time  = now + milliseconds(RandomRange(5200, 7000));
        client.next_equip_time   = now + milliseconds(RandomRange(8000, 15000));
        client.next_discard_time = now + milliseconds(RandomRange(30000, 60000));
    }

    void ResetRuntimeState(CLIENT& client)
    {
        client.connected        = false;
        client.id               = ObjID{};
        client.x                = 0;
        client.y                = 0;
        client.maxHp            = PLAYER_MAX_HP;
        client.hp               = PLAYER_MAX_HP;
        client.mp               = 0;
        client.level            = 1;
        client.exp              = 0;
        client.visible_monsters = 0;
        client.visible_players  = 0;
        client.dead             = false;
        client.facing_left      = false;
        client.zone_id          = -1;
        client.move_seq         = 0;
        for (auto& pending : client.pending_moves) pending = PendingMove{};
        client.skill_seq         = 0;
        client.basic_interval_ms = 0;
        client.skill_interval_ms = 0;
        client.focus_monster_id  = ObjID{};
        client.focus_x           = -1;
        client.focus_y           = -1;
        client.has_focus         = false;
        client.buffered_bytes   = 0;
        client.potion_slot      = INVALID_SLOT;
        client.potion_count     = 0;
        client.equip_slot       = INVALID_SLOT;
        client.equipped         = false;
        client.visible_items    = 0;
        client.target_item_id   = ObjID{};
        client.target_item_x    = -1;
        client.target_item_y    = -1;
        client.has_target_item  = false;
        client.next_move_time    = {};
        client.next_attack_time  = {};
        client.next_skill_time   = {};
        client.next_chat_time    = {};
        client.next_pickup_time  = {};
        client.next_potion_time  = {};
        client.next_equip_time   = {};
        client.next_discard_time = {};
        std::memset(client.packet_buf, 0, sizeof(client.packet_buf));
    }

    void ResetForConnect(CLIENT& client)
    {
        ResetRuntimeState(client);
        std::memset(client.recv_buf, 0, sizeof(client.recv_buf));
    }

    // 보낸 이동의 시각을 적어 둔다. 0은 "빈 칸" 표시라 피한다(49일에 한 번 1ms 어긋날 뿐).
    void RememberMoveSent(CLIENT& client, uint16_t seq)
    {
        uint32_t nowMs = static_cast<uint32_t>(NowMilliseconds());
        if (nowMs == 0) nowMs = 1;
        client.pending_moves[seq & (MOVE_RING_SIZE - 1)] = PendingMove{ seq, nowMs };
    }

    // USER_MOVE_ACK의 seq로 그 이동을 보낸 시각을 찾아 왕복 시간(ms)을 돌려준다. 잴 수 없으면 -1.
    // 응답은 보낸 순서대로 오므로 칸에는 그 seq 자신이나 그 뒤(+8, +16 …)에 보낸 이동만 있을 수 있다.
    // 칸이 이미 뒤의 이동으로 덮였다면 왕복이 8걸음(3초 이상)보다 길었다는 뜻이다 — 덮은 이동의 보낸
    // 시각으로 잰 값은 실제보다 작은 하한이지만 DELAY_LIMIT2보다 훨씬 커서, 지연 추정이 옛 값에 멈추지
    // 않고 증가 규칙이 계속 줄이는 쪽으로 움직이게 한다.
    [[nodiscard]] int TakeMoveRoundTrip(CLIENT& client, uint16_t seq)
    {
        PendingMove& pending = client.pending_moves[seq & (MOVE_RING_SIZE - 1)];
        if (pending.sentMs == 0) return -1;
        const int roundTripMs = static_cast<int>(static_cast<uint32_t>(NowMilliseconds()) - pending.sentMs);
        if (pending.seq == seq) pending = PendingMove{};   // 덮인 칸은 그 이동의 응답이 올 때 다시 잰다
        return roundTripMs;
    }

    void UpdateDelayEstimate(int delayMs)
    {
        if (delayMs < 0) return;

        // EMA for global_delay (fast, weight 7:1)
        int expected = global_delay.load(memory_order_relaxed);
        int desired;
        do {
            desired = (expected == 0) ? delayMs : ((expected * 7) + delayMs) / 8;
        } while (!global_delay.compare_exchange_weak(expected, desired, memory_order_relaxed));

        // Track min
        int curMin = delay_min_val.load(memory_order_relaxed);
        while (delayMs < curMin &&
               !delay_min_val.compare_exchange_weak(curMin, delayMs, memory_order_relaxed));

        // Track max
        int curMax = delay_max_val.load(memory_order_relaxed);
        while (delayMs > curMax &&
               !delay_max_val.compare_exchange_weak(curMax, delayMs, memory_order_relaxed));

        // Slower EMA for avg (weight 15:1)
        int expAvg = delay_avg_val.load(memory_order_relaxed);
        int desAvg;
        do {
            desAvg = (expAvg == 0) ? delayMs : ((expAvg * 15) + delayMs) / 16;
        } while (!delay_avg_val.compare_exchange_weak(expAvg, desAvg, memory_order_relaxed));
    }

    // 서버가 보내준 슬롯 하나의 최신 상태를 봇의 인벤토리 추적에 반영한다.
    // ITEM_LIST_ACK/ITEM_SLOT_INF(ITEM_SLOT_STATE_DATA)와 각종 ACK(ITEM_SLOT_DATA)가
    // 같은 이름의 필드를 주므로 처리도 한 군데로 모은다. itemId == 0이면 "그 슬롯이 비었다"는 뜻이다.
    template <typename SlotData>
    void ApplyInventorySlot(CLIENT& client, const SlotData& slot)
    {
        if (slot.itemId == HEALTH_POTION_ID)
        {
            client.potion_slot  = slot.slotIndex;
            client.potion_count = slot.count;
        }
        else if (client.potion_slot == slot.slotIndex)
        {
            // 추적하던 포션 슬롯이 비었거나 다른 아이템으로 바뀌었다.
            client.potion_slot  = INVALID_SLOT;
            client.potion_count = 0;
        }

        if (IsEquipmentItemId(slot.itemId))
        {
            client.equip_slot = slot.slotIndex;
            client.equipped   = (slot.equipped != 0);
        }
        else if (client.equip_slot == slot.slotIndex)
        {
            client.equip_slot = INVALID_SLOT;
            client.equipped   = false;
        }
    }

    [[nodiscard]] int ChebyshevDistance(int ax, int ay, int bx, int by) noexcept
    {
        const int dx = abs(ax - bx);
        const int dy = abs(ay - by);
        return dx > dy ? dx : dy;
    }

    // 내 위치가 바뀌었다(이동 응답 · 존 이동 · 강제 이동). 지역별 인원 표도 함께 옮긴다.
    // 죽어 있는 동안은 지역 표에서 빠져 있으므로(zone_id = -1 — 부활 때 다시 넣는다) 좌표만 바꾼다.
    void MoveClientTo(CLIENT& client, short x, short y)
    {
        client.x = x;
        client.y = y;
        if (client.dead) return;

        const int newZone = GetZoneId(x, y);
        if (newZone == client.zone_id) return;
        if (client.zone_id >= 0 && client.zone_id < ZONE_COUNT)
            g_zone_player_count[client.zone_id].fetch_sub(1, std::memory_order_relaxed);
        client.zone_id = newZone;
        g_zone_player_count[newZone].fetch_add(1, std::memory_order_relaxed);
    }

    // 몬스터 하나의 위치를 봤다(시야 진입 · 이동 · 밀려남 · 부활). 조준 중인 몬스터면 위치만 고치고,
    // 아니면 지금 조준 중인 것보다 가까울 때만 바꾼다.
    void ObserveMonster(CLIENT& client, ObjID id, short x, short y)
    {
        if (client.has_focus && client.focus_monster_id == id)
        {
            client.focus_x = x;
            client.focus_y = y;
            return;
        }
        if (client.has_focus &&
            ChebyshevDistance(client.x, client.y, x, y) >=
            ChebyshevDistance(client.x, client.y, client.focus_x, client.focus_y))
            return;

        client.focus_monster_id = id;
        client.focus_x          = x;
        client.focus_y          = y;
        client.has_focus        = true;
    }

    // 몬스터가 시야에서 사라졌거나 죽었다. 조준 중이던 것이면 놓는다(다음에 보이는 몬스터를 잡는다).
    void ForgetMonster(CLIENT& client, ObjID id)
    {
        if (client.has_focus && client.focus_monster_id == id)
            client.has_focus = false;
    }

    [[nodiscard]] bool IsFocusAdjacent(const CLIENT& client)
    {
        return client.has_focus &&
               ChebyshevDistance(client.x, client.y, client.focus_x, client.focus_y) <= 1;
    }

    // 주우러 갈 필드 아이템이 있으면 그쪽으로 한 칸 다가가는 방향을 고른다.
    // 서버의 줍기 판정은 "플레이어가 서 있는 바로 그 칸"만 보므로, 이렇게
    // 유도해주지 않으면 봇이 아이템 위에 올라설 일이 거의 없어 줍기 경로가
    // 사실상 테스트되지 않는다.
    [[nodiscard]] bool ChooseDirectionTowardItem(const CLIENT& client, char& outDirection)
    {
        if (!client.has_target_item)
            return false;

        const int dx = client.target_item_x - client.x;
        const int dy = client.target_item_y - client.y;
        if (dx == 0 && dy == 0)
            return false;  // 이미 그 칸 위 — 이동 대신 줍기가 나간다

        // 방향 인코딩: 0:UP(y-) 1:DOWN(y+) 2:LEFT 3:RIGHT 4:UP-LEFT 5:UP-RIGHT
        //              6:DOWN-LEFT 7:DOWN-RIGHT
        if      (dx > 0 && dy > 0) outDirection = 7;
        else if (dx > 0 && dy < 0) outDirection = 5;
        else if (dx < 0 && dy > 0) outDirection = 6;
        else if (dx < 0 && dy < 0) outDirection = 4;
        else if (dx > 0)           outDirection = 3;
        else if (dx < 0)           outDirection = 2;
        else if (dy > 0)           outDirection = 1;
        else                       outDirection = 0;
        return true;
    }

    [[nodiscard]] char ChooseMoveDirection(const CLIENT& client)
    {
        if (char towardItem = 0; ChooseDirectionTowardItem(client, towardItem))
            return towardItem;

        array<char, 8> candidates{};
        int count = 0;

        const bool canUp    = client.y > 0;
        const bool canDown  = client.y < W_HEIGHT - 1;
        const bool canLeft  = client.x > 0;
        const bool canRight = client.x < W_WIDTH  - 1;

        if (canUp)              candidates[count++] = 0;
        if (canDown)            candidates[count++] = 1;
        if (canLeft)            candidates[count++] = 2;
        if (canRight)           candidates[count++] = 3;
        if (canUp   && canLeft) candidates[count++] = 4;
        if (canUp   && canRight)candidates[count++] = 5;
        if (canDown && canLeft) candidates[count++] = 6;
        if (canDown && canRight)candidates[count++] = 7;

        if (count == 0) return 0;
        return candidates[RandomRange(0, count - 1)];
    }
}

void DisconnectClient(int clientIndex)
{
    auto& client = g_clients[clientIndex];
    const bool wasConnected = client.connected.exchange(false);

    if (client.client_socket >= 0)
    {
        // close가 epoll 등록도 지운다. 워커가 이미 꺼낸 이벤트는 connected · 소켓 번호로 걸러진다(ArmRecv · Worker_Thread)
        ::close(client.client_socket);
        client.client_socket = -1;
    }

    if (wasConnected) {
        --active_clients;
        if (client.zone_id >= 0 && client.zone_id < ZONE_COUNT)
            g_zone_player_count[client.zone_id].fetch_sub(1, std::memory_order_relaxed);
    }

    ResetRuntimeState(client);
}

// 이 클라이언트 소켓을 다음 받기에 다시 건다(EPOLLONESHOT — 한 번 깨면 다시 걸 때까지 다른 스레드가 읽지 않는다)
bool ArmRecv(int clientIndex, bool add)
{
    auto& client = g_clients[clientIndex];
    if (client.client_socket < 0) return false;
    epoll_event ev{};
    ev.events   = EPOLLIN | EPOLLRDHUP | EPOLLONESHOT;
    ev.data.u64 = (static_cast<uint64_t>(static_cast<uint32_t>(client.client_socket)) << 32) | static_cast<uint32_t>(clientIndex);
    return ::epoll_ctl(g_epoll, add ? EPOLL_CTL_ADD : EPOLL_CTL_MOD, client.client_socket, &ev) == 0;
}

bool SendPacket(int clientIndex, const void* packet)
{
    auto& client = g_clients[clientIndex];
    if (client.client_socket < 0 || packet == nullptr) return false;

    const auto* rawPacket   = reinterpret_cast<const unsigned char*>(packet);
    const uint16_t packetSize = *reinterpret_cast<const uint16_t*>(rawPacket);
    if (packetSize == 0 || packetSize > MAX_PACKET_SIZE) return false;

    bool failed = false;
    {
        std::lock_guard<std::mutex> guard(client.send_lock);
        const int fd = client.client_socket;
        if (fd < 0) return false;
        size_t sent = 0;
        while (sent < packetSize)
        {
            const ssize_t n = ::send(fd, rawPacket + sent, packetSize - sent, MSG_NOSIGNAL | MSG_DONTWAIT);
            if (n > 0) { sent += static_cast<size_t>(n); continue; }
            if (n < 0 && errno == EINTR) continue;
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            {
                pollfd p{ fd, POLLOUT, 0 };
                ::poll(&p, 1, 100);
                continue;
            }
            failed = true;
            break;
        }
    }
    if (failed)
    {
        DisconnectClient(clientIndex);
        return false;
    }
    return true;
}

bool ConnectClient(int clientIndex)
{
    auto& client = g_clients[clientIndex];
    ResetForConnect(client);

    client.client_socket = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP);
    if (client.client_socket < 0) { client.client_socket = -1; return false; }

    int optVal = 1;
    setsockopt(client.client_socket, SOL_SOCKET, SO_REUSEADDR, &optVal, sizeof(optVal));
    setsockopt(client.client_socket, IPPROTO_TCP, TCP_NODELAY, &optVal, sizeof(optVal));

    sockaddr_in serverAddr{};
    serverAddr.sin_family      = AF_INET;
    serverAddr.sin_port        = htons(g_server_port);
    serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (::connect(client.client_socket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) != 0)
    {
        DisconnectClient(clientIndex);
        return false;
    }

    if (!ArmRecv(clientIndex, true))
    {
        DisconnectClient(clientIndex);
        return false;
    }

    // 서버는 요청 크기가 그 타입 구조체 크기와 정확히 같지 않으면 버린다 — 항상 구조체 통째로 보낸다.
    USER_LOGIN_REQ_PACKET loginPacket{};
    loginPacket.size    = sizeof(loginPacket);
    loginPacket.type    = static_cast<char>(PacketType::USER_LOGIN_REQ);
    std::snprintf(loginPacket.name,     sizeof(loginPacket.name),     "bot%06d", clientIndex);
    std::snprintf(loginPacket.password, sizeof(loginPacket.password), "pw%06d",  clientIndex);
    loginPacket.version = PROTOCOL_VERSION;
    loginPacket.classId = BOT_CLASS_ID;   // 새 캐릭터일 때만 쓰인다

    if (!SendPacket(clientIndex, &loginPacket)) return false;
    return true;
}

void ProcessPacket(int clientIndex, unsigned char packet[])
{
    auto& client = g_clients[clientIndex];

    // 타입은 1바이트(0~255) — char로 비교하면 128 이상이 음수가 되므로 부호 없이 비교한다.
    switch (packet[2]) {
    case static_cast<uint8_t>(PacketType::USER_LOGIN_ACK):
    {
        const auto* p = reinterpret_cast<const USER_LOGIN_ACK_PACKET*>(packet);
        if (!client.connected.exchange(true))
            ++active_clients;
        client.id     = p->id;
        client.x      = p->x;
        client.y      = p->y;
        client.maxHp  = p->maxhp;
        client.hp     = p->hp;
        client.level  = p->level;
        client.exp    = p->exp;
        client.dead             = false;
        client.visible_monsters = 0;
        client.visible_players  = 0;
        client.has_focus        = false;
        {
            const int newZone = GetZoneId(p->x, p->y);
            if (client.zone_id >= 0 && client.zone_id < ZONE_COUNT)
                g_zone_player_count[client.zone_id].fetch_sub(1, std::memory_order_relaxed);
            client.zone_id = newZone;
            g_zone_player_count[newZone].fetch_add(1, std::memory_order_relaxed);
        }
        {
            const uint64_t pid = p->id.GetDatabaseID();
            std::lock_guard<std::mutex> lk(g_playerMtx);
            g_playerMap.emplace(pid, uint8_t(0));
        }
        ScheduleBehavior(client);
        break;
    }
    case static_cast<uint8_t>(PacketType::USER_LOGIN_FAIL_ACK):
        DisconnectClient(clientIndex);
        break;

    case static_cast<uint8_t>(PacketType::USER_MOVE_ACK):
    {
        // 내 이동의 결과(거절이어도 온다). 지연은 여기서 잰다 — 예전처럼 내 SUBJECT_MOVE_NFY의
        // move_time 메아리를 보지 않는다(순번 이동이면 서버가 본인에게 SUBJECT_MOVE_NFY를 보내지 않는다).
        const auto* p = reinterpret_cast<const USER_MOVE_ACK_PACKET*>(packet);
        UpdateDelayEstimate(TakeMoveRoundTrip(client, p->seq));
        if (static_cast<MoveResult>(p->result) == MoveResult::TooFast)
            g_move_too_fast.fetch_add(1, std::memory_order_relaxed);
        MoveClientTo(client, p->x, p->y);   // 거절이면 서버의 현재 좌표 — 그대로 따른다
        break;
    }
    case static_cast<uint8_t>(PacketType::SUBJECT_MOVE_NFY):
    {
        const auto* p = reinterpret_cast<const SUBJECT_MOVE_NFY_PACKET*>(packet);
        if (p->id == client.id)
            MoveClientTo(client, p->x, p->y);   // 존을 넘는 걸음 · 순간이동이면 본인에게도 온다
        else if (p->id.GetCategory<EnumCategory>() == EnumCategory::eMonster)
            ObserveMonster(client, p->id, p->x, p->y);
        break;
    }
    case static_cast<uint8_t>(PacketType::SUBJECT_FORCED_MOVE_NFY):
    {
        // 돌진 · 넉백 · 끌어당김 등 서버가 옮긴 이동 — 도착 칸만 쓴다.
        const auto* p = reinterpret_cast<const SUBJECT_FORCED_MOVE_NFY_PACKET*>(packet);
        if (p->id == client.id)
            MoveClientTo(client, p->toX, p->toY);
        else if (p->id.GetCategory<EnumCategory>() == EnumCategory::eMonster)
            ObserveMonster(client, p->id, p->toX, p->toY);
        break;
    }
    case static_cast<uint8_t>(PacketType::SUBJECT_ADD_NFY):
    {
        const auto* p = reinterpret_cast<const SUBJECT_ADD_NFY_PACKET*>(packet);
        switch (p->id.GetCategory<EnumCategory>())
        {
        case EnumCategory::eMonster:
        {
            ++client.visible_monsters;
            ObserveMonster(client, p->id, p->x, p->y);
            const uint64_t mid = p->id.GetDatabaseID();
            std::lock_guard<std::mutex> lk(g_monsterMtx);
            if (g_monsterMap.emplace(mid, uint8_t(0)).second)
                g_monster_known.fetch_add(1, std::memory_order_relaxed);
            break;
        }
        case EnumCategory::eUser:
        {
            ++client.visible_players;
            const uint64_t pid = p->id.GetDatabaseID();
            std::lock_guard<std::mutex> lk(g_playerMtx);
            auto res = g_playerMap.emplace(pid, uint8_t(0));
            if (!res.second && res.first->second == 1) {
                res.first->second = 0;
                g_player_dead.fetch_sub(1, std::memory_order_relaxed);
            }
            break;
        }
        case EnumCategory::eItem:
        {
            // 바닥에 떨어진 아이템이 시야에 들어왔다. 아직 목표가 없으면
            // 이걸 주우러 간다(이미 목표가 있으면 그쪽을 계속 쫓는다).
            ++client.visible_items;
            if (!client.has_target_item)
            {
                client.target_item_id  = p->id;
                client.target_item_x   = p->x;
                client.target_item_y   = p->y;
                client.has_target_item = true;
            }
            break;
        }
        default: break;
        }
        break;
    }
    case static_cast<uint8_t>(PacketType::SUBJECT_REMOVE_NFY):
    {
        const auto* p = reinterpret_cast<const SUBJECT_REMOVE_NFY_PACKET*>(packet);
        switch (p->id.GetCategory<EnumCategory>())
        {
        case EnumCategory::eMonster:
            client.visible_monsters = max(0, client.visible_monsters - 1);
            ForgetMonster(client, p->id);
            break;
        case EnumCategory::eUser:
            client.visible_players  = max(0, client.visible_players  - 1); break;
        case EnumCategory::eItem:
            // 누가 먼저 주웠거나 30초가 지나 소멸했다. 쫓던 대상이면 목표를 놓는다.
            client.visible_items = max(0, client.visible_items - 1);
            if (client.has_target_item && p->id == client.target_item_id)
                client.has_target_item = false;
            break;
        default: break;
        }
        break;
    }
    case static_cast<uint8_t>(PacketType::SUBJECT_DIE_NFY):
    {
        const auto* p = reinterpret_cast<const SUBJECT_DIE_NFY_PACKET*>(packet);
        switch (p->id.GetCategory<EnumCategory>())
        {
        case EnumCategory::eMonster:
        {
            client.visible_monsters = max(0, client.visible_monsters - 1);
            ForgetMonster(client, p->id);
            const uint64_t mid = p->id.GetDatabaseID();
            std::lock_guard<std::mutex> lk(g_monsterMtx);
            auto it = g_monsterMap.find(mid);
            if (it != g_monsterMap.end() && it->second == 0) {
                it->second = 1;
                g_monster_dead.fetch_add(1, std::memory_order_relaxed);
            }
            break;
        }
        case EnumCategory::eUser:
        {
            const uint64_t pid = p->id.GetDatabaseID();
            {
                std::lock_guard<std::mutex> lk(g_playerMtx);
                auto it = g_playerMap.find(pid);
                if (it != g_playerMap.end() && it->second == 0) {
                    it->second = 1;
                    g_player_dead.fetch_add(1, std::memory_order_relaxed);
                }
            }
            if (p->id == client.id)
            {
                client.hp               = p->hp;
                client.dead             = true;
                client.visible_monsters = 0;
                client.visible_players  = 0;
                client.has_focus        = false;
                if (client.zone_id >= 0 && client.zone_id < ZONE_COUNT)
                    g_zone_player_count[client.zone_id].fetch_sub(1, std::memory_order_relaxed);
                client.zone_id = -1;
            }
            else { client.visible_players = max(0, client.visible_players - 1); }
            break;
        }
        default: break;
        }
        break;
    }
    case static_cast<uint8_t>(PacketType::SUBJECT_RESPAWN_NFY):
    {
        const auto* p = reinterpret_cast<const SUBJECT_RESPAWN_NFY_PACKET*>(packet);
        switch (p->id.GetCategory<EnumCategory>())
        {
        case EnumCategory::eMonster:
        {
            ++client.visible_monsters;
            ObserveMonster(client, p->id, p->x, p->y);
            const uint64_t mid = p->id.GetDatabaseID();
            std::lock_guard<std::mutex> lk(g_monsterMtx);
            auto res = g_monsterMap.emplace(mid, uint8_t(0));
            if (res.second) {
                g_monster_known.fetch_add(1, std::memory_order_relaxed);
            } else if (res.first->second == 1) {
                res.first->second = 0;
                g_monster_dead.fetch_sub(1, std::memory_order_relaxed);
            }
            break;
        }
        case EnumCategory::eUser:
        {
            const uint64_t pid = p->id.GetDatabaseID();
            {
                std::lock_guard<std::mutex> lk(g_playerMtx);
                auto it = g_playerMap.find(pid);
                if (it != g_playerMap.end() && it->second == 1) {
                    it->second = 0;
                    g_player_dead.fetch_sub(1, std::memory_order_relaxed);
                }
            }
            if (p->id == client.id)
            {
                client.x    = p->x;
                client.y    = p->y;
                client.hp   = p->hp;
                client.dead = false;
                client.visible_monsters = 0;
                client.visible_players  = 0;
                client.has_focus        = false;
                {
                    const int newZone = GetZoneId(p->x, p->y);
                    client.zone_id = newZone;
                    g_zone_player_count[newZone].fetch_add(1, std::memory_order_relaxed);
                }
                ScheduleBehavior(client);
            }
            else { ++client.visible_players; }
            break;
        }
        default: break;
        }
        break;
    }
    case static_cast<uint8_t>(PacketType::SUBJECT_DAMAGE_NFY):
    {
        // 주변 모두에게 "누가 누구에게 얼마를" — 내가 맞은 것만 본다.
        const auto* p = reinterpret_cast<const SUBJECT_DAMAGE_NFY_PACKET*>(packet);
        if (p->victimId == client.id)
        {
            const int32_t hp    = p->hp;
            const int32_t maxHp = p->maxhp;
            client.hp    = static_cast<uint16_t>(hp    > 0 ? hp    : 0);
            client.maxHp = static_cast<uint16_t>(maxHp > 0 ? maxHp : 0);
        }
        break;
    }
    case static_cast<uint8_t>(PacketType::USER_RESOURCE_INF):
    {
        // 내 HP · MP(로그인 · 자연 회복 · 레벨 업 · 기술 MP 소모)
        const auto* p = reinterpret_cast<const USER_RESOURCE_INF_PACKET*>(packet);
        client.hp    = p->hp;
        client.maxHp = p->maxhp;
        client.mp    = p->mp;
        break;
    }
    case static_cast<uint8_t>(PacketType::USER_HEAL_INF):
    {
        const auto* p = reinterpret_cast<const USER_HEAL_INF_PACKET*>(packet);
        client.hp = static_cast<uint16_t>(max(0, p->hp));
        break;
    }
    case static_cast<uint8_t>(PacketType::USER_STAT_CHANGE_INF):
    {
        const auto* p = reinterpret_cast<const USER_STAT_CHANGE_INF_PACKET*>(packet);
        client.level = p->level;
        client.hp    = p->hp;
        client.maxHp = p->maxhp;
        client.exp   = p->exp;
        break;
    }
    case static_cast<uint8_t>(PacketType::SKILL_RESULT_ACK):
    {
        // 기술 누름의 결과. 성공이면 서버가 정한 재사용 대기를 다음 누름 간격의 바닥으로 쓰고,
        // 재사용 대기 중이었으면 남은 시간만큼 미룬다 — 일찍 누르면 거절 응답만 오가기 때문이다.
        const auto* p = reinterpret_cast<const SKILL_RESULT_ACK_PACKET*>(packet);
        client.mp = p->mp;
        const bool basic = (p->skillId == SKILL_BASIC_ATTACK);
        const auto result = static_cast<SkillResult>(p->result);
        if (result == SkillResult::Ok)
        {
            if (basic) client.basic_interval_ms = p->cooldownMs;
            else       client.skill_interval_ms = p->cooldownMs;
            break;
        }

        g_skill_rejected.fetch_add(1, std::memory_order_relaxed);
        if (result == SkillResult::Cooldown)
        {
            const uint32_t remainMs = p->cooldownMs;
            const auto readyAt = high_resolution_clock::now() + milliseconds(remainMs);
            auto& nextTime = basic ? client.next_attack_time : client.next_skill_time;
            if (nextTime < readyAt) nextTime = readyAt;
        }
        break;
    }
    case static_cast<uint8_t>(PacketType::ITEM_LIST_ACK):
    {
        // 로그인 직후. 이전 세션에서 들고 있던 아이템을 여기서 파악한다
        // (새 캐릭터면 빈 칸뿐 — 초보자 지급품은 뒤이어 ITEM_SLOT_INF로 하나씩 온다).
        // 가방 50칸은 두 쪽(0~29 · 30~49)으로 온다 — 첫 쪽(칸 0부터)에서만 추적을 비우고, 쪽마다 항목이 그 칸을 덮어쓴다.
        const auto* p = reinterpret_cast<const ITEM_LIST_ACK_PACKET*>(packet);
        if (p->slotCount > 0 && p->items[0].slotIndex == 0)
        {
            client.potion_slot  = INVALID_SLOT;
            client.potion_count = 0;
            client.equip_slot   = INVALID_SLOT;
            client.equipped     = false;
        }
        for (int i = 0; i < (int)p->slotCount && i < kItemListPageSlots; ++i)
            ApplyInventorySlot(client, p->items[i]);
        break;
    }
    case static_cast<uint8_t>(PacketType::ITEM_SLOT_INF):
    {
        // 슬롯 내용이 바뀔 때마다(획득 · 사용 · 장착 · 버림 …) 그 슬롯의 최신 상태.
        const auto* p = reinterpret_cast<const ITEM_SLOT_INF_PACKET*>(packet);
        ApplyInventorySlot(client, p->slot);
        break;
    }
    case static_cast<uint8_t>(PacketType::ITEM_PICKUP_ACK):
    {
        // 실제 슬롯 내용은 ITEM_SLOT_INF로 온다 — 여기선 성공 여부만 센다.
        const auto* p = reinterpret_cast<const ITEM_PICKUP_ACK_PACKET*>(packet);
        if (p->success)
        {
            g_items_picked.fetch_add(1, std::memory_order_relaxed);
            client.has_target_item = false;  // 방금 주운 게 목표였을 것이다
        }
        break;
    }
    case static_cast<uint8_t>(PacketType::ITEM_USE_ACK):
    {
        const auto* p = reinterpret_cast<const ITEM_USE_ACK_PACKET*>(packet);
        if (p->success)
        {
            ApplyInventorySlot(client, p->slot);
            g_potions_used.fetch_add(1, std::memory_order_relaxed);
        }
        break;
    }
    case static_cast<uint8_t>(PacketType::ITEM_EQUIP_ACK):
    {
        const auto* p = reinterpret_cast<const ITEM_EQUIP_ACK_PACKET*>(packet);
        if (p->success) ApplyInventorySlot(client, p->slot);
        break;
    }
    case static_cast<uint8_t>(PacketType::ITEM_UNEQUIP_ACK):
    {
        const auto* p = reinterpret_cast<const ITEM_UNEQUIP_ACK_PACKET*>(packet);
        if (p->success) ApplyInventorySlot(client, p->slot);
        break;
    }
    case static_cast<uint8_t>(PacketType::ITEM_DISCARD_ACK):
    {
        const auto* p = reinterpret_cast<const ITEM_DISCARD_ACK_PACKET*>(packet);
        if (p->success) ApplyInventorySlot(client, p->slot);
        break;
    }
    case static_cast<uint8_t>(PacketType::SYSTEM_MESSAGE_INF):
    {
        // 봇이 신경 쓰는 건 인벤토리 가득 참뿐이다. 자리가 없으면 주우러 다녀도
        // 헛수고라 목표를 놓고, 다음 줍기 시도도 뒤로 미룬다.
        const auto* p = reinterpret_cast<const SYSTEM_MESSAGE_INF_PACKET*>(packet);
        if (static_cast<SystemMessageCode>(p->code) == SystemMessageCode::InventoryFull)
        {
            client.has_target_item = false;
            client.next_pickup_time = high_resolution_clock::now() + milliseconds(RandomRange(8000, 15000));
        }
        break;
    }

    case static_cast<uint8_t>(PacketType::SC_CHAT):
    case static_cast<uint8_t>(PacketType::SKILL_NFY):
    case static_cast<uint8_t>(PacketType::EFFECT_NFY):
    case static_cast<uint8_t>(PacketType::STATUS_NFY):
    case static_cast<uint8_t>(PacketType::SUBJECT_INFO_NFY):
    case static_cast<uint8_t>(PacketType::PLAYER_LOOK_NFY):
    case static_cast<uint8_t>(PacketType::ITEM_SWAP_ACK):
    case static_cast<uint8_t>(PacketType::SUBJECT_EQUIP_CHANGE_NFY):
        break;  // 스트레스 테스트에서 처리 불필요 — 무시(자주 오는 것들)
    default:
        break;  // 그 밖의 패킷(설정 · 퀘스트 · NPC · 강화 수치 등) — 무시하고 연결 유지
    }
}

bool ConsumeReceivedBytes(int clientIndex, size_t ioSize)
{
    auto& client            = g_clients[clientIndex];
    const unsigned char* cursor = client.recv_buf;
    int remainingBytes      = static_cast<int>(ioSize);

    while (remainingBytes > 0)
    {
        if (client.buffered_bytes < static_cast<int>(sizeof(uint16_t)))
        {
            const int headerBytesNeeded = static_cast<int>(sizeof(uint16_t)) - client.buffered_bytes;
            const int bytesToCopy       = min(headerBytesNeeded, remainingBytes);
            memcpy(client.packet_buf + client.buffered_bytes, cursor, bytesToCopy);
            client.buffered_bytes += bytesToCopy;
            cursor                += bytesToCopy;
            remainingBytes        -= bytesToCopy;
            if (client.buffered_bytes < static_cast<int>(sizeof(uint16_t))) continue;
        }

        // 머리(크기 2 + 타입 1)보다 짧은 크기는 깨진 스트림이다 — 아래 복사 길이가 음수가 되기 전에 끊는다.
        const uint16_t packetSize = *reinterpret_cast<const uint16_t*>(client.packet_buf);
        if (packetSize <= sizeof(uint16_t) || packetSize > MAX_PACKET_SIZE) return false;

        const int packetBytesNeeded = static_cast<int>(packetSize) - client.buffered_bytes;
        const int bytesToCopy       = min(packetBytesNeeded, remainingBytes);
        memcpy(client.packet_buf + client.buffered_bytes, cursor, bytesToCopy);
        client.buffered_bytes += bytesToCopy;
        cursor                += bytesToCopy;
        remainingBytes        -= bytesToCopy;

        if (client.buffered_bytes == packetSize)
        {
            unsigned char packet[MAX_PACKET_SIZE]{};
            memcpy(packet, client.packet_buf, packetSize);
            client.buffered_bytes = 0;
            ProcessPacket(clientIndex, packet);
        }
    }
    return true;
}

// 이동 간격 400~600ms는 서버의 이동 토큰 버킷(Data/MoveRule.h)을 넘지 않는다: 걸음 비용은 이동속도
// 100%에서 직선 280ms · 대각 395ms이고(서약기사의 이동속도 = 100 + DEX/8 ≥ 100%라 이보다 길어지지 않는다),
// 가장 짧은 간격 400ms가 가장 비싼 걸음 395ms보다 길어 서버의 이동 시계가 현재 시각을 앞지르지 않는다.
// TooFast가 나온다면 지터가 아니라 서버가 걸음 여러 개를 몰아서 처리한 것이다(650ms어치 이상 —
// kCatchupMs 500 + kSlackMs 150) — 곧 과부하 신호이므로 간격을 바꾸지 않고 세기만 한다(g_move_too_fast).
void SendMovePacket(int clientIndex)
{
    auto& client = g_clients[clientIndex];
    USER_MOVE_REQ_PACKET pkt{};
    pkt.size = sizeof(pkt);
    pkt.type = static_cast<char>(PacketType::USER_MOVE_REQ);
    pkt.dir  = static_cast<uint8_t>(ChooseMoveDirection(client));
    pkt.seq  = ++client.move_seq;

    switch (pkt.dir) {
    case 2: case 4: case 6: client.facing_left = true;  break;
    case 3: case 5: case 7: client.facing_left = false; break;
    default: break;
    }

    RememberMoveSent(client, pkt.seq);
    SendPacket(clientIndex, &pkt);
    client.next_move_time = high_resolution_clock::now() + milliseconds(RandomRange(400, 600));
}

// 기술 칸 하나를 "누른다"(SKILL_REQ Press). 평타도 기술 칸 하나다.
// 조준: 조준 중인 몬스터가 있으면 그 id와 칸(서버가 대상 칸으로 다시 맞춘다), 없으면 바라보는 쪽 옆 칸.
// dir(화면 각)은 표시용이라 서버가 aim에서 다시 계산한다 — 0으로 둔다.
void SendSkillPress(int clientIndex, uint16_t skillId)
{
    auto& client = g_clients[clientIndex];
    SKILL_REQ_PACKET pkt{};
    pkt.size    = sizeof(pkt);
    pkt.type    = static_cast<char>(PacketType::SKILL_REQ);
    pkt.skillId = skillId;
    pkt.phase   = static_cast<uint8_t>(SkillPhase::Press);
    pkt.flags   = 0;   // 강제 공격(SKILLREQ_FORCE) 없음 — 봇끼리 치지 않는다
    if (client.has_focus)
    {
        pkt.targetId = client.focus_monster_id;
        pkt.aimX     = client.focus_x;
        pkt.aimY     = client.focus_y;
    }
    else
    {
        pkt.aimX = static_cast<short>(client.x + (client.facing_left ? -1 : 1));
        pkt.aimY = client.y;
    }
    pkt.dir = 0;
    pkt.seq = ++client.skill_seq;
    SendPacket(clientIndex, &pkt);
}

// 평타(검격). 간격은 예전처럼 400~600ms로 뽑되, 서버가 알려준 평타 간격(서약기사 1레벨 ≈ 601ms =
// 620ms ÷ 공격속도 103%)보다 짧게는 누르지 않는다 — 더 일찍 누르면 SKILL_RESULT_ACK(Cooldown)만 돌아온다.
void SendAttackPacket(int clientIndex)
{
    auto& client = g_clients[clientIndex];
    SendSkillPress(clientIndex, SKILL_BASIC_ATTACK);
    const int randomMs   = RandomRange(400, 600);
    const int intervalMs = max(randomMs, static_cast<int>(client.basic_interval_ms));
    client.next_attack_time = high_resolution_clock::now() + milliseconds(intervalMs);
}

// 5초마다 누르는 기술(맨 위 SKILL_* 설명 참고). 예전 USER_SKILL_REQ처럼 몬스터가 보일 때만 불린다.
void SendSkillPacket(int clientIndex)
{
    auto& client = g_clients[clientIndex];
    const auto now = high_resolution_clock::now();

    if (client.level >= WHIRLWIND_UNLOCK_LEVEL)
    {
        if (client.mp >= WHIRLWIND_MP)
        {
            SendSkillPress(clientIndex, SKILL_WHIRLWIND);
            // 회전 베기는 누르고 있는 기술이다. 도는 동안 평타를 누르면 회전이 끊기므로(마무리 타격 없이)
            // 서버가 최대 시간에 스스로 끝내며 마무리 타격까지 할 때까지 평타를 쉰다. 뗌(Release)은 보내지 않는다.
            const auto holdEnd = now + milliseconds(WHIRLWIND_HOLD_MS + 100);
            if (client.next_attack_time < holdEnd) client.next_attack_time = holdEnd;
        }
    }
    else if (IsFocusAdjacent(client))
    {
        if (client.mp >= SHIELD_BASH_MP)
            SendSkillPress(clientIndex, SKILL_SHIELD_BASH);
    }
    else
    {
        // 1레벨인데 붙어 있는 몬스터가 없다 — 패킷 없이 조금 뒤에 다시 본다.
        client.next_skill_time = now + milliseconds(RandomRange(400, 600));
        return;
    }

    // MP가 모자랐어도 같은 간격으로 다시 본다(서버의 MP 회복은 5초마다).
    const int randomMs   = RandomRange(5200, 7000);
    const int intervalMs = max(randomMs, static_cast<int>(client.skill_interval_ms));
    client.next_skill_time = now + milliseconds(intervalMs);
}

// 서 있는 칸의 아이템을 줍는다(서버가 위치를 직접 보므로 좌표는 보내지 않는다).
// 목표 아이템 위에 올라섰을 때가 성공 확률이 가장 높지만, 놓친 아이템이 발밑에
// 남아있을 수도 있어서 주변에 아이템이 보이면 그냥도 한 번씩 시도한다.
void SendPickupPacket(int clientIndex)
{
    auto& client = g_clients[clientIndex];

    const bool standingOnTarget = client.has_target_item &&
                                  client.x == client.target_item_x &&
                                  client.y == client.target_item_y;
    if (!standingOnTarget && client.visible_items == 0)
    {
        // 주울 것이 안 보이면 패킷을 아끼고 조금 뒤에 다시 본다.
        client.next_pickup_time = high_resolution_clock::now() + milliseconds(RandomRange(1500, 2500));
        return;
    }

    ITEM_PICKUP_REQ_PACKET pkt{};
    pkt.size = sizeof(pkt);
    pkt.type = static_cast<char>(PacketType::ITEM_PICKUP_REQ);
    SendPacket(clientIndex, &pkt);

    // 목표 위에 서 있을 땐 짧게(실패해도 곧 재시도), 아니면 느긋하게.
    client.next_pickup_time = high_resolution_clock::now() +
        milliseconds(standingOnTarget ? RandomRange(400, 700) : RandomRange(1500, 2500));
}

// 체력이 충분히 닳았을 때만 포션을 쓴다 — 가득 찬 상태로 마셔봐야 회복량이
// 잘려나가기만 하고, 실제 플레이 양상과도 멀어진다.
void TryUsePotion(int clientIndex)
{
    auto& client = g_clients[clientIndex];

    const bool wounded = client.maxHp > 0 && client.hp * 100 < client.maxHp * 60;
    if (client.potion_slot != INVALID_SLOT && client.potion_count > 0 && wounded)
    {
        ITEM_USE_REQ_PACKET pkt{};
        pkt.size      = sizeof(pkt);
        pkt.type      = static_cast<char>(PacketType::ITEM_USE_REQ);
        pkt.slotIndex = client.potion_slot;
        SendPacket(clientIndex, &pkt);
    }

    // 서버 쿨타임(5초)보다 살짝 길게 잡아 대부분의 요청이 실제로 통과하게 한다.
    client.next_potion_time = high_resolution_clock::now() + milliseconds(RandomRange(5200, 7000));
}

// 주운 장비를 장착하고, 가끔은 벗기도 한다(탈착 경로와 장착 상태 브로드캐스트도
// 부하 대상이라 한쪽만 계속 돌리지 않는다).
void TryEquipItem(int clientIndex)
{
    auto& client = g_clients[clientIndex];

    if (client.equip_slot != INVALID_SLOT)
    {
        if (!client.equipped)
        {
            ITEM_EQUIP_REQ_PACKET pkt{};
            pkt.size      = sizeof(pkt);
            pkt.type      = static_cast<char>(PacketType::ITEM_EQUIP_REQ);
            pkt.slotIndex = client.equip_slot;
            SendPacket(clientIndex, &pkt);
        }
        else if (RandomRange(0, 3) == 0)
        {
            ITEM_UNEQUIP_REQ_PACKET pkt{};
            pkt.size      = sizeof(pkt);
            pkt.type      = static_cast<char>(PacketType::ITEM_UNEQUIP_REQ);
            pkt.slotIndex = client.equip_slot;
            SendPacket(clientIndex, &pkt);
        }
    }

    client.next_equip_time = high_resolution_clock::now() + milliseconds(RandomRange(8000, 15000));
}

// 아주 가끔 포션 하나를 바닥에 버린다. 버리기 경로 자체를 태우는 목적도 있지만,
// 버려진 아이템이 다시 다른 봇의 줍기 대상이 되어 필드 아이템이 계속 돌게 하는
// 효과도 있다. 봇이 쓸 포션까지 말려버리지 않도록 여유가 있을 때만 버린다.
void TryDiscardItem(int clientIndex)
{
    auto& client = g_clients[clientIndex];

    if (client.potion_slot != INVALID_SLOT && client.potion_count >= 3)
    {
        ITEM_DISCARD_REQ_PACKET pkt{};
        pkt.size      = sizeof(pkt);
        pkt.type      = static_cast<char>(PacketType::ITEM_DISCARD_REQ);
        pkt.slotIndex = client.potion_slot;
        pkt.count     = 1;
        SendPacket(clientIndex, &pkt);
    }

    client.next_discard_time = high_resolution_clock::now() + milliseconds(RandomRange(30000, 60000));
}

void SendChatPacket(int clientIndex)
{
    static const char* const MESSAGES[] = {
        "hello",
        "what's your name",
        "today weather's good",
        "anyone here?",
        "nice to meet you",
        "let's fight together",
        "watch out for monsters",
    };
    constexpr int MSG_COUNT = static_cast<int>(sizeof(MESSAGES) / sizeof(MESSAGES[0]));

    auto& client = g_clients[clientIndex];
    // 일반 채널. 받는 사람(targetName)은 귓속말에만 쓰므로 비워 둔다.
    CS_CHAT_PACKET pkt{};
    pkt.size    = sizeof(pkt);
    pkt.type    = static_cast<char>(PacketType::CS_CHAT);
    pkt.channel = static_cast<uint8_t>(ChatChannel::Normal);
    std::snprintf(pkt.mess, sizeof(pkt.mess), "%s", MESSAGES[RandomRange(0, MSG_COUNT - 1)]);
    SendPacket(clientIndex, &pkt);
    client.next_chat_time = high_resolution_clock::now() + milliseconds(RandomRange(10000, 20000));
}

void Worker_Thread()
{
    epoll_event events[64];
    while (true)
    {
        const int n = ::epoll_wait(g_epoll, events, 64, -1);
        for (int i = 0; i < n; ++i)
        {
            const int clientIndex = static_cast<int>(events[i].data.u64 & 0xFFFFFFFFu);
            const int fd          = static_cast<int>(events[i].data.u64 >> 32);
            auto& client = g_clients[clientIndex];
            // 이미 끊긴(번호가 바뀐) 소켓의 늦은 이벤트
            if (client.client_socket != fd) continue;

            const ssize_t got = ::recv(fd, client.recv_buf, sizeof(client.recv_buf), MSG_DONTWAIT);
            if (got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
            {
                if (!ArmRecv(clientIndex, false)) DisconnectClient(clientIndex);
                continue;
            }
            if (got <= 0)
            {
                DisconnectClient(clientIndex);
                continue;
            }
            if (!ConsumeReceivedBytes(clientIndex, static_cast<size_t>(got)) || !ArmRecv(clientIndex, false))
                DisconnectClient(clientIndex);
        }
    }
}

void Adjust_Number_Of_Client()
{
    static int  delayMultiplier   = 1;
    static int  maxLimit          = (numeric_limits<int>::max)();
    static bool increasing        = true;
    static int  stableCount       = 0;
    constexpr int RECOVERY_STABLE_COUNT = 100;

    const int activeClientCount  = active_clients.load();
    const int currentConnections = num_connections.load();
    if (activeClientCount >= MAX_TEST || currentConnections >= MAX_CLIENTS) return;

    const auto now       = high_resolution_clock::now();
    const auto elapsedMs = duration_cast<milliseconds>(now - last_connect_time).count();
    if ((ACCEPT_DELAY * delayMultiplier) > elapsedMs) return;

    const int delaySnapshot = global_delay.load(memory_order_relaxed);

    // Phase: REDUCING — delay too high, start disconnecting
    if (delaySnapshot > DELAY_LIMIT2)
    {
        current_phase = TestPhase::REDUCING;
        if (increasing)
        {
            maxLimit   = activeClientCount;
            increasing = false;
        }
        stableCount = 0;

        if (activeClientCount < 100 || (ACCEPT_DELAY * 10) > elapsedMs) return;

        last_connect_time     = now;
        const int startIndex  = client_to_close.fetch_add(1);
        for (int attempt = 0; attempt < currentConnections; ++attempt)
        {
            const int victim = (startIndex + attempt) % currentConnections;
            if (g_clients[victim].connected.load())
            {
                DisconnectClient(victim);
                break;
            }
        }
        return;
    }

    // Phase: STABLE — delay elevated but below hard limit, slow down
    if (delaySnapshot > DELAY_LIMIT)
    {
        current_phase   = TestPhase::STABLE;
        delayMultiplier = 10;
        stableCount     = 0;
        return;
    }

    // Phase: RAMP_UP — delay is acceptable
    delayMultiplier = 1;

    if (!increasing)
    {
        ++stableCount;
        if (stableCount >= RECOVERY_STABLE_COUNT)
        {
            maxLimit    = (numeric_limits<int>::max)();
            stableCount = 0;
        }
    }

    if (maxLimit - (maxLimit / 20) < activeClientCount)
    {
        current_phase = TestPhase::STABLE;
        return;
    }

    current_phase = TestPhase::RAMP_UP;
    increasing    = true;
    last_connect_time = now;

    // batch connect: one slot per ACCEPT_DELAY ms elapsed, capped at MAX_BATCH
    const int batchSize = min(static_cast<int>(elapsedMs / ACCEPT_DELAY), MAX_BATCH);
    for (int b = 0; b < batchSize; ++b)
    {
        const int cur = num_connections.load();
        if (cur >= MAX_CLIENTS || active_clients.load() >= MAX_TEST) break;
        if (maxLimit - (maxLimit / 20) < active_clients.load()) break;
        num_connections.fetch_add(1);
        if (!ConnectClient(cur))
            DisconnectClient(cur);
    }
}

void Test_Thread()
{
    int historyCounter = 0;

    while (true)
    {
        Adjust_Number_Of_Client();

        const auto now               = high_resolution_clock::now();
        const int  currentConnections = num_connections.load();

        int  localDead    = 0;
        long long totalMonsters = 0;
        long long totalPlayers  = 0;
        long long totalItems    = 0;
        int  connectedCount = 0;

        for (int i = 0; i < currentConnections; ++i)
        {
            auto& client = g_clients[i];
            if (!client.connected.load()) continue;

            ++connectedCount;
            if (client.dead) { ++localDead; continue; }

            totalMonsters += client.visible_monsters;
            totalPlayers  += client.visible_players;
            totalItems    += client.visible_items;

            if (client.next_move_time <= now)
                SendMovePacket(i);
            if (client.visible_monsters > 0 && client.next_attack_time <= now)
                SendAttackPacket(i);
            if (client.visible_monsters > 0 && client.next_skill_time  <= now)
                SendSkillPacket(i);
            if (client.next_chat_time <= now)
                SendChatPacket(i);

            // 인벤토리 행동 — 몬스터를 잡아 아이템이 떨어져야 의미가 생기므로,
            // 위의 전투 행동들과 자연스럽게 맞물려 돌아간다.
            if (client.next_pickup_time <= now)
                SendPickupPacket(i);
            if (client.next_potion_time <= now)
                TryUsePotion(i);
            if (client.next_equip_time <= now)
                TryEquipItem(i);
            if (client.next_discard_time <= now)
                TryDiscardItem(i);
        }

        dead_clients_count.store(localDead, memory_order_relaxed);

        if (connectedCount > 0)
        {
            const float fConn = static_cast<float>(connectedCount);
            g_avg_visible_monsters.store(static_cast<float>(totalMonsters) / fConn, memory_order_relaxed);
            g_avg_visible_players.store (static_cast<float>(totalPlayers)  / fConn, memory_order_relaxed);
            g_avg_visible_items.store   (static_cast<float>(totalItems)    / fConn, memory_order_relaxed);
        }

        // Append history sample every ~500 ms (50 * 10ms)
        if (++historyCounter >= 50)
        {
            historyCounter  = 0;
            const int idx   = g_history_index;
            g_delay_history [idx] = static_cast<float>(global_delay.load(memory_order_relaxed));
            g_client_history[idx] = static_cast<float>(active_clients.load(memory_order_relaxed));
            g_history_index       = (idx + 1) % GRAPH_HISTORY_SIZE;
        }

        this_thread::sleep_for(10ms);
    }
}

void InitializeNetwork()
{
    for (auto& client : g_clients)
    {
        client.connected     = false;
        client.client_socket = -1;
        ResetRuntimeState(client);
    }

    num_connections    = 0;
    client_to_close    = 0;
    active_clients     = 0;
    global_delay       = 0;
    dead_clients_count = 0;
    delay_min_val      = INT_MAX;
    delay_max_val      = 0;
    delay_avg_val      = 0;
    current_phase      = TestPhase::RAMP_UP;
    g_history_index    = 0;
    g_avg_visible_monsters.store(0.0f);
    g_avg_visible_players.store(0.0f);
    g_avg_visible_items.store(0.0f);
    g_items_picked.store(0, std::memory_order_relaxed);
    g_potions_used.store(0, std::memory_order_relaxed);
    g_move_too_fast.store(0, std::memory_order_relaxed);
    g_skill_rejected.store(0, std::memory_order_relaxed);
    {
        const char* portText = std::getenv("HARVEN_STRESS_PORT");
        const int port = portText != nullptr ? atoi(portText) : 0;
        if (port > 0 && port < 65536) g_server_port = static_cast<unsigned short>(port);
    }
    memset(g_delay_history,  0, sizeof(g_delay_history));
    memset(g_client_history, 0, sizeof(g_client_history));
    for (auto& z : g_zone_player_count) z.store(0, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lk(g_monsterMtx);
        g_monsterMap.clear();
    }
    g_monster_known.store(0, std::memory_order_relaxed);
    g_monster_dead.store(0, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lk(g_playerMtx);
        g_playerMap.clear();
    }
    g_player_dead.store(0, std::memory_order_relaxed);
    last_connect_time  = high_resolution_clock::now();

    g_epoll = ::epoll_create1(EPOLL_CLOEXEC);

    const unsigned int workerCount = max(2u, thread::hardware_concurrency());
    for (unsigned int i = 0; i < workerCount; ++i)
        worker_threads.push_back(new thread{ Worker_Thread });

    test_thread = thread{ Test_Thread };
}

void GetPointCloud(int* size, float** positions, int** states)
{
    int index                    = 0;
    const int currentConnections = num_connections.load();

    for (int i = 0; i < currentConnections; ++i)
    {
        if (!g_clients[i].connected.load()) continue;
        point_cloud [index * 2]     = static_cast<float>(g_clients[i].x);
        point_cloud [index * 2 + 1] = static_cast<float>(g_clients[i].y);
        point_states[index]          = g_clients[i].dead ? 1 : 0;
        ++index;
    }

    *size      = index;
    *positions = point_cloud;
    *states    = point_states;
}
