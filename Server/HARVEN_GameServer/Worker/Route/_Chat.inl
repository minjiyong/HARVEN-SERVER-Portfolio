// 채팅 — 채널(일반 · 전체 · 거래 · 결사 · 귓속말). 검사 · 범위는 Content/Chat/ChatHelper
namespace RouteChat
{
    void OnRecv_CS_CHAT(const UserSharedPtr& pUser, const CS_CHAT_PACKET& req)
    {
        CChatHelper::HandleChat(pUser, req.channel, req.targetName, req.mess);
    }
}
