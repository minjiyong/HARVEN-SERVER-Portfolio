#pragma once

#include <atomic>
#include <ctime>
#include <functional>
#include <memory>
#include <thread>

#include "Worker/GameLogicThread.h"
#include "Content/Field/ZoneManager.h"
#include "Base/GameObjectManager.h"
#include "User/User.h"
#include "Network/GameSession.h"
#include "Content/ServicePost.h"

// ─────────────────────────────────────────────────────────────────────────────
// 전역 서비스(결사 · 경매장 · 파티) 공용
//
//  · 서비스 하나 = 작업 큐 하나(CGameLogicThread) = 스레드 하나. 서비스 상태는 그 스레드만 만진다.
//  · Zone → 서비스: Post(람다). 서비스 → Zone: PostToOwner(사람의 ObjID, 람다) — 그 사람을 소유한 Zone 스레드에서
//    CUser를 받아 실행한다(그 사이 Zone을 옮겼거나 옮기는 중이면 새 Zone으로 다시 넘긴다 · 나갔으면 nullptr).
//  · 서비스 → 사람에게 알림: 세션 PostSend(세션별 송신 락)만 — 그 사람의 게임 상태는 건드리지 않는다.
// ─────────────────────────────────────────────────────────────────────────────
namespace Service
{
    // 서비스 스레드 표시(디버그 검사용). 스레드마다 하나 — 서비스 Run이 켤 때 정한다.
    inline thread_local int LServiceId = 0;
    inline constexpr int kGuildServiceId = 1;
    inline constexpr int kAuctionServiceId = 2;
    inline constexpr int kPartyServiceId = 3;

    // 요청한 사람의 정보(요청을 받은 Zone 스레드에서 만든 사본 — 서비스는 이 값만 쓴다)
    struct Ctx
    {
        int                       playerId = 0;
        ObjID                     obj{};
        SessionWeakPtr session;
        std::string               name;
        UInt8                     level = 1;
        UInt8                     classId = 0;
    };

    [[nodiscard]] inline Ctx MakeCtx(const UserSharedPtr& user)
    {
        Ctx c;
        c.playerId = user->GetPlayerId();
        c.obj = user->GetObjID();
        c.session = user->GetGameSession();
        c.name = user->GetName();
        c.level = user->GetStat()->GetLevel();
        c.classId = static_cast<UInt8>(user->m_eClassId);
        return c;
    }

    template<typename Packet>
    void Init(Packet& p, PacketType type)
    {
        p = {};
        p.size = sizeof(Packet);
        p.type = static_cast<char>(type);
    }

    // 세션이 게임 안에 있을 때만 보낸다(아무 스레드)
    template<typename Packet>
    bool SendTo(const SessionWeakPtr& weak, const Packet& p)
    {
        const auto s = weak.lock();
        if (s == nullptr || s->m_eState != SOCKET_STATE::ST_INGAME)
            return false;
        s->PostSend(p);
        return true;
    }

    // obj를 소유한 Zone 스레드에서 fn(user)를 실행한다. 나갔으면 user = nullptr(그때는 Zone 상태를 만지지 말 것).
    // Zone을 옮기는 사이 쫓아가다 kMaxPostHops번을 넘기면 nullptr로 부른다(가진 스레드가 아닌 곳에서 살아 있는 user를 넘기지 않는다 —
    // 예전에는 그대로 fn(user)를 불러 남의 Zone 스레드에서 그 사람의 인벤토리를 만질 수 있었다). 판정은 ServicePost.h(시험 ServicePostTests).
    inline void PostToOwner(const ObjID& obj, std::function<void(const UserSharedPtr&)> fn, int hops = 0)
    {
        GZoneManager->EnqueueByObject(obj, [obj, fn = std::move(fn), hops]() mutable
        {
            auto user = ::GetGameObject<CUser>(obj);
            const bool alive = user != nullptr;
            const bool transferring = alive && user->IsTransferring();
            const bool ownedHere = alive && !transferring && GZoneManager->IsCurrentThreadOwner(obj);
            switch (NextPostStep(alive, transferring, ownedHere, hops))
            {
            case PostStep::Run:
                fn(user);
                return;
            case PostStep::ForwardToZone:
            {
                // 새 Zone(입장 처리 뒤)으로
                const ZoneId target = user->GetZoneId();
                GZoneManager->EnqueueByZone(target, [obj, fn = std::move(fn), hops]() mutable { PostToOwner(obj, std::move(fn), hops + 1); });
                return;
            }
            case PostStep::Repost:
                PostToOwner(obj, std::move(fn), hops + 1);
                return;
            case PostStep::RunOffline:
                if (alive)
                    std::cout << "[Service] PostToOwner " << hops << "번 넘김 — 소유 Zone을 따라잡지 못해 나간 사람으로 처리한다(user 없이)\n" << std::flush;
                fn(nullptr);
                return;
            }
        });
    }

    [[nodiscard]] inline Int64 UnixNow() noexcept { return static_cast<Int64>(std::time(nullptr)); }
}
