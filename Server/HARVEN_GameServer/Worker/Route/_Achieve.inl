// 개인 업적 — 검사 · 멱등 · 묻기 간격 · 보상은 소유 Zone(CAchieveHelper). 서비스 스레드 없음(사람마다의 상태는 CUser::m_Achieve)
namespace RouteAchieve
{
    void OnRecv_ACHIEVE_REQ(const UserSharedPtr& pUser, const ACHIEVE_REQ_PACKET& req)
    {
        CAchieveHelper::HandleRequest(pUser, req);
    }
}
