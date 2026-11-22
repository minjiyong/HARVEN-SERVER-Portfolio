// 캐릭터 — 이동 · 다시 시작 · 능력치 분배
namespace RouteUser
{
    void OnRecv_USER_MOVE_REQ(const UserSharedPtr& pUser, const USER_MOVE_REQ_PACKET& req)
    {
        CUserHelper::HandleMove(pUser, req.dir, req.seq);
    }

    void OnRecv_USER_RESTART_REQ(const UserSharedPtr& pUser, const USER_RESTART_REQ_PACKET& req)
    {
        CContentHelper::HandleRestartRequest(pUser);
    }

    void OnRecv_STAT_ALLOC_REQ(const UserSharedPtr& pUser, const STAT_ALLOC_REQ_PACKET& req)
    {
        CCombatHelper::HandleStatAlloc(pUser, req.stat, req.amount);
    }
}
