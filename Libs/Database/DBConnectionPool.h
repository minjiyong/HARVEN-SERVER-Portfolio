#pragma once
#include "Database/DBConnection.h"

class CDBConnectionPool
{
public:
    CDBConnectionPool();
    ~CDBConnectionPool();

    bool                        Connect(int connectionCount);
    void                        Clear();

    [[nodiscard]] DBConnectionSharedPtr Pop();
    void                        Push(DBConnectionSharedPtr connection);

private:
    std::mutex                  m_Lock;
    std::condition_variable     m_Cv;
    vector<DBConnectionSharedPtr> m_OwnedConnections;
    deque<DBConnectionSharedPtr>  m_IdleConnections;
};
