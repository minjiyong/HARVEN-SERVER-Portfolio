#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>

class CGameSession;

// 계정(playerId) 하나 = 세션 하나. 같은 계정으로 다시 로그인하면 먼저 있던 세션을 끊고,
// 그 세션이 월드에서 빠져 저장을 다 요청한 뒤(Release)에 새 로그인을 이어 간다.
// 저장 요청과 새 로그인의 읽기는 같은 DB 샤드(playerId)에 차례로 쌓이므로, 새 세션은 항상 끊긴 세션이 남긴 값을 읽는다.
class CUserManager
{
public:
    using ReadyFn = std::function<void()>;

    // DB 스레드: session이 이 계정을 잡는다. 비어 있으면 onReady를 바로 부르고,
    // 다른 세션이 잡고 있으면 그 세션을 끊고 Release될 때 onReady를 부른다.
    // 이미 기다리던 다른 로그인이 있으면 그 로그인은 실패로 돌려보낸다.
    void Claim(int playerId, const SessionSharedPtr& session, ReadyFn onReady);

    // session이 계정을 놓는다 — 월드에서 빠짐 · 로그인 실패 · 로그인 전 끊김 · 기다리다 끊김.
    void Release(int playerId, const CGameSession* session);

    [[nodiscard]] size_t GetOnlineCount() const;

private:
    struct Entry
    {
        SessionWeakPtr   holder;
        SessionSharedPtr waiter;
        ReadyFn                      onReady;
    };

    mutable std::mutex                 m_Lock;
    std::unordered_map<int, Entry>     m_Entries;
};

extern std::shared_ptr<CUserManager> GUserManager;
