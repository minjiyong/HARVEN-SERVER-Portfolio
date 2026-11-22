#include "pch.h"
#include "Network/GameSession.h"
#include "Network/GameSessionManager.h"
#include "Worker/WorkerThread.h"
#include "Worker/Route/Route.h"
#include "User/User.h"
#include "User/UserHelper.h"
#include "Content/Field/ZoneManager.h"
#include "Base/Core/ObjectPool.h"
#include <bit>
#include <cerrno>
#include <cstddef>
#include <sys/socket.h>

static_assert(std::endian::native == std::endian::little, "The current packet format uses little-endian integers");
static_assert(offsetof(USER_LOGIN_REQ_PACKET, size) == 0, "Packet framing requires a leading size field");

CGameSession::CGameSession() { ResetNetworkState(); }
CGameSession::~CGameSession() { CloseSession(); }

UserSharedPtr CGameSession::GetOwner() const
{
    std::lock_guard guard(m_SessionLock);
    return m_wpOwner.lock();
}

bool CGameSession::AttachSocket(std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    if (!socket || !socket->is_open()) return false;
    m_pSocket = std::move(socket);
    m_bNetworkClosed.store(false);
    boost::system::error_code ignored;
    m_pSocket->set_option(boost::asio::ip::tcp::no_delay(true), ignored);
    return true;
}

void CGameSession::Start()
{
    auto self = shared_from_this();
    boost::asio::post(m_pSocket->get_executor(), [self]() { self->ReadNext(); });
}

void CGameSession::CloseSocket()
{
    if (m_bNetworkClosed.exchange(true)) return;
    {
        std::lock_guard guard(m_SessionLock);
        m_PendingBytes.clear();
        m_bSendInFlight = false;
    }
    auto socket = m_pSocket;
    if (socket)
    {
        auto self = shared_from_this();
        boost::asio::post(socket->get_executor(), [self, socket]()
        {
            // 로직 스레드가 이 소켓에 바로 쓰는 중이면 끝날 때까지 기다린다(닫은 번호가 새 접속에 재사용되기 전에)
            std::lock_guard writeGuard(self->m_SocketWriteLock);
            boost::system::error_code ignored;
            socket->cancel(ignored);
            socket->shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored);
            socket->close(ignored);
        });
    }
}

void CGameSession::CloseSession() { CloseSocket(); }

void CGameSession::CloseAfterFlush()
{
    if (m_bNetworkClosed.load() || !m_pSocket) return;
    bool closeNow = false;
    bool startWrite = false;
    {
        std::lock_guard guard(m_SessionLock);
        m_bCloseAfterFlush.store(true);
        if (!m_bSendInFlight)
        {
            // 묶음 끝을 기다리던 패킷(끊는 이유 알림 등)이 있으면 지금 보내고 닫는다
            if (m_PendingBytes.empty()) closeNow = true;
            else { m_bSendInFlight = true; startWrite = true; }
        }
    }
    if (closeNow) { CloseSocket(); return; }
    if (startWrite)
    {
        auto self = shared_from_this();
        boost::asio::post(m_pSocket->get_executor(), [self]() { self->WriteNext(); });
    }

    // 상대가 읽지 않아 쓰기가 끝나지 않아도 1초 뒤에는 닫는다
    auto self = shared_from_this();
    auto timer = std::make_shared<boost::asio::steady_timer>(m_pSocket->get_executor(), std::chrono::seconds(1));
    timer->async_wait([self, timer](const boost::system::error_code&) { self->CloseSocket(); });
}

void CGameSession::ResetNetworkState()
{
    m_PendingRecvBytes = 0;
    SetRoutingZoneId(InvalidZoneId);
    std::lock_guard guard(m_SessionLock);
    m_PendingBytes.clear();
    m_WritingBytes.clear();
    m_bSendInFlight = false;
    m_bFlushQueued = false;
}

void CGameSession::QueueDisconnect()
{
    if (!TryRequestClose()) return;
    CloseSocket();
    CUserHelper::OnSessionClosed(shared_from_this());
}

namespace
{
    // 한 번 읽은 완성 패킷들 — Zone 작업 하나로 넘겨 그 스레드가 차례로 처리한다. 예전엔 패킷마다 작업(패킷 사본을 담은
    // std::function — 힙 할당)을 만들었다. 묶음은 CObjectPool에서 꺼내고 Zone 스레드가 다 처리한 뒤 돌려준다
    // (예전 Windows판 SList 패킷 작업 풀의 자리). 작업은 묶음 포인터만 들어 std::function 안에 그대로 담긴다(할당 없음).
    struct RecvBatch
    {
        SessionSharedPtr session;
        UInt32 size = 0;
        std::array<char, CGameSession::kRecvBufferSize> bytes;   // 완성 패킷만, 받은 순서대로 붙여 둔다

        RecvBatch() noexcept {}   // bytes를 0으로 채우지 않는다(size까지만 쓴다)
    };
    using RecvBatchPool = CObjectPool<RecvBatch>;

    struct RecvBatchRelease
    {
        void operator()(RecvBatch* batch) const noexcept { RecvBatchPool::Release(batch); }
    };

    void DispatchRecvBatch(RecvBatch* rawBatch)
    {
        const std::unique_ptr<RecvBatch, RecvBatchRelease> batch(rawBatch);
        // 패킷마다 따로 처리한다 — 앞 패킷으로 Zone을 옮겼으면 뒤 패킷은 Route가 새 Zone으로 넘긴다(예전과 같다)
        for (UInt32 offset = 0; offset < batch->size;)
        {
            const char* packet = batch->bytes.data() + offset;
            UInt16 packetSize = 0;
            std::memcpy(&packetSize, packet, sizeof(packetSize));
            Route::Dispatch(batch->session, packet);
            offset += packetSize;
        }
    }
}

void CGameSession::ReadNext()
{
    if (m_bNetworkClosed.load() || !m_pSocket) return;
    const size_t offset = m_PendingRecvBytes;
    if (offset >= m_RecvBuffer.size()) { QueueDisconnect(); return; }
    auto self = shared_from_this();
    m_pSocket->async_read_some(boost::asio::buffer(m_RecvBuffer.data() + offset, m_RecvBuffer.size() - offset),
        [self, offset](const boost::system::error_code& error, size_t bytes)
        {
            // 서버가 먼저 닫은 경우(CloseSession)도 끊김 처리를 한다 — 안 하면 CUser가 월드에 남는다.
            if (self->m_bNetworkClosed.load() || error || bytes == 0) { self->QueueDisconnect(); return; }

            const char* buffer = self->m_RecvBuffer.data();
            size_t available = offset + bytes;
            size_t consumed = 0;
            bool malformed = false;
            while (available - consumed >= sizeof(UInt16))
            {
                UInt16 packetSize = 0;
                std::memcpy(&packetSize, buffer + consumed, sizeof(packetSize));
                if (packetSize < sizeof(UInt16) + sizeof(UInt8) || packetSize > MAX_CLIENT_PACKET_SIZE ||
                    packetSize > self->m_RecvBuffer.size())
                {
                    malformed = true;
                    break;
                }
                if (packetSize > available - consumed) break;
                consumed += packetSize;
            }

            // 앞쪽 완성 패킷은 깨진 프레임이 뒤에 있어도 예전처럼 처리하고(끊김 처리보다 먼저 넣는다), 그다음 끊는다
            if (consumed > 0)
            {
                RecvBatch* batch = RecvBatchPool::Acquire();
                batch->session = self;
                batch->size = static_cast<UInt32>(consumed);
                std::memcpy(batch->bytes.data(), buffer, consumed);
                try
                {
                    GZoneManager->EnqueueBySession(self, [batch]() { DispatchRecvBatch(batch); });
                }
                catch (...)
                {
                    RecvBatchPool::Release(batch);   // 넣지 못했다(메모리 부족) — 작업은 포인터만 들어 묶음을 놓지 않는다
                    throw;
                }
            }
            if (malformed)
            {
                self->QueueDisconnect();
                return;
            }

            available -= consumed;
            if (available)
                std::memmove(self->m_RecvBuffer.data(), self->m_RecvBuffer.data() + consumed, available);
            self->m_PendingRecvBytes = static_cast<UInt32>(available);
            self->ReadNext();
        });
}

namespace
{
    // 이 스레드가 묶음 끝에 보낼 세션들(로직 스레드만 켠다)
    thread_local bool LDeferSends = false;
    thread_local std::vector<SessionSharedPtr> LDeferredSessions;
}

void CGameSession::BeginDeferredSends() noexcept
{
    LDeferSends = true;
}

void CGameSession::FlushDeferredSends()
{
    if (LDeferredSessions.empty())
        return;
    // 보내는 동안 새로 적히는 세션은 다음 차례에 — 목록을 바꿔 들고 돈다
    std::vector<SessionSharedPtr> sessions;
    sessions.swap(LDeferredSessions);
    for (const auto& session : sessions)
        session->FlushFromLogicThread();
    sessions.clear();
    if (LDeferredSessions.empty())
        LDeferredSessions.swap(sessions);   // 용량을 다시 쓴다
}

bool CGameSession::PostSendBytes(const char* bytes, UInt16 size)
{
    if (!bytes || size < sizeof(UInt16) || size > MAX_SERVER_PACKET_SIZE || m_bNetworkClosed.load() || m_bCloseAfterFlush.load() || !m_pSocket)
        return false;
    bool start = false;
    bool defer = false;
    {
        std::lock_guard guard(m_SessionLock);
        if (m_bNetworkClosed.load()) return false;
        m_PendingBytes.insert(m_PendingBytes.end(), bytes, bytes + size);
        if (!m_bSendInFlight)
        {
            if (LDeferSends)
            {
                if (!m_bFlushQueued) { m_bFlushQueued = true; defer = true; }
            }
            else
            {
                m_bSendInFlight = true;
                start = true;
            }
        }
    }
    if (defer)
        LDeferredSessions.push_back(shared_from_this());
    if (start)
    {
        auto self = shared_from_this();
        boost::asio::post(m_pSocket->get_executor(), [self]() { self->WriteNext(); });
    }
    return true;
}

void CGameSession::FlushFromLogicThread()
{
    {
        std::lock_guard guard(m_SessionLock);
        m_bFlushQueued = false;
        if (m_bSendInFlight || m_PendingBytes.empty() || m_bNetworkClosed.load() || !m_pSocket)
            return;
        m_bSendInFlight = true;
        m_WritingBytes.swap(m_PendingBytes);
    }

    // 소켓 버퍼에 자리가 있으면 그 자리에서 쓴다 — 대부분은 여기서 끝나 IO 스레드를 깨우지 않는다
    const size_t total = m_WritingBytes.size();
    size_t sent = 0;
    bool failed = false;
    {
        std::lock_guard writeGuard(m_SocketWriteLock);
        if (m_bNetworkClosed.load() || !m_pSocket->is_open())
        {
            failed = true;
        }
        else
        {
            const int fd = m_pSocket->native_handle();
            while (sent < total)
            {
                const ssize_t n = ::send(fd, m_WritingBytes.data() + sent, total - sent, MSG_DONTWAIT | MSG_NOSIGNAL);
                if (n > 0) { sent += static_cast<size_t>(n); continue; }
                if (n < 0 && errno == EINTR) continue;
                if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) failed = true;
                break;
            }
        }
    }

    auto self = shared_from_this();
    if (failed)
    {
        m_WritingBytes.clear();
        {
            std::lock_guard guard(m_SessionLock);
            m_bSendInFlight = false;
        }
        boost::asio::post(m_pSocket->get_executor(), [self]() { self->QueueDisconnect(); });
        return;
    }
    if (sent == total)
    {
        OnBatchWritten(false);
        return;
    }
    // 소켓 버퍼가 찼다 — 나머지는 IO 스레드가 비동기로 마저 쓴다
    boost::asio::post(m_pSocket->get_executor(), [self, sent]() { self->WriteBuffered(sent); });
}

void CGameSession::WriteNext()
{
    if (m_bNetworkClosed.load() || !m_pSocket) return;
    bool closeNow = false;
    {
        std::lock_guard guard(m_SessionLock);
        if (m_PendingBytes.empty())
        {
            m_bSendInFlight = false;
            closeNow = m_bCloseAfterFlush.load();
        }
        else
        {
            m_WritingBytes.swap(m_PendingBytes);
        }
    }
    if (m_WritingBytes.empty())
    {
        if (closeNow) CloseSocket();
        return;
    }
    WriteBuffered(0);
}

void CGameSession::WriteBuffered(size_t offset)
{
    if (m_bNetworkClosed.load() || !m_pSocket) return;
    auto self = shared_from_this();
    boost::asio::async_write(*m_pSocket, boost::asio::buffer(m_WritingBytes.data() + offset, m_WritingBytes.size() - offset),
        [self](const boost::system::error_code& error, size_t)
        {
            if (self->m_bNetworkClosed.load() || error) { self->QueueDisconnect(); return; }
            self->OnBatchWritten(true);
        });
}

// 한 번의 쓰기를 다 보냈다 — 그 사이 쌓인 것이 있으면 이어서 쓴다
void CGameSession::OnBatchWritten(bool onIoThread)
{
    m_WritingBytes.clear();
    bool more = false;
    bool closeNow = false;
    {
        std::lock_guard guard(m_SessionLock);
        // 검사와 락 사이에 CloseSocket이 큐를 비웠을 수 있다
        if (m_bNetworkClosed.load())
        {
            m_bSendInFlight = false;
            return;
        }
        more = !m_PendingBytes.empty();
        if (!more)
        {
            m_bSendInFlight = false;
            closeNow = m_bCloseAfterFlush.load();
        }
    }
    if (more)
    {
        if (onIoThread) { WriteNext(); return; }
        auto self = shared_from_this();
        boost::asio::post(m_pSocket->get_executor(), [self]() { self->WriteNext(); });
        return;
    }
    if (closeNow) CloseSocket();
}
