// 결사 · 경매장 — 검사 · 멱등은 소유 Zone(CGuildHelper · CAuctionHelper), 상태는 단일 스레드 서비스
namespace RouteGuild
{
    void OnRecv_GUILD_REQ(const UserSharedPtr& pUser, const GUILD_REQ_PACKET& req)
    {
        CGuildHelper::HandleRequest(pUser, req);
    }

    void OnRecv_GUILD_EX_REQ(const UserSharedPtr& pUser, const GUILD_EX_REQ_PACKET& req)
    {
        CGuildHelper::HandleExRequest(pUser, req);
    }

    void OnRecv_AUCTION_REQ(const UserSharedPtr& pUser, const AUCTION_REQ_PACKET& req)
    {
        CAuctionHelper::HandleRequest(pUser, req);
    }
}
