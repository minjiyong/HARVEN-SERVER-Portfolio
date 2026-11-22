#pragma once

#include "EnumCategory.h"
#include "Base/ObjID/ContentID.h"
#include "Base/ObjID/ObjID.h"
#include "Database/GameData/CombatData.h"
#include <algorithm>

constexpr int PORT_NUM = 4000;

constexpr int NAME_SIZE     = 20;
constexpr int PASSWORD_SIZE = 20;
constexpr int CHAT_SIZE     = 128;
// 플레이어/몬스터 이름(NAME_SIZE)과 우연히 값이 같아도 별개 상수로 둔다 —
// 훗날 둘 중 하나만 길이를 바꿔야 할 때 서로 영향을 주지 않기 위함.
constexpr int ITEM_NAME_SIZE = 20;

constexpr int MAX_USER    = 40000;
constexpr int MAX_MONSTER = 200000;

// 유저 한 명이 가질 수 있는 인벤토리 슬롯 수(경제 1차: 30 → 50 — 재료 탭 없이 한 가방, 클라는 보기 필터 탭).
// 소지품 목록(ITEM_LIST_ACK)은 한 패킷(255바이트)에 다 들어가지 않으므로 kItemListPageSlots칸씩 나눠 보낸다.
constexpr int MAX_INVENTORY_SLOTS = 50;
// ITEM_LIST_ACK 한 쪽의 칸 수(바이트 모양은 30칸 시절 그대로 — 0~29 · 30~49 두 쪽)
inline constexpr int kItemListPageSlots = 30;
// 칸 번호 「없음」(ITEM_ENCHANT_REQ.protectionSlot 등). 서버는 MAX_INVENTORY_SLOTS 이상을 모두 없음으로 본다.
inline constexpr uint16_t kNoInventorySlot = 0xFFFF;
static_assert(MAX_INVENTORY_SLOTS <= 2 * kItemListPageSlots, "소지품 목록은 두 쪽 안에 들어가야 한다(SendITEM_LIST_ACK)");

constexpr uint32_t PLAYER_ID_START   = 1;
constexpr uint32_t MONSTER_ID_START  = 1'000'000'000;
constexpr uint32_t AGGRO_MONSTER_BOUNDARY = MONSTER_ID_START + static_cast<uint32_t>(MAX_MONSTER / 4) - 1;  // First 25% of monsters are AGGRO

inline constexpr bool IsPlayerObjectId(uint32_t id) noexcept
{
    return id >= PLAYER_ID_START && id < MONSTER_ID_START;
}

inline constexpr bool IsMonsterObjectId(uint32_t id) noexcept
{
    return id >= MONSTER_ID_START;
}

constexpr int W_WIDTH  = 2000;
constexpr int W_HEIGHT = 2000;

constexpr int SECTOR_RANGE = 10;

constexpr int WAKE_RANGE            = 3;   // aggro monster wakes up when player is within this many tiles
constexpr int ZONE_BOUNDARY_MARGIN  = 3;   // aggro monster turns back when within this many tiles of zone boundary

constexpr int PLAYER_MAX_HP  = 100;
constexpr int MONSTER_MAX_HP = 60;

constexpr int PLAYER_OFFENSIVE  = 10;
constexpr int MONSTER_OFFENSIVE = 3;

constexpr int BUF_SIZE = 1024;

enum MONSTER_TYPE
{
    AGGRO,
    PASSIVE
};

// 상황별로 새 패킷을 계속 늘리는 대신, 서버가 클라이언트에 보내는 상황 알림을
// 코드값 하나로 표현하는 공용 포맷(SYSTEM_MESSAGE_INF_PACKET)에서 쓰는 코드다.
// 실제 문구는 클라이언트가 code 기준으로 자체 보유한 문자열 테이블에서 찾아
// 렌더링한다(서버가 언어별 문자열을 들고 있을 필요가 없다). 새 상황이 생기면
// 이 자리에 값만 추가하면 되고, 기존 값은 절대 재사용/변경하지 않는다.
enum class SystemMessageCode : uint16_t
{
    // 인벤토리가 가득 차 아이템을 얻지 못함. param1=놓친 아이템의 ItemTableId,
    // param2=놓친 수량.
    InventoryFull,

    // ── 리니지풍 컨텐츠 ──────────────────────────────────────────────────────────
    ExpLost,             // 사망으로 경험치 감소. param1=잃은 양
    NotEnoughAdena,      // param1=필요 그로시, param2=보유 그로시
    SafeZoneNoCombat,    // 안전지대에서는 공격할 수 없음
    ItemDroppedOnDeath,  // 악명 사망으로 아이템을 떨어뜨림. param1=ItemTableId, param2=강화 수치
    BecamePurple,        // 먼저 공격해 보라색 이름이 됨(정당방위 대상). param1=지속 ms
    BecameChaotic,       // PK로 성향치가 음수가 됨. param1=현재 성향치
    HasteStart,          // param1=지속 초
    HasteEnd,
    ReturnedToTown,
    Teleported,          // param1=도착 지역(zoneId)
    LevelUp,             // param1=새 레벨
    TargetInvalid,       // 공격/강화 대상이 없거나 잘못됨
    TargetTooFar,        // 대상이 너무 멀리 있음
    ItemCooldown,        // param1=남은 ms
    CannotUseHere,       // 여기서는 사용할 수 없음(예: 마을에서 옛길 두루마리)
    AlignmentRecovered,  // 성향치가 다시 0 이상이 됨. param1=현재 성향치
    // ── Phase 3-B 채팅(CS_CHAT) ──
    ChatTooFast,         // 너무 빨리 말함. param1=채널(ChatChannel), param2=기다릴 ms
    ChatRepeated,        // 같은 말을 되풀이함
    ChatBadText,         // 비었거나 깨진 글자(UTF-8) · 보낼 수 없는 채널
    ChatNoTarget,        // 귓속말 상대가 접속해 있지 않음(또는 자기 자신)
    ChatNoGuild,         // 결사에 들어 있지 않음(Phase 5 전에는 늘)
    LoggedInElsewhere,   // 같은 계정이 다른 곳에서 접속해 이 접속을 끊는다
    // ── 파티(PARTY_REQ) ──
    ChatNoParty,         // 파티에 들어 있지 않음(파티 채팅)
    // ── 처치 보상(자동 획득) ──
    LootAcquired,        // 몬스터 전리품이 가방에 바로 들어옴(막타). param1=ItemTableId, param2=수량 — 슬롯은 ITEM_SLOT_INF로 따로 온다
    LootOverflow,        // 가방이 가득 차 전리품을 발밑(죽은 자리 주변)에 떨어뜨림 — 줍기 우선권은 나 > 내 파티 > 누구나. param1=ItemTableId, param2=수량
    // ── 경제 1차(귀속 — 명세 §1) ──
    ItemBound,           // 귀속된 물건이라 할 수 없음(경매 등록 · 땅에 버리기). param1=ItemTableId, param2=칸 번호
};

// 프로토콜 버전. 클라이언트(STRESS_TEST 봇 포함)는 USER_LOGIN_REQ에 이 값을 실어 보낸다.
// 3: 경제 1차 — 가방 50칸(ITEM_LIST_ACK 두 쪽) · 보호권 없음 kNoInventorySlot · 제작/분해(CRAFT_REQ 112 · CRAFT_ACK 113) · 정예 표시 · ItemBound · AdenaReason::Craft
constexpr uint16_t PROTOCOL_VERSION = 3;

// EFFECT_NFY의 effectId. "서버는 무슨 일이 일어났는지(어디서 누가)만 알리고,
// 파티클/사운드/흔들림 같은 표현은 전부 클라이언트가 정한다" — 서버가 이펙트의
// 모양을 아는 순간 설계가 틀린 것이다.
enum class EffectId : uint16_t
{
    None,
    SkillCross,       // 십자베기. subject=시전자, x/y=시전 위치
    LevelUp,          // param=새 레벨
    PotionHeal,       // param=회복량
    TeleportOut,      // 사라지는 위치
    TeleportIn,       // 나타나는 위치
    HasteOn,
    EnchantSuccess,   // param=새 강화 수치
    EnchantFail,      // param=증발하기 전 강화 수치
    BossSlamCast,     // x/y=중심, param=반경(칸). 곧 내려찍는다는 경고
    BossSlamImpact,   // x/y=중심, param=반경
    PlayerKill,       // subject=PK한 플레이어
    Restart,          // 마을에서 다시 시작
    BossSpawn,        // subject=보스
    // V2
    DiscoverRegion,   // param=지역(zoneId) — 새 지역 발견(본인에게)
    RuneExplosion,    // x/y=중심 — 옛길 도약이 남긴 각인 폭발
    SteamBurst,       // x/y=중심 — 증발 폭발
    Counter,          // subject=반격한 기사
    WallSlam,         // x/y=부딪힌 칸
    PoisonBurst,      // subject=대상
    PriestHeal,       // subject=치유받은 플레이어, param=회복량
    // V2 아이템 특수 효과(Data/ItemRules.h ItemProc) — subject=장비 주인, x/y=효과 위치
    ProcThunder,      // 천둥의 일격(벼락이 떨어진 칸)
    ProcLifesteal,    // 생명 흡수(param=회복량)
    ProcFrost,        // 서리 손길(x/y=대상)
    ProcExecute,      // 처형자(x/y=대상)
    ProcEcho,         // 메아리(x/y=대상)
    ProcGuardian,     // 서약의 방패(주인)
    ProcEmber,        // 잔불(x/y=대상)
    ProcVenom,        // 맹독(x/y=대상)
    // Phase 2-B(끝에만 추가)
    StaggerBreak,     // subject=보스, x/y=보스 — 무력화(게이지가 비었다)
    OathRelease,      // subject=기사 — 서약 해방
    Resonance,        // subject=각인술사, x/y=폭발 칸, param=순서 — 새김 울림 연쇄 폭발
    PerfectDodge,     // subject=밤사냥꾼 — 완벽 회피(무적 중 공격을 피했다)
    CloneStrike,      // subject=밤사냥꾼, x/y=대상 — 분신이 따라 벤다
    Taunted,          // subject=기사, param=도발한 수
};

// WORLD_NOTICE_INF의 code — 전 서버에 띄우는 공지.
enum class WorldNoticeCode : uint16_t
{
    BossSpawned,      // param1=보스 종족 id, param2=지역(zoneId)
    BossKilled,       // param1=보스 종족 id, param2=지역, name=막타 플레이어
    EnchantSuccess,   // param1=ItemTableId, param2=강화 수치, name=플레이어
    PlayerKilled,     // param1=지역, name=PK한 플레이어
    RareLoot,         // V2: param1=ItemTableId, param2=희귀도(Rarity), name=주운 플레이어 — 신화 획득
    StoryComplete,    // V2: param1=퀘스트 id(메인 이야기 마지막), name=플레이어 — 하르벤이 다시 뛴다
};

// 패킷 타입 — 헤더의 1바이트(uint8_t)로 읽는다(0~255). 번호는 클라이언트와 맞춘 값이라 바꾸지 않는다.
// 새 타입은 108부터 이어 붙이고(104~107 파티), 지운 번호(0~4 · 13 · 14 · 26 · 27 · 31 · 33 · 34 · 37 · 38 · 47 · 56 · 58 — 예전 봇 · 1세대 클라 패킷)는 다시 쓰지 않는다.
enum class PacketType : uint16_t
{
    // ── 접속 ─────────────────────────────────────────────────────────────────────
    USER_LOGIN_REQ            = 35,   // C→S 로그인(이름 · 비밀번호 · 새 캐릭터 직업)
    USER_LOGIN_ACK            = 11,   // S→C 로그인 성공(내 id · 위치 · HP · 레벨)
    USER_LOGIN_FAIL_ACK       = 12,
    SERVER_CONFIG_INF         = 55,   // S→C 시야 범위 · 기본 걸음 시간 등 서버 설정

    // ── 이동 · 시야 ──────────────────────────────────────────────────────────────
    USER_MOVE_REQ             = 53,   // C→S 순번 달린 이동(토큰 버킷 검증)
    USER_MOVE_ACK             = 54,   // S→C 이동 결과(순번 · 확정 좌표) — 거절도 즉시 응답
    SUBJECT_ADD_NFY           = 21,
    SUBJECT_MOVE_NFY          = 22,
    SUBJECT_REMOVE_NFY        = 23,
    SUBJECT_FORCED_MOVE_NFY   = 82,   // S→C 서버가 옮긴 이동(돌진 · 순간이동 · 넉백 · 끌어당김 · 구르기)
    SUBJECT_INFO_NFY          = 43,   // S→C 주변 플레이어의 레벨 · 성향치 · 상태 · 무기 강화 수치
    SUBJECT_EQUIP_CHANGE_NFY  = 28,
    PLAYER_LOOK_NFY           = 72,   // S→C 주변 플레이어의 착용 외형(부위별 아이템 · 희귀도)

    // ── 캐릭터 ───────────────────────────────────────────────────────────────────
    USER_INFO_INF             = 42,   // S→C 내 정보(그로시 · 성향치 · 공격력 · 다음 레벨 경험치 등)
    USER_STATS_INF            = 60,   // S→C 내 직업 · 능력치 · 전투 능력치
    USER_RESOURCE_INF         = 61,   // S→C 내 HP · MP
    USER_HEAL_INF             = 29,   // S→C 물약으로 회복한 HP
    USER_STAT_CHANGE_INF      = 30,   // S→C 레벨 · HP · 경험치가 바뀜
    STAT_ALLOC_REQ            = 62,   // C→S 능력치 포인트 분배
    USER_RESTART_REQ          = 36,   // C→S 사망 상태에서 마을로 다시 시작
    SUBJECT_DIE_NFY           = 24,
    SUBJECT_RESPAWN_NFY       = 25,
    SYSTEM_MESSAGE_INF        = 32,   // S→C 상황 알림(SystemMessageCode)

    // ── 전투 · 기술 ──────────────────────────────────────────────────────────────
    SKILL_REQ                 = 80,   // C→S 기술 누름 · 뗌(차지 · 홀딩) · 취소 — 평타도 기술 칸 하나
    SKILL_NFY                 = 81,   // S→C 시전 알림(단계 · 조준 · 서버 시각) — 주변에
    SKILL_RESULT_ACK          = 57,   // S→C 시전 결과(쿨다운 · MP) — 시전자에게
    STATUS_NFY                = 59,   // S→C 상태이상 걸림 · 풀림 — 주변에
    SUBJECT_DAMAGE_NFY        = 49,   // S→C 누가 누구에게 얼마를(치명타 · 기술 여부 포함)
    EFFECT_NFY                = 50,   // S→C 이펙트 트리거(EffectId)
    COMBAT_GAUGE_INF          = 83,   // S→C 내 고유 기술 게이지(서약 · 새김 · 그늘)
    MONSTER_TELEGRAPH_NFY     = 63,   // S→C 몬스터 강공격 예고(모양 · 시간)
    MONSTER_GAUGE_NFY         = 84,   // S→C 보스 무력화 게이지 · 무력화 상태
    SKILL_TREE_INF            = 85,   // S→C 내 기술 레벨 · 갈래 · 남은 점수
    SKILL_GROW_REQ            = 86,   // C→S 기술 레벨 올림 · 내림 · 갈래 고르기 · 모두 초기화
    SKILL_GROW_ACK            = 87,

    // ── 소지품 ───────────────────────────────────────────────────────────────────
    ITEM_EQUIP_REQ            = 5,
    ITEM_UNEQUIP_REQ          = 6,
    ITEM_SWAP_REQ             = 7,
    ITEM_DISCARD_REQ          = 8,
    ITEM_PICKUP_REQ           = 9,
    ITEM_USE_REQ              = 10,
    ITEM_SORT_REQ             = 73,
    ITEM_EQUIP_ACK            = 15,
    ITEM_UNEQUIP_ACK          = 16,
    ITEM_SWAP_ACK             = 17,
    ITEM_DISCARD_ACK          = 18,
    ITEM_PICKUP_ACK           = 19,
    ITEM_USE_ACK              = 20,
    ITEM_LIST_ACK             = 44,   // S→C 강화 수치를 포함한 소지품 전체(로그인 때)
    ITEM_SLOT_INF             = 45,   // S→C 슬롯 하나의 최신 상태(모든 슬롯 변경에서)
    ITEM_DETAIL_INF           = 71,   // S→C 슬롯 하나의 인스턴스 값(희귀도 · 옵션) — ITEM_SLOT_INF 바로 뒤에
    ADENA_INF                 = 46,

    // ── 대장간 강화 ──────────────────────────────────────────────────────────────
    ITEM_ENCHANT_REQ          = 97,
    ENCHANT_ACK               = 98,
    ITEM_ENCHANT_LEVEL_INF    = 99,
    ENCHANT_NOTICE_INF        = 100,
    SUBJECT_ENCHANT_LEVEL_INF = 102,
    ENCHANT_GRACE_INF         = 103,  // 하르벤의 가호: 캐릭터별 장비 종류 · 강화 구간 누적 상태

    // ── 마을 NPC ─────────────────────────────────────────────────────────────────
    SHOP_BUY_REQ              = 39,
    SHOP_SELL_REQ             = 40,
    SHOP_RESULT_ACK           = 48,   // S→C 구매 · 판매 · 순간이동 결과
    NPC_TELEPORT_REQ          = 41,
    NPC_LIST_NFY              = 64,   // S→C 마을 NPC 전체 위치 · 상태(마을에 들어올 때)
    NPC_STATE_NFY             = 65,   // S→C NPC 한 걸음 · 멈춤 · 대화 상태
    NPC_BARK_NFY              = 66,   // S→C NPC 혼잣말 · 인사(말풍선)
    NPC_TALK_REQ              = 67,   // C→S NPC에게 말 걸기
    NPC_TALK_ACK              = 68,
    NPC_SERVICE_REQ           = 69,   // C→S 사제의 치유 기도 · 여관 수프
    NPC_SERVICE_ACK           = 70,

    // ── 퀘스트 ───────────────────────────────────────────────────────────────────
    QUEST_LIST_INF            = 74,   // S→C 진행 중 · 완료 · 반복 대기 전체(로그인 때)
    QUEST_UPDATE_INF          = 75,   // S→C 퀘스트 하나의 변화
    QUEST_ACCEPT_REQ          = 76,
    QUEST_COMPLETE_REQ        = 77,
    QUEST_ABANDON_REQ         = 78,
    QUEST_RESULT_ACK          = 79,

    // ── 채팅 · 공지 ──────────────────────────────────────────────────────────────
    CS_CHAT                   = 88,   // C→S 채널 · 받는 사람(귓속말) · 글
    SC_CHAT                   = 52,   // S→C 채널 채팅(보낸 사람 이름 포함)
    WORLD_NOTICE_INF          = 51,   // S→C 전 서버 공지(보스 출현 · 처치, 고강화 성공)

    // ── 결사 · 경매장 ────────────────────────────────────────────────────────────
    GUILD_REQ                 = 89,
    GUILD_ACK                 = 90,
    GUILD_INF                 = 91,
    GUILD_NFY                 = 92,
    AUCTION_REQ               = 93,
    AUCTION_ACK               = 94,
    AUCTION_LIST_INF          = 95,
    AUCTION_NFY               = 96,
    AUCTION_ENCHANT_LEVEL_INF = 101,

    // ── 파티 · 모집 게시판(Content/Party — 묶음 패킷 + op, 결사와 같은 모양) ─────────────────
    PARTY_REQ                 = 104,  // C→S 파티 요청 하나(PartyOp)
    PARTY_ACK                 = 105,  // S→C 요청마다 하나(PartyResult)
    PARTY_INF                 = 106,  // S→C 파티 구성 · 파티원 상태 · 모집 글 목록 · 신청자(PartyInfKind)
    PARTY_NFY                 = 107,  // S→C 파티 사건(PartyEvent)

    // ── 결사 성장(Content/Guild — 레벨 · 기여도 · 주간 임무 · 게시판 · 기록 · 권한 · 금고 · 상점 · 업적 · 출석 · 모집, Shared/Document/GUILD.md) ──
    GUILD_EX_REQ              = 108,  // C→S 결사 성장 요청 하나(GuildExOp)
    GUILD_EX_ACK              = 109,  // S→C 요청마다 하나(GuildExResult)
    GUILD_EX_INF              = 110,  // S→C 성장 · 임무 · 기여도 · 기록 · 게시판 · 상점 · 업적 · 칭호 · 모집(GuildExInfKind)
    GUILD_EX_NFY              = 111,  // S→C 결사 성장 사건(GuildExEvent)

    // ── 경제 1차 제작 · 분해(Content/Craft — 대장장이 곁, Shared/Document/ECONOMY.md) ──
    CRAFT_REQ                 = 112,  // C→S 제작 · 분해 요청 하나(CraftOp)
    CRAFT_ACK                 = 113,  // S→C 요청마다 하나(CraftResult)
    TRANSFER_REQ              = 114,
    TRANSFER_ACK              = 115,
    TRANSFER_STATE            = 116,
    TRANSFER_ITEM             = 117,
    SOCIAL_REQ                = 118,
    SOCIAL_ACK                = 119,
    SOCIAL_ENTRY              = 120,
    // ── 개인 업적(121~123, 끝에만 — Shared/Document/ACHIEVEMENT.md) ──
    ACHIEVE_REQ               = 121,  // C→S 업적 목록 · 받기 · 모두 받기(AchieveOp)
    ACHIEVE_ACK               = 122,  // S→C 받기 결과 · 거절(AchieveResult) — List 성공은 ACK 없이 Page만
    ACHIEVE_INF               = 123,  // S→C 요약 · 목록 한 장 · 달성 · 받음(AchieveInfKind)
};

#pragma pack (push, 1)
// slotIndex는 클라이언트가 보는 인벤토리 그리드 좌표 그대로다. 범위를 벗어나거나
// 비어있는 슬롯을 가리켜도 서버는 크래시하지 않는다 — Inventory::TryEquip 등은
// 존재하지 않는 슬롯 조회를 그냥 실패(false)로 처리하므로, 유효성 검사는
// Inventory 쪽에 이미 있다(여기서 중복으로 검사하지 않는다).
struct ITEM_EQUIP_REQ_PACKET
{
    unsigned short size;
    char           type;
    uint16_t       slotIndex;
};

struct ITEM_UNEQUIP_REQ_PACKET
{
    unsigned short size;
    char           type;
    uint16_t       slotIndex;
};

struct ITEM_SWAP_REQ_PACKET
{
    unsigned short size;
    char           type;
    uint16_t       slotIndexA;
    uint16_t       slotIndexB;
};

struct ITEM_DISCARD_REQ_PACKET
{
    unsigned short size;
    char           type;
    uint16_t       slotIndex;
    uint16_t       count;
};

// 파라미터 없음 — 대상은 항상 "요청한 플레이어가 서버 기준으로 서 있는 칸"이다.
// 클라이언트가 좌표나 대상 id를 보내지 않으므로, 위조된 좌표로 먼 곳의 아이템을
// 원격으로 줍는 부정 사용 자체가 프로토콜 레벨에서 성립하지 않는다.
struct ITEM_PICKUP_REQ_PACKET
{
    unsigned short size;
    char           type;
};

// 소비 아이템 사용(포션 등). slotIndex가 장비/빈 슬롯을 가리키면 서버는
// Inventory::TryUseItem에서 그냥 실패로 처리한다(ITEM_EQUIP_REQ와 같은 관례).
struct ITEM_USE_REQ_PACKET
{
    unsigned short size;
    char           type;
    uint16_t       slotIndex;
};


// ── Client → Server: 로그인 · 부활 · 상점 · 순간이동 ─────────────────────────

// 로그인 — 클라이언트와 STRESS_TEST 봇이 같은 패킷을 쓴다. 로그인한 세션은
// 리니지풍 컨텐츠 패킷(상점 · 강화 · 채팅 등)을 모두 주고받는다.
struct USER_LOGIN_REQ_PACKET
{
    unsigned short size;
    char           type;
    char           name[NAME_SIZE];
    char           password[PASSWORD_SIZE];
    uint16_t       version;   // PROTOCOL_VERSION
    uint8_t        classId;   // 새 캐릭터를 만들 때의 직업(ClassId). 기존 캐릭터면 무시된다.
};

// 사망 상태일 때만 유효. 마을 부활 지점에서 HP를 채워 다시 시작한다.
struct USER_RESTART_REQ_PACKET
{
    unsigned short size;
    char           type;
};

// seq: 클라이언트가 요청마다 1씩 올리는 번호. 서버는 플레이어별로 마지막으로 처리한
// 번호를 기억해 같은 요청의 재전송(더블클릭/재접속 직후 재시도 등)을 무시한다(멱등성).
struct SHOP_BUY_REQ_PACKET
{
    unsigned short size;
    char           type;
    uint16_t       npcId;
    uint16_t       itemId;
    uint16_t       count;
    uint32_t       seq;
};

struct SHOP_SELL_REQ_PACKET
{
    unsigned short size;
    char           type;
    uint16_t       npcId;
    uint16_t       slotIndex;
    uint16_t       count;
    uint32_t       seq;
};

// destIndex = 목적지 지역(zoneId). 요금은 WorldData::FindRegion(zoneId)->teleportFee(시트 Region.teleport_fee)
struct NPC_TELEPORT_REQ_PACKET
{
    unsigned short size;
    char           type;
    uint16_t       npcId;
    uint16_t       destIndex;
    uint32_t       seq;
};


struct USER_LOGIN_ACK_PACKET
{
    unsigned short size;
    char    type;
    ObjID   id;
    short   x, y;
    uint16_t    maxhp;
    uint16_t    hp;
    uint8_t     level;
    uint32_t    exp;
};

struct USER_LOGIN_FAIL_ACK_PACKET
{
    unsigned short size;
    char           type;
};

struct ITEM_SLOT_DATA
{
    uint16_t slotIndex;
    uint16_t itemId;
    uint16_t count;
    uint8_t  equipped;  // 0/1
};

// 장착/탈착 요청 결과. success=0이면 slot은 의미 없는 값(0)이고, 실패해도
// slot.slotIndex만은 항상 요청받은 값 그대로 채워 보낸다 — 클라이언트가 여러
// 요청을 연달아 보낸 뒤에도 어떤 요청에 대한 응답인지 별도 상태 없이 알 수 있게.
struct ITEM_EQUIP_ACK_PACKET
{
    unsigned short size;
    char           type;
    uint8_t        success;
    ITEM_SLOT_DATA slot;
};

struct ITEM_UNEQUIP_ACK_PACKET
{
    unsigned short size;
    char           type;
    uint8_t        success;
    ITEM_SLOT_DATA slot;
};

// 두 슬롯의 최종 상태를 함께 보낸다. 스왑 결과 어느 한쪽이 비게 되면 그 슬롯은
// itemId=0, count=0으로 채워 보낸다 — 클라이언트는 그걸 "슬롯 비움"으로 해석한다.
// (slotA/slotB의 slotIndex는 실패 시에도 항상 요청받은 값 그대로다.)
struct ITEM_SWAP_ACK_PACKET
{
    unsigned short size;
    char           type;
    uint8_t        success;
    ITEM_SLOT_DATA slotA;
    ITEM_SLOT_DATA slotB;
};

// success=0이어도 slot.slotIndex는 항상 요청받은 값 그대로다(EQUIP/UNEQUIP_ACK와
// 같은 관례). 성공 시 슬롯이 완전히 비워졌으면 itemId=0, count=0으로 온다.
struct ITEM_DISCARD_ACK_PACKET
{
    unsigned short size;
    char           type;
    uint8_t        success;
    ITEM_SLOT_DATA slot;
};

// 성공 시 실제로 채워진 슬롯 내용은 뒤이어 오는 ITEM_SLOT_INF로 전달된다 —
// 이 패킷은 "그 Z키 입력 자체가 뭔가를 주웠는지"만 알려준다. 인벤토리가 가득
// 차서 실패한 경우는 뒤이어 SYSTEM_MESSAGE_INF(InventoryFull)도 함께 온다.
struct ITEM_PICKUP_ACK_PACKET
{
    unsigned short size;
    char           type;
    uint8_t        success;
};

// success=0이어도 slot.slotIndex는 항상 요청받은 값 그대로다(다른 ACK와 같은
// 관례). 성공 시 마지막 1개를 소비해 슬롯이 완전히 비워졌으면 itemId=0,
// count=0으로 온다. 실제로 회복된 HP는 이 패킷이 아니라 뒤이어 오는
// USER_HEAL_INF로 전달된다 — "이 요청이 뭘 했는지"(슬롯 변화)와 "이 플레이어의
// HP가 바뀌었다"(이미 있던 공용 알림)를 굳이 한 패킷에 합치지 않는다.
struct ITEM_USE_ACK_PACKET
{
    unsigned short size;
    char           type;
    uint8_t        success;
    ITEM_SLOT_DATA slot;
};

// itemId 필드는 카테고리에 따라 의미가 다르다(둘 다 아니면 0/무의미):
//   eUser  : 지금 장착 중인 무기의 ItemTableId. 0이면 장착한 게 없음.
//            (Inventory::GetEquippedItemId() 참고 — 아직 "한 번에 하나만
//            장착" 규칙은 없지만, 화면 표시는 그중 하나만 보여준다.)
//   eItem  : 그 필드 아이템 자체의 ItemTableId(클라이언트가 어떤 아이콘을
//            그릴지 결정하는 데 쓴다).
struct SUBJECT_ADD_NFY_PACKET
{
    unsigned short size;
    char    type;
    char    monster_type;
    ObjID   id;
    short   x, y;
    char    name[NAME_SIZE];
    uint16_t itemId;
};

// 이미 화면에 보이는 플레이어의 장착 상태가 바뀌었을 때(장착/탈착/장착 중이던
// 아이템을 버림) 주변 뷰어에게 알린다. itemId=0이면 "지금은 아무 것도 장착
// 안 함" — SUBJECT_ADD_NFY의 itemId 필드와 동일한 의미다. 새로 시야에
// 들어올 때는 이 패킷이 아니라 SUBJECT_ADD_NFY의 itemId로 이미 전달된다.
struct SUBJECT_EQUIP_CHANGE_NFY_PACKET
{
    unsigned short size;
    char     type;
    ObjID    id;
    uint16_t itemId;
};

struct SUBJECT_REMOVE_NFY_PACKET
{
    unsigned short size;
    char    type;
    ObjID   id;
};

struct SUBJECT_MOVE_NFY_PACKET
{
    unsigned short size;
    char    type;
    ObjID   id;
    short   x, y;
    unsigned int move_time;
};


struct SUBJECT_DIE_NFY_PACKET
{
    unsigned short size;
    char type;
    ObjID id;
    uint16_t hp;
};

struct SUBJECT_RESPAWN_NFY_PACKET
{
    unsigned short size;
    char type;
    char monster_type;
    ObjID id;
    short   x, y;
    uint16_t hp;
    char    name[NAME_SIZE];
};

struct USER_HEAL_INF_PACKET
{
    unsigned short size;
    char type;
    int32_t hp;
};

struct USER_STAT_CHANGE_INF_PACKET
{
    unsigned short size;
    char   type;
    uint8_t  level;
    uint16_t hp;
    uint16_t maxhp;
    uint32_t exp;
};

// SystemMessageCode 기준의 공용 상황 알림. param1/param2의 의미는 code마다
// 다르며, 각 code 정의 옆 주석에 명시한다.
struct SYSTEM_MESSAGE_INF_PACKET
{
    unsigned short size;
    char           type;
    uint16_t       code;    // SystemMessageCode
    int32_t        param1;
    int32_t        param2;
};

// ── Server → Client: 리니지풍 컨텐츠 ─────────────────────────────────────────
// STRESS_TEST 봇의 수신 버퍼(MAX_PACKET_SIZE=255)를 넘는 패킷은 만들지 않는다 —
// 봇도 모든 패킷을 받으므로 하나라도 넘으면 받는 순간 봇 연결이 끊긴다.
// 아래 static_assert와 맨 끝의 MAX_SERVER_PACKET_SIZE로 크기를 묶어 둔다.

// ITEM_SLOT_DATA + 강화 수치. ITEM_SLOT_DATA는 장착 · 탈착 · 교환 · 버림 · 사용 ACK가 그대로 쓴다.
struct ITEM_SLOT_STATE_DATA
{
    uint16_t slotIndex;
    uint16_t itemId;     // 0 = 빈 슬롯
    uint16_t count;
    uint8_t  equipped;
    uint8_t  enchant;
};

// SUBJECT_INFO_NFY.flags / USER_INFO_INF.flags
enum SubjectFlag : uint8_t
{
    SUBJECT_FLAG_PURPLE  = 1 << 0,   // 적대(먼저 공격함)
    SUBJECT_FLAG_HASTE   = 1 << 1,   // 가속 중
    SUBJECT_FLAG_DEAD    = 1 << 2,
    SUBJECT_FLAG_SAFE    = 1 << 3,   // 안전지대 안
    SUBJECT_FLAG_ELITE   = 1 << 4,   // 16 — 정예 몬스터(경제 1차 명세 §6 「SubjectFlag ELITE」 · SUBJECT_INFO_NFY.flags)
};

struct USER_INFO_INF_PACKET
{
    unsigned short size;
    char     type;
    uint8_t  level;
    uint32_t exp;
    uint32_t expToNext;
    uint16_t hp;
    uint16_t maxhp;
    uint16_t attack;       // 레벨 기본 + 무기 + 강화 보너스
    uint32_t adena;
    int16_t  alignment;
    uint16_t pkCount;
    uint8_t  flags;        // SubjectFlag
    uint32_t hasteRemainMs;
};

// 플레이어가 시야에 들어올 때(SUBJECT_ADD_NFY 직후)와 정보가 바뀔 때 보낸다.
// 필드 아이템이면 enchant만 의미가 있다(강화된 무기를 바닥에 버린 경우).
struct SUBJECT_INFO_NFY_PACKET
{
    unsigned short size;
    char     type;
    ObjID    id;
    uint8_t  level;
    int16_t  alignment;
    uint8_t  flags;
    uint8_t  enchant;      // 장착 무기(또는 필드 아이템)의 강화 수치
    uint16_t hp;
    uint16_t maxhp;
    uint8_t  classId;      // ClassId (V2)
    uint8_t  weaponGrade;  // 장착 무기 인스턴스 등급(V2, 외형 발광)
};

// 소지품 목록 한 쪽(로그인 · 맵 이동 때). 서버는 칸 0~29 · 30~49를 두 패킷으로 보낸다 — 쪽마다 그 범위의 모든 칸이
// slotIndex 순으로 들어가고 빈 칸은 itemId 0이다. 클라는 「항목마다 그 칸을 덮어쓴다」(쪽 범위 밖은 건드리지 않는다).
struct ITEM_LIST_ACK_PACKET
{
    unsigned short    size;
    char              type;
    uint8_t           slotCount;   // 이 쪽의 항목 수(≤ kItemListPageSlots)
    ITEM_SLOT_STATE_DATA items[kItemListPageSlots];
};

// 슬롯 내용이 바뀔 때마다(획득/사용/장착/교체/버림/강화/상점) 그 슬롯의 최신 상태.
// Inventory의 모든 변경 경로가 이미 한 곳(슬롯 저장)을 거치므로 거기서 함께 보낸다.
struct ITEM_SLOT_INF_PACKET
{
    unsigned short    size;
    char              type;
    ITEM_SLOT_STATE_DATA slot;
};

enum class AdenaReason : uint8_t { Pickup, ShopBuy, ShopSell, Teleport, Init, Service, Quest,
    // Phase 5(뒤에만 붙인다): 결사 세우기 · 경매 보증금 · 경매 구매 · 정산함 받기 · 되돌림(거래가 성립하지 않았다)
    GuildCreate, AuctionDeposit, AuctionBuy, AuctionClaim, Refund,
    // 경제 1차: 제작 수수료(CRAFT_REQ)
    Craft, DirectTrade,
    // 개인 업적: 보상 그로시(ACHIEVE_REQ Claim · ClaimAll)
    Achievement };

struct ADENA_INF_PACKET
{
    unsigned short size;
    char     type;
    uint32_t adena;
    int32_t  delta;
    uint8_t  reason;   // AdenaReason
};

enum class EnchantResult : uint8_t
{
    Invalid,       // 요청 자체가 잘못됨(두루마리/대상 없음, 최대 강화 등) — 아무 것도 소모되지 않음
    Success,
    Evaporated,    // 실패 — 장비가 파괴됐다
    Protected,     // 실패 — 보호권으로 장비와 강화 단계 유지
    NotAtBlacksmith,
    Busy,
    Duplicate,
    StorageError,
};

enum class ShopKind : uint8_t { Buy, Sell, Teleport };

enum class ShopResult : uint8_t
{
    Success,
    Duplicate,          // 이미 처리한 seq — 무시됨
    NotEnoughAdena,
    InventoryFull,
    TooFar,             // NPC와 너무 멀다
    InvalidItem,        // 이 상점에서 팔지 않는 물건 / 팔 수 없는 물건
    InvalidCount,
    EquippedItem,       // 장착 중인 장비는 팔 수 없다
    InvalidState,       // 사망/이동 중 등
    InvalidNpc,
};

struct SHOP_RESULT_ACK_PACKET
{
    unsigned short size;
    char     type;
    uint8_t  kind;     // ShopKind
    uint8_t  result;   // ShopResult
    uint32_t seq;
    uint16_t itemId;   // 텔레포트면 목적지 zoneId
    uint16_t count;
    int32_t  adenaDelta;
};

enum SubjectDamageFlag : uint8_t
{
    DAMAGE_CRIT       = 1 << 0,
    DAMAGE_SKILL      = 1 << 1,
    DAMAGE_KILLED     = 1 << 2,
    DAMAGE_PVP        = 1 << 3,
    DAMAGE_BOSS_SKILL = 1 << 4,
    DAMAGE_COMBO      = 1 << 5,   // V2: 연계 성공(comboId 참고)
    DAMAGE_DOT        = 1 << 6,   // V2: 지속 피해(화상/중독)
    DAMAGE_KNOCKBACK  = 1 << 7,   // V2: 넉백됨
};

// SUBJECT_DAMAGE_NFY.flags2 (V2)
enum SubjectDamageFlag2 : uint8_t
{
    DAMAGE2_EVADE     = 1 << 0,   // 피함(피해 0)
    DAMAGE2_GUARD     = 1 << 1,   // 철벽 태세로 경감
    DAMAGE2_COUNTER   = 1 << 2,   // 반격
    DAMAGE2_WALL      = 1 << 3,   // 넉백이 벽에 부딪힘
    DAMAGE2_BACKSTAB  = 1 << 4,   // 급소/기습
    DAMAGE2_HEAVY     = 1 << 5,   // 강타(큰 히트스톱)
    DAMAGE2_PROC      = 1 << 6,   // 장비 특수 효과로 생긴 피해(벼락·메아리)
    DAMAGE2_BACK      = 1 << 7,   // Phase 2-B: 후방 타격(기습 · 급소가 아닌 등 뒤 보너스)
};

// 주변 세션 모두에게 "누가 누구에게 얼마를" 알린다 — 남이 때린 데미지 숫자와
// 몬스터 HP 바를 모두가 보게 하기 위함이다.
struct SUBJECT_DAMAGE_NFY_PACKET
{
    unsigned short size;
    char     type;
    ObjID    victimId;
    ObjID    attackerId;
    int32_t  hp;
    int32_t  maxhp;
    int32_t  damage;
    uint8_t  flags;    // SubjectDamageFlag
    // ── V2 ──
    uint16_t skillId;  // 0 = 기본/몬스터 평타
    uint8_t  comboId;  // ComboTable (DAMAGE_COMBO일 때)
    uint8_t  flags2;   // SubjectDamageFlag2
    uint8_t  hitIndex; // 연타 중 몇 번째(0부터)
};

struct EFFECT_NFY_PACKET
{
    unsigned short size;
    char     type;
    ObjID    subjectId;
    uint16_t effectId;   // EffectId
    short    x, y;
    int32_t  param;
};

struct WORLD_NOTICE_INF_PACKET
{
    unsigned short size;
    char     type;
    uint16_t code;       // WorldNoticeCode
    int32_t  param1;
    int32_t  param2;
    char     name[NAME_SIZE];
};

// Phase 3-B: 끝에만 추가한다 — 기존 값(Normal 0 · World 1 · System 2)은 그대로.
// Normal은 "같은 지역(Zone) 전체" 일반 대화로 쓴다.
enum class ChatChannel : uint8_t { Normal, World, System, Trade, Guild, WhisperIn, WhisperOut,
    Party,   // 파티원(접속 중) — CPartyService가 구성원 목록으로 보낸다(보낸 사람 포함)
};
// SC_CHAT.channel의 가장 높은 비트: 접속 직후 보내는 "최근 대화"(지금 막 한 말이 아니다 — 말풍선 · 알림음 없음)
inline constexpr uint8_t kChatHistoryBit = 0x80;

// 채널 채팅(CS_CHAT → 받는 쪽). 받는 쪽이 보낸 사람을 시야에서 모를 수
// 있으므로(지역 전체 · 전체 · 귓속말) 이름을 함께 싣는다.
struct SC_CHAT_PACKET
{
    unsigned short size;
    char     type;
    uint8_t  channel;    // ChatChannel
    ObjID    senderId;
    char     senderName[NAME_SIZE];
    char     mess[CHAT_SIZE];
};

static_assert(sizeof(ITEM_LIST_ACK_PACKET) <= 255, "STRESS_TEST 수신 버퍼(255)를 넘으면 안 된다");
static_assert(sizeof(SC_CHAT_PACKET) <= 255, "STRESS_TEST 수신 버퍼(255)를 넘으면 안 된다");
#include "Protocol/PlayerTransfer.h"

#pragma pack (pop)

#pragma pack (push, 1)

// ── 이동 ─────────────────────────────────────────────────────────────────────
// dir: 8방향(0:UP 1:DOWN 2:LEFT 3:RIGHT 4:UP-LEFT 5:UP-RIGHT
// 6:DOWN-LEFT 7:DOWN-RIGHT). seq: 클라이언트가 이동마다 1씩 올리는 번호(16비트 순환).
struct USER_MOVE_REQ_PACKET
{
    unsigned short size;
    char           type;
    uint8_t        dir;
    uint16_t       seq;
};

enum class MoveResult : uint8_t
{
    Ok,          // 이동함(벽에 막혀 제자리여도 규칙대로 처리됐으면 Ok)
    TooFast,     // 토큰 버킷 초과 — 서버 좌표로 되돌려야 한다
    Busy,        // 사망/존 이동 중 등 지금은 움직일 수 없음
    Stunned,     // 기절·빙결 등 이동 불가 상태
};

// 모든 USER_MOVE_REQ에 정확히 하나씩, 받은 순서대로 응답한다.
// x/y는 처리 직후의 서버 좌표(거절이면 현재 좌표) — 클라이언트는 이 값으로 예측을 맞춘다.
struct USER_MOVE_ACK_PACKET
{
    unsigned short size;
    char           type;
    uint16_t       seq;
    short          x, y;
    uint8_t        result;    // MoveResult
    uint16_t       stepMs;    // 이 걸음의 비용(ms). 클라 예측 속도 보정용
};

// 로그인 직후 한 번. 클라이언트가 추측하지 말고 서버 값을 쓰도록.
struct SERVER_CONFIG_INF_PACKET
{
    unsigned short size;
    char           type;
    uint16_t       protocolVersion;   // PROTOCOL_VERSION
    uint8_t        viewRange;         // 시야(체비셰프 칸)
    uint16_t       baseStepMs;        // 이동속도 100%일 때 직선 한 칸 시간
    uint16_t       diagPermille;      // 대각선 비용 배율(‰)
    uint16_t       moveSpeedPercent;  // 지금 내 이동속도(%)
};

enum class SkillResult : uint8_t
{
    Ok,
    Cooldown,        // param = 남은 ms
    NoMana,
    OutOfRange,
    InvalidTarget,
    Locked,          // 레벨이 모자람(param = 필요 레벨)
    Busy,            // 사망·존 이동 중
    SafeZone,        // 마을 안
    Stunned,         // 기절 중
    Blocked,         // 이동/도약할 칸이 없음
    WrongClass,
    // Phase 2-B(끝에만 추가)
    NoGauge,         // 고유 기술 게이지가 모자람
};

struct SKILL_RESULT_ACK_PACKET
{
    unsigned short size;
    char           type;
    uint16_t       seq;
    uint16_t       skillId;
    uint8_t        result;       // SkillResult
    uint32_t       cooldownMs;   // 성공이면 이번 쿨다운, 실패(Cooldown)면 남은 시간
    uint16_t       mp;           // 현재 MP
};

struct STATUS_NFY_PACKET
{
    unsigned short size;
    char           type;
    ObjID          subjectId;
    uint8_t        status;       // StatusId
    uint8_t        stacks;       // 0 = 풀림
    uint32_t       durationMs;   // 남은 시간
    ObjID          sourceId;
};

// 내 직업·능력치·전투 능력치(로그인·레벨업·장비 변경·포인트 분배 때)
struct USER_STATS_INF_PACKET
{
    unsigned short size;
    char           type;
    uint8_t        classId;
    uint8_t        level;
    BaseStats      base;          // 직업 성장분
    BaseStats      alloc;         // 내가 찍은 포인트
    BaseStats      gear;          // 장비 보너스
    uint16_t       freePoints;
    DerivedStats   stats;
    uint16_t       mp;
};

struct USER_RESOURCE_INF_PACKET
{
    unsigned short size;
    char           type;
    uint16_t       hp, maxhp;
    uint16_t       mp, maxmp;
};

struct STAT_ALLOC_REQ_PACKET
{
    unsigned short size;
    char           type;
    uint8_t        stat;        // StatKind
    uint8_t        amount;      // 1~10
};

// 몬스터 강공격 예고 — 클라이언트는 duration 동안 모양을 채워 가며 그린다.
enum class TelegraphShape : uint8_t { Circle, Line, Cone };
struct MONSTER_TELEGRAPH_NFY_PACKET
{
    unsigned short size;
    char           type;
    ObjID          monsterId;
    uint8_t        shape;        // TelegraphShape
    short          x, y;         // 중심(원) 또는 끝점(선)
    short          fromX, fromY; // 시작점(선/부채꼴)
    uint8_t        radius;       // 칸
    uint16_t       durationMs;
    uint8_t        pattern;      // MonsterPattern
};

// ── 살아 있는 마을(NPC) ───────────────────────────────────────────────────────
// 마을 NPC는 서버(마을 Zone 스레드)가 배회시킨다. Sector 오브젝트가 아니라서
// SUBJECT_* 대신 아래 NPC_* 패킷으로 마을 근처의 세션에 알린다.
enum class NpcActState : uint8_t { Idle, Walk, Talk };

struct NPC_ENTRY
{
    uint16_t npcId;
    short    x, y;
    uint8_t  state;        // NpcActState
    int8_t   faceX, faceY; // 바라보는 방향(타일, -1~1)
};

inline constexpr int kNpcListMax = 24;
struct NPC_LIST_NFY_PACKET
{
    unsigned short size;
    char           type;
    uint8_t        count;
    NPC_ENTRY      npcs[kNpcListMax];
};

struct NPC_STATE_NFY_PACKET
{
    unsigned short size;
    char           type;
    NPC_ENTRY      npc;
    uint16_t       stepMs;    // 걸음이면 그 걸음 시간(보간), 아니면 0
    ObjID          talkTo;    // 대화 중이면 상대
};

// line: Data/NpcTalk.h barks 번호. 0xFF = 인사(NpcRow.greeting)
inline constexpr uint8_t kNpcGreetLine = 0xFF;
struct NPC_BARK_NFY_PACKET
{
    unsigned short size;
    char           type;
    uint16_t       npcId;
    uint8_t        line;
    ObjID          toUser;    // 인사/대답 상대(없으면 빈 ObjID)
};

struct NPC_TALK_REQ_PACKET
{
    unsigned short size;
    char           type;
    uint16_t       npcId;
};

enum class NpcTalkResult : uint8_t { Ok, TooFar, Unknown, Busy };
struct NPC_TALK_ACK_PACKET
{
    unsigned short size;
    char           type;
    uint16_t       npcId;
    uint8_t        result;    // NpcTalkResult
};

struct NPC_SERVICE_REQ_PACKET
{
    unsigned short size;
    char           type;
    uint16_t       npcId;
    uint8_t        service;   // WorldData::NpcService
};

enum class NpcServiceResult : uint8_t { Ok, TooFar, Cooldown, NoAdena, Full, Invalid };
struct NPC_SERVICE_ACK_PACKET
{
    unsigned short size;
    char           type;
    uint16_t       npcId;
    uint8_t        service;
    uint8_t        result;      // NpcServiceResult
    uint32_t       cooldownMs;  // 다시 받을 수 있을 때까지(성공/쿨다운)
};

// ── 아이템 성장 ─────────────────────────────────────────────────────────────
// 슬롯 하나의 V2 인스턴스 값(Data/ItemRules.h ItemInstance::Pack). 슬롯이 바뀔 때마다
// ITEM_SLOT_INF 바로 뒤에 보낸다(빈 슬롯·소모품이면 extra = 0). 로그인 스냅샷
// (ITEM_LIST_ACK) 뒤에는 extra가 0이 아닌 슬롯만 이어서 보낸다.
struct ITEM_DETAIL_INF_PACKET
{
    unsigned short size;
    char           type;
    uint16_t       slotIndex;
    uint16_t       itemId;
    uint64_t       extra;
};

// 주변 플레이어의 착용 외형. items/rarity는 EquipSlot - 1(무기 · 투구 · 갑옷 · 장갑 ·
// 신발 · 망토 · 목걸이 · 반지) 순서. 시야에 들어올 때와 장착이 바뀔 때 보낸다.
inline constexpr int kLookSlots = 8;
struct PLAYER_LOOK_NFY_PACKET
{
    unsigned short size;
    char           type;
    ObjID          id;
    uint16_t       items[kLookSlots];
    uint8_t        rarity[kLookSlots];
    uint8_t        enchant;       // 무기 강화 수치
};

enum class SortMode : uint8_t { Default };
struct ITEM_SORT_REQ_PACKET
{
    unsigned short size;
    char           type;
    uint8_t        mode;          // SortMode
};

// ── 퀘스트 · 메인 이야기(Data/QuestData.h) ───────────────────────────────────
// 진행도는 목표마다 하나(QuestData::kMaxObjs). "목표를 다 이뤘나"는 표와 진행도로 양쪽이
// 같은 식(QuestData::ObjectivesDone)으로 계산한다 — 따로 상태를 싣지 않는다.
struct QUEST_ENTRY
{
    uint16_t questId;
    uint16_t progress[3];
};

struct QUEST_COOLDOWN
{
    uint16_t questId;
    uint32_t remainSec;       // 반복 퀘스트를 다시 받을 수 있을 때까지(초)
};

inline constexpr int kQuestListActiveMax = 8;     // = QuestData::kMaxActive
inline constexpr int kQuestListCooldownMax = 8;
struct QUEST_LIST_INF_PACKET
{
    unsigned short size;
    char           type;
    uint8_t        activeCount;
    QUEST_ENTRY    active[kQuestListActiveMax];
    uint8_t        done[32];                         // 끝낸 퀘스트(한 번뿐인 것) id 비트 1~255
    uint8_t        cooldownCount;
    QUEST_COOLDOWN cooldowns[kQuestListCooldownMax];
};

enum class QuestEvent : uint8_t
{
    Accepted,    // 받음(진행도는 받는 순간의 값 — 이미 입고 있는 장비 등)
    Progress,    // 목표 하나가 늘었다(objIndex, x/y = 계기가 된 곳 — 쓰러진 몬스터 자리)
    Completed,   // 끝(보상은 아래 필드). 반복 퀘스트면 cooldownSec
    Abandoned,
};

struct QUEST_UPDATE_INF_PACKET
{
    unsigned short size;
    char           type;
    uint16_t       questId;
    uint8_t        event;          // QuestEvent
    uint8_t        objIndex;       // Progress: 바뀐 목표(0~2), 그 밖은 0xFF
    uint16_t       progress[3];
    short          x, y;           // Progress: 계기가 된 칸(없으면 -1)
    // Completed: 받은 보상
    uint32_t       exp;
    uint32_t       adena;
    uint16_t       itemId;
    uint16_t       itemCount;
    uint8_t        rarity;         // 장비 보상의 희귀도(Rarity)
    uint32_t       cooldownSec;    // 반복 퀘스트: 다시 받을 수 있을 때까지
};

struct QUEST_ACCEPT_REQ_PACKET
{
    unsigned short size;
    char           type;
    uint16_t       questId;
};

struct QUEST_COMPLETE_REQ_PACKET
{
    unsigned short size;
    char           type;
    uint16_t       questId;
};

struct QUEST_ABANDON_REQ_PACKET
{
    unsigned short size;
    char           type;
    uint16_t       questId;
};

enum class QuestAction : uint8_t { Accept, Complete, Abandon };
enum class QuestResult : uint8_t
{
    Ok,
    Unknown,        // 없는 퀘스트 · 진행 중이 아닌 퀘스트
    TooFar,         // 주는/보고할 NPC 곁이 아니다
    NotAvailable,   // 이미 받았거나 끝냈다 · 선행 퀘스트를 안 끝냈다
    LevelTooLow,    // param = 필요 레벨
    WrongClass,
    LogFull,        // 진행 중인 퀘스트가 가득(QuestData::kMaxActive)
    NotReady,       // 목표를 아직 다 이루지 못했다
    InventoryFull,  // 보상을 받을 자리가 없다(아무것도 바뀌지 않는다)
    Cooldown,       // param = 남은 초
    Busy,           // 사망 · 존 이동 중
};

struct QUEST_RESULT_ACK_PACKET
{
    unsigned short size;
    char           type;
    uint16_t       questId;
    uint8_t        action;         // QuestAction
    uint8_t        result;         // QuestResult
    uint32_t       param;
};

// ── Phase 2-B: 기술 구조 V3 ────────────────────────────────────────────────
enum class SkillPhase : uint8_t { Press, Release, Cancel };
enum SkillReqFlag : uint8_t { SKILLREQ_FORCE = 1 << 0 };   // 강제 공격(Ctrl) — 다른 플레이어도 맞힌다

// C→S. 칸 · 유형 · 타격 시각은 SkillDefTable(Data/SkillData.h) — 서버가 판정한다.
struct SKILL_REQ_PACKET
{
    unsigned short size;
    char           type;
    uint16_t       skillId;
    uint8_t        phase;        // SkillPhase
    uint8_t        flags;        // SkillReqFlag
    ObjID          targetId;     // 커서에 가장 가까운 적(대상형 · 조준 보조, 없으면 비움)
    short          aimX, aimY;   // 커서 칸(서버가 사거리로 자른다)
    uint8_t        dir;          // 화면 각 0~255(표시용 — 판정 방향은 서버가 aim에서 다시 계산)
    uint16_t       seq;
};

enum class SkillCastPhase : uint8_t
{
    Start,        // 일반 · 캐스팅 · 각성기 · 이동기 · 콤보 한 타(stage = 몇 타)
    ChargeBegin,  // 차지 시작(모으는 자세)
    ChargeRelease,// 차지 놓음(stage = 단계, flags 완벽)
    HoldBegin,    // 홀딩 시작
    HoldEnd,      // 홀딩 끝(놓음 · 최대 시간)
    ChainStep,    // 체인 단(stage = 몇 단)
    ToggleOn,
    ToggleOff,
    Cancel,       // 끊김(이동기 · 기절)
    Identity,     // 고유 기술 발동
};
// 비트 1~2 = 켜진 갈래 Ⅰ(0 없음 · 1 · 2), 3~4 = 갈래 Ⅱ — SkillGrowth::NfyBits · FromNfy(Phase 2-D)
enum SkillNfyFlag : uint8_t { SKILLNFY_PERFECT = 1 << 0, SKILLNFY_VARIANT_MASK = 0x1E };

// S→C 주변 시전 알림 — 같은 타격 시각표로 연출을 재생한다
struct SKILL_NFY_PACKET
{
    unsigned short size;
    char           type;
    ObjID          casterId;
    uint16_t       skillId;
    uint8_t        phase;        // SkillCastPhase
    uint8_t        stage;
    uint8_t        flags;        // SkillNfyFlag
    uint8_t        dir;          // 서버가 정한 방향(화면 각 0~255)
    short          fromX, fromY; // 시전 위치
    short          aimX, aimY;   // 조준 칸(돌진 · 도약이면 도착 칸)
    ObjID          targetId;
    uint16_t       castMs;
    uint32_t       serverTime;   // GetNowTime(ms) — 연출 시각표 기준
};

enum class ForcedMoveKind : uint8_t { Dash, Blink, Knockback, Pull, Roll, Launch };
struct SUBJECT_FORCED_MOVE_NFY_PACKET
{
    unsigned short size;
    char           type;
    ObjID          id;
    short          fromX, fromY;
    short          toX, toY;
    uint16_t       durationMs;
    uint8_t        kind;         // ForcedMoveKind
};

enum CombatGaugeFlag : uint8_t { GAUGE_FULL = 1 << 0, GAUGE_ACTIVE = 1 << 1 };
struct COMBAT_GAUGE_INF_PACKET
{
    unsigned short size;
    char           type;
    uint16_t       identity;     // 0~1000
    uint8_t        flags;        // CombatGaugeFlag
    uint32_t       activeMs;     // 고유 기술이 켜져 있으면 남은 시간
};

struct MONSTER_GAUGE_NFY_PACKET
{
    unsigned short size;
    char           type;
    ObjID          monsterId;
    uint16_t       stagger;      // 남은 무력화 게이지 ‰(1000 = 가득)
    uint8_t        state;        // 0 보통 · 1 무력화 중
    uint16_t       brokenMs;     // 무력화 남은 시간
};

// ── Phase 2-D: 기술 성장(Data/SkillGrowth.h) ─────────────────────────────────
// 기술 하나 = id + 상태 한 바이트(SkillGrowth::Pack — 레벨-1 · 갈래 Ⅰ · 갈래 Ⅱ)
struct SkillGrowEntry
{
    uint16_t skillId;
    uint8_t  state;
};

// S→C 본인. 직업의 자라는 기술 여덟 개 전부(QWERASDF 기본 배치 순서) — 로그인 · 레벨 업 · 초기화 때
struct SKILL_TREE_INF_PACKET
{
    unsigned short size;
    char           type;
    uint16_t       points;       // 남은 기술 점수
    uint16_t       total;        // 캐릭터 레벨로 받은 점수 전체
    uint8_t        count;
    SkillGrowEntry skills[8];
};

enum class SkillGrowOp : uint8_t { LevelUp, LevelDown, Pick, ResetAll };
enum class SkillGrowResult : uint8_t
{
    Ok,
    Unknown,       // 이 직업의 자라는 기술이 아니다
    Locked,        // 아직 배우지 않은 기술(param = 필요 레벨)
    MaxLevel,
    MinLevel,
    NoPoints,      // param = 필요한 점수
    TierLocked,    // 갈래를 고를 레벨이 아니다(param = 필요한 기술 레벨)
    InCombat,      // 싸우는 중(마지막 공격 · 피격 5초 안) · 시전 중에는 바꿀 수 없다
    Busy,          // 사망 · 존 이동 중
};

// C→S. seq가 직전과 같으면 다시 처리하지 않고 결과만 다시 보낸다(재전송 멱등)
struct SKILL_GROW_REQ_PACKET
{
    unsigned short size;
    char           type;
    uint8_t        op;           // SkillGrowOp
    uint16_t       skillId;
    uint8_t        tier;         // Pick: 0 = 갈래 Ⅰ(레벨 4) · 1 = 갈래 Ⅱ(레벨 7)
    uint8_t        choice;       // Pick: 0 = 끄기 · 1 · 2
    uint16_t       seq;
};

struct SKILL_GROW_ACK_PACKET
{
    unsigned short size;
    char           type;
    uint16_t       seq;
    uint8_t        op;           // SkillGrowOp
    uint8_t        result;       // SkillGrowResult
    uint16_t       param;
    SkillGrowEntry skill;        // 바뀐 뒤 상태(초기화면 skillId 0 — SKILL_TREE_INF가 뒤따른다)
    uint16_t       points;       // 남은 점수
};

// ── Phase 3-B: 채널 채팅 ──────────────────────────────────────────────────────
// 받는 쪽은 기존 SC_CHAT(168B) 그대로 — channel에 Trade · Guild · WhisperIn · WhisperOut 값만 끝에 추가했다.
// 서버: 소유 Zone 스레드에서 검사(토큰 버킷 5줄/5초 · 같은 말 되풀이 · UTF-8 · 채널별 재사용 대기) →
//   일반 = 같은 Zone(지역) 전체 / 전체 · 거래 = 서버 전체 /
//   귓속말 = 이름 색인(GameSessionManager) / 결사 = Phase 5.
struct CS_CHAT_PACKET
{
    unsigned short size;
    char           type;
    uint8_t        channel;                 // ChatChannel(Normal · World · Trade · Guild · WhisperOut=귓속말 보내기 · Party)
    char           targetName[NAME_SIZE];   // 귓속말 받는 사람(그 밖에는 비움)
    char           mess[CHAT_SIZE];         // UTF-8, 널 종료(128바이트 ≈ 한글 42자)
};

static_assert(sizeof(SKILL_TREE_INF_PACKET) <= 255, "모든 패킷 255바이트 이하");
static_assert(sizeof(SKILL_GROW_REQ_PACKET) <= 255, "모든 패킷 255바이트 이하");
static_assert(sizeof(SKILL_GROW_ACK_PACKET) <= 255, "모든 패킷 255바이트 이하");
static_assert(sizeof(CS_CHAT_PACKET) == 152 && sizeof(CS_CHAT_PACKET) <= 255, "모든 패킷 255바이트 이하");

static_assert(sizeof(SKILL_REQ_PACKET) <= 255, "모든 패킷 255바이트 이하");
static_assert(sizeof(SKILL_NFY_PACKET) <= 255, "모든 패킷 255바이트 이하");
static_assert(sizeof(SUBJECT_FORCED_MOVE_NFY_PACKET) <= 255, "모든 패킷 255바이트 이하");
static_assert(sizeof(COMBAT_GAUGE_INF_PACKET) <= 255, "모든 패킷 255바이트 이하");
static_assert(sizeof(MONSTER_GAUGE_NFY_PACKET) <= 255, "모든 패킷 255바이트 이하");

static_assert(sizeof(USER_STATS_INF_PACKET) <= 255, "STRESS_TEST 수신 버퍼(255)를 넘으면 안 된다");
static_assert(sizeof(QUEST_LIST_INF_PACKET) <= 255, "클라 수신 한 패킷(255) 안에 들어가야 한다");
static_assert(sizeof(PLAYER_LOOK_NFY_PACKET) <= 255, "클라 수신 한 패킷(255) 안에 들어가야 한다");
static_assert(sizeof(NPC_LIST_NFY_PACKET) <= 255, "클라 수신 한 패킷(255) 안에 들어가야 한다");

// ─────────────────────────────────────────────────────────────────────────────
// Phase 5: 결사 · 경매장 — 묶음 패킷.
// 요청은 op 하나로 가르고, 필드는 op마다 쓰는 것만 채운다(나머지 0). 모든 요청에 seq(멱등 — 같은 순번은 한 번만).
// 규칙 · 한도는 Data/GuildAuction.h. 서버는 받은 Zone(소유 스레드)에서 검사한 뒤 서비스(GuildService · AuctionService) 큐로 넘긴다.
// ─────────────────────────────────────────────────────────────────────────────
enum class GuildOp : uint8_t
{
    Info,          // 내 결사 정보(GUILD_INF Info)
    Members,       // 구성원 페이지 arg8 = 페이지, arg32 = 정렬(0 접속·직위 · 1 레벨 · 2 이름 · 3 최근 접속)
    Create,        // 세우기 text = 이름, arg8 = 문장 — 결사 회관지기 곁에서
    Invite,        // 초대 target = 이름
    InviteReply,   // 초대에 답 arg8 = 1 수락 / 0 거절, arg32 = 결사 번호
    Leave,         // 탈퇴(결사장은 넘긴 뒤에)
    Kick,          // 추방 target
    SetRank,       // 직위 target, arg8 = GuildRule::Rank
    Transfer,      // 결사장 넘기기 target
    Notice,        // 공지 text
    Disband,       // 해산(결사장 혼자 남았을 때)
    Name,          // 결사 이름 묻기 arg32 = 결사 번호(머리 위 표시 캐시)
    Emblem,        // 문장 바꾸기 arg8 — 결사 회관지기 곁에서
    Top,           // 결사 회관 깃발 뜰에 걸 결사(GUILD_INF Top — 인원 많은 순 · 먼저 세운 순). Phase 5 자기 검토
};
enum class GuildResult : uint8_t
{
    Ok, NotInGuild, AlreadyInGuild, NoPermission, NameTaken, NameInvalid, NoAdena, LevelTooLow, Full,
    TargetNotFound, TargetInGuild, InviteExpired, NotAtNpc, Busy, Invalid, Duplicate, LeaderMustTransfer, NotEmpty,
};
struct GUILD_REQ_PACKET
{
    unsigned short size;
    char           type;
    uint8_t        op;                      // GuildOp
    uint32_t       seq;
    uint8_t        arg8;
    uint32_t       arg32;
    char           target[NAME_SIZE];       // 대상 사람 이름(ASCII)
    char           text[92];                // 결사 이름(UTF-8, 한글 8자) · 공지(한글 30자) — 널 종료
};
struct GUILD_ACK_PACKET
{
    unsigned short size;
    char           type;
    uint8_t        op;                      // GuildOp
    uint8_t        result;                  // GuildResult
    uint32_t       seq;
    uint32_t       param;                   // 세우기: 결사 번호 · 그로시 부족: 필요한 값
};

enum class GuildInfKind : uint8_t { Info, Members, Name, Top };
struct GUILD_INFO_INF_PACKET                // GUILD_INF, kind = Info — 결사가 없으면 guildId 0
{
    unsigned short size;
    char           type;
    uint8_t        kind;
    uint32_t       guildId;
    uint8_t        emblem;
    uint8_t        myRank;                  // GuildRule::Rank
    uint8_t        members;
    uint8_t        online;
    char           name[25];
    char           notice[92];
    char           leader[NAME_SIZE];
};
struct GuildMemberRow
{
    char     name[NAME_SIZE];
    uint8_t  rank;
    uint8_t  level;
    uint8_t  classId;
    uint8_t  online;
    uint32_t lastSeenMin;                   // 마지막 접속(서버 시작 기준이 아니라 유닉스 분 — 접속 중이면 0)
};
struct GUILD_MEMBERS_INF_PACKET             // GUILD_INF, kind = Members — 8명씩
{
    unsigned short size;
    char           type;
    uint8_t        kind;
    uint8_t        page;
    uint8_t        pages;
    uint8_t        count;
    GuildMemberRow rows[8];
};
struct GUILD_NAME_INF_PACKET                // GUILD_INF, kind = Name — 머리 위 표시 캐시
{
    unsigned short size;
    char           type;
    uint8_t        kind;
    uint32_t       guildId;
    uint8_t        emblem;
    char           name[25];
};
struct GuildTopRow
{
    uint32_t guildId;
    uint8_t  emblem;
    uint8_t  members;
    char     name[25];
};
struct GUILD_TOP_INF_PACKET                 // GUILD_INF, kind = Top — 결사 회관 깃발 뜰(인원 많은 순 · 같으면 먼저 세운 순)
{
    unsigned short size;
    char           type;
    uint8_t        kind;
    uint8_t        count;
    GuildTopRow    rows[3];
};

enum class GuildEvent : uint8_t
{
    InviteFrom,    // who = 초대한 사람, name = 결사 이름, guildId · emblem
    Joined, Left, Kicked,                   // who(, by)
    RankChanged,   // who · rank · by
    LeaderChanged, // who = 새 결사장
    Online, Offline,
    Disbanded,     // name
    Created,       // 월드 공지: name · who(결사장) · emblem — 모두에게
    Tag,           // 머리 위 표시: obj · guildId · emblem(0 번호 = 결사 없음) — 주변 세션에
    NoticeChanged, // by
    InviteDeclined, // who = 거절한 사람(초대한 사람에게)
    EmblemChanged,
};
struct GUILD_NFY_PACKET
{
    unsigned short size;
    char           type;
    uint8_t        event;                   // GuildEvent
    uint8_t        emblem;
    uint8_t        rank;
    uint32_t       guildId;
    ObjID          obj;
    char           who[NAME_SIZE];
    char           by[NAME_SIZE];
    char           name[25];
};

enum class AuctionOp : uint8_t
{
    Search,        // cat · sub · minRarity · minEnchant · minLevel · maxLevel · option · sort · page · query
    Register,      // slot · count · price · duration(0~2) — 경매인 곁에서
    Buy,           // listingId · price(보이던 값 — 그 사이 바뀌면 거절)
    Cancel,        // listingId
    Mine,          // 내 경매 page
    Settlements,   // 정산함 page
    Claim,         // settlementId(0 = 맨 앞 하나) — "모두 받기"는 하나씩 이어서
    PriceHint,     // itemId · minRarity — 현재 최저가 · 최근 거래가
};
enum class AuctionResult : uint8_t
{
    Ok, NotFound, Sold, NoAdena, TooMany, BadPrice, BadItem, Equipped, InventoryFull, NotYours, NotAtNpc, Busy, Invalid, Duplicate,
    Bound,   // 14 — 귀속된 물건(경제 1차 — Item.bind eAcquire 또는 인스턴스 bit 8)은 올릴 수 없다(SYSTEM_MESSAGE ItemBound도 함께 온다)
};
struct AUCTION_REQ_PACKET
{
    unsigned short size;
    char           type;
    uint8_t        op;                      // AuctionOp
    uint32_t       seq;
    // 검색
    uint8_t        cat, sub, minRarity, minEnchant, minLevel, maxLevel, option, sort;
    uint16_t       page;
    char           query[26];
    // 등록 · 구매 · 취소 · 받기 · 시세
    uint16_t       slot;
    uint16_t       count;
    uint32_t       price;
    uint8_t        duration;
    uint32_t       id;                      // listingId · settlementId
    uint16_t       itemId;
};
struct AUCTION_ACK_PACKET
{
    unsigned short size;
    char           type;
    uint8_t        op;                      // AuctionOp
    uint8_t        result;                  // AuctionResult
    uint32_t       seq;
    uint32_t       id;                      // 목록 · 정산함 번호
    uint32_t       adena;                   // 요청 뒤 내 그로시(바뀌었으면)
    int32_t        delta;
    uint16_t       itemId;
    uint16_t       count;
};
enum class AuctionListKind : uint8_t { Search, Mine, Settlements };
struct AuctionRow
{
    uint32_t id;                            // 목록 번호 · 정산함 번호
    uint16_t itemId;                        // 정산함 대금이면 0
    uint16_t count;
    uint8_t  enchant;
    uint64_t extra;                         // ItemInstance(희귀도 · 옵션)
    uint32_t price;                         // 값 · 정산함 대금
    uint16_t minutesLeft;                   // 남은 시간(분) · 정산함: 사유(AuctionSettleReason)
    uint8_t  flags;                         // 1 = 내 목록 · 2 = 거래 중
};
enum class AuctionSettleReason : uint8_t { Sold, Bought, Cancelled, Expired };
struct AUCTION_LIST_INF_PACKET
{
    unsigned short size;
    char           type;
    uint8_t        kind;                    // AuctionListKind
    uint16_t       page;
    uint16_t       pages;
    uint32_t       total;
    uint8_t        count;
    AuctionRow     rows[6];
};
enum class AuctionEvent : uint8_t { Sold, Expired, SettleCount, PriceHint, Listed };
struct AUCTION_NFY_PACKET
{
    unsigned short size;
    char           type;
    uint8_t        event;                   // AuctionEvent
    uint32_t       id;
    uint16_t       itemId;
    uint32_t       price;                   // 팔린 값 · 정산 받을 대금
    uint16_t       settleCount;             // 받지 않은 정산함 행 수
    uint32_t       lowest;                  // 시세: 지금 최저가(없으면 0)
    uint32_t       lastSold;                // 시세: 마지막 거래가
    uint32_t       avgSold;                 // 시세: 최근 평균
    uint16_t       soldCount;               // 시세: 최근 거래 수
};

static_assert(sizeof(GUILD_REQ_PACKET) <= 255, "모든 패킷 255바이트 이하");
static_assert(sizeof(GUILD_ACK_PACKET) <= 255, "모든 패킷 255바이트 이하");
static_assert(sizeof(GUILD_INFO_INF_PACKET) <= 255, "모든 패킷 255바이트 이하");
static_assert(sizeof(GUILD_MEMBERS_INF_PACKET) <= 255, "모든 패킷 255바이트 이하 — 구성원 8명씩");
static_assert(sizeof(GUILD_NAME_INF_PACKET) <= 255, "모든 패킷 255바이트 이하");
static_assert(sizeof(GUILD_TOP_INF_PACKET) <= 255, "모든 패킷 255바이트 이하 — 깃발 뜰 3줄");
static_assert(sizeof(GUILD_NFY_PACKET) <= 255, "모든 패킷 255바이트 이하");
static_assert(sizeof(AUCTION_REQ_PACKET) <= 255, "모든 패킷 255바이트 이하");
static_assert(sizeof(AUCTION_ACK_PACKET) <= 255, "모든 패킷 255바이트 이하");
static_assert(sizeof(AUCTION_LIST_INF_PACKET) <= 255, "모든 패킷 255바이트 이하 — 6줄씩");
static_assert(sizeof(AUCTION_NFY_PACKET) <= 255, "모든 패킷 255바이트 이하");
static_assert(sizeof(AuctionRow) == 24, "경매 한 줄 24바이트(6줄 = 144B)");

// 대장간 강화(97~103) — 예전 강화 요청 · 결과 패킷을 대신한다.
struct ITEM_ENCHANT_REQ_PACKET
{
    uint16_t size; char type;
    uint32_t seq;
    uint16_t npcId, ticketSlot, targetSlot, protectionSlot; // 보호권 미사용 = kNoInventorySlot(0xFFFF — 서버는 MAX_INVENTORY_SLOTS 이상을 모두 없음으로 본다)
};
struct ENCHANT_ACK_PACKET
{
    uint16_t size; char type;
    uint32_t seq;
    uint8_t result;
    uint16_t targetSlot, itemId;
    uint32_t before, after;
};
struct ITEM_ENCHANT_LEVEL_INF_PACKET
{
    uint16_t size; char type;
    uint16_t slotIndex;
    uint32_t level;
};
struct ENCHANT_NOTICE_INF_PACKET
{
    uint16_t size; char type;
    uint16_t itemId;
    uint32_t level;
    uint8_t first; // 장비 테이블 ID별, 단계별 최초(+11 이상)
    char name[NAME_SIZE];
};
struct AUCTION_ENCHANT_LEVEL_INF_PACKET
{
    uint16_t size; char type;
    uint32_t seq, id, level;
    uint8_t op;
};
struct SUBJECT_ENCHANT_LEVEL_INF_PACKET
{
    uint16_t size; char type;
    ObjID id;
    uint32_t level;
};
static_assert(sizeof(ITEM_ENCHANT_REQ_PACKET) <= 255);
static_assert(sizeof(ENCHANT_NOTICE_INF_PACKET) <= 255);
struct ENCHANT_GRACE_INF_PACKET
{
    uint16_t size; char type;
    uint16_t itemId;
    uint32_t currentLevel;
    uint16_t failures, guaranteedAttempt;
    uint64_t successTickets; // 다음 시도 확률 = successTickets / EnchantRule::kRollScale
};
static_assert(sizeof(ENCHANT_GRACE_INF_PACKET) == 21);

// ─────────────────────────────────────────────────────────────────────────────
// 파티 · 모집 게시판(104~107) — 묶음 패킷. 요청은 op 하나로 가르고, 필드는 op마다 쓰는 것만 채운다(나머지 0).
// 변경 요청에는 seq(멱등 — 같은 순번은 한 번만, 소유 Zone이 사람마다 기억한다).
// 규칙 · 한도는 Database/GameData/PartyRules.h(구조 상수) · 시트 PartyConst(튜닝 값).
// 설계 · 스레드 소유권은 Shared/Document/PARTY.md · Content/Party/PartyService.h 머리 주석.
// ─────────────────────────────────────────────────────────────────────────────
enum class PartyOp : uint8_t
{
    Info,           // 내 파티 구성(PARTY_INF Roster — 파티가 없으면 partyId 0)
    Create,         // 혼자 파티 만들기(파티장)
    Invite,         // 초대 target = 이름(파티가 없으면 받아들일 때 내가 파티장인 파티가 생긴다)
    InviteReply,    // 초대에 답 arg8 = 1 수락 / 0 거절, arg32 = 초대 번호(PARTY_NFY InviteFrom의 arg32)
    Leave,          // 탈퇴(파티장이면 다음 사람에게 넘어간다)
    Kick,           // 추방 target(파티장)
    Transfer,       // 파티장 위임 target(접속 중인 파티원)
    Disband,        // 해산(파티장)
    Settings,       // 초대 권한 arg8 = PartyInvitePolicy(파티장)
    Target,         // 내 대상 공유 obj(ObjID::npos = 대상 없음) — 파티원에게 PARTY_NFY Target
    Mark,           // 공격 집중 표시 obj(파티장, npos = 지움) — 파티원에게 PARTY_NFY Mark
    RecruitList,    // 모집 글 목록 arg8 = 목적(PartyPurpose, 0xFF = 전체), arg32 = 페이지 → PARTY_INF RecruitList
    RecruitPost,    // 글 올리기 · 고치기(끌어올림) purpose · minLevel · maxLevel · slots · arg8 = 1 자동 가입, text = 제목(파티가 없으면 만든다)
    RecruitClose,   // 글 내리기(파티장)
    RecruitApply,   // 신청 arg32 = 글 번호(자동 가입 글이면 바로 들어간다)
    RecruitAccept,  // 신청 승인 target = 신청자 이름(파티장)
    RecruitReject,  // 신청 거절 target(파티장)
    RecruitApplicants, // 내 글의 신청자 목록 → PARTY_INF Applicants(파티장)
    RecruitCancel,  // 내 신청 거두기
};
enum class PartyResult : uint8_t
{
    Ok, NotInParty, AlreadyInParty, NoPermission, Full, TargetNotFound, TargetInParty, InviteExpired,
    Busy, Invalid, Duplicate, TextInvalid, LevelOutOfRange, RecruitNotFound, AlreadyApplied, TooFast, NotApplied,
};
enum class PartyInvitePolicy : uint8_t { LeaderOnly, Anyone };
enum class PartyPurpose : uint8_t { Hunt, Boss, Quest, Count };
struct PARTY_REQ_PACKET
{
    unsigned short size;
    char           type;
    uint8_t        op;                      // PartyOp
    uint32_t       seq;
    uint8_t        arg8;
    uint32_t       arg32;
    ObjID          obj;                     // Target · Mark
    uint8_t        purpose;                 // 모집 글: PartyPurpose
    uint8_t        minLevel;
    uint8_t        maxLevel;
    uint8_t        slots;                   // 모집 글: 목표 인원(파티장 포함)
    char           target[NAME_SIZE];       // 대상 사람 이름(ASCII)
    char           text[46];                // 모집 글 제목(UTF-8, 한글 15자 = 45B) — 널 종료
};
struct PARTY_ACK_PACKET
{
    unsigned short size;
    char           type;
    uint8_t        op;                      // PartyOp
    uint8_t        result;                  // PartyResult
    uint32_t       seq;
    uint32_t       param;                   // 만들기 · 들어감: 파티 번호 · 글 올리기: 글 번호 · 신청: 1 = 바로 들어감 / 0 = 승인 대기 · 레벨: 필요한 값
};

enum class PartyInfKind : uint8_t { Roster, Members, RecruitList, Applicants };
// 파티원 상태 비트(PartyRosterRow · PartyMemberRow의 flags)
enum PartyMemberFlag : uint8_t
{
    PARTY_MEMBER_ONLINE = 1 << 0,
    PARTY_MEMBER_DEAD   = 1 << 1,
    PARTY_MEMBER_COMBAT = 1 << 2,   // 최근 5초 안에 때렸거나 맞았다(전투 중)
    PARTY_MEMBER_LEADER = 1 << 3,
};
struct PartyRosterRow
{
    char     name[NAME_SIZE];
    ObjID    obj;                           // 접속 중이 아니면 npos
    uint8_t  level;
    uint8_t  classId;
    uint8_t  flags;                         // PartyMemberFlag
    uint16_t zone;                          // 마지막으로 본 지역(ZoneId)
    uint16_t hp, maxHp, mp, maxMp;
};
// PARTY_INF, kind = Roster — 파티 구성(사람 · 파티장 · 설정이 바뀔 때마다 모두에게). 한 패킷 4줄, 8명이면 두 패킷(first = 첫 줄의 자리 번호).
// version: 구성이 바뀔 때마다 1씩 — Members 줄의 자리 번호(slot)가 이 구성 기준이다(낡은 version의 Members는 버린다).
struct PARTY_ROSTER_INF_PACKET
{
    unsigned short size;
    char           type;
    uint8_t        kind;
    uint32_t       partyId;                 // 0 = 파티 없음(창을 비운다)
    uint8_t        version;
    uint8_t        leader;                  // 파티장 자리 번호
    uint8_t        invitePolicy;            // PartyInvitePolicy
    uint8_t        maxMembers;
    uint8_t        total;                   // 파티원 수
    uint8_t        first;                   // 이 패킷 첫 줄의 자리 번호
    uint8_t        count;
    uint32_t       recruitId;               // 이 파티의 모집 글(0 = 없음)
    ObjID          mark;                    // 공격 집중 대상(npos = 없음)
    PartyRosterRow rows[4];
};
struct PartyMemberRow
{
    uint8_t  slot;                          // Roster 자리 번호
    uint8_t  level;
    uint8_t  flags;                         // PartyMemberFlag
    uint16_t zone;
    int16_t  x, y;                          // 미니맵(같은 지역일 때)
    uint16_t hp, maxHp, mp, maxMp;
};
// PARTY_INF, kind = Members — 바뀐 파티원 줄만(파티원 Zone이 시트 sync_interval_ms마다 모아 보낸다)
struct PARTY_MEMBERS_INF_PACKET
{
    unsigned short size;
    char           type;
    uint8_t        kind;
    uint32_t       partyId;
    uint8_t        version;
    uint8_t        count;
    PartyMemberRow rows[8];
};
// 모집 글 한 줄 — flags: 1 = 자동 가입 · 2 = 내 파티 글 · 4 = 내가 신청한 글
struct PartyRecruitRow
{
    uint32_t id;
    uint8_t  purpose;                       // PartyPurpose
    uint8_t  minLevel;
    uint8_t  maxLevel;
    uint8_t  members;                       // 지금 인원
    uint8_t  slots;                         // 목표 인원(빈자리 = slots - members)
    uint8_t  flags;
    char     leader[NAME_SIZE];
    char     title[46];
};
// PARTY_INF, kind = RecruitList — 한 페이지 3줄(빈자리 있는 글 먼저 · 최근에 움직인 글 먼저)
struct PARTY_RECRUIT_LIST_INF_PACKET
{
    unsigned short  size;
    char            type;
    uint8_t         kind;
    uint8_t         purpose;                // 거른 목적(0xFF = 전체)
    uint16_t        page;
    uint16_t        pages;
    uint16_t        total;
    uint8_t         count;
    PartyRecruitRow rows[3];
};
struct PartyApplicantRow
{
    char    name[NAME_SIZE];
    uint8_t level;
    uint8_t classId;
};
// PARTY_INF, kind = Applicants — 내 글의 신청자(파티장에게, 먼저 신청한 순)
struct PARTY_APPLICANTS_INF_PACKET
{
    unsigned short    size;
    char              type;
    uint8_t           kind;
    uint32_t          recruitId;
    uint8_t           count;
    PartyApplicantRow rows[8];
};

enum class PartyEvent : uint8_t
{
    InviteFrom,      // who = 초대한 사람, arg32 = 초대 번호(InviteReply에 그대로), arg8 = 초대한 사람의 파티 인원(0 = 파티 없음)
    InviteDeclined,  // who = 거절한 사람(초대한 사람에게)
    Joined,          // who · obj
    Left,            // who
    Kicked,          // who · by
    LeaderChanged,   // who = 새 파티장
    Disbanded,
    Online, Offline, // who
    Died,            // who · obj, arg16 = 지역(ZoneId), arg32 = 쓰러진 자리 (x << 16) | y
    Revived,         // who · obj
    Target,          // obj = 대상을 고른 파티원, target = 그 대상(npos = 대상 없음)
    Mark,            // target = 공격 집중 대상(npos = 지움), who = 표시한 파티장
    SettingsChanged, // arg8 = PartyInvitePolicy
    RecruitApplied,  // who = 신청자(파티장에게), arg8 = 레벨, arg16 = 직업
    RecruitAccepted, // 신청이 받아들여졌다(신청자에게), arg32 = 글 번호
    RecruitRejected, // 신청이 거절됐다(신청자에게), arg32 = 글 번호
    RecruitClosed,   // arg32 = 글 번호, arg8 = PartyRecruitCloseReason(파티장 · 신청자에게)
    ExpShare,        // 파티 사냥 경험치(받은 사람에게) arg32 = 받은 경험치, arg8 = 내 피해 몫 %, arg16 = 1이면 쩔 보장이 붙었다
    MemberTimeout,   // who = 접속이 끊긴 채 오래 지나 빠진 파티원
};
enum class PartyRecruitCloseReason : uint8_t { Leader, Idle, Disbanded, LeaderLeft };
struct PARTY_NFY_PACKET
{
    unsigned short size;
    char           type;
    uint8_t        event;                   // PartyEvent
    uint8_t        arg8;
    uint16_t       arg16;
    uint32_t       arg32;
    ObjID          obj;
    ObjID          target;
    char           who[NAME_SIZE];
    char           by[NAME_SIZE];
};

static_assert(sizeof(PARTY_REQ_PACKET) <= 255, "모든 패킷 255바이트 이하");
static_assert(sizeof(PARTY_ACK_PACKET) <= 255, "모든 패킷 255바이트 이하");
static_assert(sizeof(PARTY_ROSTER_INF_PACKET) <= 255, "모든 패킷 255바이트 이하 — 구성 4줄씩");
static_assert(sizeof(PARTY_MEMBERS_INF_PACKET) <= 255, "모든 패킷 255바이트 이하 — 파티원 8줄");
static_assert(sizeof(PARTY_RECRUIT_LIST_INF_PACKET) <= 255, "모든 패킷 255바이트 이하 — 모집 글 3줄씩");
static_assert(sizeof(PARTY_APPLICANTS_INF_PACKET) <= 255, "모든 패킷 255바이트 이하 — 신청자 8줄");
static_assert(sizeof(PARTY_NFY_PACKET) <= 255, "모든 패킷 255바이트 이하");

// ─────────────────────────────────────────────────────────────────────────────
// 결사 성장 — 묶음 패킷(결사 · 파티와 같은 모양: 요청 op 하나 · 응답 Ack · 정보 kind · 사건 event).
// 규칙 · 상한은 Database/GameData/GuildGrowth.h, 튜닝 값은 시트 GuildConst · GuildLevel · GuildMission · GuildShop · GuildAchievement · GuildTitle · GuildAttend.
// 기존 결사 패킷(GUILD_REQ 89~92)은 그대로 — 초대 · 추방 · 직위 · 공지는 GUILD_REQ, 그 권한 판정만 결사마다 정한 권한표(GuildExOp::Perms)를 쓴다.
// 결사장 위임 청구 · 가입 신청 · 게시판 · 금고 · 상점은 여기. 모든 요청에 seq(멱등 — 같은 순번은 한 번만).
// ─────────────────────────────────────────────────────────────────────────────
enum class GuildExOp : uint8_t
{
    Overview,      // 내 결사 성장(GUILD_EX_INF Overview)
    Missions,      // 이번 주 임무(Missions)
    MemberStats,   // 기여도 페이지 arg8 = 페이지, arg32 = 정렬(0 이번 주 기여 · 1 누적 기여 · 2 가입일 · 3 이번 주 활동)
    Log,           // 결사 기록 arg8 = 페이지, arg32 = 거르기(GuildLogFilter)
    Board,         // 게시판 목록 arg8 = 페이지, arg32 = 분류(0xFF = 전체)
    PostRead,      // 글 하나 arg32 = 글 번호(Post)
    PostWrite,     // 글 쓰기 arg8 = 분류(GuildPostCategory), title · body
    PostDelete,    // 글 내리기 arg32 = 글 번호(쓴 사람 · Board 권한)
    PostPin,       // 고정 arg32 = 글 번호, arg8 = 1 고정 / 0 풀기(Board 권한)
    Intro,         // 가입 안내 · 모집 설정 body = 안내, arg8 = 1 모집 중 / 0 닫음, arg32 = 최소 레벨(Notice 권한)
    RecruitList,   // 모집 중인 결사 arg8 = 페이지(Recruit) — 결사가 없어도
    RecruitView,   // 결사 하나의 공개 정보 arg32 = 결사 번호(Public) — 결사가 없어도
    Apply,         // 가입 신청 arg32 = 결사 번호
    ApplyCancel,   // 내 신청 거두기
    Applicants,    // 신청자 목록(Approve 권한)
    ApplyReply,    // target = 신청자, arg8 = 1 승인 / 0 거절(Approve 권한)
    Perms,         // 직위 권한 arg8 = 직위(부관 · 정예 · 결사원), arg32 = GuildPerm 비트(결사장만)
    Title,         // 칭호 고르기 arg8 = 칭호 id(0 = 없음, 결사장만 — 열린 칭호만)
    Shop,          // 결사 상점 arg8 = 페이지(Shop)
    Buy,           // 사기 arg32 = 상점 id(Shop 권한 — 금고의 인장)
    Achievements,  // 업적 arg8 = 페이지
    Titles,        // 칭호 목록 arg8 = 페이지
    Claim,         // 결사장 위임 청구(결사장이 오래 들어오지 않았을 때 — 결사장을 뺀 가장 높은 직위)
};
enum class GuildExResult : uint8_t
{
    Ok, NotInGuild, NoPermission, Busy, Invalid, NotFound, NoSeals, LevelTooLow, LimitReached, InventoryFull,
    BadText, Duplicate, InGuild, Full, AlreadyApplied, NotRecruiting, NotYet, TooMany,
};
enum class GuildPostCategory : uint8_t { Notice, Recruit, Guide, Item, Free, Count };
enum class GuildLogKind : uint8_t
{
    Joined,        // who
    Left,          // who
    Kicked,        // who, what = 추방한 사람
    Rank,          // who, value = 새 직위, what = 바꾼 사람
    Leader,        // who = 새 결사장, what = 넘긴 사람
    ClaimStart,    // who = 청구한 사람, value = 기다리는 초
    ClaimCancel,   // who = 돌아온 결사장
    ClaimDone,     // who = 새 결사장, what = 예전 결사장
    LevelUp,       // value = 새 레벨
    Mission,       // value = 임무 id, what = 임무 이름
    Achievement,   // value = 업적 id, what = 업적 이름
    Attend,        // value = 출석 단계, what = ""(그날 출석 수는 value2 없이 덧말에)
    SealsIn,       // value = 들어온 인장, what = 까닭(임무 · 업적 · 출석 이름)
    SealsOut,      // value = 나간 인장, who = 산 사람, what = 아이템 이름
    Perms,         // who, value = (직위 << 8) | 권한 비트
    Title,         // who, value = 칭호 id, what = 칭호 이름
    Notice,        // who
    Intro,         // who, value = 1 모집 중 / 0 닫음
    Emblem,        // who
    Accepted,      // who = 들어온 사람(신청 승인), what = 승인한 사람
};
enum class GuildLogFilter : uint8_t { All, Members, Treasury, Growth };
enum class GuildExInfKind : uint8_t { Overview, Missions, MemberStats, Log, Board, Post, Public, Recruit, Applicants, Shop, Achievements, Titles };

struct GUILD_EX_REQ_PACKET
{
    unsigned short size;
    char           type;
    uint8_t        op;                      // GuildExOp
    uint32_t       seq;
    uint8_t        arg8;
    uint32_t       arg32;
    char           target[NAME_SIZE];       // 사람 이름(ASCII)
    char           title[31];               // 글 제목(UTF-8, 한글 10자) — 널 종료
    char           body[151];               // 글 본문 · 가입 안내(UTF-8, 한글 50자 · 40자) — 널 종료
};
struct GUILD_EX_ACK_PACKET
{
    unsigned short size;
    char           type;
    uint8_t        op;                      // GuildExOp
    uint8_t        result;                  // GuildExResult
    uint32_t       seq;
    uint32_t       param;                   // 글 쓰기: 글 번호 · 인장 부족: 필요한 값 · 레벨 부족: 필요한 레벨 · 청구 이름: 남은 초
};

// GUILD_EX_INF, kind = Overview — 결사가 없으면 guildId 0(나머지 0)
struct GUILD_EX_OVERVIEW_INF_PACKET
{
    unsigned short size;
    char           type;
    uint8_t        kind;
    uint32_t       guildId;
    uint8_t        level;
    uint8_t        frame;                   // 문장 테두리 단계
    uint8_t        title;                   // 고른 칭호(0 = 없음)
    uint8_t        myRank;
    uint8_t        members;
    uint8_t        maxMembers;
    uint8_t        recruiting;
    uint8_t        recruitMinLevel;
    uint32_t       exp;                     // 누적 결사 경험치
    uint32_t       expLevel;                // 이 레벨이 시작된 누적 경험치
    uint32_t       expNext;                 // 다음 레벨의 누적 경험치(0 = 가장 높은 레벨)
    uint32_t       seals;                   // 금고의 결사 인장
    uint8_t        perms[4];                // 직위별 권한(GuildPerm 비트, [0] 결사장 = 전부)
    uint32_t       weekId;
    uint32_t       weekLeft;                // 이번 주가 끝날 때까지(초)
    uint32_t       dayLeft;                 // 오늘이 끝날 때까지(초)
    uint8_t        attendToday;             // 오늘 출석한 결사원 수
    uint8_t        attendTier;              // 오늘 채운 출석 단계
    uint8_t        attendNext;              // 다음 단계에 필요한 출석 수(0 = 가장 높은 단계)
    uint8_t        myAttended;              // 내가 오늘 출석했는가
    uint16_t       myActiveToday;           // 내 오늘 활동(분)
    uint16_t       attendActiveMin;         // 출석에 필요한 활동(분)
    uint32_t       myContribWeek;
    uint32_t       myContribTotal;
    uint32_t       myJoinedAt;              // 유닉스 초
    uint32_t       myExpToday;              // 내가 오늘 사냥으로 올려 준 결사 경험치
    uint32_t       expCapDaily;             // 한 사람이 하루에 사냥으로 올려 줄 수 있는 결사 경험치
    uint32_t       titlesUnlocked;          // 열린 칭호(비트 = 칭호 id)
    uint8_t        achDone;
    uint8_t        achTotal;
    uint16_t       missionsDone;            // 누적 주간 임무 달성
    uint32_t       kills;
    uint32_t       bossKills;
    uint32_t       coopKills;
    uint16_t       ageDays;                 // 세운 뒤 지난 날
    uint16_t       attendDays;              // 출석 1단계 이상을 채운 날 수
    char           claimant[NAME_SIZE];     // 위임 청구 중이면 청구한 사람
    uint32_t       claimLeft;               // 청구가 이뤄지기까지(초)
    uint32_t       claimIn;                 // 결사장 위임 청구가 열리기까지(초, 0 = 지금 청구할 수 있다 · 결사장이 접속 중이면 0xFFFFFFFF)
};
struct GuildMissionRow
{
    uint16_t id;
    uint8_t  kind;                          // GuildMissionKind
    uint8_t  done;
    uint32_t target;
    uint32_t progress;
    uint32_t sub;
    uint32_t exp;
    uint32_t seals;
    uint32_t mine;                          // 내 몫(활동 임무는 내 이번 주 활동 분)
    uint8_t  contributors;                  // 참여한 결사원 수
    uint8_t  bonus;                         // 참여 덧보상 조건을 채웠는가
    uint16_t contrib;                       // 달성하면 참여한 사람마다 받는 기여도
    char     name[31];
};
// GUILD_EX_INF, kind = Missions — 이번 주 임무(시트 GuildConst mission_slots개)
struct GUILD_EX_MISSIONS_INF_PACKET
{
    unsigned short  size;
    char            type;
    uint8_t         kind;
    uint32_t        weekId;
    uint32_t        weekLeft;
    uint8_t         bonusMembers;           // 참여 덧보상 조건(서로 다른 결사원 수)
    uint8_t         bonusPct;
    uint8_t         count;
    GuildMissionRow rows[3];
};
// 기여도 한 줄 — flags: 1 = 오늘 출석 · 2 = 새 결사원(협력 처치 대상)
struct GuildStatRow
{
    char     name[NAME_SIZE];
    uint16_t joinedDay;                     // 들어온 날(유닉스 날 = 초 / 86400)
    uint32_t contribWeek;
    uint32_t contribTotal;
    uint16_t activeWeek;                    // 이번 주 활동(분)
    uint8_t  flags;
};
struct GUILD_EX_MEMBER_STATS_INF_PACKET
{
    unsigned short size;
    char           type;
    uint8_t        kind;
    uint8_t        sort;
    uint8_t        page;
    uint8_t        pages;
    uint8_t        count;
    GuildStatRow   rows[7];
};
struct GuildLogRow
{
    uint32_t at;                            // 유닉스 초
    uint8_t  kind;                          // GuildLogKind
    int32_t  value;
    char     who[NAME_SIZE];
    char     what[31];
};
// GUILD_EX_INF, kind = Log — 최근 것부터 4줄씩
struct GUILD_EX_LOG_INF_PACKET
{
    unsigned short size;
    char           type;
    uint8_t        kind;
    uint8_t        filter;                  // GuildLogFilter
    uint8_t        page;
    uint8_t        pages;
    uint8_t        count;
    GuildLogRow    rows[4];
};
struct GuildPostRow
{
    uint32_t postId;
    uint32_t at;
    uint8_t  category;                      // GuildPostCategory
    uint8_t  pinned;
    char     author[NAME_SIZE];
    char     title[31];
};
// GUILD_EX_INF, kind = Board — 고정 글 먼저 · 최근 것부터 4줄씩
struct GUILD_EX_BOARD_INF_PACKET
{
    unsigned short size;
    char           type;
    uint8_t        kind;
    uint8_t        category;                // 거른 분류(0xFF = 전체)
    uint8_t        page;
    uint8_t        pages;
    uint8_t        count;
    GuildPostRow   rows[4];
};
struct GUILD_EX_POST_INF_PACKET             // GUILD_EX_INF, kind = Post — 글 하나(없으면 postId 0)
{
    unsigned short size;
    char           type;
    uint8_t        kind;
    uint32_t       postId;
    uint32_t       at;
    uint8_t        category;
    uint8_t        pinned;
    uint8_t        canDelete;               // 내가 내릴 수 있는가(쓴 사람 · Board 권한)
    char           author[NAME_SIZE];
    char           title[31];
    char           body[151];
};
struct GUILD_EX_PUBLIC_INF_PACKET           // GUILD_EX_INF, kind = Public — 결사 하나의 공개 정보(모집 · 내 결사 가입 안내)
{
    unsigned short size;
    char           type;
    uint8_t        kind;
    uint32_t       guildId;                 // 0 = 없는 결사
    uint8_t        emblem;
    uint8_t        level;
    uint8_t        frame;
    uint8_t        title;
    uint8_t        members;
    uint8_t        maxMembers;
    uint8_t        recruiting;
    uint8_t        minLevel;
    uint8_t        applied;                 // 내가 이 결사에 신청해 두었는가
    char           name[25];
    char           leader[NAME_SIZE];
    char           titleName[25];
    char           intro[121];
};
struct GuildRecruitRow
{
    uint32_t guildId;
    uint8_t  emblem;
    uint8_t  level;
    uint8_t  frame;
    uint8_t  members;
    uint8_t  maxMembers;
    uint8_t  minLevel;
    uint8_t  applied;
    char     name[25];
};
// GUILD_EX_INF, kind = Recruit — 모집 중인 결사(레벨 높은 순 · 인원 많은 순) 6줄씩
struct GUILD_EX_RECRUIT_INF_PACKET
{
    unsigned short  size;
    char            type;
    uint8_t         kind;
    uint8_t         page;
    uint8_t         pages;
    uint8_t         count;
    GuildRecruitRow rows[6];
};
struct GuildApplicantRow
{
    char     name[NAME_SIZE];
    uint8_t  level;
    uint8_t  classId;
    uint32_t at;                            // 신청한 때(유닉스 초)
};
struct GUILD_EX_APPLICANTS_INF_PACKET       // GUILD_EX_INF, kind = Applicants — 먼저 신청한 순
{
    unsigned short    size;
    char              type;
    uint8_t           kind;
    uint8_t           count;
    GuildApplicantRow rows[8];
};
struct GuildShopRow
{
    uint16_t id;
    uint16_t itemId;
    uint16_t count;
    uint32_t price;                         // 결사 인장
    uint8_t  minLevel;
    uint8_t  weeklyLimit;                   // 0 = 제한 없음
    uint8_t  bought;                        // 내가 이번 주에 산 수
};
struct GUILD_EX_SHOP_INF_PACKET             // GUILD_EX_INF, kind = Shop — 10줄씩
{
    unsigned short size;
    char           type;
    uint8_t        kind;
    uint8_t        page;
    uint8_t        pages;
    uint8_t        count;
    uint32_t       seals;
    GuildShopRow   rows[10];
};
struct GuildAchRow
{
    uint16_t id;
    uint8_t  kind;                          // GuildAchKind
    uint8_t  done;
    uint32_t target;
    uint32_t progress;
    uint32_t seals;
    uint32_t exp;
    uint32_t at;                            // 이룬 때(유닉스 초, 0 = 아직)
    uint8_t  titleId;
    char     name[31];
};
struct GUILD_EX_ACH_INF_PACKET              // GUILD_EX_INF, kind = Achievements — 4줄씩(시트 id 순)
{
    unsigned short size;
    char           type;
    uint8_t        kind;
    uint8_t        page;
    uint8_t        pages;
    uint8_t        count;
    GuildAchRow    rows[4];
};
struct GuildTitleRow
{
    uint8_t id;
    uint8_t unlocked;
    char    name[25];
};
struct GUILD_EX_TITLES_INF_PACKET           // GUILD_EX_INF, kind = Titles — 8줄씩(시트 id 순)
{
    unsigned short size;
    char           type;
    uint8_t        kind;
    uint8_t        page;
    uint8_t        pages;
    uint8_t        count;
    uint8_t        active;                  // 고른 칭호
    GuildTitleRow  rows[8];
};

enum class GuildExEvent : uint8_t
{
    LevelUp,         // value = 새 레벨, value2 = 새 인원 한도(결사원 모두)
    MissionProgress, // value = 임무 id, value2 = 진행(결사원 모두, 결사마다 2초에 한 번 모아서)
    MissionDone,     // value = 임무 id, value2 = 받은 인장, text = 임무 이름
    AchievementDone, // value = 업적 id, value2 = 연 칭호 id(0 = 없음), text = 업적 이름
    AttendTier,      // value = 출석 단계, value2 = 오늘 출석 수
    Seals,           // value = 바뀐 인장(+ 들어옴 / - 나감), value2 = 금고 합, text = 까닭, who = 산 사람
    Bought,          // who = 산 사람, value = 아이템 id, value2 = 수량
    Posted,          // who = 쓴 사람, value = 분류, text = 제목
    Applied,         // who = 신청자, value = 레벨(Approve 권한이 있는 결사원에게)
    ApplyResult,     // value = 1 승인 / 0 거절 · 2 만료, value2 = 결사 번호, text = 결사 이름(신청자에게)
    ClaimStarted,    // who = 청구한 사람, value = 기다리는 초
    ClaimCancelled,  // who = 돌아온 결사장
    ClaimDone,       // who = 새 결사장
    Pulse,           // value = 결사 누적 경험치, value2 = 내 이번 주 기여(바뀌었으면 3초마다 — 막대 · 떠오르는 +N)
    TitleChanged,    // value = 칭호 id, text = 칭호 이름
    PermsChanged,    // value = 직위, value2 = 권한 비트
    IntroChanged,    // who, value = 모집 중인가
    TitleUnlocked,   // value = 칭호 id, text = 칭호 이름
};
struct GUILD_EX_NFY_PACKET
{
    unsigned short size;
    char           type;
    uint8_t        event;                   // GuildExEvent
    int32_t        value;
    int32_t        value2;
    char           who[NAME_SIZE];
    char           text[31];
};

static_assert(sizeof(GUILD_EX_REQ_PACKET) == 215, "결사 성장 요청");
static_assert(sizeof(GUILD_EX_ACK_PACKET) == 13);
static_assert(sizeof(GUILD_EX_OVERVIEW_INF_PACKET) <= 255 && sizeof(GUILD_EX_MISSIONS_INF_PACKET) <= 255);
static_assert(sizeof(GUILD_EX_MEMBER_STATS_INF_PACKET) <= 255 && sizeof(GUILD_EX_LOG_INF_PACKET) <= 255);
static_assert(sizeof(GUILD_EX_BOARD_INF_PACKET) <= 255 && sizeof(GUILD_EX_POST_INF_PACKET) <= 255);
static_assert(sizeof(GUILD_EX_PUBLIC_INF_PACKET) <= 255 && sizeof(GUILD_EX_RECRUIT_INF_PACKET) <= 255);
static_assert(sizeof(GUILD_EX_APPLICANTS_INF_PACKET) <= 255 && sizeof(GUILD_EX_SHOP_INF_PACKET) <= 255);
static_assert(sizeof(GUILD_EX_ACH_INF_PACKET) <= 255 && sizeof(GUILD_EX_TITLES_INF_PACKET) <= 255);
static_assert(sizeof(GUILD_EX_NFY_PACKET) <= 255);

// ─────────────────────────────────────────────────────────────────────────────
// 경제 1차 제작 · 분해(112~113) — 대장장이(EnchantRule::kBlacksmithNpcId) 곁에서만. 요청에는 seq(멱등 — 강화와 같은 방식).
// 저장은 강화와 같은 「준비(메모리 변경 없이 계획 · 칸 잠금) → 한 DB 트랜잭션(P_ITEM_BATCH_TRADE) → 소유 Zone에서 완료」.
// 규칙 · 표는 Database/GameData/EconRules.h(CraftRecipe · Dismantle), 설계는 Shared/Document/ECONOMY.md.
// ─────────────────────────────────────────────────────────────────────────────
enum class CraftOp : uint8_t { Craft = 0, Dismantle = 1 };
enum class CraftResult : uint8_t
{
    Ok,
    Invalid,          // 요청이 잘못됨(없는 아이템 · 빈 칸 · 장비 아님 · 죽음 · 이동 중 · 모르는 op)
    NotAtBlacksmith,  // 대장장이 곁이 아니다(npcId 또는 거리)
    Busy,             // 강화 · 제작 · 경매 처리 중
    Duplicate,        // 이미 처리한 seq — 무시됨
    NoRecipe,         // 제작할 수 없는 아이템(route에 Craft 없음 · 레시피 없음) · 분해할 수 없는 등급(신화)
    LackMaterial,     // 재료가 모자란다
    LackAdena,        // 수수료(그로시)가 모자란다
    BagFull,          // 결과물이 들어갈 자리가 없다
    Equipped,         // 착용 중인 장비는 분해할 수 없다
    StorageError,     // DB 반영 실패 — 이 뒤 서버가 연결을 닫는다(강화와 같음)
};
struct CRAFT_REQ_PACKET
{
    unsigned short size;
    char     type;
    uint8_t  op;          // CraftOp
    uint32_t seq;         // 0이면 Duplicate. 마지막으로 처리한 seq보다 커야 한다
    uint16_t npcId;       // 대장장이
    uint16_t itemId;      // Craft: 만들 장비 id(Dismantle은 0)
    uint16_t slotIndex;   // Dismantle: 분해할 칸(Craft는 0)
};
struct CRAFT_ACK_PACKET
{
    unsigned short size;
    char     type;
    uint8_t  op;          // CraftOp
    uint8_t  result;      // CraftResult
    uint32_t seq;
    uint16_t itemId;      // Craft: 만든 장비 · Dismantle: 분해한 장비(실패면 요청 값)
    uint16_t slotIndex;   // Craft: 결과가 들어간 칸 · Dismantle: 비운 칸(실패면 요청 값)
    uint32_t fee;         // 낸 수수료(그로시 — Dismantle 0)
    uint16_t outItem[2];  // Dismantle: [장비 파편, 정수](0 = 없음) — Craft 0
    uint16_t outCount[2];
};
static_assert(sizeof(CRAFT_REQ_PACKET) == 14, "제작 · 분해 요청 14바이트(명세 §7)");
static_assert(sizeof(CRAFT_ACK_PACKET) == 25, "제작 · 분해 결과 25바이트(명세 §7)");

// ─────────────────────────────────────────────────────────────────────────────
// 개인 업적(121~123) — 진척은 서버가 센다(클라는 세지 않는다). 표(이름 · 설명 · 보상 · 분야 · 묶음)는 GameData Achievement.json.
// 규칙 · 색인은 Database/GameData/AchieveRules.h, 사람마다의 상태는 Content/Achieve/AchieveBook, 설계는 Shared/Document/ACHIEVEMENT.md.
//  · List(seq 없음 — 묻기, 사람마다 500ms에 한 번 · 넘치면 TooFast ACK) → Page 여러 장(category 거르기 · 30개씩 · 진척은 목표에서 자른다).
//  · Claim · ClaimAll은 바꾸는 요청 — 접속마다 1부터 늘어나는 seq(파티 순번 계약과 같다 — Duplicate ACK의 claimed 칸 = 마지막 순번).
// ─────────────────────────────────────────────────────────────────────────────
enum class AchieveOp : uint8_t { List = 0, Claim = 1, ClaimAll = 2 };
enum class AchieveResult : uint8_t
{
    Ok,
    Invalid,          // 모르는 op · 없는 업적 · 분야 값 · 죽음 · 이동 중
    NotDone,          // 아직 달성하지 않았다
    AlreadyClaimed,   // 이미 받았다
    BagFull,          // 가방(또는 지갑 상한)이 모자라다 — 아무것도 받지 않았다
    Busy,             // 강화 · 제작 · 경매 · 교환 진행 중(칸 잠금)
    Duplicate,        // 이미 받은 seq — claimed 칸 = 이 접속에서 마지막으로 받은 seq
    TooFast,          // List 간격(500ms)
    NothingToClaim,   // ClaimAll: 받을 것이 없다
};
enum class AchieveInfKind : uint8_t { Summary = 0, Page = 1, Completed = 2, Claimed = 3 };
struct ACHIEVE_REQ_PACKET
{
    unsigned short size;
    char     type;
    uint8_t  op;          // AchieveOp
    uint32_t seq;         // Claim · ClaimAll(List는 보지 않는다)
    uint16_t achId;       // Claim
    uint8_t  category;    // List: AchCategory(0~5) · 0xFF = 모두
};
struct ACHIEVE_ACK_PACKET
{
    unsigned short size;
    char     type;
    uint8_t  op;          // AchieveOp
    uint8_t  result;      // AchieveResult
    uint32_t seq;
    uint16_t achId;       // Claim: 요청 값
    uint16_t claimed;     // 받은 업적 수(Claim 1 · ClaimAll n) · Duplicate면 마지막 seq(아래 16비트)
    uint32_t adena;       // 받은 그로시(ClaimAll은 합)
    uint16_t itemId;      // 받은 아이템(ClaimAll은 첫 것)
    uint16_t itemCount;
};
struct ACHIEVE_ENTRY
{
    uint16_t id;
    uint32_t progress;    // 진척(목표에서 자른다)
    uint8_t  state;       // 0 진행 · 1 달성(받기 전) · 2 받음
};
struct ACHIEVE_INF_PACKET
{
    unsigned short size;
    char     type;
    uint8_t  kind;        // AchieveInfKind
    uint32_t points;      // 업적 점수(달성한 업적 points 합)
    uint16_t done;        // 달성한 수(받음 포함)
    uint16_t total;       // 표의 업적 수
    uint16_t claimable;   // 달성했고 아직 받지 않은 수
    uint8_t  page;        // Page · Claimed: 몇째 장(0부터)
    uint8_t  pageCount;
    uint8_t  count;       // entries 줄 수(Summary 0 · Completed 1)
    ACHIEVE_ENTRY entries[30];
};
static_assert(sizeof(ACHIEVE_REQ_PACKET) == 11, "업적 요청 11바이트(명세 §3)");
static_assert(sizeof(ACHIEVE_ACK_PACKET) == 21, "업적 응답 21바이트(명세 §3)");
static_assert(sizeof(ACHIEVE_ENTRY) == 7, "업적 줄 7바이트(명세 §3)");
static_assert(sizeof(ACHIEVE_INF_PACKET) == 227, "업적 알림 227바이트(명세 §3)");

#pragma pack (pop)

// ── 패킷 크기 상한 ─────────────────────────────────────────────────────────────
template <typename... _Packets>
constexpr size_t MaxSizeOf() noexcept { return std::max({ sizeof(_Packets)... }); }

// 받는 쪽 버퍼(Client → Server). 요청을 더하면 여기에도 더한다 — Route가 타입마다 정확한 크기를 검사한다.
#include "Protocol/PlayerSocial.h"
constexpr size_t MAX_CLIENT_PACKET_SIZE = MaxSizeOf<
    USER_LOGIN_REQ_PACKET, USER_MOVE_REQ_PACKET, USER_RESTART_REQ_PACKET, STAT_ALLOC_REQ_PACKET,
    SKILL_REQ_PACKET, SKILL_GROW_REQ_PACKET,
    ITEM_EQUIP_REQ_PACKET, ITEM_UNEQUIP_REQ_PACKET, ITEM_SWAP_REQ_PACKET, ITEM_DISCARD_REQ_PACKET, ITEM_PICKUP_REQ_PACKET,
    ITEM_USE_REQ_PACKET, ITEM_SORT_REQ_PACKET, ITEM_ENCHANT_REQ_PACKET,
    SHOP_BUY_REQ_PACKET, SHOP_SELL_REQ_PACKET, NPC_TELEPORT_REQ_PACKET, NPC_TALK_REQ_PACKET, NPC_SERVICE_REQ_PACKET,
    QUEST_ACCEPT_REQ_PACKET, QUEST_COMPLETE_REQ_PACKET, QUEST_ABANDON_REQ_PACKET,
    CS_CHAT_PACKET, GUILD_REQ_PACKET, AUCTION_REQ_PACKET, PARTY_REQ_PACKET, GUILD_EX_REQ_PACKET, CRAFT_REQ_PACKET, TRANSFER_REQ_PACKET, SOCIAL_REQ_PACKET,
    ACHIEVE_REQ_PACKET>();

// 보내는 쪽 한 패킷(Server → Client). 알림을 더하면 여기에도 더한다.
constexpr size_t MAX_SERVER_PACKET_SIZE = MaxSizeOf<
    USER_LOGIN_ACK_PACKET, USER_LOGIN_FAIL_ACK_PACKET, SERVER_CONFIG_INF_PACKET,
    USER_MOVE_ACK_PACKET, SUBJECT_ADD_NFY_PACKET, SUBJECT_MOVE_NFY_PACKET, SUBJECT_REMOVE_NFY_PACKET,
    SUBJECT_FORCED_MOVE_NFY_PACKET, SUBJECT_INFO_NFY_PACKET, SUBJECT_EQUIP_CHANGE_NFY_PACKET, PLAYER_LOOK_NFY_PACKET,
    USER_INFO_INF_PACKET, USER_STATS_INF_PACKET, USER_RESOURCE_INF_PACKET, USER_HEAL_INF_PACKET, USER_STAT_CHANGE_INF_PACKET,
    SUBJECT_DIE_NFY_PACKET, SUBJECT_RESPAWN_NFY_PACKET, SYSTEM_MESSAGE_INF_PACKET,
    SKILL_NFY_PACKET, SKILL_RESULT_ACK_PACKET, STATUS_NFY_PACKET, SUBJECT_DAMAGE_NFY_PACKET, EFFECT_NFY_PACKET,
    COMBAT_GAUGE_INF_PACKET, MONSTER_TELEGRAPH_NFY_PACKET, MONSTER_GAUGE_NFY_PACKET, SKILL_TREE_INF_PACKET, SKILL_GROW_ACK_PACKET,
    ITEM_EQUIP_ACK_PACKET, ITEM_UNEQUIP_ACK_PACKET, ITEM_SWAP_ACK_PACKET, ITEM_DISCARD_ACK_PACKET, ITEM_PICKUP_ACK_PACKET,
    ITEM_USE_ACK_PACKET, ITEM_LIST_ACK_PACKET, ITEM_SLOT_INF_PACKET, ITEM_DETAIL_INF_PACKET, ADENA_INF_PACKET,
    ENCHANT_ACK_PACKET, ITEM_ENCHANT_LEVEL_INF_PACKET, ENCHANT_NOTICE_INF_PACKET, SUBJECT_ENCHANT_LEVEL_INF_PACKET,
    ENCHANT_GRACE_INF_PACKET, SHOP_RESULT_ACK_PACKET, NPC_LIST_NFY_PACKET, NPC_STATE_NFY_PACKET, NPC_BARK_NFY_PACKET,
    NPC_TALK_ACK_PACKET, NPC_SERVICE_ACK_PACKET, QUEST_LIST_INF_PACKET, QUEST_UPDATE_INF_PACKET, QUEST_RESULT_ACK_PACKET,
    SC_CHAT_PACKET, WORLD_NOTICE_INF_PACKET, GUILD_ACK_PACKET, GUILD_INFO_INF_PACKET, GUILD_MEMBERS_INF_PACKET,
    GUILD_NAME_INF_PACKET, GUILD_TOP_INF_PACKET, GUILD_NFY_PACKET, AUCTION_ACK_PACKET, AUCTION_LIST_INF_PACKET, AUCTION_NFY_PACKET,
    AUCTION_ENCHANT_LEVEL_INF_PACKET, PARTY_ACK_PACKET, PARTY_ROSTER_INF_PACKET, PARTY_MEMBERS_INF_PACKET,
    PARTY_RECRUIT_LIST_INF_PACKET, PARTY_APPLICANTS_INF_PACKET, PARTY_NFY_PACKET,
    GUILD_EX_ACK_PACKET, GUILD_EX_OVERVIEW_INF_PACKET, GUILD_EX_MISSIONS_INF_PACKET, GUILD_EX_MEMBER_STATS_INF_PACKET, GUILD_EX_LOG_INF_PACKET,
    GUILD_EX_BOARD_INF_PACKET, GUILD_EX_POST_INF_PACKET, GUILD_EX_PUBLIC_INF_PACKET, GUILD_EX_RECRUIT_INF_PACKET, GUILD_EX_APPLICANTS_INF_PACKET,
    GUILD_EX_SHOP_INF_PACKET, GUILD_EX_ACH_INF_PACKET, GUILD_EX_TITLES_INF_PACKET, GUILD_EX_NFY_PACKET, CRAFT_ACK_PACKET,
    TRANSFER_ACK_PACKET, TRANSFER_STATE_PACKET, TRANSFER_ITEM_PACKET, SOCIAL_ACK_PACKET, SOCIAL_ENTRY_PACKET, ACHIEVE_ACK_PACKET, ACHIEVE_INF_PACKET>();
static_assert(MAX_SERVER_PACKET_SIZE <= 255 && MAX_CLIENT_PACKET_SIZE <= 255, "모든 패킷 255바이트 이하");
