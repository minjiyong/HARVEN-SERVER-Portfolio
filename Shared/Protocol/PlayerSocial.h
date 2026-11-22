#pragma once
#include <cstdint>
#pragma pack(push, 1)
enum class SocialOp : uint8_t { List, FriendRequest, FriendAccept, FriendDecline, FriendRemove, Block, Unblock, Report };
enum class SocialResult : uint8_t { Ok, Invalid, NotFound, Blocked, Full, TooFast, StorageError, NoRequest };
enum class SocialEntry : uint8_t { Begin, Friend, Block, Incoming, Outgoing, End };
enum class ReportReason : uint8_t { Abuse, Cheating, Fraud, Other };
struct SOCIAL_REQ_PACKET
{
    unsigned short size; char type;
    uint8_t op; uint32_t seq; uint8_t reason;
    char targetName[20]; char note[128];
};
struct SOCIAL_ACK_PACKET
{
    unsigned short size; char type;
    uint8_t op, result; uint32_t seq; uint64_t receipt;
    char targetName[20];
};
struct SOCIAL_ENTRY_PACKET
{
    unsigned short size; char type;
    uint32_t revision; uint8_t kind, online; char name[20];
};
#pragma pack(pop)
static_assert(sizeof(SOCIAL_REQ_PACKET)==157 && sizeof(SOCIAL_ACK_PACKET)==37 && sizeof(SOCIAL_ENTRY_PACKET)==29);
