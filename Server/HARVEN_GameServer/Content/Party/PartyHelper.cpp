#include "pch.h"
#include "Content/Party/PartyHelper.h"

#include "Base/GameObjectManager.h"
#include "Content/Chat/ChatHelper.h"
#include "Content/Field/Sector.h"
#include "Content/Field/ZoneManager.h"
#include "Content/Party/PartyService.h"
#include "Database/GameData/PartyRules.h"
#include "User/User.h"
#include "Worker/TimerThread.h"

namespace CPartyHelper
{
    namespace
    {
        // Zone 틱 예약의 주인 — 객체 표에 없는 id(eNone)라 그 Zone 스레드가 직접 돌린다(CTimerThread::Schedule → ScheduleLocal)
        const ObjID kTickId(EnumCategory::eNone, 0x5041525459ull);   // "PARTY"
        // 파티원이 없는 Zone은 덜 자주 본다(들어오면 ApplyPartyId가 바로 한 번 보낸다)
        constexpr UInt32 kIdleTickMultiplier = 4;

        [[nodiscard]] bool IsMutating(PartyOp op) noexcept
        {
            return op != PartyOp::Info && op != PartyOp::RecruitList && op != PartyOp::RecruitApplicants && op != PartyOp::Target;
        }

        void ZoneAck(const UserSharedPtr& user, PartyOp op, PartyResult r, UInt32 seq, UInt32 param = 0)
        {
            PARTY_ACK_PACKET p;
            Service::Init(p, PacketType::PARTY_ACK);
            p.op = static_cast<UInt8>(op);
            p.result = static_cast<UInt8>(r);
            p.seq = seq;
            p.param = param;
            if (auto s = user->GetGameSession()) s->PostSend(p);
        }

        [[nodiscard]] bool ValidTargetObj(const ObjID& o) noexcept
        {
            if (o == ObjID::npos)
                return true;
            const auto c = o.GetCategory<EnumCategory>();
            return c == EnumCategory::eUser || c == EnumCategory::eMonster;
        }

        [[nodiscard]] PartySyncRow BuildRow(CUser& u, UInt32 now)   // GetStat()이 const가 아니다
        {
            PartySyncRow row;
            row.playerId = u.GetPlayerId();
            row.obj = u.GetObjID();
            row.level = u.GetStat()->GetLevel();
            if (u.GetStat()->IsDead())
                row.flags |= PARTY_MEMBER_DEAD;
            if (u.m_LastCombatAt != 0 && now - u.m_LastCombatAt < PartyRule::kCombatWindowMs)
                row.flags |= PARTY_MEMBER_COMBAT;
            row.zone = u.GetZoneId();
            row.x = u.GetX();
            row.y = u.GetY();
            row.hp = u.GetStat()->GetHp();
            row.maxHp = u.GetStat()->GetMaxHp();
            row.mp = u.m_Mp;
            row.maxMp = static_cast<UInt16>(std::min<UInt32>(u.m_Derived.maxMp, 0xFFFF));
            return row;
        }

        [[nodiscard]] CUser::PartySent ToSent(const PartySyncRow& row) noexcept
        {
            CUser::PartySent s;
            s.bSent = true;
            s.level = row.level;
            s.flags = row.flags;
            s.zone = row.zone;
            s.x = row.x;
            s.y = row.y;
            s.hp = row.hp;
            s.maxHp = row.maxHp;
            s.mp = row.mp;
            s.maxMp = row.maxMp;
            return s;
        }

        void SendSyncNow(const UserSharedPtr& user)
        {
            if (GPartyService == nullptr || user->IsTransferring())
                return;   // Zone을 옮기는 중 — 새 Zone 틱이 보낸다
            const PartySyncRow row = BuildRow(*user, GetNowTime());
            user->m_PartySent = ToSent(row);
            GPartyService->Post([row]() { GPartyService->OnSync({ row }); });
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 요청 — 형식 · 멱등 · 도배는 여기서, 상태 판정은 서비스에서
    // ─────────────────────────────────────────────────────────────────────────
    void HandleRequest(const UserSharedPtr& user, const PARTY_REQ_PACKET& p)
    {
        if (user == nullptr)
            return;
        const auto op = static_cast<PartyOp>(p.op);
        if (GPartyService == nullptr)
            return ZoneAck(user, op, PartyResult::Busy, p.seq);
        if (IsMutating(op))
        {
            // 멱등: 같은(또는 지난) 순번은 한 번만 — 소유 Zone이 사람마다 기억한다(세션마다 — 다시 접속하면 새 CUser라 0부터).
            // Duplicate의 param = 마지막으로 받은 순번: 처음 보낸 요청이 Duplicate면 클라의 순번이 뒤처진 것(나간 뒤 0으로 되돌림 등) —
            // 클라는 순번을 param으로 맞추고 param + 1로 다시 보낸다(PARTY.md 7장 「순번 계약」). 재전송은 그대로 버려진다.
            if (p.seq <= user->m_LastPartySeq)
                return ZoneAck(user, op, PartyResult::Duplicate, p.seq, user->m_LastPartySeq);
            user->m_LastPartySeq = p.seq;
        }

        const UInt32 now = GetNowTime();
        switch (op)
        {
        case PartyOp::Info: case PartyOp::RecruitList: case PartyOp::RecruitApplicants:
            // 묻기 — 사람마다 kQueryMs에 한 번(목록 정렬 · 패킷이 서비스 스레드 몫이다)
            if (user->m_LastPartyQueryAt != 0 && now - user->m_LastPartyQueryAt < PartyRule::kQueryMs)
                return ZoneAck(user, op, PartyResult::TooFast, p.seq, PartyRule::kQueryMs - (now - user->m_LastPartyQueryAt));
            user->m_LastPartyQueryAt = now;
            if (op == PartyOp::RecruitList && p.arg8 != PartyRule::kAnyPurpose && !PartyRule::ValidPurpose(p.arg8))
                return ZoneAck(user, op, PartyResult::Invalid, p.seq);
            break;
        case PartyOp::Target:
            // 대상 공유 — 응답 없음. 파티가 없거나 너무 잦으면 버린다(클라는 대상이 바뀔 때만 보낸다)
            if (user->m_PartyId.load(std::memory_order_relaxed) == 0 || !ValidTargetObj(p.obj))
                return;
            if (user->m_LastPartyTargetAt != 0 && now - user->m_LastPartyTargetAt < PartyRule::kTargetShareMs)
                return;
            user->m_LastPartyTargetAt = now;
            break;
        case PartyOp::Mark:
            if (!ValidTargetObj(p.obj))
                return ZoneAck(user, op, PartyResult::Invalid, p.seq);
            break;
        case PartyOp::Invite: case PartyOp::Kick: case PartyOp::Transfer:
        case PartyOp::RecruitAccept: case PartyOp::RecruitReject:
            if (p.target[0] == '\0')
                return ZoneAck(user, op, PartyResult::TargetNotFound, p.seq);
            break;
        case PartyOp::Settings:
            if (!PartyRule::ValidPolicy(p.arg8))
                return ZoneAck(user, op, PartyResult::Invalid, p.seq);
            break;
        case PartyOp::RecruitPost:
        {
            if (!PartyRule::ValidPurpose(p.purpose) || p.minLevel < 1 || p.minLevel > p.maxLevel || p.maxLevel > LevelTable::kMaxLevel
                || p.slots < 2 || p.slots > PartyRule::MaxMembers() || p.arg8 > 1)
                return ZoneAck(user, op, PartyResult::Invalid, p.seq);
            char title[sizeof(p.text) + 1]{};
            ::memcpy(title, p.text, sizeof(p.text));
            const size_t n = ::strnlen(title, sizeof(title));
            if (n >= static_cast<size_t>(PartyRule::kTitleBytes) || !CChatHelper::IsValidUtf8(title, n)
                || PartyRule::CheckTitle(title) != PartyRule::TitleCheck::Ok)
                return ZoneAck(user, op, PartyResult::TextInvalid, p.seq);
            break;
        }
        case PartyOp::RecruitApply:
            if (p.arg32 == 0)
                return ZoneAck(user, op, PartyResult::RecruitNotFound, p.seq);
            break;
        case PartyOp::Create: case PartyOp::InviteReply: case PartyOp::Leave: case PartyOp::Disband:
        case PartyOp::RecruitClose: case PartyOp::RecruitCancel:
            break;
        default:
            return ZoneAck(user, op, PartyResult::Invalid, p.seq);
        }

        const Service::Ctx ctx = Service::MakeCtx(user);
        const PARTY_REQ_PACKET copy = p;
        GPartyService->Post([ctx, copy]() { GPartyService->Handle(ctx, copy); });
    }

    void OnEnterWorld(const UserSharedPtr& user)
    {
        if (user == nullptr || GPartyService == nullptr)
            return;
        const Service::Ctx ctx = Service::MakeCtx(user);
        GPartyService->Post([ctx]() { GPartyService->OnLogin(ctx); });
    }

    void OnLeaveWorld(const UserSharedPtr& user)
    {
        if (user == nullptr || GPartyService == nullptr)
            return;
        const int playerId = user->GetPlayerId();
        const ObjID obj = user->GetObjID();
        GPartyService->Post([playerId, obj]() { GPartyService->OnLogout(playerId, obj); });
    }

    void OnDied(const UserSharedPtr& user)
    {
        if (user == nullptr || GPartyService == nullptr || user->m_PartyId.load(std::memory_order_relaxed) == 0)
            return;
        const int playerId = user->GetPlayerId();
        const ObjID obj = user->GetObjID();
        const UInt16 zone = user->GetZoneId();
        const Int16 x = user->GetX(), y = user->GetY();
        user->m_PartySent.bSent = false;   // 살아나면 다음 틱에 다시 보낸다
        GPartyService->Post([=]() { GPartyService->OnDied(playerId, obj, zone, x, y); });
    }

    void OnRevived(const UserSharedPtr& user)
    {
        if (user == nullptr || GPartyService == nullptr || user->m_PartyId.load(std::memory_order_relaxed) == 0)
            return;
        const int playerId = user->GetPlayerId();
        const ObjID obj = user->GetObjID();
        GPartyService->Post([playerId, obj]() { GPartyService->OnRevived(playerId, obj); });
        SendSyncNow(user);   // 다시 선 자리 · 가득 찬 HP를 바로
    }

    void RelayChat(const UserSharedPtr& sender, const char* body)
    {
        if (sender == nullptr || GPartyService == nullptr)
            return;
        const int playerId = sender->GetPlayerId();
        const ObjID obj = sender->GetObjID();
        std::string name = sender->GetName();
        std::string text = body;
        GPartyService->Post([playerId, obj, name = std::move(name), text = std::move(text)]()
        {
            GPartyService->RelayChat(playerId, obj, name, text);
        });
    }

    void ApplyPartyId(const UserSharedPtr& user, UInt32 partyId, UInt32 applySeq)
    {
        if (user == nullptr || applySeq <= user->m_PartyApplySeq)
            return;   // Zone을 옮기는 사이 늦게 온 옛 값
        user->m_PartyApplySeq = applySeq;
        user->m_PartyId.store(partyId, std::memory_order_relaxed);
        user->m_PartySent = {};
        if (partyId != 0)
            SendSyncNow(user);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Zone 틱 — 이 Zone 섹터의 파티원 중 바뀐 사람만 한 묶음으로
    // ─────────────────────────────────────────────────────────────────────────
    void InitForZone(UInt16 zoneId)
    {
        (void)zoneId;
        GTimerThread->ScheduleAfter(kTickId, std::chrono::milliseconds(PartyRule::SyncIntervalMs()), TIMER_EVENT_TYPE::EV_PARTY_TICK);
    }

    void HandleZoneTick()
    {
        const UInt32 interval = PartyRule::SyncIntervalMs();
        if (GSector == nullptr || GPartyService == nullptr)
        {
            GTimerThread->ScheduleAfter(kTickId, std::chrono::milliseconds(interval * kIdleTickMultiplier), TIMER_EVENT_TYPE::EV_PARTY_TICK);
            return;
        }
        const UInt32 now = GetNowTime();
        bool any = false;
        std::vector<PartySyncRow> rows;
        // 섹터에 든 사람 = 이 Zone 스레드가 가진 사람(죽어 섹터에서 빠진 사람은 OnDied가 알렸다)
        GSector->ForEachUserInZone([&](const SubjectSharedPtr& object)
        {
            auto* user = static_cast<CUser*>(object.get());
            if (user->m_PartyId.load(std::memory_order_relaxed) == 0)
                return;
            any = true;
            if (user->IsTransferring())
                return;
            const PartySyncRow row = BuildRow(*user, now);
            const CUser::PartySent sent = ToSent(row);
            if (user->m_PartySent == sent)
                return;
            user->m_PartySent = sent;
            rows.push_back(row);
        });
        if (!rows.empty())
            GPartyService->Post([rows = std::move(rows)]() { GPartyService->OnSync(rows); });
        GTimerThread->ScheduleAfter(kTickId, std::chrono::milliseconds(any ? interval : interval * kIdleTickMultiplier), TIMER_EVENT_TYPE::EV_PARTY_TICK);
    }
}
