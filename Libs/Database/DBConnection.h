#pragma once
#include <sql.h>
#include <sqlext.h>
#include <deque>
using namespace std;

class CDBConnection
{
public:
    CDBConnection() = default;
    ~CDBConnection();

    bool            Connect();
    bool            EnsureConnected();
    void            Clear();

    bool            Execute(const wchar_t* query);
    // 행 하나 · 더 없음 · 오류를 나눠 돌려준다(Fetch()는 앞의 둘만 구분 — 오류도 false다).
    enum class FetchResult { Row, NoData, Error };
    FetchResult     FetchRow();
    bool            Fetch();
    int             GetRowCount();
    void            Unbind();

    // 마지막 호출이 연결 끊김(SQLSTATE 08xxx · 서버 사라짐)으로 실패했는가. 그러면 다음 EnsureConnected가 새로 붙는다.
    [[nodiscard]] bool LostConnection() const noexcept { return m_bConnectionLost; }

public:
    bool            BindParam(SQLUSMALLINT paramIndex, SQLSMALLINT cType, SQLSMALLINT sqlType, SQLULEN len, SQLPOINTER ptr, SQLLEN* index);
    bool            BindCol(SQLUSMALLINT columnIndex, SQLSMALLINT cType, SQLULEN len, SQLPOINTER value, SQLLEN* index);
    void            HandleError(SQLRETURN ret);

public:
    // 반환값 = 호출 성공(오류 없음). 데이터는 그와 따로 돌려준다 — "없음"과 "읽다 실패"를 섞지 않는다.
    // 이름으로 찾는 것은 로그인의 이 둘(P_USER_SELECT_BY_NAME · P_USER_CREATE)뿐이고 나머지는 전부 User.UserID(playerId)로 찾는다.
    bool            ExtractUserInfo(const string& name, DB_USER_INFO& out, bool& found);   // P_USER_SELECT_BY_NAME(대소문자 구분 없음)
    bool            ExtractUserInfo(int playerId, DB_USER_INFO& out, bool& found);         // P_USER_SELECT
    // P_USER_CREATE: outPlayerId = 그 이름의 UserID. created = 이 호출이 행을 넣었는가(같은 이름을 동시에 만들면 하나만 true).
    bool            AddUserInfoInDataBase(const string& name, const string& password, int& outPlayerId, bool& created);
    bool            SaveUserInfo(int playerId, short x, short y, UInt8 level, UInt32 exp);  // P_USER_UPSERT

    // playerId는 로그인/계정생성 시 확보해 CUser에 캐싱해둔 정수 PK(User.UserID)를 그대로 쓴다.
    bool            ExtractInventory(int playerId, vector<DB_ITEM_INFO>& out);               // P_INVENTORY_SELECT
    bool            SaveInventorySlot(int playerId, UInt16 slotIndex, UInt16 itemId, UInt16 count, bool equipped, UInt32 enchant, UInt64 extra = 0);
    bool            DeleteInventorySlot(int playerId, UInt16 slotIndex);
    bool SaveEnchant(int playerId, const DB_ENCHANT_SAVE& save, bool& first);
    // 경제 1차 제작 · 분해: 여러 칸 + 그로시를 한 트랜잭션으로(P_ITEM_BATCH_TRADE — 칸마다 낙관 검사, 그로시는 바뀔 때만 검사 · 저장).
    // false = 커밋하지 않았다(검사에 걸림 · 연결 오류 — 아무것도 바뀌지 않는다).
    bool SaveItemBatch(int playerId, const DB_ITEM_BATCH_SAVE& save);
    DB_TRANSFER_RESULT SaveTransfer(const DB_TRANSFER_SAVE& save);
    bool LoadEnchantGrace(int playerId, std::vector<DB_ENCHANT_GRACE>& out);

    // ── 리니지풍 컨텐츠 확장 ──
    // 그로시/성향치는 name이 아니라 playerId 기준(인벤토리와 같은 샤드)으로 저장한다.
    bool            SavePlayerAdena(int playerId, UInt32 adena);
    bool            SavePlayerAlignment(int playerId, Int16 alignment, UInt16 pkCount);
    // 상점 거래: 그로시 + 슬롯 하나를 하나의 SQL 트랜잭션으로 반영한다(P_SHOP_TRADE 프로시저).
    bool            ShopTransaction(int playerId, UInt32 adena, const DB_ITEM_SLOT_SAVE& slot);
    // V2 캐릭터 진행 데이터(직업·능력치 포인트·퀘스트 등)를 한 덩어리(버전 있는 바이너리)로
    // 읽고 쓴다(UserProgress 테이블). 기능이 늘 때마다 컬럼을 늘리지 않기 위함. 행이 없으면 out은 비고 true.
    bool            LoadPlayerProgress(int playerId, std::vector<UInt8>& out);
    bool            SavePlayerProgress(int playerId, const std::vector<UInt8>& data);
    // Phase 2-D 기술 성장(UserSkill): 레벨 1 · 갈래 없음이 아닌 기술만 행이 있다
    bool            LoadPlayerSkills(int playerId, std::vector<DB_SKILL_INFO>& out);
    bool            SavePlayerSkill(int playerId, const DB_SKILL_INFO& skill);   // 한 기술 upsert(기본값이면 삭제) — 한 트랜잭션
    bool            ResetPlayerSkills(int playerId);                             // 모두 초기화(행 삭제)

    // a/b 두 슬롯을 하나의 SQL 트랜잭션으로 함께 반영한다(각 슬롯은 독립적으로
    // upsert 또는 삭제될 수 있다 — DB_ITEM_SLOT_SAVE::hasItem 참고). 장착 시 이전
    // 장비 자동 탈착, 슬롯 교체처럼 "두 슬롯이 논리적으로 한 동작"인 경우 반드시
    // 이걸 쓴다 — SaveInventorySlot을 두 번 따로 부르면 그 사이에 서버가 죽었을 때
    // DB에 한쪽만 반영된 불변조건 위반 상태가 남을 수 있다.
    bool            SaveTwoInventorySlots(int playerId, const DB_ITEM_SLOT_SAVE& a, const DB_ITEM_SLOT_SAVE& b);

    // ── Phase 5 결사 · 경매장 ──
    // 프로시저 하나 = 트랜잭션 하나. bool = 커밋했는가(상태 검사에 걸리면 false — 아무것도 바뀌지 않는다).
    // 결사 이름 · 공지는 UTF-8로 받아 UTF-16(NVARCHAR)으로 넘긴다. 시각은 유닉스 초(BIGINT).
    bool            LoadGuilds(vector<DB_GUILD_ROW>& out);
    bool            LoadGuildMembers(vector<DB_GUILD_MEMBER_ROW>& out);
    bool            GuildCreate(int playerId, UInt32 adenaAfter, int guildId, const string& name, const string& nameKey, UInt8 emblem);
    bool            GuildMemberSet(int guildId, int playerId, UInt8 rank);          // 들어옴 · 직위(행이 있으면 바꾼다)
    bool            GuildMemberRemove(int guildId, int playerId);                   // 나감 · 추방
    bool            GuildTransfer(int guildId, int fromId, int toId);               // 결사장 넘기기(두 행 + 결사 행)
    bool            GuildSetInfo(int guildId, UInt8 emblem, const string& notice); // 문장 · 공지
    bool            GuildDisband(int guildId);
    bool            GuildMemberSeen(int playerId, Int64 unixSec);                  // 마지막 접속(나갈 때)
    // ── 결사 성장(GUILD.md) ──
    bool            LoadGuildProgress(vector<DB_GUILD_PROGRESS_ROW>& out);          // 모든 결사(진행이 없는 결사도 — data 빈 채로)
    bool            GuildProgressSave(int guildId, const std::vector<UInt8>& data); // 진행 덩어리(결사 번호 샤드 — 넣은 순서대로)
    // 금고 ± + 기록 한 줄(한 트랜잭션). 빼서 0보다 작아지면 아무것도 바꾸지 않고 false
    bool            GuildSealsAdd(int guildId, Int32 delta, const DB_GUILD_LOG_ROW& log);
    // 결사 상점(사람 샤드): 금고 - price(모자라면 false) + 소지품 한 칸 + 기록 한 줄 — 한 트랜잭션
    bool            GuildShopBuy(int playerId, int guildId, UInt32 price, const DB_ITEM_SLOT_SAVE& slot, const DB_GUILD_LOG_ROW& log);
    bool            GuildLogAdd(const DB_GUILD_LOG_ROW& log);
    bool            LoadGuildLogs(vector<DB_GUILD_LOG_ROW>& out, int keepPerGuild); // 결사마다 최근 keepPerGuild줄(오래된 것부터)
    bool            LoadGuildPosts(vector<DB_GUILD_POST_ROW>& out);
    bool            GuildPostAdd(const DB_GUILD_POST_ROW& post, Int64 dropPostId);  // 넣고 dropPostId(0 = 없음)를 지운다 — 한 트랜잭션
    bool            GuildPostDelete(int guildId, Int64 postId);
    bool            GuildPostPin(int guildId, Int64 postId, bool pinned);
    bool            GuildPostMaxId(Int64& maxPostId);
    bool            LoadAuctionListings(vector<DB_LISTING_ROW>& out);               // 판매 중(status 0)
    bool            LoadAuctionSettlements(vector<DB_SETTLEMENT_ROW>& out);         // 받지 않은 행
    bool            LoadAuctionPrices(vector<DB_PRICE_ROW>& out, Int64 sinceUnix);
    bool            AuctionMaxIds(Int64& maxListing, Int64& maxSettlement, int& maxGuild);
    bool            AuctionRegister(int sellerId, UInt32 adenaAfter, const DB_ITEM_SLOT_SAVE& slot, const DB_LISTING_ROW& listing);
    bool            AuctionBuy(int buyerId, UInt32 adenaAfter, Int64 listingId, UInt32 price, Int64 buyerSettleId, Int64 sellerSettleId, UInt32 sellerGets, Int64 nowUnix,
                               UInt64 settleExtraOr);   // 구매자 정산 줄 extra |= settleExtraOr(산 장비의 거래 이력 bit 9)
    bool            AuctionClose(Int64 listingId, UInt8 status, Int64 settleId, Int64 nowUnix);   // 2 취소 · 3 만료 → 판매자 정산함(물건)
    bool            SettlementClaim(int playerId, Int64 settlementId, UInt32 adenaAfter, const DB_ITEM_SLOT_SAVE& slot, Int64 nowUnix);
    // 장애 시험: 그로시 총량 · 물건 수(소지품 · 판매 중 목록 · 받지 않은 정산함) — 서버를 켤 때 한 줄
    bool            AuditTotals(UInt64& walletAdena, UInt64& settleAdena, UInt64& invUnits, UInt64& listingUnits, UInt64& settleUnits, UInt64& feesAndDeposits);

private:
    // UTF-8 input converted to UTF-16 must outlive SQLExecute.
    std::deque<std::u16string> m_BoundText;
    SQLHENV         m_pEnviroment = SQL_NULL_HANDLE;
    SQLHDBC         m_pConnection = SQL_NULL_HANDLE;
    SQLHSTMT        m_pStatement = SQL_NULL_HANDLE;
    SQLRETURN       m_Retcode = SQL_SUCCESS;
    bool            m_bConnectionLost = false;
    // 붙을 때의 "연결 끊김 세대". 어느 커넥션이든 끊김을 보면 세대가 올라가고, 그 전에 붙은 커넥션은
    // 다음 EnsureConnected에서 새로 붙는다 — DB가 재시작되면 풀에서 쉬던 커넥션도 다 끊긴 상태이기 때문이다.
    UInt64      m_LossGeneration = 0;
};
