#pragma once
#include <cstdint>
#pragma pack(push, 1)
enum class TransferKind : uint8_t { Exchange, Gift, Sale };
enum class TransferOp : uint8_t { Request, Accept, SetItem, SetPrice, Ready, Confirm, Cancel };
enum class TransferPhase : uint8_t { Invited, Editing, Ready, Committing, Completed, Cancelled };
enum class TransferResult : uint8_t { Ok, Invalid, Busy, Duplicate, TooFar, Stale, BadItem, Bound,
    Equipped, BadCount, BagFull, LackAdena, WalletFull, NotReady, Declined, Expired, Unavailable, StorageError, TooFast };
inline constexpr uint8_t kTransferMaxItems = 8;
struct TRANSFER_REQ_PACKET
{
    unsigned short size;
    char type;
    uint8_t op, kind;
    uint32_t seq;
    uint64_t transferId;
    uint32_t revision;
    ObjID target;
    uint16_t slotIndex, count;
    uint32_t price;
    char targetName[NAME_SIZE]; // Request: target가 npos이면 같은 Zone에서 이 이름을 찾는다
};
struct TRANSFER_ACK_PACKET
{
    unsigned short size;
    char type;
    uint8_t op, result;
    uint32_t seq;
    uint64_t transferId;
    uint32_t revision;
};
struct TRANSFER_STATE_PACKET
{
    unsigned short size;
    char type;
    uint64_t transferId;
    uint32_t revision;
    uint8_t kind, phase, result, side; // side=0 요청자(증여자/판매자), 1 상대
    ObjID peer;
    char peerName[NAME_SIZE];
    uint32_t price, remainingMs;
    uint8_t readyMask, confirmMask, itemCounts[2];
};
struct TRANSFER_ITEM_PACKET
{
    unsigned short size;
    char type;
    uint64_t transferId;
    uint32_t revision;
    uint8_t side, index;
    uint16_t slotIndex, itemId, count;
    uint32_t enchant;
    uint64_t extra;
};
static_assert(sizeof(TRANSFER_REQ_PACKET) <= 255 && sizeof(TRANSFER_STATE_PACKET) <= 255);
static_assert(sizeof(TRANSFER_REQ_PACKET) == 65 && sizeof(TRANSFER_ACK_PACKET) == 21 &&
    sizeof(TRANSFER_STATE_PACKET) == 67 && sizeof(TRANSFER_ITEM_PACKET) == 35, "교환 패킷 바이트 계약");
#pragma pack(pop)
