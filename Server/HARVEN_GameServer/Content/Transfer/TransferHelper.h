#pragma once

// 같은 Zone 스레드가 두 참여자의 상태를 소유한다. DB 콜백은 시작 Zone 큐로만 돌아온다.
namespace CTransferHelper
{
    void CancelWith(const UserSharedPtr& user,int playerId);
    void HandleRequest(const UserSharedPtr& user, const TRANSFER_REQ_PACKET& packet);
    void InitForZone();
    void Tick();
    void Cancel(const UserSharedPtr& user, TransferResult reason = TransferResult::Unavailable);
    // DB 확정 전 로그아웃은 보류(계정·객체 유지). 완료 콜백이 LeaveWorld를 다시 호출한다.
    bool DeferLogout(const UserSharedPtr& user);
}
