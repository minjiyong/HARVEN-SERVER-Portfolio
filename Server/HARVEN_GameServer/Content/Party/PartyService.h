#pragma once

#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "Content/ServiceCommon.h"

struct PARTY_REQ_PACKET;

// 파티원 Zone이 모아 보내는 상태 한 줄(CPartyHelper::HandleZoneTick · SendSyncNow → CPartyService::OnSync)
struct PartySyncRow
{
    int    playerId = 0;
    ObjID  obj{};
    UInt8  level = 0;
    UInt8  flags = 0;        // PARTY_MEMBER_DEAD · PARTY_MEMBER_COMBAT(ONLINE · LEADER는 서비스가 붙인다)
    UInt16 zone = 0;
    Int16  x = 0, y = 0;
    UInt16 hp = 0, maxHp = 0, mp = 0, maxMp = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// 파티 · 모집 게시판 — 규칙 Database/GameData/PartyRules.h · 시트 PartyConst, 설계 Shared/Document/PARTY.md
//
//  CPartyService: 파티 · 파티원 · 초대 · 모집 글 · 신청의 주인. 전용 스레드 하나(작업 큐)만 상태를 만진다(락 없음).
//   · 결사와 같은 모양(Content/Guild/GuildService.h) — 다만 파티는 DB에 남기지 않는다(서버를 다시 켜면 흩어진다).
//   · 사람의 게임 상태(HP · 경험치 · 소지품)는 건드리지 않는다. 바꿀 것은 "그 사람의 Zone에서 해 달라"(Service::PostToOwner)로만 —
//     파티 번호 사본(CUser::m_PartyId)이 그것. 경험치 나눔 · 줍기 우선권은 몬스터를 가진 Zone 스레드가 그 사본으로 판정한다.
//   · 알림은 세션 PostSend(세션별 송신 락)만. 한 세션에 가는 파티 패킷은 모두 이 스레드가 넣은 순서대로 간다 —
//     PARTY_INF Roster(version) 뒤에 오는 Members 줄의 자리 번호가 어긋나지 않는다.
//   · 접속이 끊긴 파티원은 시트 offline_keep_sec 동안 남는다(다시 들어오면 그대로 파티원). 파티장이 나가면 접속 중인 다음 사람에게.
//   · 모집 글은 파티 하나에 하나. 파티장이 올리고(파티가 없으면 그때 만든다), 시트 recruit_idle_sec 동안 움직임이 없으면 닫는다.
//
//  CPartyHelper(Content/Party/PartyHelper.h): 요청한 사람의 Zone 스레드 쪽 — 검사 · 멱등 · 도배 → 서비스 큐, 파티원 상태 모으기,
//  처치 경험치 나눔 · 떨어질 물건의 주인.
// ─────────────────────────────────────────────────────────────────────────────
class CPartyService
{
public:
    using Task = CGameLogicThread::Task;

    CPartyService();

    void Run();                              // 서비스 스레드
    void Post(Task task);                    // 아무 스레드 → 서비스 큐

    // ── 아래는 전부 서비스 스레드(Post 안)에서만 ──
    void Tick();                             // 1초마다 — 초대 만료 · 오래 끊긴 파티원 · 움직임 없는 모집 글
    void OnLogin(const Service::Ctx& ctx);
    void OnLogout(int playerId, const ObjID& obj);
    void OnBlock(int owner,int target);
    void OnSync(const std::vector<PartySyncRow>& rows);
    void OnDied(int playerId, const ObjID& obj, UInt16 zone, Int16 x, Int16 y);
    void OnRevived(int playerId, const ObjID& obj);
    void Handle(const Service::Ctx& ctx, const PARTY_REQ_PACKET& p);
    void RelayChat(int playerId, const ObjID& senderObj, const std::string& senderName, const std::string& body);

    [[nodiscard]] size_t PartyCount() const noexcept { return m_Parties.size(); }
    [[nodiscard]] size_t RecruitCount() const noexcept { return m_Recruits.size(); }

private:
    struct Member
    {
        int            playerId = 0;
        std::string    name;
        ObjID          obj{};                // 접속 중일 때의 객체(끊기면 npos)
        SessionWeakPtr session;
        UInt8          classId = 0;
        bool           online = false;
        UInt32         offlineAt = 0;        // GetNowTime — 끊긴 때
        PartySyncRow   state{};              // 마지막으로 받은 상태(레벨 · 지역 · 위치 · HP · MP · 플래그)
    };
    struct Party
    {
        UInt32              id = 0;
        int                 leaderId = 0;
        std::vector<Member> members;         // 들어온 순서 = 자리 번호(PARTY_INF Roster · Members)
        UInt8               invitePolicy = 0;   // PartyInvitePolicy
        UInt8               version = 0;     // 자리 번호가 바뀔 때마다(들어옴 · 나감) 1씩
        ObjID               mark{};          // 공격 집중 대상
        UInt32              recruitId = 0;   // 이 파티의 모집 글
    };
    struct Invite
    {
        UInt32      id = 0;
        int         inviterId = 0;
        std::string inviterName;
        UInt32      partyId = 0;             // 초대할 때 초대한 사람의 파티(0 = 없음 — 받아들이면 초대한 사람이 파티장인 파티를 만든다)
        UInt32      expiresAt = 0;           // GetNowTime
    };
    struct Applicant
    {
        int         playerId = 0;
        std::string name;
        UInt8       level = 1;
        UInt8       classId = 0;
    };
    struct Recruit
    {
        UInt32                 id = 0;
        UInt32                 partyId = 0;
        UInt8                  purpose = 0;  // PartyPurpose
        UInt8                  minLevel = 1;
        UInt8                  maxLevel = 1;
        UInt8                  slots = 2;    // 목표 인원(파티장 포함)
        bool                   autoJoin = false;
        std::string            title;
        UInt32                 activeAt = 0; // 마지막 움직임(GetNowTime) — 올림 · 고침 · 승인 · 거절 · 들어옴
        std::vector<Applicant> applicants;   // 먼저 신청한 순
    };

    [[nodiscard]] static std::string NameKey(const std::string& name);
    [[nodiscard]] Party* PartyOf(int playerId);
    [[nodiscard]] static Member* FindMember(Party& p, int playerId);
    [[nodiscard]] static Member* FindMemberByName(Party& p, const std::string& name);
    [[nodiscard]] static int SlotOf(const Party& p, int playerId);
    [[nodiscard]] static UInt8 MemberFlags(const Party& p, const Member& m);
    [[nodiscard]] static bool CanInvite(const Party& p, int playerId) noexcept;
    bool CanSocialJoin(const Party& p,int playerId) const;
    [[nodiscard]] const Service::Ctx* OnlineCtx(int playerId) const;

    // 보내기
    void Ack(const Service::Ctx& ctx, PartyOp op, PartyResult r, UInt32 seq, UInt32 param = 0) const;
    void SendRoster(const SessionWeakPtr& session, const Party* p) const;
    void BroadcastRoster(const Party& p) const;
    void SendMembers(const Party& p, UInt8 slotMask) const;
    // 접속 중인 파티원 모두에게(exceptPlayerId = 빼고 보낼 사람)
    void Notify(const Party& p, const PARTY_NFY_PACKET& n, int exceptPlayerId = 0) const;
    [[nodiscard]] static PARTY_NFY_PACKET MakeNfy(PartyEvent ev, const std::string& who = {}, const std::string& by = {});
    void SendRecruitList(const Service::Ctx& ctx, UInt8 purpose, UInt32 page) const;
    void SendApplicants(const Service::Ctx& ctx, const Recruit& r) const;

    // 그 사람의 Zone에 파티 번호 사본을 넘긴다(0 = 파티 없음). 늦게 도착한 옛 값이 새 값을 덮지 않게 순번을 싣는다
    void ApplyPartyId(const ObjID& obj, UInt32 partyId);

    // 구조 변경(서비스 스레드)
    Party& CreateParty(const Service::Ctx& leader);
    void AddMember(Party& p, const Service::Ctx& ctx);
    // reason: Left · Kicked · MemberTimeout. 파티가 비면 해산한다 — p는 이 뒤에 쓰지 않는다
    void RemoveMember(Party& p, int playerId, PartyEvent reason, const std::string& by = {});
    void PassLeadership(Party& p);
    void Disband(Party& p);
    void CloseRecruit(UInt32 recruitId, PartyRecruitCloseReason reason);
    void Withdraw(int playerId);             // 이 사람의 승인 대기 신청을 거둔다
    void TouchRecruit(const Party& p);       // 모집 글 움직임(자동 닫기 시계를 다시)

    // 요청별
    void DoInvite(const Service::Ctx& ctx, UInt32 seq, const std::string& target);
    void Reply(const Service::Ctx& ctx, UInt32 seq, bool accept, UInt32 inviteId);
    void Leave(const Service::Ctx& ctx, UInt32 seq);
    void Kick(const Service::Ctx& ctx, UInt32 seq, const std::string& target);
    void Transfer(const Service::Ctx& ctx, UInt32 seq, const std::string& target);
    void DoDisband(const Service::Ctx& ctx, UInt32 seq);
    void Settings(const Service::Ctx& ctx, UInt32 seq, UInt8 policy);
    void ShareTarget(const Service::Ctx& ctx, const ObjID& target);
    void SetMark(const Service::Ctx& ctx, UInt32 seq, const ObjID& target);
    void RecruitPost(const Service::Ctx& ctx, const PARTY_REQ_PACKET& p, const std::string& title);
    void RecruitClose(const Service::Ctx& ctx, UInt32 seq);
    void RecruitApply(const Service::Ctx& ctx, UInt32 seq, UInt32 recruitId);
    void RecruitDecide(const Service::Ctx& ctx, PartyOp op, UInt32 seq, const std::string& target);
    void RecruitApplicants(const Service::Ctx& ctx, UInt32 seq);
    void RecruitCancel(const Service::Ctx& ctx, UInt32 seq);

private:
    CGameLogicThread                             m_Loop;
    std::unordered_map<UInt32, Party>            m_Parties;
    std::unordered_map<int, UInt32>              m_PartyOf;     // playerId → 파티 번호
    std::unordered_map<int, Invite>              m_Invites;     // 받는 사람 playerId → 초대(한 사람에게 하나 — 새 초대가 덮는다)
    std::map<UInt32, Recruit>                    m_Recruits;    // 글 번호 → 모집 글
    std::unordered_map<int, UInt32>              m_AppliedTo;   // 신청한 사람 → 글 번호(한 사람에 하나)
    std::unordered_map<int, Service::Ctx>        m_Online;      // 접속 중(로그인 · 로그아웃 알림 순서 그대로)
    UInt32                                       m_NextPartyId = 1;
    UInt32                                       m_NextInviteId = 1;
    UInt32                                       m_NextRecruitId = 1;
    UInt32                                       m_ApplySeq = 0; // ApplyPartyId 순번
};

extern std::shared_ptr<CPartyService> GPartyService;
