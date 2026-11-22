// 접속 · 로그인 — 세션만 있고 CUser는 아직 없다(기본 로직 스레드에서 처리한다).
namespace RouteAuth
{
    void OnRecv_USER_LOGIN_REQ(const SessionSharedPtr& pSession, const USER_LOGIN_REQ_PACKET& req)
    {
        // 한 세션에 로그인은 한 번만 — 두 번째 로그인 패킷은 버린다.
        if (!pSession->TryBeginLogin())
            return;

        char name[NAME_SIZE + 1]{};
        char password[PASSWORD_SIZE + 1]{};
        ::strncpy_s(name, req.name, NAME_SIZE);
        ::strncpy_s(password, req.password, PASSWORD_SIZE);

        pSession->SetNewClassId(ClassTable::IsValid(req.classId) ? req.classId : 0);
        GDBThread->RequestLogin(pSession, name, password);
    }
}
