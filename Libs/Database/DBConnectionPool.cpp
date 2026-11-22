#include "pch.h"
#include "Database/DBConnectionPool.h"

CDBConnectionPool::CDBConnectionPool()
{
}

CDBConnectionPool::~CDBConnectionPool()
{
    Clear();
}

bool CDBConnectionPool::Connect(int connectionCount)
{
    vector<DBConnectionSharedPtr> newConnections;
    newConnections.reserve(connectionCount);

    for (Int32 i = 0; i < connectionCount; i++)
    {
        auto connection = make_shared<CDBConnection>();
        if (connection->Connect() == false)
        {
            std::cerr << "DB connection pool initialization failed at connection " << (i + 1) << "\n";
            return false;
        }

        newConnections.push_back(connection);
    }

    {
        std::scoped_lock lock(m_Lock);
        for (auto& connection : newConnections)
        {
            m_IdleConnections.push_back(connection);
            m_OwnedConnections.push_back(std::move(connection));
        }
    }

    m_Cv.notify_all();
    cout << "DB Connect Success" << endl;
    return true;
}

void CDBConnectionPool::Clear()
{
    {
        std::scoped_lock lock(m_Lock);
        m_IdleConnections.clear();
        m_OwnedConnections.clear();
    }
    m_Cv.notify_all();
}

DBConnectionSharedPtr CDBConnectionPool::Pop()
{
    std::unique_lock lock(m_Lock);
    m_Cv.wait(lock, [this]()
    {
        return m_IdleConnections.empty() == false || m_OwnedConnections.empty();
    });
    if (m_IdleConnections.empty())
        return nullptr;

    auto connection = m_IdleConnections.back();
    m_IdleConnections.pop_back();
    lock.unlock();
    if (!connection->EnsureConnected())
    {
        Push(std::move(connection));
        return nullptr;
    }
    return connection;
}

void CDBConnectionPool::Push(DBConnectionSharedPtr connection)
{
    {
        std::scoped_lock lock(m_Lock);
        m_IdleConnections.push_back(std::move(connection));
    }

    m_Cv.notify_one();
}
