#include "pch.h"
#include "Network/GameSessionManager.h"
#include "Network/GameSession.h"

SessionSharedPtr CGameSessionManager::CreateSession()
{
    unique_lock lock(m_Lock);

    if (m_NextObjectId >= MONSTER_ID_START)
        return nullptr;

    auto session      = std::make_shared<CGameSession>();
    session->m_ObjectId = m_NextObjectId++;
    m_Sessions.emplace(session.get(), session);
    return session;
}

SessionSharedPtr CGameSessionManager::FindSession(CGameSession* key) const
{
    shared_lock lock(m_Lock);
    auto it = m_Sessions.find(key);
    return it != m_Sessions.end() ? it->second : nullptr;
}

bool CGameSessionManager::ReleaseSession(CGameSession* key)
{
    {
        // 이름 색인: 이 세션이 가진 이름이면 뺀다(같은 이름으로 다시 들어온 새 세션의 자리는 건드리지 않는다)
        unique_lock nameLock(m_NameLock);
        if (auto it = m_NameOf.find(key); it != m_NameOf.end())
        {
            if (auto nit = m_ByName.find(it->second); nit != m_ByName.end())
            {
                const auto cur = nit->second.lock();
                if (cur == nullptr || cur.get() == key)
                    m_ByName.erase(nit);
            }
            m_NameOf.erase(it);
        }
    }
    unique_lock lock(m_Lock);
    return m_Sessions.erase(key) > 0;
}

string CGameSessionManager::NameKey(const string& name)
{
    string key = name;
    for (char& c : key)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return key;
}

void CGameSessionManager::RegisterName(const string& name, const SessionSharedPtr& session)
{
    if (name.empty() || session == nullptr)
        return;
    const string key = NameKey(name);
    unique_lock lock(m_NameLock);
    m_ByName[key] = session;
    m_NameOf[session.get()] = key;
}

SessionSharedPtr CGameSessionManager::FindByName(const string& name) const
{
    if (name.empty())
        return nullptr;
    const string key = NameKey(name);
    shared_lock lock(m_NameLock);
    auto it = m_ByName.find(key);
    return it != m_ByName.end() ? it->second.lock() : nullptr;
}

vector<SessionSharedPtr> CGameSessionManager::SnapshotSessions() const
{
    vector<SessionSharedPtr> sessions;
    shared_lock lock(m_Lock);
    sessions.reserve(m_Sessions.size());
    for (const auto& [key, session] : m_Sessions)
        sessions.push_back(session);
    return sessions;
}
