#pragma once

class CGameSession;

// 받은 패킷 하나를 처리한다 — 타입별 처리 함수는 Worker/Route/_영역.inl의 OnRecv_패킷이름.
namespace Route
{
    void Dispatch(const SessionSharedPtr& pSession, const char* packet);
}
