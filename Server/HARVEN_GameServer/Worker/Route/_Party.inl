// 파티 · 모집 게시판 — 검사 · 멱등 · 도배는 소유 Zone(CPartyHelper), 상태는 단일 스레드 서비스(CPartyService)
namespace RouteParty
{
    void OnRecv_PARTY_REQ(const UserSharedPtr& pUser, const PARTY_REQ_PACKET& req)
    {
        CPartyHelper::HandleRequest(pUser, req);
    }
}
