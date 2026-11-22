#include "pch.h"
#include "Content/Chat/ChatHelper.h"
#include "User/User.h"
#include "Network/GameSession.h"
#include "Network/GameSessionManager.h"
#include "Base/GameObjectManager.h"
#include "Content/Field/Sector.h"
#include "Entity/Subject/SubjectHelper.h"
#include "User/UserHelper.h"
#include "Content/Guild/GuildService.h"
#include "Content/Party/PartyHelper.h"
#include "Database/GameData/Load/GameData.h"
#include "Content/Economy/EconomyLog.h"
#include "Content/Social/SocialHelper.h"

#include <deque>
#include <string_view>

namespace CChatHelper
{
    namespace
    {
        template<typename Packet>
        void InitializePacket(Packet& packet, PacketType type)
        {
            packet = {};
            packet.size = sizeof(Packet);
            packet.type = static_cast<char>(type);
        }

        // 최근 대화(접속 직후 20줄) — 여러 Zone 스레드가 쓰므로 락. 한 줄마다 잠깐(복사 1회).
        struct RecentLine
        {
            UInt8    channel = 0;
            ZoneId   zone = InvalidZoneId;   // 일반 대화의 지역(전체 · 거래는 무시)
            ObjID    sender{};
            char     name[NAME_SIZE]{};
            char     mess[CHAT_SIZE]{};
        };
        std::mutex             g_recentLock;
        std::deque<RecentLine> g_recent;

        void Remember(ChatChannel ch, ZoneId zone, const ObjID& sender, const std::string& name, const char* text)
        {
            RecentLine line;
            line.channel = static_cast<UInt8>(ch);
            line.zone = zone;
            line.sender = sender;
            ::strncpy_s(line.name, NAME_SIZE, name.c_str(), _TRUNCATE);
            ::strncpy_s(line.mess, CHAT_SIZE, text, _TRUNCATE);
            lock_guard lock(g_recentLock);
            g_recent.push_back(line);
            // 지역마다 일반 대화가 섞여 있으니 넉넉히(지역 16 · 전체 · 거래)
            while (g_recent.size() > kRecentMax * 8)
                g_recent.pop_front();
        }

        UInt32 Hash(const char* s)
        {
            UInt32 h = 2166136261u;
            for (; *s; ++s) { h ^= static_cast<UInt8>(*s); h *= 16777619u; }
            return h;
        }

        void Fill(SC_CHAT_PACKET& p, UInt8 channel, const ObjID& id, const std::string& name, const char* text)
        {
            InitializePacket(p, PacketType::SC_CHAT);
            p.channel = channel;
            p.senderId = id;
            ::strncpy_s(p.senderName, NAME_SIZE, name.c_str(), _TRUNCATE);
            ::strncpy_s(p.mess, CHAT_SIZE, text, _TRUNCATE);
        }

        bool Deliverable(const SessionSharedPtr& s)
        {
            return s != nullptr && s->m_eState == SOCKET_STATE::ST_INGAME;
        }

        void Reject(const UserSharedPtr& user, SystemMessageCode code, Int32 p1 = 0, Int32 p2 = 0)
        {
            CUserHelper::SendSystem(user, code, p1, p2);
        }

        // ── 개발 서버(--dev) 데이터 명령 — 보낸 사람에게만 일반 대화 한 줄로 답한다(새 패킷 없음, 대화로 퍼지지 않음) ──
        //   /dataver     지금 기획 데이터 판(manifest 해시) · 세대 수
        //   /reloaddata  GameData/를 바로 다시 읽기(값만 바뀐 판만 — 감시 스레드가 하고, 끝나면 답한다)
        //   /economy     경제 로그의 지금 값(CEconomyLog::SnapshotJson — 오늘 06:00 KST부터, 되돌리지 않는다). 한 줄 JSON을
        //                대화 한 줄(127바이트)씩 잘라 보낸다(보낸 사람 「경제」 — 이어 붙이면 원래 JSON)
        void DevReply(const SessionSharedPtr& s, const ObjID& id, const std::string& text, const char* from = "데이터")
        {
            SC_CHAT_PACKET pk;
            Fill(pk, static_cast<UInt8>(ChatChannel::Normal), id, from, text.c_str());
            s->PostSend(pk);
        }

        bool DevDataCommand(const UserSharedPtr& user, const SessionSharedPtr& session, const char* body)
        {
            const std::string_view cmd(body);
            if (cmd == "/dataver")
            {
                DevReply(session, user->GetObjID(), "판 " + GameData::Version() + " · 세대 " + std::to_string(GameData::Generations()));
                return true;
            }
            if (cmd == "/economy")
            {
                const std::string json = CEconomyLog::SnapshotJson();   // ASCII뿐 — 바이트로 잘라도 글자가 깨지지 않는다
                constexpr size_t kChunk = CHAT_SIZE - 1;
                for (size_t at = 0; at < json.size(); at += kChunk)
                    DevReply(session, user->GetObjID(), json.substr(at, kChunk), "경제");
                return true;
            }
            if (cmd == "/reloaddata")
            {
                DevReply(session, user->GetObjID(), "기획 데이터를 다시 읽는다");
                SessionWeakPtr weak = session;
                const ObjID id = user->GetObjID();
                GameData::RequestReload([weak, id](const GameData::ReloadResult& r)
                {
                    if (auto s = weak.lock())
                        DevReply(s, id, r.published ? "판 " + r.from + " → " + r.load.version + " (세대 " + std::to_string(r.generation) + ")"
                                                    : "거부 — " + r.reason);
                });
                return true;
            }
            return false;
        }
    }

    std::string ReportEvidence(const std::string& target)
    {
        lock_guard lock(g_recentLock); std::string out; int count=0;
        for(auto it=g_recent.rbegin();it!=g_recent.rend() && count<10;++it)
            if(target==it->name) { out+=std::to_string(it->channel)+":"+it->mess+"\n"; ++count; }
        return out;
    }
    bool IsValidUtf8(const char* s, size_t n)
    {
        size_t i = 0;
        while (i < n)
        {
            const UInt8 c = static_cast<UInt8>(s[i]);
            if (c < 0x80)
            {
                if (c < 0x20 || c == 0x7F) return false;   // 제어 문자(줄바꿈 · 탭 포함) 거부
                ++i;
                continue;
            }
            int len = 0;
            UInt32 cp = 0;
            if ((c & 0xE0) == 0xC0) { len = 2; cp = c & 0x1F; }
            else if ((c & 0xF0) == 0xE0) { len = 3; cp = c & 0x0F; }
            else if ((c & 0xF8) == 0xF0) { len = 4; cp = c & 0x07; }
            else return false;
            if (i + static_cast<size_t>(len) > n) return false;
            for (int k = 1; k < len; ++k)
            {
                const UInt8 cc = static_cast<UInt8>(s[i + static_cast<size_t>(k)]);
                if ((cc & 0xC0) != 0x80) return false;
                cp = (cp << 6) | (cc & 0x3F);
            }
            // 너무 긴 표현 · 서로게이트 · 범위 밖
            if ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000)) return false;
            if (cp >= 0xD800 && cp <= 0xDFFF) return false;
            if (cp > 0x10FFFF) return false;
            i += static_cast<size_t>(len);
        }
        return true;
    }

    void HandleChat(const UserSharedPtr& sender, UInt8 channel, const char* targetName, const char* mess)
    {
        if (sender == nullptr)
            return;
        auto session = sender->GetGameSession();
        if (!session || session->m_eState != SOCKET_STATE::ST_INGAME)
            return;

        // ── 글: 복사(널 종료 보장) · 앞뒤 공백 · UTF-8 ──
        char text[CHAT_SIZE + 1]{};
        ::memcpy(text, mess, CHAT_SIZE);
        size_t n = ::strnlen(text, CHAT_SIZE);
        while (n > 0 && text[n - 1] == ' ') text[--n] = '\0';
        size_t start = 0;
        while (start < n && text[start] == ' ') ++start;
        const char* body = text + start;
        const size_t len = n - start;
        const ChatChannel ch = static_cast<ChatChannel>(channel);
        const bool knownChannel = ch == ChatChannel::Normal || ch == ChatChannel::World || ch == ChatChannel::Trade
            || ch == ChatChannel::Guild || ch == ChatChannel::WhisperOut || ch == ChatChannel::Party;
        if (len == 0 || !knownChannel || !IsValidUtf8(body, len))
        {
            Reject(sender, SystemMessageCode::ChatBadText);
            return;
        }
        if (body[0] == '/' && GameData::DevMode() && DevDataCommand(sender, session, body))
            return;

        const UInt32 now = GetNowTime();
        // ── 토큰 버킷(5줄 / 5초) ──
        if (sender->m_ChatRefillAt == 0) sender->m_ChatRefillAt = now;
        if (now > sender->m_ChatRefillAt)
        {
            const UInt32 add = (now - sender->m_ChatRefillAt) / kBucketRefillMs;
            if (add > 0)
            {
                const UInt32 t = std::min<UInt32>(kBucketMax, sender->m_ChatTokens + add);
                sender->m_ChatTokens = static_cast<UInt8>(t);
                sender->m_ChatRefillAt = t >= static_cast<UInt32>(kBucketMax) ? now : sender->m_ChatRefillAt + add * kBucketRefillMs;
            }
        }
        if (sender->m_ChatTokens == 0)
        {
            const UInt32 wait = kBucketRefillMs - std::min<UInt32>(kBucketRefillMs, now - sender->m_ChatRefillAt);
            Reject(sender, SystemMessageCode::ChatTooFast, channel, static_cast<Int32>(wait));
            return;
        }
        // ── 같은 말 되풀이(30초 안의 세 번째부터) ──
        const UInt32 h = Hash(body);
        const bool same = h == sender->m_ChatLastHash && now - sender->m_ChatLastAt < kRepeatWindowMs;
        if (same && sender->m_ChatRepeat >= 1)
        {
            Reject(sender, SystemMessageCode::ChatRepeated);
            return;
        }
        // ── 채널 재사용 대기 · 대상 ──
        if (ch == ChatChannel::World && sender->m_LastWorldChatTime != 0 && now < sender->m_LastWorldChatTime + kWorldCooldownMs)
        {
            Reject(sender, SystemMessageCode::ChatTooFast, channel, static_cast<Int32>(sender->m_LastWorldChatTime + kWorldCooldownMs - now));
            return;
        }
        if (ch == ChatChannel::Trade && sender->m_LastTradeChatTime != 0 && now < sender->m_LastTradeChatTime + kTradeCooldownMs)
        {
            Reject(sender, SystemMessageCode::ChatTooFast, channel, static_cast<Int32>(sender->m_LastTradeChatTime + kTradeCooldownMs - now));
            return;
        }
        if (ch == ChatChannel::Guild && sender->m_GuildId.load(std::memory_order_relaxed) == 0)
        {
            Reject(sender, SystemMessageCode::ChatNoGuild);   // 결사가 없다
            return;
        }
        if (ch == ChatChannel::Party && sender->m_PartyId.load(std::memory_order_relaxed) == 0)
        {
            Reject(sender, SystemMessageCode::ChatNoParty);   // 파티가 없다
            return;
        }
        SessionSharedPtr target;
        UserSharedPtr targetUser;
        if (ch == ChatChannel::WhisperOut)
        {
            char name[NAME_SIZE + 1]{};
            ::memcpy(name, targetName, NAME_SIZE);
            target = GSessionManager->FindByName(name);
            targetUser = target ? target->GetOwner() : nullptr;
            if(targetUser && CSocialHelper::Blocked(sender->GetPlayerId(),targetUser->GetPlayerId()))
            { Reject(sender,SystemMessageCode::ChatNoTarget); return; }
            if (!Deliverable(target) || targetUser == nullptr || target == session)
            {
                Reject(sender, SystemMessageCode::ChatNoTarget);
                return;
            }
        }

        // ── 받아들임: 상태 갱신 ──
        --sender->m_ChatTokens;
        sender->m_ChatRepeat = same ? static_cast<UInt8>(sender->m_ChatRepeat + 1) : 0;
        sender->m_ChatLastHash = h;
        sender->m_ChatLastAt = now;
        if (ch == ChatChannel::World) sender->m_LastWorldChatTime = now;
        if (ch == ChatChannel::Trade) sender->m_LastTradeChatTime = now;

        const ObjID senderId = sender->GetObjID();
        SC_CHAT_PACKET pk;
        switch (ch)
        {
        case ChatChannel::Normal:
        {
            // 같은 지역(Zone) 전체 — 보낸 사람은 섹터에서 빠져 있어도(죽음) 먼저 받는다
            Fill(pk, static_cast<UInt8>(ChatChannel::Normal), senderId, sender->GetName(), body);
            session->PostSend(pk);
            GSector->ForEachUserInZone([&](const SubjectSharedPtr& object)
            {
                if (object->GetObjID() == senderId) return;
                const auto* recipient=static_cast<CUser*>(object.get());
                auto s = recipient->GetGameSession();
                if (Deliverable(s) && !CSocialHelper::Blocked(sender->GetPlayerId(),recipient->GetPlayerId())) s->PostSend(pk);
            });
            Remember(ch, LCurrentZoneId, senderId, sender->GetName(), body);
            break;
        }
        case ChatChannel::World:
        case ChatChannel::Trade:
        {
            Fill(pk, channel, senderId, sender->GetName(), body);
            for (const auto& s : GSessionManager->SnapshotSessions())
            {
                const auto recipient=s->GetOwner();
                if (Deliverable(s) && recipient && !CSocialHelper::Blocked(sender->GetPlayerId(),recipient->GetPlayerId())) s->PostSend(pk);
            }
            Remember(ch, LCurrentZoneId, senderId, sender->GetName(), body);
            break;
        }
        case ChatChannel::Guild:
        {
            // Phase 5: 결사원(접속 중)에게 — 구성원 목록의 주인인 CGuildService가 보낸다(보낸 사람 포함)
            CGuildHelper::RelayChat(sender, body);
            break;
        }
        case ChatChannel::Party:
        {
            // 파티원(접속 중)에게 — 구성원 목록의 주인인 CPartyService가 보낸다(보낸 사람 포함). 최근 대화에 남기지 않는다
            CPartyHelper::RelayChat(sender, body);
            break;
        }
        case ChatChannel::WhisperOut:
        {
            // 받는 사람: "WhisperIn — 보낸 이", 보낸 사람: "WhisperOut — 받는 이"(이름은 받는 사람 것으로 맞춘다)
            Fill(pk, static_cast<UInt8>(ChatChannel::WhisperIn), senderId, sender->GetName(), body);
            target->PostSend(pk);
            SC_CHAT_PACKET echo;
            Fill(echo, static_cast<UInt8>(ChatChannel::WhisperOut), targetUser->GetObjID(), targetUser->GetName(), body);
            session->PostSend(echo);
            break;
        }
        default:
            break;
        }
    }

    void BroadcastSystem(const ObjID& subject, const std::string& name, const char* text)
    {
        SC_CHAT_PACKET pk;
        Fill(pk, static_cast<UInt8>(ChatChannel::System), subject, name, text);
        for (const auto& s : GSessionManager->SnapshotSessions())
            if (Deliverable(s)) s->PostSend(pk);
        Remember(ChatChannel::System, LCurrentZoneId, subject, name, text);
    }

    void SendSystemTo(const UserSharedPtr& user, const char* text)
    {
        if (user == nullptr)
            return;
        const auto session = user->GetGameSession();
        if (!Deliverable(session))
            return;
        SC_CHAT_PACKET pk;
        Fill(pk, static_cast<UInt8>(ChatChannel::System), user->GetObjID(), user->GetName(), text);
        session->PostSend(pk);
    }

    void OnEnterWorld(const UserSharedPtr& user)
    {
        if (user == nullptr)
            return;
        auto session = user->GetGameSession();
        if (!session)
            return;
        GSessionManager->RegisterName(user->GetName(), session);
        CSocialHelper::OnEnterWorld(user);

        // 최근 대화: 전체 · 거래 · 시스템(전리품 알림) + 이 지역의 일반 대화에서 마지막 20줄
        std::vector<RecentLine> lines;
        {
            lock_guard lock(g_recentLock);
            for (auto it = g_recent.rbegin(); it != g_recent.rend() && lines.size() < kRecentMax; ++it)
            {
                const bool normal = it->channel == static_cast<UInt8>(ChatChannel::Normal);
                if (normal && it->zone != LCurrentZoneId) continue;
                if (CSocialHelper::BlockedNames(user->GetName(),it->name)) continue;
                lines.push_back(*it);
            }
        }
        for (auto it = lines.rbegin(); it != lines.rend(); ++it)
        {
            SC_CHAT_PACKET pk;
            Fill(pk, static_cast<UInt8>(it->channel | kChatHistoryBit), it->sender, it->name, it->mess);
            session->PostSend(pk);
        }
    }
}
