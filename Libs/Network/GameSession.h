#pragma once

#include "Content/Field/ZoneTypes.h"

class CGameSession;
class CUser;

class CGameSession : public enable_shared_from_this<CGameSession>
{
public:
    atomic<SOCKET_STATE> m_eState{ SOCKET_STATE::ST_FREE };
    UInt32 m_PendingRecvBytes = 0;
    UInt32 m_ObjectId = 0;
    // 이 세션의 인바운드 패킷을 어느 Zone 큐에 넣을지 정하는 "라우팅 키 캐시"다.
    // 값 자체는 소유 오브젝트의 CTransform::m_ZoneId와 같지만 역할이 다르다 —
    // 이쪽은 IO 스레드가 패킷마다 읽는다(CZoneManager::EnqueueBySession).
    // 워커가 CTransform 쪽을 직접 보려면 GetOwner()로 weak_ptr을 잠가 CUser를 얻어야
    // 하는데, 그 비용을 패킷마다 내는 대신 소유 Zone 스레드가 이동할 때 여기에
    // 한 번 찍어두는 쪽을 택했다. 로그인 전(CUser가 아직 없는 구간)에는 이 캐시가
    // 유일한 라우팅 근거이기도 하다.
    atomic<ZoneId> m_RoutingZoneId{ InvalidZoneId };
    mutable mutex m_SessionLock;
    UserWeakPtr m_wpOwner;
    atomic<UInt8> m_NewClassId{ 0 };   // V2: 새 캐릭터를 만들 때 고른 직업(로그인 패킷)
    // 이 세션이 잡은 계정(CUserManager). 0이면 아직 없음 — 끊길 때 이 값으로 계정을 놓는다.
    atomic<int> m_PlayerId{ 0 };

    // 한 번에 읽는 크기(받는 버퍼) — 가장 큰 요청 패킷 하나는 들어간다
    static constexpr size_t kRecvBufferSize = BUF_SIZE > MAX_CLIENT_PACKET_SIZE ? BUF_SIZE : MAX_CLIENT_PACKET_SIZE;

protected:
    std::shared_ptr<boost::asio::ip::tcp::socket> m_pSocket;
    std::array<char, kRecvBufferSize> m_RecvBuffer{};
    // 보낼 바이트를 두 버퍼로 주고받는다 — 새 패킷은 m_PendingBytes 뒤에 붙고(m_SessionLock),
    // 쓰기를 시작할 때 두 버퍼를 바꿔 m_WritingBytes를 통째로 쓴다(쓰는 쪽 하나만 만진다). 용량은 계속 다시 쓴다.
    std::vector<char> m_PendingBytes;
    std::vector<char> m_WritingBytes;
    bool m_bSendInFlight = false;
    bool m_bFlushQueued = false;          // 로직 스레드가 묶음 끝에 보내기로 적어 둠(m_SessionLock)
    std::mutex m_SocketWriteLock;         // 로직 스레드가 바로 쓰는 동안 소켓이 닫히지(번호가 재사용되지) 않게
    std::atomic<bool> m_bNetworkClosed{ false };
    std::atomic<bool> m_bCloseRequested{ false };
    std::atomic<bool> m_bCloseAfterFlush{ false };   // CloseAfterFlush — 새 송신은 받지 않고, 큐가 비면 닫는다

public:
    CGameSession();
    virtual ~CGameSession();

    [[nodiscard]] UserSharedPtr GetOwner() const;

    // ── 세션 상태 ──────────────────────────────────────────────────────────────
    // 로그인 요청은 접속 중(ST_ALLOC)에 한 번만 받는다 — 같은 세션의 두 번째 로그인 패킷은 버린다.
    [[nodiscard]] bool TryBeginLogin() noexcept
    {
        auto expected = SOCKET_STATE::ST_ALLOC;
        return m_eState.compare_exchange_strong(expected, SOCKET_STATE::ST_LOGIN);
    }
    // 로그인 실패 — 다시 시도할 수 있게 접속 중으로 되돌린다(이미 끊겼으면 그대로 둔다).
    void CancelLogin() noexcept
    {
        auto expected = SOCKET_STATE::ST_LOGIN;
        (void)m_eState.compare_exchange_strong(expected, SOCKET_STATE::ST_ALLOC);
    }
    // 로그인 완료 — 소유 CUser를 묶고 게임 중으로. 그 사이 끊겼으면(ST_CLOSED) false: 호출한 쪽이 CUser를 거둔다.
    // MarkClosed와 같은 락이라 "끊김 처리는 owner 없음을 봤는데 로그인은 owner를 묶는" 엇갈림이 없다.
    [[nodiscard]] bool TryEnterGame(const UserSharedPtr& owner)
    {
        lock_guard<mutex> guard(m_SessionLock);
        if (m_eState.load() != SOCKET_STATE::ST_LOGIN)
            return false;
        m_wpOwner = owner;
        m_eState.store(SOCKET_STATE::ST_INGAME);
        return true;
    }
    // 접속 끊김 — 상태를 닫고 그 순간의 owner를 돌려준다(nullptr이면 로그인 전).
    [[nodiscard]] UserSharedPtr MarkClosed()
    {
        lock_guard<mutex> guard(m_SessionLock);
        m_eState.store(SOCKET_STATE::ST_CLOSED);
        return m_wpOwner.lock();
    }
    // 끊김 처리는 한 번만(읽기 · 쓰기 실패와 서버가 먼저 닫은 경우가 겹쳐도).
    [[nodiscard]] bool TryRequestClose() noexcept { return !m_bCloseRequested.exchange(true); }
    [[nodiscard]] int GetPlayerId() const noexcept { return m_PlayerId.load(memory_order_acquire); }
    void SetPlayerId(int playerId) noexcept { m_PlayerId.store(playerId, memory_order_release); }
    [[nodiscard]] UInt8 GetNewClassId() const noexcept { return m_NewClassId.load(memory_order_relaxed); }
    void SetNewClassId(UInt8 classId) noexcept { m_NewClassId.store(classId, memory_order_relaxed); }
    // release/acquire — Zone을 옮길 때 새 Zone 큐에 입장 처리를 넣은 "뒤에" 바꾸므로, 새 값을 본 IO 스레드가 넣는 패킷은
    // 그 큐에서 반드시 입장 처리 뒤에 선다(HandleZoneTransfer)
    [[nodiscard]] ZoneId GetRoutingZoneId() const noexcept { return m_RoutingZoneId.load(memory_order_acquire); }
    void SetRoutingZoneId(ZoneId zoneId) noexcept { m_RoutingZoneId.store(zoneId, memory_order_release); }
    bool AttachSocket(std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void Start();
    void CloseSocket();
    void CloseSession();
    // 이미 넣은 패킷(끊는 이유 알림 등)을 다 보낸 뒤 닫는다. 1초 안에 못 보내면 그냥 닫는다.
    void CloseAfterFlush();
    void ResetNetworkState();

    // ── 묶어 보내기 ────────────────────────────────────────────────────
    // 로직 스레드(Zone)가 한 묶음의 일을 처리하는 동안 만든 패킷은 세션 큐에 모아 두었다가 묶음이 끝날 때
    // 세션마다 한 번에 보낸다 — 소켓이 받을 수 있으면 그 자리에서 쓰고(IO 스레드를 깨우지 않는다),
    // 다 못 쓴 나머지만 IO 스레드(Asio)에 넘긴다. 로직 스레드가 아닌 곳(DB · 서비스 · IO)은 예전처럼 바로 넘긴다.
    static void BeginDeferredSends() noexcept;
    static void FlushDeferredSends();

    template<typename Packet>
    bool PostSend(const Packet& packet)
    {
        static_assert(std::is_trivially_copyable_v<Packet>);
        return PostSendBytes(reinterpret_cast<const char*>(&packet), packet.size);
    }

private:
    bool PostSendBytes(const char* bytes, UInt16 size);
    void ReadNext();
    void WriteNext();
    void WriteBuffered(size_t offset);
    void OnBatchWritten(bool onIoThread);
    void FlushFromLogicThread();
    void QueueDisconnect();
};

