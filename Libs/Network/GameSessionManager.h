#pragma once

#include <shared_mutex>
#include <unordered_map>

class CGameSession;

class CGameSessionManager
{
public:
    CGameSessionManager() = default;
    ~CGameSessionManager() = default;

    [[nodiscard]] SessionSharedPtr CreateSession();
    [[nodiscard]] SessionSharedPtr FindSession(CGameSession* key) const;
    bool                                  ReleaseSession(CGameSession* key);

    // 전체 세션의 스냅샷(월드 공지 같은 드문 전역 브로드캐스트용). 락은 복사하는
    // 동안만 잡고, 실제 송신(PostSend — 세션별 락)은 락을 놓은 뒤에 한다.
    [[nodiscard]] vector<SessionSharedPtr> SnapshotSessions() const;

    // ── Phase 3-B: 이름 → 세션 색인(귓속말) ──
    // 로그인 완료(ST_INGAME) 때 넣고, 세션을 풀 때(ReleaseSession) 뺀다. 이름은 ASCII 소문자로 맞춰 찾는다.
    // 읽기(귓속말마다)가 많고 쓰기(로그인 · 접속 종료)가 적어 shared_mutex. 세션 표와 다른 락이라 서로 기다리지 않는다.
    void RegisterName(const string& name, const SessionSharedPtr& session);
    [[nodiscard]] SessionSharedPtr FindByName(const string& name) const;

private:
    [[nodiscard]] static string NameKey(const string& name);

    mutable shared_mutex                                 m_Lock;
    unordered_map<CGameSession*, SessionSharedPtr> m_Sessions;
    UInt32                                             m_NextObjectId = PLAYER_ID_START;

    mutable shared_mutex                                 m_NameLock;
    unordered_map<string, SessionWeakPtr>         m_ByName;     // 소문자 이름 → 세션
    unordered_map<CGameSession*, string>                  m_NameOf;     // 세션 → 소문자 이름(풀 때 지우기)
};
