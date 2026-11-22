#pragma once

class CGameSession;

// ─────────────────────────────────────────────────────────────────────────────
// CSocketManager — 접속 받기(Boost.Asio)와 IO 스레드.
//
// IO 스레드 하나가 io_context 하나를 혼자 돌린다(io_context 풀). 새 접속은 돌아가며 한 io_context에 묶이고,
// 그 소켓의 읽기 · 쓰기 · 닫기는 늘 그 스레드에서 돈다 — 소켓마다 strand가 필요 없고, 스레드 여럿이 io_context 하나의
// 스케줄러 잠금을 두고 다투지 않는다. 접속 받기는 0번 io_context.
// ─────────────────────────────────────────────────────────────────────────────
class CSocketManager
{
public:
    CSocketManager();
    ~CSocketManager();

    static void     Init();
    static bool     MakeListenSocket();
    static bool     Bind();
    static bool     Listen();
    static bool     StartAccepting();

    // IO 스레드 수(= io_context 수, CPU 코어 수). 스레드 i는 Run(i)를 돈다
    [[nodiscard]] static UInt32 GetIoThreadCount() noexcept;
    static void     Run(UInt32 ioIndex);

    static void     Clear();
};
