// 기술 — 누름 · 뗌 · 취소(SKILL_REQ) · 기술 성장
namespace RouteSkill
{
    void OnRecv_SKILL_REQ(const UserSharedPtr& pUser, const SKILL_REQ_PACKET& req)
    {
        CUserSkillHelper::ExecuteSkill(pUser, req);
    }

    void OnRecv_SKILL_GROW_REQ(const UserSharedPtr& pUser, const SKILL_GROW_REQ_PACKET& req)
    {
        SkillGrow::HandleRequest(pUser, req);
    }
}
