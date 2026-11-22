// 퀘스트 — 받기 · 보고 · 포기
namespace RouteQuest
{
    void OnRecv_QUEST_ACCEPT_REQ(const UserSharedPtr& pUser, const QUEST_ACCEPT_REQ_PACKET& req)
    {
        CQuestHelper::HandleAccept(pUser, req.questId);
    }

    void OnRecv_QUEST_COMPLETE_REQ(const UserSharedPtr& pUser, const QUEST_COMPLETE_REQ_PACKET& req)
    {
        CQuestHelper::HandleComplete(pUser, req.questId);
    }

    void OnRecv_QUEST_ABANDON_REQ(const UserSharedPtr& pUser, const QUEST_ABANDON_REQ_PACKET& req)
    {
        CQuestHelper::HandleAbandon(pUser, req.questId);
    }
}
