namespace RouteTransfer
{
    void OnRecv_TRANSFER_REQ(const UserSharedPtr& user, const TRANSFER_REQ_PACKET& req)
    { CTransferHelper::HandleRequest(user, req); }
}
