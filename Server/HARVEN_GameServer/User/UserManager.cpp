#include "pch.h"
#include "User/UserManager.h"
#include "User/UserHelper.h"
#include "Network/GameSession.h"

std::shared_ptr<CUserManager> GUserManager = std::make_shared<CUserManager>();

void CUserManager::Claim(int playerId, const SessionSharedPtr& session, ReadyFn onReady)
{
    if (session == nullptr || playerId <= 0)
        return;

    SessionSharedPtr kick;
    SessionSharedPtr rejected;
    bool ready = false;
    {
        std::scoped_lock lock(m_Lock);
        auto& entry = m_Entries[playerId];
        const auto holder = entry.holder.lock();
        if (holder == nullptr || holder == session)
        {
            entry.holder = session;
            ready = true;
        }
        else
        {
            if (entry.waiter != nullptr && entry.waiter != session)
                rejected = std::move(entry.waiter);
            entry.waiter  = session;
            entry.onReady = std::move(onReady);
            kick = holder;
        }
    }

    if (rejected != nullptr)
    {
        rejected->SetPlayerId(0);
        CUserHelper::HandleLoginFail(rejected);
    }
    if (kick != nullptr)
    {
        // 먼저 있던 세션에 알리고 닫는다 — 끊김 처리(OnSessionClosed → LeaveWorld)가 끝에서 Release를 부른다.
        CUserHelper::NotifyLoggedInElsewhere(kick);
        kick->CloseAfterFlush();
    }
    if (ready && onReady)
        onReady();
}

void CUserManager::Release(int playerId, const CGameSession* session)
{
    if (playerId <= 0)
        return;

    ReadyFn next;
    {
        std::scoped_lock lock(m_Lock);
        auto it = m_Entries.find(playerId);
        if (it == m_Entries.end())
            return;

        auto& entry = it->second;
        const auto holder = entry.holder.lock();
        if (holder != nullptr && holder.get() != session)
        {
            // 잡고 있지 않은 세션 — 기다리던 로그인이 먼저 끊긴 경우만 지운다.
            if (entry.waiter != nullptr && entry.waiter.get() == session)
            {
                entry.waiter.reset();
                entry.onReady = nullptr;
            }
            return;
        }

        if (entry.waiter == nullptr)
        {
            m_Entries.erase(it);
            return;
        }
        entry.holder = entry.waiter;
        entry.waiter.reset();
        next = std::move(entry.onReady);
        entry.onReady = nullptr;
    }
    if (next)
        next();
}

size_t CUserManager::GetOnlineCount() const
{
    std::scoped_lock lock(m_Lock);
    return m_Entries.size();
}
