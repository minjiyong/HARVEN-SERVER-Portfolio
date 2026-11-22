#include "pch.h"
#include "Database/TransferSave.h"
#include "Database/DBConnection.h"
#include <sstream>
#include <codecvt>
#include <stdexcept>

namespace
{
    static_assert(sizeof(SQLWCHAR) == sizeof(char16_t), "unixODBC SQLWCHAR must be UTF-16");
    std::u16string ToSqlWide(const std::wstring& value)
    {
        std::wstring_convert<std::codecvt_utf8<wchar_t>> wide;
        std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> utf16;
        return utf16.from_bytes(wide.to_bytes(value));
    }
    std::wstring Utf8Config(const char* value)
    {
        if (value == nullptr || *value == '\0') return {};
        try { return std::wstring_convert<std::codecvt_utf8<wchar_t>>{}.from_bytes(value); }
        catch (const std::range_error&) { return {}; }
    }


    std::wstring OdbcValue(const std::wstring& value, bool alwaysBrace = false)
    {
        if (!alwaysBrace && value.find_first_of(L";{}") == std::wstring::npos)
            return value;
        std::wstring escaped = L"{";
        for (wchar_t ch : value)
        {
            escaped += ch;
            if (ch == L'}') escaped += L'}';
        }
        return escaped + L"}";
    }

    struct DBSettings
    {
        std::wstring driver, host, port, database, user, password, ca;   // 모두 환경 변수(HARVEN_DB_*)에서
    };

    DBSettings LoadSettings()
    {
        DBSettings settings;
        settings.driver = Utf8Config(std::getenv("HARVEN_DB_DRIVER"));
        settings.host = Utf8Config(std::getenv("HARVEN_DB_HOST"));
        settings.port = Utf8Config(std::getenv("HARVEN_DB_PORT"));
        settings.database = Utf8Config(std::getenv("HARVEN_DB_NAME"));
        settings.user = Utf8Config(std::getenv("HARVEN_DB_USER"));
        settings.password = Utf8Config(std::getenv("HARVEN_DB_PASSWORD"));
        settings.ca = Utf8Config(std::getenv("HARVEN_DB_CA"));
        return settings;
    }

    struct StatementCleanup
    {
        explicit StatementCleanup(CDBConnection& connection) : m_Connection(connection) { }
        ~StatementCleanup() { m_Connection.Unbind(); }

        CDBConnection& m_Connection;
    };

    // 연결 실패 진단은 끊긴 동안 한 번만 찍는다(DB 스레드가 끊긴 동안 계속 다시 붙어 보므로). 붙으면 다시 찍는다.
    std::atomic<bool> s_connectFailing{ false };
    // 연결 끊김을 본 횟수. DB 서버가 재시작되면 풀의 모든 커넥션이 끊기는데, 드라이버의 SQL_ATTR_CONNECTION_DEAD는
    // 쓰기 전까지 모를 수 있다(MariaDB Connector/ODBC 3.1) — 그래서 한 커넥션이 끊김을 보면 그 전에 붙은 커넥션은
    // 다음에 쓰기 전에 새로 붙는다. 평소(끊김 없음)에는 추가 왕복이 없다.
    std::atomic<UInt64> s_lossGeneration{ 0 };

    // 연결이 끊겨서 난 오류인가 — SQLSTATE 08xxx(연결 예외) 또는 MySQL/MariaDB 클라이언트의 연결 오류 번호.
    // 2002/2003 접속 불가 · 2006 server has gone away · 2013 lost connection · 2055 lost connection(SSL 등)
    // · 1053 server shutdown · 1927 connection killed · 4031 idle timeout으로 끊김
    bool IsConnectionLoss(char state0, char state1, SQLINTEGER native)
    {
        if (state0 == '0' && state1 == '8')
            return true;
        switch (native)
        {
        case 2002: case 2003: case 2006: case 2013: case 2055: case 1053: case 1927: case 4031:
            return true;
        default:
            return false;
        }
    }
}

CDBConnection::~CDBConnection()
{
    Clear();
}

bool CDBConnection::Connect()
{
    const DBSettings settings = LoadSettings();
    if (settings.driver.empty() || settings.host.empty() || settings.user.empty() ||
        settings.password.empty() || settings.ca.empty())
    {
        // 연결 수(8)만큼 부르므로 한 번만 찍는다. 값은 찍지 않는다(이름만)
        static std::atomic<bool> s_reported{ false };
        if (!s_reported.exchange(true))
        {
            std::cerr << "Missing DB setting:";
            if (settings.driver.empty()) std::cerr << " HARVEN_DB_DRIVER";
            if (settings.host.empty()) std::cerr << " HARVEN_DB_HOST";
            if (settings.user.empty()) std::cerr << " HARVEN_DB_USER";
            if (settings.password.empty()) std::cerr << " HARVEN_DB_PASSWORD";
            if (settings.ca.empty()) std::cerr << " HARVEN_DB_CA";
            std::cerr << " - fill harven.env (copy of harven.env.example) or export them\n";
        }
        return false;
    }
    const std::wstring connectionString =
        L"DRIVER=" + OdbcValue(settings.driver, true) + L";SERVER=" + OdbcValue(settings.host) +
        L";PORT=" + OdbcValue(settings.port.empty() ? L"3306" : settings.port) +
        L";DATABASE=" + OdbcValue(settings.database.empty() ? L"GameServerDB" : settings.database) +
        L";USER=" + OdbcValue(settings.user) + L";PASSWORD=" + OdbcValue(settings.password) +
        L";SSLMODE=VERIFY_IDENTITY;SSLCA=" + OdbcValue(settings.ca) + L";";

    // Allocate environment handle
    m_Retcode = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &m_pEnviroment);
    if (m_Retcode != SQL_SUCCESS && m_Retcode != SQL_SUCCESS_WITH_INFO)
        return false;

    // Set ODBC version environment attribute
    m_Retcode = SQLSetEnvAttr(m_pEnviroment, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
    if (m_Retcode != SQL_SUCCESS && m_Retcode != SQL_SUCCESS_WITH_INFO)
        return false;

    // Allocate connection handle
    m_Retcode = SQLAllocHandle(SQL_HANDLE_DBC, m_pEnviroment, &m_pConnection);
    if (m_Retcode != SQL_SUCCESS && m_Retcode != SQL_SUCCESS_WITH_INFO)
        return false;

    // Set login timeout to 5 seconds
    m_Retcode = SQLSetConnectAttr(m_pConnection, SQL_LOGIN_TIMEOUT, (SQLPOINTER)5, 0);
    if (m_Retcode != SQL_SUCCESS && m_Retcode != SQL_SUCCESS_WITH_INFO)
        return false;

    // Connect to the data source
    const auto sqlConnectionString = ToSqlWide(connectionString);
    m_Retcode = SQLDriverConnectW(m_pConnection, nullptr,
        (SQLWCHAR*)sqlConnectionString.c_str(), SQL_NTS, nullptr, 0, nullptr, SQL_DRIVER_NOPROMPT);
    if (m_Retcode != SQL_SUCCESS && m_Retcode != SQL_SUCCESS_WITH_INFO)
    {
        if (!s_connectFailing.exchange(true))
            HandleError(m_Retcode);
        return false;
    }
    s_connectFailing.store(false);
    m_LossGeneration = s_lossGeneration.load();


    // Allocate statement handle
    m_Retcode = SQLAllocHandle(SQL_HANDLE_STMT, m_pConnection, &m_pStatement);
    if (m_Retcode != SQL_SUCCESS && m_Retcode != SQL_SUCCESS_WITH_INFO)
    {
        HandleError(m_Retcode);
        return false;
    }

    return true;
}

bool CDBConnection::EnsureConnected()
{
    // 앞 호출이 연결 끊김으로 실패했거나, 이 커넥션이 붙은 뒤 다른 커넥션이 끊김을 봤으면
    // 드라이버가 아직 살아 있다고 해도 새로 붙는다.
    if (m_pConnection != SQL_NULL_HANDLE && m_pStatement != SQL_NULL_HANDLE && !m_bConnectionLost &&
        m_LossGeneration == s_lossGeneration.load())
    {
        SQLUINTEGER dead = SQL_CD_TRUE;
        const SQLRETURN result = ::SQLGetConnectAttr(m_pConnection, SQL_ATTR_CONNECTION_DEAD, &dead, 0, nullptr);
        if ((result == SQL_SUCCESS || result == SQL_SUCCESS_WITH_INFO) && dead == SQL_CD_FALSE)
            return true;
    }
    Clear();
    return Connect();
}

void CDBConnection::Clear()
{
    m_BoundText.clear();
    m_bConnectionLost = false;
    if (m_pStatement != SQL_NULL_HANDLE)
    {
        ::SQLFreeHandle(SQL_HANDLE_STMT, m_pStatement);
        m_pStatement = SQL_NULL_HANDLE;
    }

    if (m_pConnection != SQL_NULL_HANDLE)
    {
        ::SQLDisconnect(m_pConnection);
        ::SQLFreeHandle(SQL_HANDLE_DBC, m_pConnection);
        m_pConnection = SQL_NULL_HANDLE;
    }

    if (m_pEnviroment != SQL_NULL_HANDLE)
    {
        ::SQLFreeHandle(SQL_HANDLE_ENV, m_pEnviroment);
        m_pEnviroment = SQL_NULL_HANDLE;
    }
}

bool CDBConnection::Execute(const wchar_t* query)
{
    const auto sqlQuery = ToSqlWide(query);
    SQLRETURN ret = ::SQLExecDirectW(m_pStatement, (SQLWCHAR*)sqlQuery.c_str(), SQL_NTS);
    if (ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO)
        return true;

    HandleError(ret);
    return false;
}

CDBConnection::FetchResult CDBConnection::FetchRow()
{
    const SQLRETURN ret = ::SQLFetch(m_pStatement);
    if (ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO)
        return FetchResult::Row;
    if (ret == SQL_NO_DATA)
        return FetchResult::NoData;
    HandleError(ret);
    return FetchResult::Error;
}

bool CDBConnection::Fetch()
{
    return FetchRow() == FetchResult::Row;
}

int CDBConnection::GetRowCount()
{
    SQLLEN count = 0;
    SQLRETURN ret = ::SQLRowCount(m_pStatement, OUT & count);

    if (ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO)
        return static_cast<int>(count);

    return -1;
}

void CDBConnection::Unbind()
{
    SQLRETURN more = SQL_SUCCESS;
    while ((more = ::SQLMoreResults(m_pStatement)) == SQL_SUCCESS || more == SQL_SUCCESS_WITH_INFO) {}
    ::SQLFreeStmt(m_pStatement, SQL_UNBIND);
    ::SQLFreeStmt(m_pStatement, SQL_RESET_PARAMS);
    ::SQLFreeStmt(m_pStatement, SQL_CLOSE);
    m_BoundText.clear();
}

bool CDBConnection::BindParam(SQLUSMALLINT paramIndex, SQLSMALLINT cType, SQLSMALLINT sqlType, SQLULEN len, SQLPOINTER ptr, SQLLEN* index)
{
    SQLLEN bufferBytes = 0;
    if (cType == SQL_C_CHAR && (sqlType == SQL_VARCHAR || sqlType == SQL_WVARCHAR))
    {
        const char* input = static_cast<const char*>(ptr);
        const int bytes = static_cast<int>(len);
        try
        {
            m_BoundText.emplace_back(std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t>{}.from_bytes(input, input + bytes));
        }
        catch (const std::range_error&) { return false; }
        ptr = const_cast<char16_t*>(m_BoundText.back().c_str());
        len = static_cast<SQLULEN>((std::max)(size_t{1}, m_BoundText.back().size()));
        cType = SQL_C_WCHAR;
        sqlType = SQL_WVARCHAR;
        bufferBytes = static_cast<SQLLEN>((m_BoundText.back().size() + 1) * sizeof(SQLWCHAR));
        index = nullptr;
    }
    else if (cType == SQL_C_WCHAR)
        bufferBytes = static_cast<SQLLEN>((len + 1) * sizeof(SQLWCHAR));
    else if (cType == SQL_C_BINARY)
        bufferBytes = index != nullptr ? *index : static_cast<SQLLEN>(len);
    SQLRETURN ret = ::SQLBindParameter(m_pStatement, paramIndex, SQL_PARAM_INPUT, cType, sqlType, len, 0, ptr, bufferBytes, index);
    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO)
    {
        HandleError(ret);
        return false;
    }

    return true;
}

bool CDBConnection::BindCol(SQLUSMALLINT columnIndex, SQLSMALLINT cType, SQLULEN len, SQLPOINTER value, SQLLEN* index)
{
    SQLRETURN ret = ::SQLBindCol(m_pStatement, columnIndex, cType, value, len, index);
    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO)
    {
        HandleError(ret);
        return false;
    }

    return true;
}

void CDBConnection::HandleError(SQLRETURN ret)
{
    if (ret == SQL_SUCCESS)
        return;

    SQLSMALLINT handleType = SQL_HANDLE_STMT;
    SQLHANDLE handle = m_pStatement;

    if (handle == SQL_NULL_HANDLE)
    {
        handleType = SQL_HANDLE_DBC;
        handle = m_pConnection;
    }

    if (handle == SQL_NULL_HANDLE)
    {
        handleType = SQL_HANDLE_ENV;
        handle = m_pEnviroment;
    }

    if (handle == SQL_NULL_HANDLE)
        return;

    SQLSMALLINT index = 1;
    SQLCHAR sqlState[6]{};
    SQLCHAR errMsg[1024]{};
    SQLINTEGER nativeErr = 0;
    SQLSMALLINT msgLen = 0;
    while (::SQLGetDiagRecA(handleType, handle, index++, sqlState, &nativeErr,
        errMsg, static_cast<SQLSMALLINT>(sizeof(errMsg)), &msgLen) == SQL_SUCCESS)
    {
        if (IsConnectionLoss(static_cast<char>(sqlState[0]), static_cast<char>(sqlState[1]), nativeErr))
        {
            m_bConnectionLost = true;
            s_lossGeneration.fetch_add(1);
        }
        std::cerr << "ODBC [" << sqlState << "] " << errMsg << '\n';
    }
}

bool CDBConnection::ExtractInventory(int playerId, vector<DB_ITEM_INFO>& out)
{
    out.clear();
    StatementCleanup cleanup(*this);

    SQLINTEGER  pid = playerId;
    SQLSMALLINT slotIndex{}, itemId{}, count{};
    SQLCHAR     equipped{};
    SQLUINTEGER     enchant{};
    SQLBIGINT   extra{};
    SQLLEN cb_slot{}, cb_item{}, cb_count{}, cb_equipped{}, cb_enchant{}, cb_extra{};

    if (!BindParam(1, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&pid, nullptr)) return false;
    if (!Execute(L"CALL P_INVENTORY_SELECT(?)")) return false;
    if (!BindCol(1, SQL_SMALLINT, sizeof(slotIndex), &slotIndex, &cb_slot))     return false;
    if (!BindCol(2, SQL_SMALLINT, sizeof(itemId),    &itemId,    &cb_item))     return false;
    if (!BindCol(3, SQL_SMALLINT, sizeof(count),     &count,     &cb_count))    return false;
    if (!BindCol(4, SQL_BIT,      sizeof(equipped),  &equipped,  &cb_equipped)) return false;
    if (!BindCol(5, SQL_C_ULONG,  sizeof(enchant),   &enchant,   &cb_enchant))  return false;
    // V2: 희귀도·옵션(UserInventory.Extra BIGINT). NULL이면(마이그레이션 직후 등) 0으로 본다.
    if (!BindCol(6, SQL_C_SBIGINT, sizeof(extra),    &extra,     &cb_extra))    return false;

    // 슬롯 개수만큼 여러 행 — 더 없음(SQL_NO_DATA)까지 읽는다. 중간에 오류가 나면 실패(빈 가방으로 로그인하지 않게).
    FetchResult fetched;
    while ((fetched = FetchRow()) == FetchResult::Row)
    {
        DB_ITEM_INFO info{};
        info.m_SlotIndex  = static_cast<UInt16>(slotIndex);
        info.m_ItemId     = static_cast<UInt16>(itemId);
        info.m_Count      = static_cast<UInt16>(count);
        info.m_bEquipped  = (equipped != 0);
        info.m_Enchant    = static_cast<UInt32>(enchant);
        info.m_Extra      = (cb_extra == SQL_NULL_DATA || extra < 0) ? 0 : static_cast<UInt64>(extra);
        out.push_back(info);
    }
    if (fetched == FetchResult::Error)
    {
        out.clear();
        return false;
    }
    return true;
}

bool CDBConnection::SaveInventorySlot(int playerId, UInt16 slotIndex, UInt16 itemId, UInt16 count, bool equipped, UInt32 enchant, UInt64 extra)
{
    StatementCleanup cleanup(*this);

    wstring query = L"CALL P_INVENTORY_UPSERT(?, ?, ?, ?, ?, ?, ?)";

    SQLINTEGER  pid  = playerId;
    SQLSMALLINT slot = static_cast<SQLSMALLINT>(slotIndex);
    SQLSMALLINT item = static_cast<SQLSMALLINT>(itemId);
    SQLSMALLINT cnt  = static_cast<SQLSMALLINT>(count);
    SQLCHAR     eq   = equipped ? 1 : 0;
    SQLUINTEGER     enc  = enchant;
    SQLBIGINT   ext  = static_cast<SQLBIGINT>(extra & 0x3FFFFFFFFFFFFFFFull);   // 상위 2비트는 항상 0(ItemInstance)

    if (!BindParam(1, SQL_C_LONG,     SQL_INTEGER,  0, (SQLPOINTER)&pid,  nullptr)) return false;
    if (!BindParam(2, SQL_C_SHORT,    SQL_SMALLINT, 0, (SQLPOINTER)&slot, nullptr)) return false;
    if (!BindParam(3, SQL_C_SHORT,    SQL_SMALLINT, 0, (SQLPOINTER)&item, nullptr)) return false;
    if (!BindParam(4, SQL_C_SHORT,    SQL_SMALLINT, 0, (SQLPOINTER)&cnt,  nullptr)) return false;
    if (!BindParam(5, SQL_C_BIT,      SQL_BIT,      0, (SQLPOINTER)&eq,   nullptr)) return false;
    if (!BindParam(6, SQL_C_ULONG, SQL_INTEGER,  0, (SQLPOINTER)&enc,  nullptr)) return false;
    if (!BindParam(7, SQL_C_SBIGINT,  SQL_BIGINT,   0, (SQLPOINTER)&ext,  nullptr)) return false;
    return Execute(query.c_str());
}

bool CDBConnection::DeleteInventorySlot(int playerId, UInt16 slotIndex)
{
    StatementCleanup cleanup(*this);

    wstring query = L"CALL P_INVENTORY_DELETE(?, ?)";

    SQLINTEGER  pid  = playerId;
    SQLSMALLINT slot = static_cast<SQLSMALLINT>(slotIndex);

    if (!BindParam(1, SQL_C_LONG,  SQL_INTEGER,  0, (SQLPOINTER)&pid,  nullptr)) return false;
    if (!BindParam(2, SQL_C_SHORT, SQL_SMALLINT, 0, (SQLPOINTER)&slot, nullptr)) return false;
    return Execute(query.c_str());
}

bool CDBConnection::SaveTwoInventorySlots(int playerId, const DB_ITEM_SLOT_SAVE& a, const DB_ITEM_SLOT_SAVE& b)
{
    StatementCleanup cleanup(*this);

    wstring query = L"CALL P_INVENTORY_SWAP(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";

    SQLINTEGER  pid    = playerId;
    SQLSMALLINT slotA  = static_cast<SQLSMALLINT>(a.slotIndex);
    SQLCHAR     hasA   = a.hasItem ? 1 : 0;
    SQLSMALLINT itemA  = static_cast<SQLSMALLINT>(a.itemId);
    SQLSMALLINT cntA   = static_cast<SQLSMALLINT>(a.count);
    SQLCHAR     eqA    = a.equipped ? 1 : 0;
    SQLSMALLINT slotB  = static_cast<SQLSMALLINT>(b.slotIndex);
    SQLCHAR     hasB   = b.hasItem ? 1 : 0;
    SQLSMALLINT itemB  = static_cast<SQLSMALLINT>(b.itemId);
    SQLSMALLINT cntB   = static_cast<SQLSMALLINT>(b.count);
    SQLCHAR     eqB    = b.equipped ? 1 : 0;
    SQLUINTEGER     encA   = a.enchant;
    SQLUINTEGER     encB   = b.enchant;
    SQLBIGINT   extA   = static_cast<SQLBIGINT>(a.extra & 0x3FFFFFFFFFFFFFFFull);
    SQLBIGINT   extB   = static_cast<SQLBIGINT>(b.extra & 0x3FFFFFFFFFFFFFFFull);

    if (!BindParam(1,  SQL_C_LONG,  SQL_INTEGER,  0, (SQLPOINTER)&pid,   nullptr)) return false;
    if (!BindParam(2,  SQL_C_SHORT, SQL_SMALLINT, 0, (SQLPOINTER)&slotA, nullptr)) return false;
    if (!BindParam(3,  SQL_C_BIT,   SQL_BIT,      0, (SQLPOINTER)&hasA,  nullptr)) return false;
    if (!BindParam(4,  SQL_C_SHORT, SQL_SMALLINT, 0, (SQLPOINTER)&itemA, nullptr)) return false;
    if (!BindParam(5,  SQL_C_SHORT, SQL_SMALLINT, 0, (SQLPOINTER)&cntA,  nullptr)) return false;
    if (!BindParam(6,  SQL_C_BIT,   SQL_BIT,      0, (SQLPOINTER)&eqA,   nullptr)) return false;
    if (!BindParam(7,  SQL_C_SHORT, SQL_SMALLINT, 0, (SQLPOINTER)&slotB, nullptr)) return false;
    if (!BindParam(8,  SQL_C_BIT,   SQL_BIT,      0, (SQLPOINTER)&hasB,  nullptr)) return false;
    if (!BindParam(9,  SQL_C_SHORT, SQL_SMALLINT, 0, (SQLPOINTER)&itemB, nullptr)) return false;
    if (!BindParam(10, SQL_C_SHORT, SQL_SMALLINT, 0, (SQLPOINTER)&cntB,  nullptr)) return false;
    if (!BindParam(11, SQL_C_BIT,   SQL_BIT,      0, (SQLPOINTER)&eqB,   nullptr)) return false;
    // 강화 수치는 프로시저 끝 두 파라미터(@enchantA, @enchantB)로 붙였다.
    if (!BindParam(12, SQL_C_ULONG, SQL_INTEGER, 0, (SQLPOINTER)&encA, nullptr)) return false;
    if (!BindParam(13, SQL_C_ULONG, SQL_INTEGER, 0, (SQLPOINTER)&encB, nullptr)) return false;
    // V2 희귀도·옵션은 그 뒤 두 파라미터(@extraA, @extraB)
    if (!BindParam(14, SQL_C_SBIGINT,  SQL_BIGINT,  0, (SQLPOINTER)&extA, nullptr)) return false;
    if (!BindParam(15, SQL_C_SBIGINT,  SQL_BIGINT,  0, (SQLPOINTER)&extB, nullptr)) return false;
    return Execute(query.c_str());
}

bool CDBConnection::SavePlayerAdena(int playerId, UInt32 adena)
{
    StatementCleanup cleanup(*this);

    wstring query = L"CALL P_USER_ADENA_UPSERT(?, ?)";

    SQLINTEGER pid = playerId;
    SQLINTEGER amt = static_cast<SQLINTEGER>(adena);

    if (!BindParam(1, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&pid, nullptr)) return false;
    if (!BindParam(2, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&amt, nullptr)) return false;
    return Execute(query.c_str());
}

bool CDBConnection::SavePlayerAlignment(int playerId, Int16 alignment, UInt16 pkCount)
{
    StatementCleanup cleanup(*this);

    wstring query = L"CALL P_USER_ALIGNMENT_UPSERT(?, ?, ?)";

    SQLINTEGER  pid = playerId;
    SQLSMALLINT al  = alignment;
    SQLSMALLINT pk  = static_cast<SQLSMALLINT>(pkCount);

    if (!BindParam(1, SQL_C_LONG,  SQL_INTEGER,  0, (SQLPOINTER)&pid, nullptr)) return false;
    if (!BindParam(2, SQL_C_SHORT, SQL_SMALLINT, 0, (SQLPOINTER)&al,  nullptr)) return false;
    if (!BindParam(3, SQL_C_SHORT, SQL_SMALLINT, 0, (SQLPOINTER)&pk,  nullptr)) return false;
    return Execute(query.c_str());
}

bool CDBConnection::ShopTransaction(int playerId, UInt32 adena, const DB_ITEM_SLOT_SAVE& slot)
{
    StatementCleanup cleanup(*this);

    wstring query = L"CALL P_SHOP_TRADE(?, ?, ?, ?, ?, ?, ?, ?)";

    SQLINTEGER  pid   = playerId;
    SQLINTEGER  amt   = static_cast<SQLINTEGER>(adena);
    SQLSMALLINT idx   = static_cast<SQLSMALLINT>(slot.slotIndex);
    SQLCHAR     has   = slot.hasItem ? 1 : 0;
    SQLSMALLINT item  = static_cast<SQLSMALLINT>(slot.itemId);
    SQLSMALLINT cnt   = static_cast<SQLSMALLINT>(slot.count);
    SQLCHAR     eq    = slot.equipped ? 1 : 0;
    SQLUINTEGER     enc   = slot.enchant;

    if (!BindParam(1, SQL_C_LONG,     SQL_INTEGER,  0, (SQLPOINTER)&pid,  nullptr)) return false;
    if (!BindParam(2, SQL_C_LONG,     SQL_INTEGER,  0, (SQLPOINTER)&amt,  nullptr)) return false;
    if (!BindParam(3, SQL_C_SHORT,    SQL_SMALLINT, 0, (SQLPOINTER)&idx,  nullptr)) return false;
    if (!BindParam(4, SQL_C_BIT,      SQL_BIT,      0, (SQLPOINTER)&has,  nullptr)) return false;
    if (!BindParam(5, SQL_C_SHORT,    SQL_SMALLINT, 0, (SQLPOINTER)&item, nullptr)) return false;
    if (!BindParam(6, SQL_C_SHORT,    SQL_SMALLINT, 0, (SQLPOINTER)&cnt,  nullptr)) return false;
    if (!BindParam(7, SQL_C_BIT,      SQL_BIT,      0, (SQLPOINTER)&eq,   nullptr)) return false;
    if (!BindParam(8, SQL_C_ULONG, SQL_INTEGER,  0, (SQLPOINTER)&enc,  nullptr)) return false;
    return Execute(query.c_str());
}

// ─────────────────────────────────────────────────────────────────────────────
// V2 캐릭터 진행 데이터 — UserProgress(UserID, Data VARBINARY(4000))
// ─────────────────────────────────────────────────────────────────────────────
bool CDBConnection::LoadPlayerProgress(int playerId, std::vector<UInt8>& out)
{
    out.clear();
    StatementCleanup cleanup(*this);
    wstring query = L"CALL P_USER_PROGRESS_SELECT(?)";
    SQLINTEGER pid = playerId;
    std::vector<UInt8> buffer(4000);
    SQLLEN cb_data{};
    if (!BindParam(1, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&pid, nullptr)) return false;
    if (!Execute(query.c_str())) return false;
    if (!BindCol(1, SQL_C_BINARY, buffer.size(), buffer.data(), &cb_data)) return false;
    switch (FetchRow())
    {
    case FetchResult::NoData: return true;    // 행 없음 = 아직 저장한 적 없는 캐릭터(기본값으로 시작)
    case FetchResult::Error:  return false;   // 읽다 실패 — 기본값으로 시작하면 다음 저장이 진행을 덮어쓴다
    case FetchResult::Row:    break;
    }
    if (cb_data > 0 && static_cast<size_t>(cb_data) <= buffer.size())
        out.assign(buffer.begin(), buffer.begin() + cb_data);
    return true;
}

bool CDBConnection::SavePlayerProgress(int playerId, const std::vector<UInt8>& data)
{
    StatementCleanup cleanup(*this);
    wstring query = L"CALL P_USER_PROGRESS_UPSERT(?, ?)";
    SQLINTEGER pid = playerId;
    SQLLEN cb_data = static_cast<SQLLEN>(data.size());
    if (!BindParam(1, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&pid, nullptr)) return false;
    if (!BindParam(2, SQL_C_BINARY, SQL_VARBINARY, 4000, (SQLPOINTER)data.data(), &cb_data)) return false;
    return Execute(query.c_str());
}

// ─────────────────────────────────────────────────────────────────────────────
// Phase 2-D 기술 성장 — UserSkill(UserID, SkillID, Level, Pick1, Pick2)
// ─────────────────────────────────────────────────────────────────────────────
bool CDBConnection::LoadPlayerSkills(int playerId, std::vector<DB_SKILL_INFO>& out)
{
    out.clear();
    StatementCleanup cleanup(*this);
    wstring query = L"CALL P_USER_SKILL_SELECT(?)";
    SQLINTEGER  pid = playerId;
    SQLSMALLINT skillId{};
    SQLCHAR     level{}, pick1{}, pick2{};
    SQLLEN cb_id{}, cb_level{}, cb_p1{}, cb_p2{};
    if (!BindParam(1, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&pid, nullptr)) return false;
    if (!Execute(query.c_str())) return false;
    if (!BindCol(1, SQL_C_SHORT,    sizeof(skillId), &skillId, &cb_id))    return false;
    if (!BindCol(2, SQL_C_UTINYINT, sizeof(level),   &level,   &cb_level)) return false;
    if (!BindCol(3, SQL_C_UTINYINT, sizeof(pick1),   &pick1,   &cb_p1))    return false;
    if (!BindCol(4, SQL_C_UTINYINT, sizeof(pick2),   &pick2,   &cb_p2))    return false;
    FetchResult fetched;
    while ((fetched = FetchRow()) == FetchResult::Row)
    {
        DB_SKILL_INFO info{};
        info.m_SkillId = static_cast<UInt16>(skillId);
        info.m_Level   = static_cast<UInt8>(level);
        info.m_Pick1   = static_cast<UInt8>(pick1);
        info.m_Pick2   = static_cast<UInt8>(pick2);
        out.push_back(info);
    }
    if (fetched == FetchResult::Error) { out.clear(); return false; }
    return true;
}

bool CDBConnection::SavePlayerSkill(int playerId, const DB_SKILL_INFO& skill)
{
    StatementCleanup cleanup(*this);
    wstring query = L"CALL P_USER_SKILL_UPSERT(?, ?, ?, ?, ?)";
    SQLINTEGER  pid   = playerId;
    SQLSMALLINT sid   = static_cast<SQLSMALLINT>(skill.m_SkillId);
    SQLCHAR     level = skill.m_Level;
    SQLCHAR     p1    = skill.m_Pick1;
    SQLCHAR     p2    = skill.m_Pick2;
    if (!BindParam(1, SQL_C_LONG,     SQL_INTEGER,  0, (SQLPOINTER)&pid,   nullptr)) return false;
    if (!BindParam(2, SQL_C_SHORT,    SQL_SMALLINT, 0, (SQLPOINTER)&sid,   nullptr)) return false;
    if (!BindParam(3, SQL_C_UTINYINT, SQL_TINYINT,  0, (SQLPOINTER)&level, nullptr)) return false;
    if (!BindParam(4, SQL_C_UTINYINT, SQL_TINYINT,  0, (SQLPOINTER)&p1,    nullptr)) return false;
    if (!BindParam(5, SQL_C_UTINYINT, SQL_TINYINT,  0, (SQLPOINTER)&p2,    nullptr)) return false;
    return Execute(query.c_str());
}

bool CDBConnection::ResetPlayerSkills(int playerId)
{
    StatementCleanup cleanup(*this);
    wstring query = L"CALL P_USER_SKILL_DELETE(?)";
    SQLINTEGER pid = playerId;
    if (!BindParam(1, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&pid, nullptr)) return false;
    return Execute(query.c_str());
}

// ─────────────────────────────────────────────────────────────────────────────
// Phase 5 결사 · 경매장(Shared/Database/mysql/setup.sql)
// 프로시저는 결과 한 줄(Ok)을 돌려준다 — 상태 검사에 걸리면 0이고 아무것도 바뀌지 않는다(XACT_ABORT + 한 트랜잭션).
// 결사 이름 · 공지는 한글이라 UTF-8 → UTF-16으로 바꿔 NVARCHAR로 넘기고, 읽을 때 되돌린다.
// ─────────────────────────────────────────────────────────────────────────────
namespace
{
    using DbTextChar = SQLWCHAR;
    std::u16string Utf8ToWide(const string& s)
    {
        try { return std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t>{}.from_bytes(s); }
        catch (const std::range_error&) { return {}; }
    }
    string WideToUtf8(const SQLWCHAR* w, SQLLEN bytes)
    {
        if (!w || bytes <= 0) return {};
        const auto* first = reinterpret_cast<const char16_t*>(w);
        const size_t count = static_cast<size_t>(bytes) / sizeof(SQLWCHAR);
        return std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t>{}.to_bytes(first, first + count);
    }

    // P_USER_SELECT · P_USER_SELECT_BY_NAME 결과 한 줄(두 프로시저가 같은 열을 같은 순서로 돌려준다)
    // UserID, Name, PosX, PosY, Level, Exp, Adena, Alignment, PkCount. 행이 없으면 found = false로 성공.
    bool ReadUserRow(CDBConnection& c, DB_USER_INFO& out, bool& found)
    {
        SQLINTEGER  id{}, x{}, y{}, exp{}, adena{};
        SQLCHAR     level{};
        SQLSMALLINT alignment{}, pk{};
        DbTextChar  name[64]{};   // VARCHAR(20) = UTF-16 40단위 이하
        SQLLEN cb_id{}, cb_name{}, cb_x{}, cb_y{}, cb_level{}, cb_exp{}, cb_adena{}, cb_alignment{}, cb_pk{};
        if (!c.BindCol(1, SQL_C_SLONG,    sizeof(id),        &id,        &cb_id))        return false;
        if (!c.BindCol(2, SQL_C_WCHAR,    sizeof(name),      name,       &cb_name))      return false;
        if (!c.BindCol(3, SQL_C_SLONG,    sizeof(x),         &x,         &cb_x))         return false;
        if (!c.BindCol(4, SQL_C_SLONG,    sizeof(y),         &y,         &cb_y))         return false;
        if (!c.BindCol(5, SQL_C_UTINYINT, sizeof(level),     &level,     &cb_level))     return false;
        if (!c.BindCol(6, SQL_C_SLONG,    sizeof(exp),       &exp,       &cb_exp))       return false;
        if (!c.BindCol(7, SQL_C_SLONG,    sizeof(adena),     &adena,     &cb_adena))     return false;
        if (!c.BindCol(8, SQL_C_SSHORT,   sizeof(alignment), &alignment, &cb_alignment)) return false;
        if (!c.BindCol(9, SQL_C_SSHORT,   sizeof(pk),        &pk,        &cb_pk))        return false;
        switch (c.FetchRow())
        {
        case CDBConnection::FetchResult::NoData:
            found = false;
            return true;
        case CDBConnection::FetchResult::Error:
            return false;
        case CDBConnection::FetchResult::Row:
            break;
        }
        if (cb_id == SQL_NULL_DATA || id <= 0)
            return false;
        const SQLLEN maxNameBytes = static_cast<SQLLEN>(sizeof(name) - sizeof(DbTextChar));
        out = DB_USER_INFO{};
        out.m_PlayerId  = static_cast<int>(id);
        out.m_Name      = cb_name == SQL_NULL_DATA ? string() : WideToUtf8(name, (std::min)(cb_name, maxNameBytes));
        out.m_X         = x;
        out.m_Y         = y;
        out.m_Level     = static_cast<UInt8>(level);
        out.m_Exp       = static_cast<UInt32>(exp);
        out.m_Adena     = adena > 0 ? static_cast<UInt32>(adena) : 0;
        out.m_Alignment = static_cast<Int16>(alignment);
        out.m_PkCount   = pk > 0 ? static_cast<UInt16>(pk) : 0;
        found = true;
        return true;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 캐릭터(User). 이름은 로그인에서만 쓴다(P_USER_SELECT_BY_NAME · P_USER_CREATE) — 그 뒤로는 UserID.
// ─────────────────────────────────────────────────────────────────────────────
bool CDBConnection::ExtractUserInfo(const string& name, DB_USER_INFO& out, bool& found)
{
    found = false;
    StatementCleanup cleanup(*this);
    if (!BindParam(1, SQL_C_CHAR, SQL_WVARCHAR, name.size(), (SQLPOINTER)name.c_str(), nullptr)) return false;
    if (!Execute(L"CALL P_USER_SELECT_BY_NAME(?)")) return false;
    return ReadUserRow(*this, out, found);
}

bool CDBConnection::ExtractUserInfo(int playerId, DB_USER_INFO& out, bool& found)
{
    found = false;
    StatementCleanup cleanup(*this);
    SQLINTEGER pid = playerId;
    if (!BindParam(1, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&pid, nullptr)) return false;
    if (!Execute(L"CALL P_USER_SELECT(?)")) return false;
    return ReadUserRow(*this, out, found);
}

bool CDBConnection::AddUserInfoInDataBase(const string& name, const string& password, int& outPlayerId, bool& created)
{
    created = false;
    StatementCleanup cleanup(*this);
    if (!BindParam(1, SQL_C_CHAR, SQL_VARCHAR, name.size(),     (SQLPOINTER)name.c_str(),     nullptr)) return false;
    if (!BindParam(2, SQL_C_CHAR, SQL_VARCHAR, password.size(), (SQLPOINTER)password.c_str(), nullptr)) return false;
    if (!Execute(L"CALL P_USER_CREATE(?, ?)")) return false;

    SQLINTEGER playerId{};
    SQLCHAR    createdFlag{};
    SQLLEN cb_playerId{}, cb_created{};
    if (!BindCol(1, SQL_C_SLONG,    sizeof(playerId),    &playerId,    &cb_playerId)) return false;
    if (!BindCol(2, SQL_C_UTINYINT, sizeof(createdFlag), &createdFlag, &cb_created))  return false;
    if (FetchRow() != FetchResult::Row || cb_playerId == SQL_NULL_DATA || cb_created == SQL_NULL_DATA || playerId <= 0)
        return false;
    outPlayerId = static_cast<int>(playerId);
    created     = createdFlag != 0;
    return true;
}

bool CDBConnection::SaveUserInfo(int playerId, short x, short y, UInt8 level, UInt32 exp)
{
    StatementCleanup cleanup(*this);
    SQLINTEGER pid = playerId;
    if (!BindParam(1, SQL_C_LONG,     SQL_INTEGER, 0, (SQLPOINTER)&pid,   nullptr)) return false;
    if (!BindParam(2, SQL_C_SHORT,    SQL_INTEGER, 0, (SQLPOINTER)&x,     nullptr)) return false;
    if (!BindParam(3, SQL_C_SHORT,    SQL_INTEGER, 0, (SQLPOINTER)&y,     nullptr)) return false;
    if (!BindParam(4, SQL_C_UTINYINT, SQL_TINYINT, 0, (SQLPOINTER)&level, nullptr)) return false;
    if (!BindParam(5, SQL_C_ULONG,    SQL_INTEGER, 0, (SQLPOINTER)&exp,   nullptr)) return false;
    return Execute(L"CALL P_USER_UPSERT(?, ?, ?, ?, ?)");
}

// 결과 한 줄(Ok)을 읽는다 — 프로시저가 SELECT v_ok AS Ok로 끝난다
#define PHASE5_FETCH_OK()                                                          \
    do {                                                                           \
        SQLCHAR ok_{}; SQLLEN cb_ok_{};                                            \
        if (!BindCol(1, SQL_C_BIT, sizeof(ok_), &ok_, &cb_ok_)) return false;      \
        if (!Fetch()) return false;                                                \
        return ok_ != 0;                                                           \
    } while (false)

bool CDBConnection::LoadGuilds(vector<DB_GUILD_ROW>& out)
{
    out.clear();
    StatementCleanup cleanup(*this);
    wstring query = L"CALL P_GUILD_SELECT_ALL()";
    SQLINTEGER gid{}, leader{};
    SQLCHAR emblem{};
    DbTextChar name[32]{}, notice[48]{};
    SQLLEN cb_gid{}, cb_name{}, cb_emblem{}, cb_notice{}, cb_leader{};
    if (!Execute(query.c_str())) return false;
    if (!BindCol(1, SQL_C_LONG, sizeof(gid), &gid, &cb_gid)) return false;
    if (!BindCol(2, SQL_C_WCHAR, sizeof(name), name, &cb_name)) return false;
    if (!BindCol(3, SQL_C_UTINYINT, sizeof(emblem), &emblem, &cb_emblem)) return false;
    if (!BindCol(4, SQL_C_WCHAR, sizeof(notice), notice, &cb_notice)) return false;
    if (!BindCol(5, SQL_C_LONG, sizeof(leader), &leader, &cb_leader)) return false;
    FetchResult fetched;
    while ((fetched = FetchRow()) == FetchResult::Row)
    {
        DB_GUILD_ROW r;
        r.guildId = static_cast<int>(gid);
        r.name = WideToUtf8(name, cb_name);
        r.emblem = static_cast<UInt8>(emblem);
        r.notice = cb_notice == SQL_NULL_DATA ? string() : WideToUtf8(notice, cb_notice);
        r.leaderId = static_cast<int>(leader);
        out.push_back(std::move(r));
    }
    if (fetched == FetchResult::Error) { out.clear(); return false; }
    return true;
}

bool CDBConnection::LoadGuildMembers(vector<DB_GUILD_MEMBER_ROW>& out)
{
    out.clear();
    StatementCleanup cleanup(*this);
    wstring query = L"CALL P_GUILD_MEMBER_SELECT_ALL()";
    SQLINTEGER gid{}, pid{};
    SQLCHAR rank{}, level{};
    DbTextChar name[NAME_SIZE + 1]{};
    SQLBIGINT seen{};
    SQLLEN cb_gid{}, cb_pid{}, cb_rank{}, cb_name{}, cb_level{}, cb_seen{};
    if (!Execute(query.c_str())) return false;
    if (!BindCol(1, SQL_C_LONG, sizeof(gid), &gid, &cb_gid)) return false;
    if (!BindCol(2, SQL_C_LONG, sizeof(pid), &pid, &cb_pid)) return false;
    if (!BindCol(3, SQL_C_UTINYINT, sizeof(rank), &rank, &cb_rank)) return false;
    if (!BindCol(4, SQL_C_WCHAR, sizeof(name), name, &cb_name)) return false;
    if (!BindCol(5, SQL_C_UTINYINT, sizeof(level), &level, &cb_level)) return false;
    if (!BindCol(6, SQL_C_SBIGINT, sizeof(seen), &seen, &cb_seen)) return false;
    FetchResult fetched;
    while ((fetched = FetchRow()) == FetchResult::Row)
    {
        DB_GUILD_MEMBER_ROW r;
        r.guildId = static_cast<int>(gid);
        r.playerId = static_cast<int>(pid);
        r.rank = static_cast<UInt8>(rank);
        r.name = WideToUtf8(name, cb_name);
        r.level = static_cast<UInt8>(level);
        r.lastSeenMin = (cb_seen == SQL_NULL_DATA || seen <= 0) ? 0u : static_cast<UInt32>(seen / 60);
        out.push_back(std::move(r));
    }
    if (fetched == FetchResult::Error) { out.clear(); return false; }
    return true;
}

bool CDBConnection::GuildCreate(int playerId, UInt32 adenaAfter, int guildId, const string& name, const string& nameKey, UInt8 emblem)
{
    StatementCleanup cleanup(*this);
    wstring query = L"CALL P_GUILD_CREATE(?, ?, ?, ?, ?, ?)";
    SQLINTEGER pid = playerId, gid = guildId;
    SQLINTEGER adena = static_cast<SQLINTEGER>(adenaAfter);
    SQLCHAR em = emblem;
    const auto wname = Utf8ToWide(name), wkey = Utf8ToWide(nameKey);
    SQLLEN cb_name = static_cast<SQLLEN>(wname.size() * sizeof(wname[0])), cb_key = static_cast<SQLLEN>(wkey.size() * sizeof(wkey[0]));
    if (!BindParam(1, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&pid, nullptr)) return false;
    if (!BindParam(2, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&adena, nullptr)) return false;
    if (!BindParam(3, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&gid, nullptr)) return false;
    if (!BindParam(4, SQL_C_WCHAR, SQL_WVARCHAR, 16, (SQLPOINTER)wname.c_str(), &cb_name)) return false;
    if (!BindParam(5, SQL_C_WCHAR, SQL_WVARCHAR, 16, (SQLPOINTER)wkey.c_str(), &cb_key)) return false;
    if (!BindParam(6, SQL_C_UTINYINT, SQL_TINYINT, 0, (SQLPOINTER)&em, nullptr)) return false;
    if (!Execute(query.c_str())) return false;
    PHASE5_FETCH_OK();
}

bool CDBConnection::GuildMemberSet(int guildId, int playerId, UInt8 rank)
{
    StatementCleanup cleanup(*this);
    wstring query = L"CALL P_GUILD_MEMBER_UPSERT(?, ?, ?)";
    SQLINTEGER gid = guildId, pid = playerId;
    SQLCHAR rk = rank;
    if (!BindParam(1, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&gid, nullptr)) return false;
    if (!BindParam(2, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&pid, nullptr)) return false;
    if (!BindParam(3, SQL_C_UTINYINT, SQL_TINYINT, 0, (SQLPOINTER)&rk, nullptr)) return false;
    if (!Execute(query.c_str())) return false;
    PHASE5_FETCH_OK();
}

bool CDBConnection::GuildMemberRemove(int guildId, int playerId)
{
    StatementCleanup cleanup(*this);
    wstring query = L"CALL P_GUILD_MEMBER_DELETE(?, ?)";
    SQLINTEGER gid = guildId, pid = playerId;
    if (!BindParam(1, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&gid, nullptr)) return false;
    if (!BindParam(2, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&pid, nullptr)) return false;
    if (!Execute(query.c_str())) return false;
    PHASE5_FETCH_OK();
}

bool CDBConnection::GuildTransfer(int guildId, int fromId, int toId)
{
    StatementCleanup cleanup(*this);
    wstring query = L"CALL P_GUILD_TRANSFER(?, ?, ?)";
    SQLINTEGER gid = guildId, a = fromId, b = toId;
    if (!BindParam(1, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&gid, nullptr)) return false;
    if (!BindParam(2, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&a, nullptr)) return false;
    if (!BindParam(3, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&b, nullptr)) return false;
    if (!Execute(query.c_str())) return false;
    PHASE5_FETCH_OK();
}

bool CDBConnection::GuildSetInfo(int guildId, UInt8 emblem, const string& notice)
{
    StatementCleanup cleanup(*this);
    wstring query = L"CALL P_GUILD_UPSERT(?, ?, ?)";
    SQLINTEGER gid = guildId;
    SQLCHAR em = emblem;
    const auto wnotice = Utf8ToWide(notice);
    SQLLEN cb_notice = static_cast<SQLLEN>(wnotice.size() * sizeof(wnotice[0]));
    if (!BindParam(1, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&gid, nullptr)) return false;
    if (!BindParam(2, SQL_C_UTINYINT, SQL_TINYINT, 0, (SQLPOINTER)&em, nullptr)) return false;
    if (!BindParam(3, SQL_C_WCHAR, SQL_WVARCHAR, 40, (SQLPOINTER)wnotice.c_str(), &cb_notice)) return false;
    if (!Execute(query.c_str())) return false;
    PHASE5_FETCH_OK();
}

bool CDBConnection::GuildDisband(int guildId)
{
    StatementCleanup cleanup(*this);
    wstring query = L"CALL P_GUILD_DELETE(?)";
    SQLINTEGER gid = guildId;
    if (!BindParam(1, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&gid, nullptr)) return false;
    if (!Execute(query.c_str())) return false;
    PHASE5_FETCH_OK();
}

bool CDBConnection::GuildMemberSeen(int playerId, Int64 unixSec)
{
    StatementCleanup cleanup(*this);
    wstring query = L"CALL P_GUILD_MEMBER_SEEN_UPSERT(?, ?)";
    SQLINTEGER pid = playerId;
    SQLBIGINT t = static_cast<SQLBIGINT>(unixSec);
    if (!BindParam(1, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&pid, nullptr)) return false;
    if (!BindParam(2, SQL_C_SBIGINT, SQL_BIGINT, 0, (SQLPOINTER)&t, nullptr)) return false;
    return Execute(query.c_str());
}

namespace
{
    // 목록 · 정산함 행을 읽는 공통 열 묶음
    struct ItemCols
    {
        SQLSMALLINT itemId{}, count{};
        SQLUINTEGER     enchant{};
        SQLBIGINT   extra{};
        SQLLEN cb_item{}, cb_count{}, cb_enchant{}, cb_extra{};
    };
}

bool CDBConnection::LoadAuctionListings(vector<DB_LISTING_ROW>& out)
{
    out.clear();
    StatementCleanup cleanup(*this);
    wstring query = L"CALL P_AUCTION_LISTING_SELECT_ALL()";
    SQLBIGINT lid{}, created{}, expires{};
    SQLINTEGER seller{}, price{}, deposit{};
    ItemCols ic;
    SQLLEN cb_lid{}, cb_seller{}, cb_price{}, cb_dep{}, cb_created{}, cb_expires{};
    if (!Execute(query.c_str())) return false;
    if (!BindCol(1, SQL_C_SBIGINT, sizeof(lid), &lid, &cb_lid)) return false;
    if (!BindCol(2, SQL_C_LONG, sizeof(seller), &seller, &cb_seller)) return false;
    if (!BindCol(3, SQL_C_SHORT, sizeof(ic.itemId), &ic.itemId, &ic.cb_item)) return false;
    if (!BindCol(4, SQL_C_SHORT, sizeof(ic.count), &ic.count, &ic.cb_count)) return false;
    if (!BindCol(5, SQL_C_ULONG, sizeof(ic.enchant), &ic.enchant, &ic.cb_enchant)) return false;
    if (!BindCol(6, SQL_C_SBIGINT, sizeof(ic.extra), &ic.extra, &ic.cb_extra)) return false;
    if (!BindCol(7, SQL_C_LONG, sizeof(price), &price, &cb_price)) return false;
    if (!BindCol(8, SQL_C_LONG, sizeof(deposit), &deposit, &cb_dep)) return false;
    if (!BindCol(9, SQL_C_SBIGINT, sizeof(created), &created, &cb_created)) return false;
    if (!BindCol(10, SQL_C_SBIGINT, sizeof(expires), &expires, &cb_expires)) return false;
    FetchResult fetched;
    while ((fetched = FetchRow()) == FetchResult::Row)
    {
        DB_LISTING_ROW r;
        r.listingId = static_cast<Int64>(lid);
        r.sellerId = static_cast<int>(seller);
        r.itemId = static_cast<UInt16>(ic.itemId);
        r.count = static_cast<UInt16>(ic.count);
        r.enchant = static_cast<UInt32>(ic.enchant);
        r.extra = ic.extra < 0 ? 0 : static_cast<UInt64>(ic.extra);
        r.price = static_cast<UInt32>(price);
        r.deposit = static_cast<UInt32>(deposit);
        r.createdAt = static_cast<Int64>(created);
        r.expiresAt = static_cast<Int64>(expires);
        r.status = 0;
        out.push_back(r);
    }
    if (fetched == FetchResult::Error) { out.clear(); return false; }
    return true;
}

bool CDBConnection::LoadAuctionSettlements(vector<DB_SETTLEMENT_ROW>& out)
{
    out.clear();
    StatementCleanup cleanup(*this);
    wstring query = L"CALL P_AUCTION_SETTLEMENT_SELECT_ALL()";
    SQLBIGINT sid{}, lid{}, created{};
    SQLINTEGER pid{}, adena{};
    SQLCHAR kind{}, reason{};
    ItemCols ic;
    SQLLEN cb_sid{}, cb_pid{}, cb_kind{}, cb_adena{}, cb_lid{}, cb_reason{}, cb_created{};
    if (!Execute(query.c_str())) return false;
    if (!BindCol(1, SQL_C_SBIGINT, sizeof(sid), &sid, &cb_sid)) return false;
    if (!BindCol(2, SQL_C_LONG, sizeof(pid), &pid, &cb_pid)) return false;
    if (!BindCol(3, SQL_C_UTINYINT, sizeof(kind), &kind, &cb_kind)) return false;
    if (!BindCol(4, SQL_C_LONG, sizeof(adena), &adena, &cb_adena)) return false;
    if (!BindCol(5, SQL_C_SHORT, sizeof(ic.itemId), &ic.itemId, &ic.cb_item)) return false;
    if (!BindCol(6, SQL_C_SHORT, sizeof(ic.count), &ic.count, &ic.cb_count)) return false;
    if (!BindCol(7, SQL_C_ULONG, sizeof(ic.enchant), &ic.enchant, &ic.cb_enchant)) return false;
    if (!BindCol(8, SQL_C_SBIGINT, sizeof(ic.extra), &ic.extra, &ic.cb_extra)) return false;
    if (!BindCol(9, SQL_C_SBIGINT, sizeof(lid), &lid, &cb_lid)) return false;
    if (!BindCol(10, SQL_C_UTINYINT, sizeof(reason), &reason, &cb_reason)) return false;
    if (!BindCol(11, SQL_C_SBIGINT, sizeof(created), &created, &cb_created)) return false;
    FetchResult fetched;
    while ((fetched = FetchRow()) == FetchResult::Row)
    {
        DB_SETTLEMENT_ROW r;
        r.settlementId = static_cast<Int64>(sid);
        r.playerId = static_cast<int>(pid);
        r.kind = static_cast<UInt8>(kind);
        r.adena = static_cast<UInt32>(adena);
        r.itemId = static_cast<UInt16>(ic.itemId);
        r.count = static_cast<UInt16>(ic.count);
        r.enchant = static_cast<UInt32>(ic.enchant);
        r.extra = ic.extra < 0 ? 0 : static_cast<UInt64>(ic.extra);
        r.listingId = static_cast<Int64>(lid);
        r.reason = static_cast<UInt8>(reason);
        r.createdAt = static_cast<Int64>(created);
        out.push_back(r);
    }
    if (fetched == FetchResult::Error) { out.clear(); return false; }
    return true;
}

bool CDBConnection::LoadAuctionPrices(vector<DB_PRICE_ROW>& out, Int64 sinceUnix)
{
    out.clear();
    StatementCleanup cleanup(*this);
    wstring query = L"CALL P_AUCTION_PRICE_SELECT(?)";
    SQLBIGINT since = static_cast<SQLBIGINT>(sinceUnix), soldAt{};
    SQLSMALLINT itemId{};
    SQLINTEGER price{};
    SQLLEN cb_item{}, cb_price{}, cb_sold{};
    if (!BindParam(1, SQL_C_SBIGINT, SQL_BIGINT, 0, (SQLPOINTER)&since, nullptr)) return false;
    if (!Execute(query.c_str())) return false;
    if (!BindCol(1, SQL_C_SHORT, sizeof(itemId), &itemId, &cb_item)) return false;
    if (!BindCol(2, SQL_C_LONG, sizeof(price), &price, &cb_price)) return false;
    if (!BindCol(3, SQL_C_SBIGINT, sizeof(soldAt), &soldAt, &cb_sold)) return false;
    FetchResult fetched;
    while ((fetched = FetchRow()) == FetchResult::Row)
        out.push_back(DB_PRICE_ROW{ static_cast<UInt16>(itemId), static_cast<UInt32>(price), static_cast<Int64>(soldAt) });
    if (fetched == FetchResult::Error) { out.clear(); return false; }
    return true;
}

bool CDBConnection::AuctionMaxIds(Int64& maxListing, Int64& maxSettlement, int& maxGuild)
{
    StatementCleanup cleanup(*this);
    wstring query = L"CALL P_AUCTION_MAX_ID_SELECT()";
    SQLBIGINT ml{}, ms{};
    SQLINTEGER mg{};
    SQLLEN cb1{}, cb2{}, cb3{};
    if (!Execute(query.c_str())) return false;
    if (!BindCol(1, SQL_C_SBIGINT, sizeof(ml), &ml, &cb1)) return false;
    if (!BindCol(2, SQL_C_SBIGINT, sizeof(ms), &ms, &cb2)) return false;
    if (!BindCol(3, SQL_C_LONG, sizeof(mg), &mg, &cb3)) return false;
    if (!Fetch()) return false;
    maxListing = static_cast<Int64>(ml);
    maxSettlement = static_cast<Int64>(ms);
    maxGuild = static_cast<int>(mg);
    return true;
}

bool CDBConnection::AuctionRegister(int sellerId, UInt32 adenaAfter, const DB_ITEM_SLOT_SAVE& slot, const DB_LISTING_ROW& l)
{
    StatementCleanup cleanup(*this);
    wstring query = L"CALL P_AUCTION_REGISTER(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
    SQLINTEGER  pid = sellerId, adena = static_cast<SQLINTEGER>(adenaAfter);
    SQLSMALLINT sIdx = static_cast<SQLSMALLINT>(slot.slotIndex), sItem = static_cast<SQLSMALLINT>(slot.itemId), sCnt = static_cast<SQLSMALLINT>(slot.count);
    SQLCHAR     sHas = slot.hasItem ? 1 : 0, sEq = slot.equipped ? 1 : 0;
    SQLUINTEGER sEnc = slot.enchant;
    SQLBIGINT   sExt = static_cast<SQLBIGINT>(slot.extra & 0x3FFFFFFFFFFFFFFFull);
    SQLBIGINT   lid = static_cast<SQLBIGINT>(l.listingId), created = static_cast<SQLBIGINT>(l.createdAt), expires = static_cast<SQLBIGINT>(l.expiresAt);
    SQLSMALLINT lItem = static_cast<SQLSMALLINT>(l.itemId), lCnt = static_cast<SQLSMALLINT>(l.count);
    SQLUINTEGER     lEnc = l.enchant;
    SQLBIGINT   lExt = static_cast<SQLBIGINT>(l.extra & 0x3FFFFFFFFFFFFFFFull);
    SQLINTEGER  price = static_cast<SQLINTEGER>(l.price), deposit = static_cast<SQLINTEGER>(l.deposit);
    int i = 1;
    if (!BindParam(i++, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&pid, nullptr)) return false;
    if (!BindParam(i++, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&adena, nullptr)) return false;
    if (!BindParam(i++, SQL_C_SHORT, SQL_SMALLINT, 0, (SQLPOINTER)&sIdx, nullptr)) return false;
    if (!BindParam(i++, SQL_C_BIT, SQL_BIT, 0, (SQLPOINTER)&sHas, nullptr)) return false;
    if (!BindParam(i++, SQL_C_SHORT, SQL_SMALLINT, 0, (SQLPOINTER)&sItem, nullptr)) return false;
    if (!BindParam(i++, SQL_C_SHORT, SQL_SMALLINT, 0, (SQLPOINTER)&sCnt, nullptr)) return false;
    if (!BindParam(i++, SQL_C_BIT, SQL_BIT, 0, (SQLPOINTER)&sEq, nullptr)) return false;
    if (!BindParam(i++, SQL_C_ULONG, SQL_INTEGER, 0, (SQLPOINTER)&sEnc, nullptr)) return false;
    if (!BindParam(i++, SQL_C_SBIGINT, SQL_BIGINT, 0, (SQLPOINTER)&sExt, nullptr)) return false;
    if (!BindParam(i++, SQL_C_SBIGINT, SQL_BIGINT, 0, (SQLPOINTER)&lid, nullptr)) return false;
    if (!BindParam(i++, SQL_C_SHORT, SQL_SMALLINT, 0, (SQLPOINTER)&lItem, nullptr)) return false;
    if (!BindParam(i++, SQL_C_SHORT, SQL_SMALLINT, 0, (SQLPOINTER)&lCnt, nullptr)) return false;
    if (!BindParam(i++, SQL_C_ULONG, SQL_INTEGER, 0, (SQLPOINTER)&lEnc, nullptr)) return false;
    if (!BindParam(i++, SQL_C_SBIGINT, SQL_BIGINT, 0, (SQLPOINTER)&lExt, nullptr)) return false;
    if (!BindParam(i++, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&price, nullptr)) return false;
    if (!BindParam(i++, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&deposit, nullptr)) return false;
    if (!BindParam(i++, SQL_C_SBIGINT, SQL_BIGINT, 0, (SQLPOINTER)&created, nullptr)) return false;
    if (!BindParam(i++, SQL_C_SBIGINT, SQL_BIGINT, 0, (SQLPOINTER)&expires, nullptr)) return false;
    if (!Execute(query.c_str())) return false;
    PHASE5_FETCH_OK();
}

bool CDBConnection::AuctionBuy(int buyerId, UInt32 adenaAfter, Int64 listingId, UInt32 price, Int64 buyerSettleId, Int64 sellerSettleId, UInt32 sellerGets, Int64 nowUnix,
    UInt64 settleExtraOr)
{
    StatementCleanup cleanup(*this);
    wstring query = L"CALL P_AUCTION_BUY(?, ?, ?, ?, ?, ?, ?, ?, ?)";
    SQLINTEGER pid = buyerId, adena = static_cast<SQLINTEGER>(adenaAfter), pr = static_cast<SQLINTEGER>(price), gets = static_cast<SQLINTEGER>(sellerGets);
    SQLBIGINT lid = static_cast<SQLBIGINT>(listingId), bs = static_cast<SQLBIGINT>(buyerSettleId), ss = static_cast<SQLBIGINT>(sellerSettleId), now = static_cast<SQLBIGINT>(nowUnix);
    SQLBIGINT extraOr = static_cast<SQLBIGINT>(settleExtraOr);
    int i = 1;
    if (!BindParam(i++, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&pid, nullptr)) return false;
    if (!BindParam(i++, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&adena, nullptr)) return false;
    if (!BindParam(i++, SQL_C_SBIGINT, SQL_BIGINT, 0, (SQLPOINTER)&lid, nullptr)) return false;
    if (!BindParam(i++, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&pr, nullptr)) return false;
    if (!BindParam(i++, SQL_C_SBIGINT, SQL_BIGINT, 0, (SQLPOINTER)&bs, nullptr)) return false;
    if (!BindParam(i++, SQL_C_SBIGINT, SQL_BIGINT, 0, (SQLPOINTER)&ss, nullptr)) return false;
    if (!BindParam(i++, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&gets, nullptr)) return false;
    if (!BindParam(i++, SQL_C_SBIGINT, SQL_BIGINT, 0, (SQLPOINTER)&now, nullptr)) return false;
    if (!BindParam(i++, SQL_C_SBIGINT, SQL_BIGINT, 0, (SQLPOINTER)&extraOr, nullptr)) return false;
    if (!Execute(query.c_str())) return false;
    PHASE5_FETCH_OK();
}

bool CDBConnection::AuctionClose(Int64 listingId, UInt8 status, Int64 settleId, Int64 nowUnix)
{
    StatementCleanup cleanup(*this);
    wstring query = L"CALL P_AUCTION_CLOSE(?, ?, ?, ?)";
    SQLBIGINT lid = static_cast<SQLBIGINT>(listingId), sid = static_cast<SQLBIGINT>(settleId), now = static_cast<SQLBIGINT>(nowUnix);
    SQLCHAR st = status;
    if (!BindParam(1, SQL_C_SBIGINT, SQL_BIGINT, 0, (SQLPOINTER)&lid, nullptr)) return false;
    if (!BindParam(2, SQL_C_UTINYINT, SQL_TINYINT, 0, (SQLPOINTER)&st, nullptr)) return false;
    if (!BindParam(3, SQL_C_SBIGINT, SQL_BIGINT, 0, (SQLPOINTER)&sid, nullptr)) return false;
    if (!BindParam(4, SQL_C_SBIGINT, SQL_BIGINT, 0, (SQLPOINTER)&now, nullptr)) return false;
    if (!Execute(query.c_str())) return false;
    PHASE5_FETCH_OK();
}

bool CDBConnection::SettlementClaim(int playerId, Int64 settlementId, UInt32 adenaAfter, const DB_ITEM_SLOT_SAVE& slot, Int64 nowUnix)
{
    StatementCleanup cleanup(*this);
    wstring query = L"CALL P_AUCTION_SETTLEMENT_CLAIM(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
    SQLINTEGER  pid = playerId, adena = static_cast<SQLINTEGER>(adenaAfter);
    SQLBIGINT   sid = static_cast<SQLBIGINT>(settlementId), now = static_cast<SQLBIGINT>(nowUnix);
    SQLSMALLINT sIdx = static_cast<SQLSMALLINT>(slot.slotIndex), sItem = static_cast<SQLSMALLINT>(slot.itemId), sCnt = static_cast<SQLSMALLINT>(slot.count);
    SQLCHAR     sHas = slot.hasItem ? 1 : 0, sEq = slot.equipped ? 1 : 0;
    SQLUINTEGER sEnc = slot.enchant;
    SQLBIGINT   sExt = static_cast<SQLBIGINT>(slot.extra & 0x3FFFFFFFFFFFFFFFull);
    int i = 1;
    if (!BindParam(i++, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&pid, nullptr)) return false;
    if (!BindParam(i++, SQL_C_SBIGINT, SQL_BIGINT, 0, (SQLPOINTER)&sid, nullptr)) return false;
    if (!BindParam(i++, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&adena, nullptr)) return false;
    if (!BindParam(i++, SQL_C_SHORT, SQL_SMALLINT, 0, (SQLPOINTER)&sIdx, nullptr)) return false;
    if (!BindParam(i++, SQL_C_BIT, SQL_BIT, 0, (SQLPOINTER)&sHas, nullptr)) return false;
    if (!BindParam(i++, SQL_C_SHORT, SQL_SMALLINT, 0, (SQLPOINTER)&sItem, nullptr)) return false;
    if (!BindParam(i++, SQL_C_SHORT, SQL_SMALLINT, 0, (SQLPOINTER)&sCnt, nullptr)) return false;
    if (!BindParam(i++, SQL_C_BIT, SQL_BIT, 0, (SQLPOINTER)&sEq, nullptr)) return false;
    if (!BindParam(i++, SQL_C_ULONG, SQL_INTEGER, 0, (SQLPOINTER)&sEnc, nullptr)) return false;
    if (!BindParam(i++, SQL_C_SBIGINT, SQL_BIGINT, 0, (SQLPOINTER)&sExt, nullptr)) return false;
    if (!BindParam(i++, SQL_C_SBIGINT, SQL_BIGINT, 0, (SQLPOINTER)&now, nullptr)) return false;
    if (!Execute(query.c_str())) return false;
    PHASE5_FETCH_OK();
}

bool CDBConnection::AuditTotals(UInt64& walletAdena, UInt64& settleAdena, UInt64& invUnits, UInt64& listingUnits, UInt64& settleUnits, UInt64& feesAndDeposits)
{
    StatementCleanup cleanup(*this);
    wstring query = L"CALL P_AUCTION_AUDIT()";
    SQLBIGINT a{}, b{}, c{}, d{}, e{}, f{};
    SQLLEN cb[6]{};
    if (!Execute(query.c_str())) return false;
    if (!BindCol(1, SQL_C_SBIGINT, sizeof(a), &a, &cb[0])) return false;
    if (!BindCol(2, SQL_C_SBIGINT, sizeof(b), &b, &cb[1])) return false;
    if (!BindCol(3, SQL_C_SBIGINT, sizeof(c), &c, &cb[2])) return false;
    if (!BindCol(4, SQL_C_SBIGINT, sizeof(d), &d, &cb[3])) return false;
    if (!BindCol(5, SQL_C_SBIGINT, sizeof(e), &e, &cb[4])) return false;
    if (!BindCol(6, SQL_C_SBIGINT, sizeof(f), &f, &cb[5])) return false;
    if (!Fetch()) return false;
    walletAdena = static_cast<UInt64>(a); settleAdena = static_cast<UInt64>(b); invUnits = static_cast<UInt64>(c);
    listingUnits = static_cast<UInt64>(d); settleUnits = static_cast<UInt64>(e); feesAndDeposits = static_cast<UInt64>(f);
    return true;
}
#undef PHASE5_FETCH_OK

bool CDBConnection::SaveEnchant(int playerId, const DB_ENCHANT_SAVE& save, bool& first)
{
    first = false;
    if (save.count < 2 || save.count > 3) return false;
    const auto& gearBefore = save.before[1];
    const auto& gearAfter = save.after[1];
    const bool success = gearAfter.hasItem && gearBefore.enchant < EnchantRule::kMaxLevel && gearAfter.enchant == gearBefore.enchant + 1;
    if (save.itemId != gearBefore.itemId || !EnchantRule::ValidGraceChange(gearBefore.enchant, save.graceBefore, save.graceAfter, success)) return false;
    StatementCleanup cleanup(*this);
    std::ostringstream json;
    json << "[";
    for (size_t i = 0; i < save.count; ++i)
    {
        if (i) json << ",";
        const auto& b = save.before[i]; const auto& a = save.after[i];
        json << "{\"slot\":" << b.slotIndex << ",\"item\":" << b.itemId << ",\"count\":" << b.count
             << ",\"equipped\":" << b.equipped << ",\"level\":" << b.enchant << ",\"extra\":" << b.extra
             << ",\"keep\":" << a.hasItem << ",\"newCount\":" << a.count << ",\"newLevel\":" << a.enchant;
        if (i == 1) json << ",\"graceLimit\":" << EnchantRule::GraceAttempts(b.enchant)
            << ",\"graceBefore\":" << save.graceBefore << ",\"graceAfter\":" << save.graceAfter;
        json << "}";
    }
    json << "]";
    std::string slots = json.str(), name = save.name;
    SQLINTEGER pid = playerId;
    SQLSMALLINT item = static_cast<SQLSMALLINT>(save.itemId);
    SQLUINTEGER level = save.level;
    SQLLEN slotsLen = static_cast<SQLLEN>(slots.size()), nameLen = static_cast<SQLLEN>(name.size());
    if (!BindParam(1, SQL_C_LONG, SQL_INTEGER, 0, &pid, nullptr)) return false;
    if (!BindParam(2, SQL_C_CHAR, SQL_LONGVARCHAR, slots.size(), slots.data(), &slotsLen)) return false;
    if (!BindParam(3, SQL_C_SHORT, SQL_SMALLINT, 0, &item, nullptr)) return false;
    if (!BindParam(4, SQL_C_ULONG, SQL_INTEGER, 0, &level, nullptr)) return false;
    if (!BindParam(5, SQL_C_CHAR, SQL_VARCHAR, name.size(), name.data(), &nameLen)) return false;
    if (!Execute(L"CALL P_ENCHANT_TRADE(?, ?, ?, ?, ?)")) return false;
    SQLINTEGER ok{}, isFirst{}; SQLLEN cbOk{}, cbFirst{};
    if (!BindCol(1, SQL_C_LONG, sizeof(ok), &ok, &cbOk) || !BindCol(2, SQL_C_LONG, sizeof(isFirst), &isFirst, &cbFirst) || !Fetch()) return false;
    first = isFirst != 0;
    return ok != 0;
}

bool CDBConnection::SaveItemBatch(int playerId, const DB_ITEM_BATCH_SAVE& save)
{
    if (save.before.empty() || save.before.size() != save.after.size() || save.before.size() > static_cast<size_t>(MAX_INVENTORY_SLOTS))
        return false;
    StatementCleanup cleanup(*this);
    // 칸마다 지금(낙관 검사) · 바뀐 뒤 — 프로시저가 JSON을 한 칸씩 읽는다(setup.sql P_ITEM_BATCH_TRADE 머리 주석)
    std::ostringstream json;
    json << "[";
    for (size_t i = 0; i < save.before.size(); ++i)
    {
        const auto& b = save.before[i];
        const auto& a = save.after[i];
        if (a.slotIndex != b.slotIndex) return false;
        if (i) json << ",";
        json << "{\"slot\":" << b.slotIndex << ",\"has\":" << (b.hasItem ? 1 : 0) << ",\"item\":" << b.itemId << ",\"count\":" << b.count
             << ",\"equipped\":" << (b.equipped ? 1 : 0) << ",\"enchant\":" << b.enchant << ",\"extra\":" << b.extra
             << ",\"keep\":" << (a.hasItem && a.count > 0 ? 1 : 0) << ",\"newItem\":" << a.itemId << ",\"newCount\":" << a.count
             << ",\"newEquipped\":" << (a.equipped ? 1 : 0) << ",\"newEnchant\":" << a.enchant << ",\"newExtra\":" << a.extra << "}";
    }
    json << "]";
    std::string slots = json.str();
    SQLINTEGER pid = playerId;
    SQLINTEGER adenaBefore = static_cast<SQLINTEGER>(save.adenaBefore), adenaAfter = static_cast<SQLINTEGER>(save.adenaAfter);
    SQLLEN slotsLen = static_cast<SQLLEN>(slots.size());
    if (!BindParam(1, SQL_C_LONG, SQL_INTEGER, 0, &pid, nullptr)) return false;
    if (!BindParam(2, SQL_C_CHAR, SQL_LONGVARCHAR, slots.size(), slots.data(), &slotsLen)) return false;
    if (!BindParam(3, SQL_C_LONG, SQL_INTEGER, 0, &adenaBefore, nullptr)) return false;
    if (!BindParam(4, SQL_C_LONG, SQL_INTEGER, 0, &adenaAfter, nullptr)) return false;
    if (!Execute(L"CALL P_ITEM_BATCH_TRADE(?, ?, ?, ?)")) return false;
    SQLINTEGER ok{};
    SQLLEN cbOk{};
    if (!BindCol(1, SQL_C_LONG, sizeof(ok), &ok, &cbOk) || !Fetch()) return false;
    return ok != 0;
}

DB_TRANSFER_RESULT CDBConnection::SaveTransfer(const DB_TRANSFER_SAVE& save)
{
    if (!TransferSave::Valid(save)) return DB_TRANSFER_RESULT::Rejected;
    StatementCleanup cleanup(*this);
    auto receipt = save.receipt;
    auto data = TransferSave::Json(save);
    SQLLEN keyLen = receipt.size(), dataLen = data.size();
    if (!BindParam(1, SQL_C_CHAR, SQL_VARCHAR, receipt.size(), receipt.data(), &keyLen) ||
        !BindParam(2, SQL_C_CHAR, SQL_LONGVARCHAR, data.size(), data.data(), &dataLen)) return DB_TRANSFER_RESULT::Rejected;
    if (!Execute(L"CALL P_PLAYER_TRANSFER(?, ?)"))
        return LostConnection() ? DB_TRANSFER_RESULT::Retry : DB_TRANSFER_RESULT::Rejected;
    SQLINTEGER ok{}; SQLLEN cb{};
    // COMMIT 뒤 결과 읽기가 실패하면 결과를 추측하지 않는다. 같은 receipt로 프로시저를 다시 부른다.
    if (!BindCol(1, SQL_C_LONG, sizeof(ok), &ok, &cb) || !Fetch()) return DB_TRANSFER_RESULT::Retry;
    return ok ? DB_TRANSFER_RESULT::Committed : DB_TRANSFER_RESULT::Rejected;
}

bool CDBConnection::LoadEnchantGrace(int playerId, std::vector<DB_ENCHANT_GRACE>& out)
{
    out.clear();
    StatementCleanup cleanup(*this);
    SQLINTEGER pid = playerId;
    SQLUSMALLINT item{}, failures{};
    SQLUINTEGER level{};
    SQLLEN cbItem{}, cbLevel{}, cbFailures{};
    if (!BindParam(1, SQL_C_LONG, SQL_INTEGER, 0, &pid, nullptr) || !Execute(L"CALL P_ENCHANT_GRACE_SELECT(?)")) return false;
    if (!BindCol(1, SQL_C_USHORT, sizeof(item), &item, &cbItem) ||
        !BindCol(2, SQL_C_ULONG, sizeof(level), &level, &cbLevel) ||
        !BindCol(3, SQL_C_USHORT, sizeof(failures), &failures, &cbFailures)) return false;
    for (;;)
    {
        const auto ret = ::SQLFetch(m_pStatement);
        if (ret == SQL_NO_DATA) return true;
        if (!SQL_SUCCEEDED(ret)) { HandleError(ret); out.clear(); return false; }
        if (cbItem == SQL_NULL_DATA || cbLevel == SQL_NULL_DATA || cbFailures == SQL_NULL_DATA) { out.clear(); return false; }
        // 설정에서 가호를 끈 구간은 보관만 하고 활성 상태에는 넣지 않는다.
        if (EnchantRule::GraceAttempts(level) != 0) out.push_back({item, level, failures});
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 결사 성장(Shared/Document/GUILD.md · setup.sql GuildProgress · GuildLog · GuildPost)
// 진행은 버전 있는 바이너리 한 덩어리(VARBINARY(16000)), 금고(Seals)는 더하고 빼는 식으로만.
// ─────────────────────────────────────────────────────────────────────────────
// 결과 한 줄(Ok)을 읽는다 — 프로시저가 SELECT v_ok AS Ok로 끝난다(위 PHASE5_FETCH_OK와 같다 — 그 매크로는 앞에서 풀린다)
#define GUILD_FETCH_OK()                                                           \
    do {                                                                           \
        SQLCHAR ok_{}; SQLLEN cb_ok_{};                                            \
        if (!BindCol(1, SQL_C_BIT, sizeof(ok_), &ok_, &cb_ok_)) return false;      \
        if (!Fetch()) return false;                                                \
        return ok_ != 0;                                                           \
    } while (false)

namespace
{
    constexpr SQLULEN kGuildProgressBytes = 16000;

    // 기록 한 줄의 매개변수(P_GUILD_SEALS_ADD · P_GUILD_SHOP_BUY · P_GUILD_LOG_INSERT 끝의 다섯 칸) — 바인드가 끝날 때까지 산다
    struct LogParams
    {
        SQLBIGINT       at{};
        SQLCHAR         kind{};
        SQLINTEGER      value{};
        std::u16string  who, what;
        SQLLEN          cbWho{}, cbWhat{};

        explicit LogParams(const DB_GUILD_LOG_ROW& log)
            : at(static_cast<SQLBIGINT>(log.at)), kind(log.kind), value(log.value), who(Utf8ToWide(log.who)), what(Utf8ToWide(log.what))
        {
            cbWho = static_cast<SQLLEN>(who.size() * sizeof(char16_t));
            cbWhat = static_cast<SQLLEN>(what.size() * sizeof(char16_t));
        }
        bool Bind(CDBConnection& c, int& i)
        {
            return c.BindParam(static_cast<SQLUSMALLINT>(i++), SQL_C_SBIGINT, SQL_BIGINT, 0, (SQLPOINTER)&at, nullptr)
                && c.BindParam(static_cast<SQLUSMALLINT>(i++), SQL_C_UTINYINT, SQL_TINYINT, 0, (SQLPOINTER)&kind, nullptr)
                && c.BindParam(static_cast<SQLUSMALLINT>(i++), SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&value, nullptr)
                && c.BindParam(static_cast<SQLUSMALLINT>(i++), SQL_C_WCHAR, SQL_WVARCHAR, 20, (SQLPOINTER)who.c_str(), &cbWho)
                && c.BindParam(static_cast<SQLUSMALLINT>(i++), SQL_C_WCHAR, SQL_WVARCHAR, 30, (SQLPOINTER)what.c_str(), &cbWhat);
        }
    };
}

bool CDBConnection::LoadGuildProgress(vector<DB_GUILD_PROGRESS_ROW>& out)
{
    out.clear();
    StatementCleanup cleanup(*this);
    wstring query = L"CALL P_GUILD_PROGRESS_SELECT_ALL()";
    SQLINTEGER gid{};
    SQLUINTEGER seals{};
    SQLBIGINT created{};
    std::vector<UInt8> buffer(kGuildProgressBytes);
    SQLLEN cb_gid{}, cb_seals{}, cb_data{}, cb_created{};
    if (!Execute(query.c_str())) return false;
    if (!BindCol(1, SQL_C_LONG, sizeof(gid), &gid, &cb_gid)) return false;
    if (!BindCol(2, SQL_C_ULONG, sizeof(seals), &seals, &cb_seals)) return false;
    if (!BindCol(3, SQL_C_BINARY, buffer.size(), buffer.data(), &cb_data)) return false;
    if (!BindCol(4, SQL_C_SBIGINT, sizeof(created), &created, &cb_created)) return false;
    FetchResult fetched;
    while ((fetched = FetchRow()) == FetchResult::Row)
    {
        DB_GUILD_PROGRESS_ROW r;
        r.guildId = static_cast<int>(gid);
        r.seals = cb_seals == SQL_NULL_DATA ? 0u : static_cast<UInt32>(seals);
        if (cb_data > 0 && static_cast<size_t>(cb_data) <= buffer.size())
            r.data.assign(buffer.begin(), buffer.begin() + cb_data);
        r.createdAt = cb_created == SQL_NULL_DATA ? 0 : static_cast<Int64>(created);
        out.push_back(std::move(r));
    }
    if (fetched == FetchResult::Error) { out.clear(); return false; }
    return true;
}

bool CDBConnection::GuildProgressSave(int guildId, const std::vector<UInt8>& data)
{
    if (data.size() > kGuildProgressBytes)
        return false;
    StatementCleanup cleanup(*this);
    wstring query = L"CALL P_GUILD_PROGRESS_SAVE(?, ?)";
    SQLINTEGER gid = guildId;
    SQLLEN cb_data = static_cast<SQLLEN>(data.size());
    if (!BindParam(1, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&gid, nullptr)) return false;
    if (!BindParam(2, SQL_C_BINARY, SQL_VARBINARY, kGuildProgressBytes, (SQLPOINTER)data.data(), &cb_data)) return false;
    if (!Execute(query.c_str())) return false;
    GUILD_FETCH_OK();
}

bool CDBConnection::GuildSealsAdd(int guildId, Int32 delta, const DB_GUILD_LOG_ROW& log)
{
    StatementCleanup cleanup(*this);
    wstring query = L"CALL P_GUILD_SEALS_ADD(?, ?, ?, ?, ?, ?, ?)";
    SQLINTEGER gid = guildId, d = delta;
    LogParams lp(log);
    int i = 1;
    if (!BindParam(static_cast<SQLUSMALLINT>(i++), SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&gid, nullptr)) return false;
    if (!BindParam(static_cast<SQLUSMALLINT>(i++), SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&d, nullptr)) return false;
    if (!lp.Bind(*this, i)) return false;
    if (!Execute(query.c_str())) return false;
    GUILD_FETCH_OK();
}

bool CDBConnection::GuildShopBuy(int playerId, int guildId, UInt32 price, const DB_ITEM_SLOT_SAVE& slot, const DB_GUILD_LOG_ROW& log)
{
    StatementCleanup cleanup(*this);
    wstring query = L"CALL P_GUILD_SHOP_BUY(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
    SQLINTEGER  pid = playerId, gid = guildId;
    SQLUINTEGER pr = price;
    SQLSMALLINT sIdx = static_cast<SQLSMALLINT>(slot.slotIndex), sItem = static_cast<SQLSMALLINT>(slot.itemId), sCnt = static_cast<SQLSMALLINT>(slot.count);
    SQLCHAR     sEq = slot.equipped ? 1 : 0;
    SQLUINTEGER sEnc = slot.enchant;
    SQLBIGINT   sExt = static_cast<SQLBIGINT>(slot.extra & 0x3FFFFFFFFFFFFFFFull);
    LogParams lp(log);
    int i = 1;
    if (!BindParam(static_cast<SQLUSMALLINT>(i++), SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&pid, nullptr)) return false;
    if (!BindParam(static_cast<SQLUSMALLINT>(i++), SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&gid, nullptr)) return false;
    if (!BindParam(static_cast<SQLUSMALLINT>(i++), SQL_C_ULONG, SQL_INTEGER, 0, (SQLPOINTER)&pr, nullptr)) return false;
    if (!BindParam(static_cast<SQLUSMALLINT>(i++), SQL_C_SHORT, SQL_SMALLINT, 0, (SQLPOINTER)&sIdx, nullptr)) return false;
    if (!BindParam(static_cast<SQLUSMALLINT>(i++), SQL_C_SHORT, SQL_SMALLINT, 0, (SQLPOINTER)&sItem, nullptr)) return false;
    if (!BindParam(static_cast<SQLUSMALLINT>(i++), SQL_C_SHORT, SQL_SMALLINT, 0, (SQLPOINTER)&sCnt, nullptr)) return false;
    if (!BindParam(static_cast<SQLUSMALLINT>(i++), SQL_C_BIT, SQL_BIT, 0, (SQLPOINTER)&sEq, nullptr)) return false;
    if (!BindParam(static_cast<SQLUSMALLINT>(i++), SQL_C_ULONG, SQL_INTEGER, 0, (SQLPOINTER)&sEnc, nullptr)) return false;
    if (!BindParam(static_cast<SQLUSMALLINT>(i++), SQL_C_SBIGINT, SQL_BIGINT, 0, (SQLPOINTER)&sExt, nullptr)) return false;
    if (!lp.Bind(*this, i)) return false;
    if (!Execute(query.c_str())) return false;
    GUILD_FETCH_OK();
}

bool CDBConnection::GuildLogAdd(const DB_GUILD_LOG_ROW& log)
{
    StatementCleanup cleanup(*this);
    wstring query = L"CALL P_GUILD_LOG_INSERT(?, ?, ?, ?, ?, ?)";
    SQLINTEGER gid = log.guildId;
    LogParams lp(log);
    int i = 1;
    if (!BindParam(static_cast<SQLUSMALLINT>(i++), SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&gid, nullptr)) return false;
    if (!lp.Bind(*this, i)) return false;
    return Execute(query.c_str());
}

bool CDBConnection::LoadGuildLogs(vector<DB_GUILD_LOG_ROW>& out, int keepPerGuild)
{
    out.clear();
    StatementCleanup cleanup(*this);
    wstring query = L"CALL P_GUILD_LOG_SELECT_RECENT(?)";
    SQLINTEGER keep = keepPerGuild;
    SQLINTEGER gid{}, value{};
    SQLBIGINT at{};
    SQLCHAR kind{};
    DbTextChar who[NAME_SIZE + 1]{};
    DbTextChar what[40]{};
    SQLLEN cb[6]{};
    if (!BindParam(1, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&keep, nullptr)) return false;
    if (!Execute(query.c_str())) return false;
    if (!BindCol(1, SQL_C_LONG, sizeof(gid), &gid, &cb[0])) return false;
    if (!BindCol(2, SQL_C_SBIGINT, sizeof(at), &at, &cb[1])) return false;
    if (!BindCol(3, SQL_C_UTINYINT, sizeof(kind), &kind, &cb[2])) return false;
    if (!BindCol(4, SQL_C_LONG, sizeof(value), &value, &cb[3])) return false;
    if (!BindCol(5, SQL_C_WCHAR, sizeof(who), who, &cb[4])) return false;
    if (!BindCol(6, SQL_C_WCHAR, sizeof(what), what, &cb[5])) return false;
    FetchResult fetched;
    while ((fetched = FetchRow()) == FetchResult::Row)
    {
        DB_GUILD_LOG_ROW r;
        r.guildId = static_cast<int>(gid);
        r.at = static_cast<Int64>(at);
        r.kind = static_cast<UInt8>(kind);
        r.value = static_cast<Int32>(value);
        r.who = cb[4] == SQL_NULL_DATA ? string() : WideToUtf8(who, cb[4]);
        r.what = cb[5] == SQL_NULL_DATA ? string() : WideToUtf8(what, cb[5]);
        out.push_back(std::move(r));
    }
    if (fetched == FetchResult::Error) { out.clear(); return false; }
    return true;
}

bool CDBConnection::LoadGuildPosts(vector<DB_GUILD_POST_ROW>& out)
{
    out.clear();
    StatementCleanup cleanup(*this);
    wstring query = L"CALL P_GUILD_POST_SELECT_ALL()";
    SQLBIGINT pid{}, at{};
    SQLINTEGER gid{}, author{};
    SQLCHAR cat{}, pin{};
    DbTextChar name[NAME_SIZE + 1]{};
    DbTextChar title[24]{};
    DbTextChar body[64]{};
    SQLLEN cb[9]{};
    if (!Execute(query.c_str())) return false;
    if (!BindCol(1, SQL_C_SBIGINT, sizeof(pid), &pid, &cb[0])) return false;
    if (!BindCol(2, SQL_C_LONG, sizeof(gid), &gid, &cb[1])) return false;
    if (!BindCol(3, SQL_C_LONG, sizeof(author), &author, &cb[2])) return false;
    if (!BindCol(4, SQL_C_WCHAR, sizeof(name), name, &cb[3])) return false;
    if (!BindCol(5, SQL_C_UTINYINT, sizeof(cat), &cat, &cb[4])) return false;
    if (!BindCol(6, SQL_C_UTINYINT, sizeof(pin), &pin, &cb[5])) return false;
    if (!BindCol(7, SQL_C_WCHAR, sizeof(title), title, &cb[6])) return false;
    if (!BindCol(8, SQL_C_WCHAR, sizeof(body), body, &cb[7])) return false;
    if (!BindCol(9, SQL_C_SBIGINT, sizeof(at), &at, &cb[8])) return false;
    FetchResult fetched;
    while ((fetched = FetchRow()) == FetchResult::Row)
    {
        DB_GUILD_POST_ROW r;
        r.postId = static_cast<Int64>(pid);
        r.guildId = static_cast<int>(gid);
        r.authorId = static_cast<int>(author);
        r.author = WideToUtf8(name, cb[3]);
        r.category = static_cast<UInt8>(cat);
        r.pinned = static_cast<UInt8>(pin);
        r.title = WideToUtf8(title, cb[6]);
        r.body = WideToUtf8(body, cb[7]);
        r.at = static_cast<Int64>(at);
        out.push_back(std::move(r));
    }
    if (fetched == FetchResult::Error) { out.clear(); return false; }
    return true;
}

bool CDBConnection::GuildPostAdd(const DB_GUILD_POST_ROW& post, Int64 dropPostId)
{
    StatementCleanup cleanup(*this);
    wstring query = L"CALL P_GUILD_POST_INSERT(?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
    SQLBIGINT pid = static_cast<SQLBIGINT>(post.postId), at = static_cast<SQLBIGINT>(post.at), drop = static_cast<SQLBIGINT>(dropPostId);
    SQLINTEGER gid = post.guildId, author = post.authorId;
    SQLCHAR cat = post.category, pin = post.pinned;
    const auto wname = Utf8ToWide(post.author), wtitle = Utf8ToWide(post.title), wbody = Utf8ToWide(post.body);
    SQLLEN cbName = static_cast<SQLLEN>(wname.size() * sizeof(char16_t));
    SQLLEN cbTitle = static_cast<SQLLEN>(wtitle.size() * sizeof(char16_t));
    SQLLEN cbBody = static_cast<SQLLEN>(wbody.size() * sizeof(char16_t));
    int i = 1;
    if (!BindParam(static_cast<SQLUSMALLINT>(i++), SQL_C_SBIGINT, SQL_BIGINT, 0, (SQLPOINTER)&pid, nullptr)) return false;
    if (!BindParam(static_cast<SQLUSMALLINT>(i++), SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&gid, nullptr)) return false;
    if (!BindParam(static_cast<SQLUSMALLINT>(i++), SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&author, nullptr)) return false;
    if (!BindParam(static_cast<SQLUSMALLINT>(i++), SQL_C_WCHAR, SQL_WVARCHAR, 20, (SQLPOINTER)wname.c_str(), &cbName)) return false;
    if (!BindParam(static_cast<SQLUSMALLINT>(i++), SQL_C_UTINYINT, SQL_TINYINT, 0, (SQLPOINTER)&cat, nullptr)) return false;
    if (!BindParam(static_cast<SQLUSMALLINT>(i++), SQL_C_UTINYINT, SQL_TINYINT, 0, (SQLPOINTER)&pin, nullptr)) return false;
    if (!BindParam(static_cast<SQLUSMALLINT>(i++), SQL_C_WCHAR, SQL_WVARCHAR, 16, (SQLPOINTER)wtitle.c_str(), &cbTitle)) return false;
    if (!BindParam(static_cast<SQLUSMALLINT>(i++), SQL_C_WCHAR, SQL_WVARCHAR, 60, (SQLPOINTER)wbody.c_str(), &cbBody)) return false;
    if (!BindParam(static_cast<SQLUSMALLINT>(i++), SQL_C_SBIGINT, SQL_BIGINT, 0, (SQLPOINTER)&at, nullptr)) return false;
    if (!BindParam(static_cast<SQLUSMALLINT>(i++), SQL_C_SBIGINT, SQL_BIGINT, 0, (SQLPOINTER)&drop, nullptr)) return false;
    if (!Execute(query.c_str())) return false;
    GUILD_FETCH_OK();
}

bool CDBConnection::GuildPostDelete(int guildId, Int64 postId)
{
    StatementCleanup cleanup(*this);
    wstring query = L"CALL P_GUILD_POST_DELETE(?, ?)";
    SQLINTEGER gid = guildId;
    SQLBIGINT pid = static_cast<SQLBIGINT>(postId);
    if (!BindParam(1, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&gid, nullptr)) return false;
    if (!BindParam(2, SQL_C_SBIGINT, SQL_BIGINT, 0, (SQLPOINTER)&pid, nullptr)) return false;
    if (!Execute(query.c_str())) return false;
    GUILD_FETCH_OK();
}

bool CDBConnection::GuildPostPin(int guildId, Int64 postId, bool pinned)
{
    StatementCleanup cleanup(*this);
    wstring query = L"CALL P_GUILD_POST_PIN(?, ?, ?)";
    SQLINTEGER gid = guildId;
    SQLBIGINT pid = static_cast<SQLBIGINT>(postId);
    SQLCHAR pin = pinned ? 1 : 0;
    if (!BindParam(1, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&gid, nullptr)) return false;
    if (!BindParam(2, SQL_C_SBIGINT, SQL_BIGINT, 0, (SQLPOINTER)&pid, nullptr)) return false;
    if (!BindParam(3, SQL_C_UTINYINT, SQL_TINYINT, 0, (SQLPOINTER)&pin, nullptr)) return false;
    if (!Execute(query.c_str())) return false;
    GUILD_FETCH_OK();
}

bool CDBConnection::GuildPostMaxId(Int64& maxPostId)
{
    StatementCleanup cleanup(*this);
    wstring query = L"CALL P_GUILD_POST_MAX_ID()";
    SQLBIGINT m{};
    SQLLEN cb{};
    if (!Execute(query.c_str())) return false;
    if (!BindCol(1, SQL_C_SBIGINT, sizeof(m), &m, &cb)) return false;
    if (!Fetch()) return false;
    maxPostId = cb == SQL_NULL_DATA ? 0 : static_cast<Int64>(m);
    return true;
}

#undef GUILD_FETCH_OK
