#pragma once

#include <deque>
#include <functional>
#include <array>
#include <map>
#include <set>
#include <optional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Content/ServiceCommon.h"
#include "Database/GameData/GuildAuction.h"

class CDBConnection;
class CUser;
struct AUCTION_REQ_PACKET;

// ─────────────────────────────────────────────────────────────────────────────
// Phase 5 경매장 · 정산함 — 규칙 Database/GameData/GuildAuction.h
//
//  CAuctionService: 판매 중 목록 · 받지 않은 정산함 · 시세의 주인. 전용 스레드 하나만 상태를 만진다(락 없음).
//   · 구매: 서비스가 목록을 잠근다(거래 중) → 구매자 Zone이 그로시를 확인해 메모리에서 빼고 → 구매자 샤드 한 트랜잭션
//     (목록 판매 중 확인 · 판매됨 · 구매자 그로시 · 구매자 물건 정산 행 · 판매자 대금 정산 행 · 시세) → 커밋되면 서비스가 확정.
//     이미 잠긴 · 팔린 목록은 잠그는 단계에서 거절 — 진 쪽의 그로시는 한 번도 빠지지 않는다.
//   · 판매자에게는 대금을 직접 넣지 않고 정산함 행으로 — 판매자가 오프라인 · 다른 Zone이어도 그로시의 작성자는 그 사람의 Zone 하나뿐.
//   · 등록: 서비스가 자리(1인 10개) · 번호를 예약 → 판매자 Zone이 슬롯에서 빼고 보증금을 뺀다 → 판매자 샤드 한 트랜잭션 → 공개.
//   · 취소 · 만료: 서비스가 잠그고 판매자 샤드 한 트랜잭션(목록 상태 · 판매자 물건 정산 행). 만료는 1분마다.
//   · 정산함 받기: 받는 사람 Zone이 자리를 잡아 메모리에 넣고 → 그 사람 샤드 한 트랜잭션(행 받음 · 그로시 · 슬롯).
//   · 한 사람의 진행 중인 경매 거래(등록 · 구매 · 취소 · 받기)는 한 번에 하나(m_Inflight).
//
//  CAuctionHelper: 요청한 사람의 Zone 스레드 쪽(검사 · 멱등 → 서비스 큐).
// ─────────────────────────────────────────────────────────────────────────────
class CAuctionService
{
public:
    using Task = CGameLogicThread::Task;

    CAuctionService();

    // 켤 때(메인 스레드 · 접속을 받기 전)
    bool LoadFromDB(CDBConnection& db, Int64 maxListingId, Int64 maxSettlementId);
    void Run();
    void Post(Task task);

    // ── 서비스 스레드(Post 안)에서만 ──
    void Tick();                                   // 1초마다 — 만료는 kExpireTickMs마다
    void OnLogin(const Service::Ctx& ctx);
    void OnLogout(int playerId);
    void Handle(const Service::Ctx& ctx, const AUCTION_REQ_PACKET& p);

    [[nodiscard]] size_t ListingCount() const noexcept { return m_Listings.size(); }
    [[nodiscard]] size_t SettlementCount() const noexcept;
    [[nodiscard]] static UInt32 HourSec() noexcept;   // 기간 한 시간의 실제 초(시험 배율)

private:
    enum class State : UInt8 { Selling, Buying, Closing };
    struct Listing
    {
        DB_LISTING_ROW row;
        State    state = State::Selling;
        int      buyer = 0;                     // Buying일 때
        // 검색 색인(등록 · 불러올 때 한 번)
        UInt8  cat = 0, sub = 0, rarity = 0, reqLevel = 0;
        UInt32 unit = 0;                        // 한 개 값
        const char* name = "";                  // 표시 이름(UTF-8, 표의 상수)
    };
    struct Settlement
    {
        DB_SETTLEMENT_ROW row;
        bool claiming = false;
    };
    struct Online
    {
        ObjID obj{};
        SessionWeakPtr session;
    };

    void Index(Listing& l) const;
    [[nodiscard]] UInt16 MinutesLeft(const DB_LISTING_ROW& r, Int64 now) const noexcept;
    [[nodiscard]] int UnclaimedCount(int playerId) const;

    void Ack(const Service::Ctx& ctx, AuctionOp op, AuctionResult r, UInt32 seq, UInt32 id = 0,
        UInt32 adena = 0, Int32 delta = 0, UInt16 itemId = 0, UInt16 count = 0) const;
    void NotifySettleCount(int playerId) const;
    void SendTo(int playerId, const AUCTION_NFY_PACKET& p) const;

    void Search(const Service::Ctx& ctx, const AUCTION_REQ_PACKET& p) const;
    void SendMine(const Service::Ctx& ctx, UInt16 page) const;
    void SendSettlements(const Service::Ctx& ctx, UInt16 page) const;
    void PriceHint(const Service::Ctx& ctx, UInt16 itemId, UInt8 minRarity) const;

    void Register(const Service::Ctx& ctx, const AUCTION_REQ_PACKET& p);
    void FinishRegister(bool ok, const Service::Ctx& ctx, UInt32 seq, const DB_LISTING_ROW& row, UInt32 adenaAfter);
    void Buy(const Service::Ctx& ctx, UInt32 seq, UInt32 listingId, UInt32 price);
    void FinishBuy(bool ok, const Service::Ctx& ctx, UInt32 seq, UInt32 listingId, Int64 buyerSettle, Int64 sellerSettle,
        UInt32 adenaAfter, Int64 now);
    void Cancel(const Service::Ctx& ctx, UInt32 seq, UInt32 listingId);
    void Close(UInt32 listingId, UInt8 status, const std::optional<Service::Ctx>& ctx, UInt32 seq);   // 2 취소(ctx 있음) · 3 만료
    void FinishClose(bool ok, UInt32 listingId, UInt8 status, Int64 settleId, Int64 now, const std::optional<Service::Ctx>& ctx, UInt32 seq);
    void Claim(const Service::Ctx& ctx, UInt32 seq, UInt32 settlementId);
    void FinishClaim(bool ok, const Service::Ctx& ctx, UInt32 seq, Int64 settlementId, UInt32 adenaAfter);

    void RecordPrice(UInt16 itemId, UInt32 unit);

private:
    CGameLogicThread                                     m_Loop;
    std::map<UInt32, Listing>                            m_Listings;       // 판매 중 · 거래 중 · 닫는 중
    std::array<std::set<UInt32>, static_cast<size_t>(AuctionRule::Cat::Count)> m_ByCat;   // 분류 → 목록 번호(검색 색인)
    std::unordered_map<int, int>                        m_PerSeller;      // 등록 수(예약 포함)
    std::unordered_map<int, std::vector<Settlement>>    m_Settlements;    // 받는 사람 → 받지 않은 행(번호 순)
    std::unordered_map<UInt16, std::deque<UInt32>>      m_Prices;         // 품목 → 최근 한 개 값
    std::unordered_map<int, Online>                     m_Online;
    std::unordered_set<int>                             m_Inflight;       // 거래가 진행 중인 사람
    UInt32                                              m_NextListingId = 1;
    Int64                                               m_NextSettlementId = 1;
    UInt32                                              m_LastExpireAt = 0;
};

extern std::shared_ptr<CAuctionService> GAuctionService;

namespace CAuctionHelper
{
    // ── 요청한 사람의 소유 Zone 스레드 ──
    void HandleRequest(const UserSharedPtr& user, const AUCTION_REQ_PACKET& p);
    void OnEnterWorld(const UserSharedPtr& user);
    void OnLeaveWorld(const UserSharedPtr& user);
}
