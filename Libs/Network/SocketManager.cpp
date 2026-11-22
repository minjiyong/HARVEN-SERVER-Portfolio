#include "pch.h"
#include "Network/SocketManager.h"
#include "Network/GameSession.h"
#include "Network/GameSessionManager.h"

#include <sys/resource.h>

namespace
{
    constexpr int CONCURRENT_ACCEPTS = 8;

    using WorkGuard = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;

    // IO 스레드마다 io_context 하나(concurrency hint 1 — 이 스레드만 돌린다. 다른 스레드의 post는 그대로 안전)
    std::vector<std::unique_ptr<boost::asio::io_context>> GIoContexts;
    std::vector<WorkGuard>                                 GIoWorkGuards;   // 할 일이 없어도 run()이 돌아오지 않게
    std::unique_ptr<boost::asio::ip::tcp::acceptor>        GAcceptor;
    std::atomic<UInt32>                                    GNextIoContext{ 0 };

    boost::asio::io_context& NextIoContext()
    {
        const UInt32 index = GNextIoContext.fetch_add(1, std::memory_order_relaxed) % static_cast<UInt32>(GIoContexts.size());
        return *GIoContexts[index];
    }

    void AcceptNext()
    {
        auto socket = std::make_shared<boost::asio::ip::tcp::socket>(NextIoContext());
        GAcceptor->async_accept(*socket, [socket](const boost::system::error_code& error)
        {
            if (!error)
            {
                auto session = GSessionManager->CreateSession();
                if (session && session->AttachSocket(socket))
                {
                    session->m_eState = SOCKET_STATE::ST_ALLOC;
                    session->Start();
                }
                else
                {
                    boost::system::error_code ignored;
                    socket->close(ignored);
                    if (session) GSessionManager->ReleaseSession(session.get());
                }
            }
            else if (error != boost::asio::error::operation_aborted)
            {
                std::cerr << "Accept error: " << error.message() << '\n';
            }

            if (GAcceptor->is_open()) AcceptNext();
        });
    }
}

CSocketManager::CSocketManager() = default;
CSocketManager::~CSocketManager() = default;
void CSocketManager::Init()
{
    const UInt32 count = (std::max)(1u, std::thread::hardware_concurrency());
    GIoContexts.clear();
    GIoWorkGuards.clear();
    for (UInt32 i = 0; i < count; ++i)
    {
        GIoContexts.push_back(std::make_unique<boost::asio::io_context>(1));
        GIoWorkGuards.push_back(boost::asio::make_work_guard(*GIoContexts.back()));
    }
    GAcceptor = std::make_unique<boost::asio::ip::tcp::acceptor>(*GIoContexts.front());

    struct rlimit rl{};
    if (getrlimit(RLIMIT_NOFILE, &rl) == 0)
    {
        const rlim_t target = 65535;
        rl.rlim_cur = (rl.rlim_max >= target) ? target : rl.rlim_max;
        if (setrlimit(RLIMIT_NOFILE, &rl) != 0)
        {
            rl.rlim_cur = rl.rlim_max;
            setrlimit(RLIMIT_NOFILE, &rl);
        }
        if (getrlimit(RLIMIT_NOFILE, &rl) == 0)
        {
            std::cout << "[Net] File descriptor limit (RLIMIT_NOFILE): " << rl.rlim_cur << " / " << rl.rlim_max << std::endl;
        }
    }
}

bool CSocketManager::MakeListenSocket()
{
    boost::system::error_code error;
    GAcceptor->open(boost::asio::ip::tcp::v4(), error);
    if (error) std::cerr << "Socket open failed: " << error.message() << '\n';
    return !error;
}

bool CSocketManager::Bind()
{
    boost::system::error_code error;
    GAcceptor->set_option(boost::asio::socket_base::reuse_address(true), error);
    if (!error) GAcceptor->bind({boost::asio::ip::tcp::v4(), static_cast<unsigned short>(GServerPort)}, error);
    if (error) std::cerr << "Bind failed: " << error.message() << '\n';
    return !error;
}

bool CSocketManager::Listen()
{
    boost::system::error_code error;
    GAcceptor->listen(boost::asio::socket_base::max_listen_connections, error);
    if (error) std::cerr << "Listen failed: " << error.message() << '\n';
    return !error;
}

bool CSocketManager::StartAccepting()
{
    if (!GAcceptor || !GAcceptor->is_open()) return false;
    for (int i = 0; i < CONCURRENT_ACCEPTS; ++i)
    {
        AcceptNext();
    }
    return true;
}

UInt32 CSocketManager::GetIoThreadCount() noexcept
{
    return static_cast<UInt32>(GIoContexts.size());
}

void CSocketManager::Run(UInt32 ioIndex)
{
    if (ioIndex < GIoContexts.size())
        GIoContexts[ioIndex]->run();
}

void CSocketManager::Clear()
{
    boost::system::error_code ignored;
    if (GAcceptor) GAcceptor->close(ignored);
    GIoWorkGuards.clear();
    for (auto& context : GIoContexts)
        context->stop();
}
