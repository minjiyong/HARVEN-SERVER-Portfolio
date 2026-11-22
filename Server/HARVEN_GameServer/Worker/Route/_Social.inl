namespace RouteSocial
{
    void OnRecv_SOCIAL_REQ(const UserSharedPtr& user,const SOCIAL_REQ_PACKET& req)
    { CSocialHelper::HandleRequest(user,req); }
}
