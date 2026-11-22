// 마을 NPC — 상점 · 순간이동 · 말 걸기 · 서비스
namespace RouteNpc
{
    void OnRecv_SHOP_BUY_REQ(const UserSharedPtr& pUser, const SHOP_BUY_REQ_PACKET& req)
    {
        CContentHelper::HandleShopBuy(pUser, req.npcId, req.itemId, req.count, req.seq);
    }

    void OnRecv_SHOP_SELL_REQ(const UserSharedPtr& pUser, const SHOP_SELL_REQ_PACKET& req)
    {
        CContentHelper::HandleShopSell(pUser, req.npcId, req.slotIndex, req.count, req.seq);
    }

    void OnRecv_NPC_TELEPORT_REQ(const UserSharedPtr& pUser, const NPC_TELEPORT_REQ_PACKET& req)
    {
        CContentHelper::HandleNpcTeleport(pUser, req.npcId, req.destIndex, req.seq);
    }

    void OnRecv_NPC_TALK_REQ(const UserSharedPtr& pUser, const NPC_TALK_REQ_PACKET& req)
    {
        CNpcHelper::HandleTalk(pUser, req.npcId);
    }

    void OnRecv_NPC_SERVICE_REQ(const UserSharedPtr& pUser, const NPC_SERVICE_REQ_PACKET& req)
    {
        CNpcHelper::HandleService(pUser, req.npcId, req.service);
    }
}
