#pragma once

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Content/Guild/GuildProgress.h"
#include "Content/ServiceCommon.h"

class CDBConnection;
class CUser;
struct GUILD_REQ_PACKET;
struct GUILD_EX_REQ_PACKET;

// ─────────────────────────────────────────────────────────────────────────────
// Phase 5 결사(길드) — 규칙 Database/GameData/GuildAuction.h
//
//  CGuildService: 결사 · 결사원 · 초대의 주인. 전용 스레드 하나(작업 큐)만 상태를 만진다(락 없음).
//   · 이름 겹침 · 인원 · 직위 · 권한은 서비스 메모리가 기준이다(DB UNIQUE는 뒷문).
//   · DB 쓰기는 먼저 하고(결과 콜백) 커밋되면 메모리에 반영한다 — 메모리와 DB가 어긋나지 않는다.
//     세우기 = 만드는 사람 샤드(그로시와 한 트랜잭션), 나머지 = 결사 번호 샤드(한 결사의 변경이 넣은 순서대로).
//   · 한 사람의 결사 변경(세우기 · 들어옴 · 나감)은 한 번에 하나(m_BusyPlayers) — 샤드가 달라도 순서가 뒤집히지 않는다.
//   · 한 결사의 구조 변경(추방 · 직위 · 넘기기 · 공지 · 문장 · 해산)도 한 번에 하나(Guild::busy). 들어옴은 여러 건 동시에.
//
//  결사 성장(Shared/Document/GUILD.md — 레벨 · 기여도 · 주간 임무 · 게시판 · 기록 · 직위 권한 · 위임 청구 · 금고 · 상점 · 업적 · 출석 · 모집):
//   · 진행(GuildProgress)은 결사마다 메모리가 기준 — 바뀌면 dirty, 몇 초 모아 결사 번호 샤드에 한 덩어리로 저장한다(설정 · 권한은 바로).
//   · 금고(결사 인장)는 DB ±를 먼저 하고 커밋되면 메모리에(보상 = 결사 번호 샤드, 상점 = 사는 사람 샤드에서 소지품과 한 트랜잭션).
//   · 사냥 · 전리품 · 퀘스트는 Zone이 Post로 알린다(결사마다 한 번 — 함께 때린 결사원을 묶어서). 규칙은 GuildModel(순수 함수).
//
//  CGuildHelper: 요청한 사람의 Zone 스레드 쪽(검사 → 서비스 큐), 머리 위 표시 · 결사 채팅 전달.
// ─────────────────────────────────────────────────────────────────────────────
class CGuildService
{
public:
    using Task = CGameLogicThread::Task;

    CGuildService();

    // 켤 때(메인 스레드 · 접속을 받기 전): DB의 결사 · 결사원으로 메모리를 만든다. maxGuildId = DB 최댓값
    bool LoadFromDB(CDBConnection& db, int maxGuildId);
    void Run();                              // 서비스 스레드
    void Post(Task task);                    // 아무 스레드 → 서비스 큐

    // ── 아래는 전부 서비스 스레드(Post 안)에서만 ──
    void Tick();                             // 1초마다 — 초대 만료
    void OnLogin(const Service::Ctx& ctx);
    void OnLogout(int playerId, Int64 nowUnix);
    void OnLevel(int playerId, UInt8 level);
    void Handle(const Service::Ctx& ctx, const GUILD_REQ_PACKET& p);
    void RelayChat(int playerId, const ObjID& senderObj, const std::string& senderName, const std::string& body);
    // ── 결사 성장(GuildGrowthService.cpp) ──
    bool LoadGrowth(CDBConnection& db);      // LoadFromDB 끝(메인 스레드 · 서비스 시작 전)
    void HandleEx(const Service::Ctx& ctx, const GUILD_EX_REQ_PACKET& p);
    void OnGuildKill(UInt32 guildId, const GuildModel::KillIn& kill);
    void OnGuildLoot(UInt32 guildId, int playerId, UInt16 itemId, UInt16 count, bool material, bool equipment, UInt8 rarity);
    void OnGuildQuest(UInt32 guildId, int playerId);

    [[nodiscard]] size_t GuildCount() const noexcept { return m_Guilds.size(); }
    [[nodiscard]] size_t MemberCount() const noexcept { return m_GuildOf.size(); }

private:
    struct Member
    {
        int         playerId = 0;
        std::string name;
        UInt8       rank = 3;
        UInt8       level = 1;
        UInt8       classId = 0;
        UInt32      lastSeenMin = 0;          // 유닉스 분(나간 때)
        bool        online = false;
        ObjID       obj{};
        SessionWeakPtr session;
    };
    struct Applicant
    {
        Service::Ctx ctx;                     // 신청한 사람(승인되면 이 값으로 결사원을 만든다)
        UInt32       at = 0;                  // 유닉스 초
    };
    // 결사 성장 — 결사마다(진행 덩어리 + 금고 + 메모리만의 것)
    struct Growth
    {
        GuildProgress                  p;
        UInt32                         seals = 0;
        UInt32                         sealsReserved = 0;   // 상점 구매가 DB에 가 있는 몫
        Int64                          createdAt = 0;
        bool                           dirty = false;       // 진행 덩어리를 다시 저장해야 한다
        bool                           urgent = false;      // 설정 · 권한 · 청구 — 다음 틱에 바로
        bool                           saving = false;      // 저장이 DB에 가 있다(겹쳐 보내지 않는다)
        UInt32                         dirtyAt = 0;         // GetNowTime
        bool                           missionDirty = false;
        UInt32                         missionSentAt = 0;
        UInt32                         pulseAt = 0;
        UInt32                         pulseExp = 0;        // 마지막으로 알린 결사 경험치
        std::unordered_set<int>        pulse;               // 기여가 바뀐 결사원
        std::vector<DB_GUILD_LOG_ROW>  logs;                // 최근 log_keep줄(오래된 것부터)
        std::vector<DB_GUILD_POST_ROW> posts;               // 넣은 차례
        int                            pendingPosts = 0;
        std::vector<Applicant>         applicants;          // 메모리만(서버를 다시 켜면 사라진다)
        std::unordered_set<int>        shopBusy;            // 상점 구매가 DB에 가 있는 사람
        bool                           claimBusy = false;   // 위임 청구의 넘기기가 DB에 가 있다
        // 이번 주에 나간 결사원의 한도 몫(메모리만 — 주가 넘어가면 비운다). 다시 들어오면 잇는다(GuildModel::Rejoin)
        std::unordered_map<int, GuildModel::Departed> departed;
    };
    struct Guild
    {
        UInt32              id = 0;
        std::string         name;
        std::string         key;              // 겹침 판정용(ASCII 소문자)
        UInt8               emblem = 0;
        std::string         notice;
        int                 leaderId = 0;
        std::vector<Member> members;
        int                 pendingJoins = 0; // DB에 넣는 중인 들어옴(인원 한도에 센다)
        bool                busy = false;     // 구조 변경 하나가 DB에 가 있다
        Growth              grow;
    };
    struct Invite
    {
        UInt32      guildId = 0;
        int         inviterId = 0;
        std::string inviterName;
        UInt32      expiresAt = 0;            // GetNowTime
    };

    [[nodiscard]] static std::string NameKey(const std::string& name);
    [[nodiscard]] Guild* GuildOf(int playerId);
    [[nodiscard]] static Member* FindMember(Guild& g, int playerId);
    [[nodiscard]] static Member* FindMemberByName(Guild& g, const std::string& name);

    void Ack(const Service::Ctx& ctx, GuildOp op, GuildResult r, UInt32 seq, UInt32 param = 0) const;
    void SendInfo(const SessionWeakPtr& session, const Guild* g, int playerId) const;
    void SendMembers(const Service::Ctx& ctx, const Guild& g, UInt8 page, UInt8 sort) const;   // sort: 0 접속·직위 · 1 레벨 · 2 이름 · 3 최근 접속
    void SendName(const Service::Ctx& ctx, UInt32 guildId) const;
    // 깃발 뜰: 인원 많은 순(같으면 번호가 작은 = 먼저 세운) 결사 kHallTop개 — 메모리만 읽는다(DB 없음)
    void SendTop(const Service::Ctx& ctx) const;
    // 온라인 결사원 모두에게(except = 빼고 보낼 사람)
    void NotifyMembers(const Guild& g, GuildEvent ev, const std::string& who, const std::string& by, UInt8 rank = 0, int except = 0) const;
    // 그 사람의 Zone에서 머리 위 표시를 바꾸고 주변에 알린다(guildId 0 = 결사 없음)
    static void ApplyTag(const ObjID& obj, UInt32 guildId, UInt8 emblem, UInt8 rank);

    // 요청별(서비스 스레드)
    void Create(const Service::Ctx& ctx, UInt32 seq, const std::string& name, UInt8 emblem);
    void FinishCreate(bool ok, const Service::Ctx& ctx, UInt32 seq, UInt32 id, const std::string& name, const std::string& key, UInt8 emblem);
    void DoInvite(const Service::Ctx& ctx, UInt32 seq, const std::string& target);
    void Reply(const Service::Ctx& ctx, UInt32 seq, bool accept, UInt32 guildId);
    void FinishJoin(bool ok, const Service::Ctx& ctx, UInt32 seq, UInt32 guildId);
    void Leave(const Service::Ctx& ctx, UInt32 seq);
    void Kick(const Service::Ctx& ctx, UInt32 seq, const std::string& target);
    void FinishRemove(bool ok, const Service::Ctx& ctx, UInt32 seq, UInt32 guildId, int targetId, bool kicked);
    void SetRank(const Service::Ctx& ctx, UInt32 seq, const std::string& target, UInt8 rank);
    void Transfer(const Service::Ctx& ctx, UInt32 seq, const std::string& target);
    void SetNotice(const Service::Ctx& ctx, UInt32 seq, const std::string& text);
    void SetEmblem(const Service::Ctx& ctx, UInt32 seq, UInt8 emblem);
    void Disband(const Service::Ctx& ctx, UInt32 seq);
    // 들어온 결사원을 메모리에(초대 수락 · 가입 신청 승인 — DB가 커밋된 뒤)
    Member& AddMember(Guild& g, const Service::Ctx& who);
    // 접속 중이면 지금 세션의 ctx, 아니면 who 그대로(DB를 기다리는 사이 다시 접속한 사람 — 옛 세션 · 옛 ObjID로 알리지 않게)
    [[nodiscard]] const Service::Ctx& CurrentCtx(const Service::Ctx& who) const;
    // 이 사람의 가입 신청(한 사람에 하나)을 거둔다 — 결사에 들어가거나 스스로 세울 때 · 거두기
    void WithdrawApply(int playerId);

    // ── 결사 성장(GuildGrowthService.cpp) ──
    [[nodiscard]] static UInt8 MaxMembers(const Guild& g) noexcept;          // 결사 레벨의 인원 한도
    [[nodiscard]] static bool  HasPerm(const Guild& g, UInt8 rank, GuildPerm perm) noexcept;
    void   GrowthTick(UInt32 nowMs, Int64 nowUnix);
    void   InitGrowth(Guild& g, Int64 nowUnix, bool fresh);                  // 세우기 · 불러오기 뒤
    void   MarkDirty(Guild& g, bool urgent = false);
    void   SaveGrowth(Guild& g);
    void   AddLog(Guild& g, GuildLogKind kind, Int32 value, const std::string& who, const std::string& what, bool persist = true);
    void   ApplyEffects(Guild& g, GuildModel::Effects& fx);
    void   AddSeals(Guild& g, const GuildModel::Seal& seal);
    void   NotifyEx(const Guild& g, GuildExEvent ev, Int32 value, Int32 value2, const std::string& who, const std::string& text,
                    UInt8 permFilter = 0, int except = 0) const;
    void   NotifyOne(const SessionWeakPtr& session, GuildExEvent ev, Int32 value, Int32 value2, const std::string& who, const std::string& text) const;
    void   AckEx(const Service::Ctx& ctx, GuildExOp op, GuildExResult r, UInt32 seq, UInt32 param = 0) const;
    void   SendOverview(const SessionWeakPtr& session, const Guild* g, int playerId) const;
    void   SendMissions(const Service::Ctx& ctx, const Guild& g) const;
    void   SendStats(const Service::Ctx& ctx, const Guild& g, UInt8 page, UInt8 sort) const;
    void   SendLog(const Service::Ctx& ctx, const Guild& g, UInt8 page, UInt8 filter) const;
    void   SendBoard(const Service::Ctx& ctx, const Guild& g, UInt8 page, UInt8 category) const;
    void   SendPost(const Service::Ctx& ctx, const Guild& g, UInt32 postId) const;
    void   SendPublic(const Service::Ctx& ctx, UInt32 guildId) const;
    void   SendRecruit(const Service::Ctx& ctx, UInt8 page) const;
    void   SendApplicants(const Service::Ctx& ctx, const Guild& g) const;
    void   SendShop(const Service::Ctx& ctx, const Guild& g, UInt8 page) const;
    void   SendAch(const Service::Ctx& ctx, const Guild& g, UInt8 page) const;
    void   SendTitles(const Service::Ctx& ctx, const Guild& g, UInt8 page) const;
    void   PostWrite(const Service::Ctx& ctx, UInt32 seq, UInt8 category, const std::string& title, const std::string& body);
    void   PostDelete(const Service::Ctx& ctx, UInt32 seq, UInt32 postId);
    void   PostPin(const Service::Ctx& ctx, UInt32 seq, UInt32 postId, bool pin);
    void   SetIntro(const Service::Ctx& ctx, UInt32 seq, const std::string& text, bool recruiting, UInt8 minLevel);
    void   Apply(const Service::Ctx& ctx, UInt32 seq, UInt32 guildId);
    void   ApplyCancel(const Service::Ctx& ctx, UInt32 seq);
    void   ApplyReply(const Service::Ctx& ctx, UInt32 seq, const std::string& target, bool accept);
    void   SetPerms(const Service::Ctx& ctx, UInt32 seq, UInt8 rank, UInt8 mask);
    void   SetTitle(const Service::Ctx& ctx, UInt32 seq, UInt8 titleId);
    void   Buy(const Service::Ctx& ctx, UInt32 seq, UInt16 shopId);
    void   FinishBuy(bool ok, const Service::Ctx& ctx, UInt32 seq, UInt32 guildId, UInt16 shopId, UInt32 price, Int64 at);
    void   Claim(const Service::Ctx& ctx, UInt32 seq);
    void   FinishClaim(Guild& g);
    void   CancelClaim(Guild& g, const std::string& by);

private:
    CGameLogicThread                                 m_Loop;
    std::unordered_map<UInt32, Guild>                m_Guilds;
    std::unordered_map<int, UInt32>                  m_GuildOf;       // playerId → 결사 번호
    std::unordered_map<std::string, UInt32>          m_ByKey;         // 이름 키 → 결사 번호
    std::unordered_set<std::string>                  m_PendingKeys;   // 세우는 중인 이름
    std::unordered_set<int>                          m_BusyPlayers;   // 결사 변경이 DB에 가 있는 사람
    std::unordered_map<int, Invite>                  m_Invites;       // 받는 사람 playerId → 초대
    // 접속 중 → 지금 세션(로그인 · 로그아웃 알림 순서 그대로 — 세션 상태를 직접 보지 않는다). 들어옴 · 세우기가 DB에 가 있는 사이
    // 다시 접속했으면 요청 때의 ctx(옛 세션 · 옛 ObjID) 대신 이 값으로 결사원을 만든다(CurrentCtx)
    std::unordered_map<int, Service::Ctx>            m_Online;
    UInt32                                           m_NextGuildId = 1;
    std::unordered_map<int, UInt32>                  m_ApplyOf;       // 가입 신청(신청한 사람 playerId → 결사 번호 — 한 사람에 하나)
    Int64                                            m_NextPostId = 1;
};

extern std::shared_ptr<CGuildService> GGuildService;

namespace CGuildHelper
{
    // ── 요청한 사람의 소유 Zone 스레드 ──
    void HandleRequest(const UserSharedPtr& user, const GUILD_REQ_PACKET& p);
    void HandleExRequest(const UserSharedPtr& user, const GUILD_EX_REQ_PACKET& p);
    // 결사 성장 — 처치(함께 때린 이 Zone의 사람들 — 결사마다 묶어 서비스로) · 전리품 · 퀘스트
    void OnKill(std::span<const UserSharedPtr> damagers, UInt32 monsterExp, UInt8 monsterLevel, bool boss);
    void OnLoot(const UserSharedPtr& user, UInt16 itemId, UInt16 count, UInt64 extra);
    void OnQuestDone(const UserSharedPtr& user);
    void OnEnterWorld(const UserSharedPtr& user);    // 로그인 직후
    void OnLeaveWorld(const UserSharedPtr& user);    // 접속 종료
    void OnLevelUp(const UserSharedPtr& user);
    // 결사 채팅: 검사가 끝난 글을 서비스로(서비스가 접속 중인 결사원에게 보낸다)
    void RelayChat(const UserSharedPtr& sender, const char* body);
    // 시야에 들어온 target의 머리 위 결사 표시를 viewer에게 — 결사가 없으면 보내지 않는다
    void SendTag(const UserSharedPtr& viewer, const UserSharedPtr& target);
    // Zone 스레드: 머리 위 표시를 본인과 주변 세션에
    void BroadcastTag(const UserSharedPtr& user);
}
