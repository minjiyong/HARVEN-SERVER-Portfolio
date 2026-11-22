#pragma once
#include <array>
#include <vector>

#include "Entity/Subject/Subject.h"
#include "Content/Inventory/Inventory.h"
#include "Content/Achieve/AchieveBook.h"
#include "Database/GameData/LevelTable.h"
#include "Database/GameData/CombatData.h"
#include "Database/GameData/ItemRules.h"
#include "Database/GameData/QuestData.h"
#include "Database/GameData/SkillGrowth.h"
#include "Content/Skill/UserSkillScheduler.h"

class CUser : public CSubject
{
public:
    std::shared_ptr<struct PlayerTransfer> m_Transfer;
    UInt32 m_LastTransferSeq = 0, m_LastTransferRequestAt = 0;
    UInt32 m_PendingGuildEconomy = 0; // 결사 생성/상점의 Zone 콜백·DB 완료까지
    using SharedPtr = UserSharedPtr;
    using WeakPtr = UserWeakPtr;

public:
    UInt32 m_LastPotionUseTime = 0;

    // ── 리니지풍 컨텐츠 확장 ──────────────────────────────────────────────────
    // 아래 값들은 전부 소유 Zone 스레드에서만 읽고 쓴다(CInventory와 같은 전제).
    UInt32 m_LastScrollUseTime = 0;   // 두루마리/바람걸음 물약 쿨타임(물약과 별개)
    UInt32 m_LastEnchantSeq = 0;
    UInt32 m_LastCraftSeq      = 0;   // 제작 · 분해(CRAFT_REQ) 멱등성 — 강화와 같은 방식
    UInt32 m_LastShopSeq       = 0;   // 상점/텔레포트 요청 멱등성 — 마지막으로 처리한 seq
    UInt32 m_PurpleUntil       = 0;   // GetNowTime() 기준. 이 시각 전까지 적대
    UInt32 m_HasteUntil        = 0;   // GetNowTime() 기준. 이 시각 전까지 가속
    UInt32 m_DeathSeq          = 0;   // 사망할 때마다 증가 — 늦게 도착한 부활 타이머를 걸러낸다
    UInt32 m_LastWorldChatTime = 0;   // 전체 채팅 도배 방지
    // Phase 3-B 채팅(Content/ChatHelper) — 소유 Zone 스레드만
    UInt32 m_LastTradeChatTime = 0;   // 거래 채팅 재사용 대기
    UInt32 m_ChatRefillAt      = 0;   // 토큰 버킷: 마지막으로 채운 시각
    UInt32 m_ChatLastHash      = 0;   // 되풀이: 마지막 문장의 해시 · 시각 · 연속 횟수
    UInt32 m_ChatLastAt        = 0;
    UInt8  m_ChatTokens        = 5;
    UInt8  m_ChatRepeat        = 0;
    // 마지막으로 DB에 저장한 성향치. 사냥할 때마다 1~20씩 오르므로 매번 저장하지 않고
    // 일정 폭 이상 변했을 때/PK/로그아웃 때만 저장한다(CContentHelper::SaveAlignment).
    Int16  m_SavedAlignment    = 0;

    // ── V2 이동(토큰 버킷, Data/MoveRule.h) ───────────────────────────────────
    UInt32 m_MoveClock         = 0;   // 마지막으로 허용한 걸음이 "끝나는" 가상 시각

    // ── V2 전투(직업·능력치·스킬) — Data/CombatData.h, Combat/* ────────────────
    ClassId      m_eClassId       = ClassId::Knight;
    BaseStats    m_Alloc{};                 // 내가 찍은 능력치 포인트
    GearBonus    m_Gear{};                  // 장비 보너스(RecomputeStats가 채움)
    DerivedStats m_Derived{};               // 전투 능력치(RecomputeStats가 채움)
    UInt16       m_Mp            = 0;
    UInt32       m_LastCounterAt = 0;           // 철벽 태세 반격 간격
    UInt32       m_LastCombatAt  = 0;           // 마지막 전투 시각(비전투 회복)
    // ── Phase 2-B 기술(REIW 기술 수명 — Content/Skill/UserSkillHelper · UserSkillScheduler) — 소유 Zone 스레드 ──
    // 진행 중인 시전 · 일정 · 칸별 재사용 대기 · 콤보/체인 입력 창(REIW CSkillScheduler · CSkillSequence)
    CUserSkillScheduler m_SkillScheduler;
    [[nodiscard]] CUserSkillScheduler& GetSkillScheduler() noexcept { return m_SkillScheduler; }
    UInt16     m_Identity      = 0;           // 고유 기술 게이지 0~1000(서약 · 새김 · 그늘)
    UInt16     m_IdentitySent  = 0xFFFF;
    UInt32     m_SuperArmorUntil = 0;         // 경직 면역(몬스터의 기절 · 빙결을 받지 않는다)
    UInt32     m_InvulnUntil   = 0;           // 무적(이동기 · 각성기)

    // ── Phase 2-D 기술 성장(Data/SkillGrowth.h, SkillGrow::HandleRequest) — DB PlayerSkills ──
    std::array<UInt8, SkillGrowth::kIdSpan> m_SkillGrow{};   // 기술 id → SkillGrowth::Pack(0 = 레벨 1 · 갈래 없음)
    UInt16       m_LastGrowSeq    = 0;
    bool         m_bGrowSeqSeen   = false;       // 첫 요청 전에는 seq 비교를 하지 않는다
    [[nodiscard]] SkillGrowth::State GrowState(UInt16 skillId) const noexcept
    {
        return skillId < SkillGrowth::kIdSpan ? SkillGrowth::Unpack(m_SkillGrow[skillId]) : SkillGrowth::State{};
    }

    // ── V2 아이템 성장(Data/ItemRules.h) ───────────────────────────────────────
    GearExtras   m_GearExtras{};                // 착용 장비의 특수 효과·기술 강화·획득 보너스(RecomputeStats가 채움)
    UInt16       m_PityRare      = 0;       // 희귀를 못 본 필드 장비 수(천장 LootConst.rare_pity — CLootHelper::RollFieldGear)
    UInt16       m_PityHeroic    = 0;       // 영웅 이상을 못 본 정예 · 보스 장비 수(천장 heroic_pity — RollEliteGear · RollBossLoot)
    UInt32       m_LastSortAt    = 0;       // 소지품 정리 도배 방지
    UInt32       m_GuardianReadyAt = 0;     // 서약의 방패(특수 효과) 재발동 시각
    UInt32       m_LifestealFxAt = 0;       // 생명 흡수 연출 간격

    // ── V2 마을 NPC(Npc/NpcHelper) ─────────────────────────────────────────────
    UInt32     m_NpcSeenTick   = 0;   // 마지막으로 NPC 틱이 이 사용자를 마을에서 본 번호(새로 오면 목록을 보낸다)
    UInt32     m_NpcHealReadyAt = 0;  // 사제 기도 재사용
    UInt32     m_NpcSoupReadyAt = 0;  // 여관 수프 재사용

    // ── V2 퀘스트(Data/QuestData.h, Quest/QuestHelper) — 진행 데이터 v3에 저장 ────────
    struct QuestSlot
    {
        UInt16 id = 0;                                   // 0 = 빈 칸
        std::array<UInt16, QuestData::kMaxObjs> progress{};
        std::array<UInt8, QuestData::kMaxObjs> misses{}; // 수집 실패 횟수(운 보정, 저장하지 않는다)
    };
    std::array<QuestSlot, QuestData::kMaxActive> m_Quests{};
    std::array<UInt8, 32> m_QuestDone{};                  // 끝낸 퀘스트(한 번뿐인 것) id 비트
    struct QuestCooldown { UInt16 id; UInt32 readyAtUnix; };
    std::vector<QuestCooldown> m_QuestCooldowns;            // 반복 퀘스트: 다시 받을 수 있는 시각(벽시계)

    // ── Phase 5 결사 · 경매장(Content/Guild/GuildService · Content/Auction/AuctionService) ────────
    // 머리 위 결사 표시: 서비스가 소유 Zone 큐로 넘겨 바꾸고, 시야 경계의 다른 Zone 스레드도 읽으므로 atomic.
    std::atomic<UInt32> m_GuildId{ 0 };
    std::atomic<UInt8>  m_GuildEmblem{ 0 };
    UInt8      m_GuildRank     = 3;      // 소유 Zone만(표시용 사본 — 권한 판정은 CGuildService)
    // 멱등: 마지막으로 받은 변경 요청의 순번(소유 Zone). 재접속하면 CUser가 새로 만들어져 0부터
    UInt32     m_LastGuildSeq  = 0;
    UInt32     m_LastSocialAt = 0;
    UInt32     m_LastAuctionSeq = 0;
    std::unordered_set<UInt32> m_PendingAuctionSeqs; // 소유 Zone: 강화와 미확정 경매 슬롯 변경의 겹침 방지
    UInt32     m_LastAuctionQueryAt = 0; // 검색 · 목록 요청 간격
    UInt32     m_LastGuildTopAt = 0;     // 깃발 뜰 순위(GuildOp::Top) 요청 간격
    // 결사 성장(GUILD_EX_REQ): 멱등 순번 · 묻기 간격 · 글 쓰기 간격(소유 Zone)
    UInt32     m_LastGuildExSeq = 0;
    UInt32     m_LastGuildExQueryAt = 0;
    UInt32     m_LastGuildPostAt = 0;

    // ── 파티(Content/Party/PartyService · PartyHelper) ────────────────────────────────────────────
    // 파티 번호: 서비스가 소유 Zone 큐로 넘겨 바꾼다(PostToOwner). 경험치 나눔 · 줍기 우선권 · 파티 채팅 · 파티원 상태 모으기가 읽는다.
    // 판정의 기준은 CPartyService — 이 값은 소유 Zone의 사본(늦어도 한 번의 Zone 작업 뒤 맞는다).
    std::atomic<UInt32> m_PartyId{ 0 };
    UInt32     m_LastPartySeq       = 0;   // 멱등: 마지막으로 받은 변경 요청의 순번(소유 Zone)
    UInt32     m_PartyApplySeq      = 0;   // 서비스가 파티 번호를 넘긴 순번 — Zone을 옮기는 사이 늦게 도착한 옛 값이 새 값을 덮지 않게
    UInt32     m_LastPartyQueryAt   = 0;   // 목록 · 구성 · 신청자 묻기 간격
    UInt32     m_LastPartyTargetAt  = 0;   // 대상 공유 간격
    // 마지막으로 서비스에 보낸 내 상태(바뀐 것만 보낸다 — CPartyHelper::HandleZoneTick). bSent = false면 다음 틱에 꼭 보낸다
    struct PartySent
    {
        bool   bSent = false;
        UInt8  level = 0, flags = 0;
        UInt16 zone = 0;
        Int16  x = 0, y = 0;
        UInt16 hp = 0, maxHp = 0, mp = 0, maxMp = 0;
        [[nodiscard]] bool operator==(const PartySent&) const = default;
    };
    PartySent  m_PartySent{};

    // ── 개인 업적(Content/Achieve/AchieveHelper · AchieveBook) — 소유 Zone 스레드. 진행 기록 판 4에 저장 ──
    AchieveBook m_Achieve;
    UInt32     m_LastAchieveSeq     = 0;   // 멱등: 마지막으로 받은 Claim · ClaimAll 순번(접속마다 0부터 — 파티와 같은 계약)
    UInt32     m_LastAchieveListAt  = 0;   // List 묻기 간격(AchieveRule::kListIntervalMs)
    bool       m_AchieveSaveQueued  = false;   // 더러움 저장 예약(EV_ACHIEVE_SAVE)이 걸려 있다
    UInt8      m_LoadedProgressVersion = 0; // 로그인 때 읽은 진행 기록 판(0 = 없음 · V2 아님) — 판 4 미만일 때만 「가장 높았던」 소급(강화)

private:
    SessionSharedPtr m_pSession;
    // CMonster는 인벤토리가 없으므로 CSubject가 아닌 CUser에 둔다 (m_pSession과 같은 이유).
    CInventory::SharedPtr m_pInventory;
    // Players.playerId(DB 발급 정수 PK). 로그인/계정생성 시 DB 스레드가 한 번 조회해
    // CUserHelper::HandleGetUserInfo/HandleAddUserInfo에서 곧바로 세팅해준다 —
    // 이후 CInventory 저장 경로는 매번 name으로 재조회하지 않고 이 값을 그대로 쓴다.
    int m_PlayerId = 0;

    Int16  m_Alignment = 0;   // 성향치(명예 +, 악명 -)
    UInt16 m_PkCount   = 0;

public:
    CUser() = default;
    ~CUser() = default;

    virtual void InitInstance() override;

    [[nodiscard]] SessionSharedPtr GetGameSession() const;
    void SetGameSession(const SessionSharedPtr& session);

    // GetStat()과 동일하게 shared_ptr을 복사해서 반환한다.
    [[nodiscard]] CInventory::SharedPtr GetInventory() noexcept { return m_pInventory; }

    [[nodiscard]] int GetPlayerId() const noexcept { return m_PlayerId; }
    void SetPlayerId(int playerId) noexcept { m_PlayerId = playerId; }


    [[nodiscard]] Int16  GetAlignment() const noexcept { return m_Alignment; }
    [[nodiscard]] UInt16 GetPkCount() const noexcept { return m_PkCount; }
    void SetAlignment(Int16 alignment) noexcept { m_Alignment = alignment; }
    void SetPkCount(UInt16 pkCount) noexcept { m_PkCount = pkCount; }

    [[nodiscard]] bool IsPurple(UInt32 now) const noexcept { return now < m_PurpleUntil; }
    [[nodiscard]] bool IsHasted(UInt32 now) const noexcept { return now < m_HasteUntil; }
    // 이동속도(%). 100 = 기본. 가속·(이후) 스탯/장비/상태이상이 더해진다.
    [[nodiscard]] UInt32 GetMoveSpeedPercent(UInt32 now) const noexcept;


    // V2: 직업·레벨·포인트·장비로 전투 능력치를 다시 계산한다.
    // 바뀌었으면 true.
    bool RecomputeStats(bool fullHeal);
    [[nodiscard]] UInt16 FreePoints() const noexcept;
    // V2 진행 데이터 직렬화(DB PlayerProgress). 알 수 없는 버전/깨진 데이터는 기본값으로 둔다.
    [[nodiscard]] std::vector<UInt8> SerializeProgress() const;
    bool DeserializeProgress(const std::vector<UInt8>& data);   // false = V2 기록 없음(기존 캐릭터의 첫 V2 접속)
    [[nodiscard]] BaseStats TotalBase() const noexcept;
};
