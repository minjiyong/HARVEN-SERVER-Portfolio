#include "pch.h"
#include "Content/Social/SocialHelper.h"
#include "Content/ServiceCommon.h"
#include "Content/Chat/ChatHelper.h"
#include "Content/Transfer/TransferHelper.h"
#include "Content/Party/PartyService.h"
#include "Network/GameSessionManager.h"

namespace CSocialHelper
{
    namespace
    {
        std::atomic<uint32_t> revision{0};
        std::mutex snapshotLock;
        std::string Name(const char* p,size_t n) { return std::string(p,strnlen(p,n)); }
        void Snapshot(int id,const SessionWeakPtr& session)
        {
            std::lock_guard guard(snapshotLock);
            SOCIAL_ENTRY_PACKET p{}; p.size=sizeof(p); p.type=120; p.revision=++revision;
            p.kind=static_cast<uint8_t>(SocialEntry::Begin); Service::SendTo(session,p);
            for(const auto& e:Social::Global().List(id))
            {
                p.kind=static_cast<uint8_t>(e.kind);
                ::strncpy_s(p.name,sizeof(p.name),e.name.c_str(),_TRUNCATE);
                const auto peer=GSessionManager->FindByName(e.name);
                p.online=peer && peer->m_eState==ST_INGAME;
                Service::SendTo(session,p);
            }
            p.kind=static_cast<uint8_t>(SocialEntry::End); Service::SendTo(session,p);
        }
        void Ack(const UserSharedPtr& user,const SOCIAL_REQ_PACKET& req,SocialResult result,uint64_t receipt=0)
        {
            SOCIAL_ACK_PACKET p{}; p.size=sizeof(p); p.type=119; p.op=req.op; p.seq=req.seq;
            p.result=static_cast<uint8_t>(result); p.receipt=receipt;
            ::memcpy(p.targetName,req.targetName,sizeof(p.targetName)); Service::SendTo(user->GetGameSession(),p);
        }
    }
    bool Blocked(int a,int b) { return Social::Global().Blocked(a,b); }
    bool BlockedNames(const std::string& a,const std::string& b)
    { auto& store=Social::Global(); return store.Blocked(store.Find(a),store.Find(b)); }
    void OnEnterWorld(const UserSharedPtr& user)
    {
        if(!user) return;
        Social::Global().Register(user->GetPlayerId(),user->GetName()); Snapshot(user->GetPlayerId(),user->GetGameSession());
    }
    void HandleRequest(const UserSharedPtr& user,const SOCIAL_REQ_PACKET& req)
    {
        if(!user) return;
        const auto op=static_cast<SocialOp>(req.op);
        if(req.op>static_cast<uint8_t>(SocialOp::Report) || req.seq==0) return Ack(user,req,SocialResult::Invalid);
        const auto now=GetNowTime();
        if(user->m_LastSocialAt && now-user->m_LastSocialAt<300) return Ack(user,req,SocialResult::TooFast);
        user->m_LastSocialAt=now;
        if(op==SocialOp::List) { Ack(user,req,SocialResult::Ok); Snapshot(user->GetPlayerId(),user->GetGameSession()); return; }
        const auto name=Name(req.targetName,sizeof(req.targetName)),note=Name(req.note,sizeof(req.note));
        if(name.empty() || name.size()>=sizeof(req.targetName) || !CChatHelper::IsValidUtf8(name.data(),name.size()) ||
           !CChatHelper::IsValidUtf8(note.data(),note.size())) return Ack(user,req,SocialResult::Invalid);
        auto& store=Social::Global(); const int me=user->GetPlayerId(); store.Register(me,user->GetName());
        const auto peerSession=GSessionManager->FindByName(name); const auto peer=peerSession?peerSession->GetOwner():nullptr;
        if(peer) store.Register(peer->GetPlayerId(),peer->GetName());
        const int other=store.Find(name); uint64_t receipt=0;
        const auto result=op==SocialOp::Report ? store.Report(me,other,static_cast<ReportReason>(req.reason),note,
            CChatHelper::ReportEvidence(name),receipt) : store.Change(me,other,op);
        if(result==SocialResult::Ok && op==SocialOp::Block)
        {
            CTransferHelper::CancelWith(user,other);
            if(GPartyService) GPartyService->Post([me,other]{ GPartyService->OnBlock(me,other); });
        }
        Ack(user,req,result,receipt);
        if(result==SocialResult::Ok && op!=SocialOp::Report)
        { Snapshot(me,user->GetGameSession()); if(peerSession) Snapshot(other,peerSession); }
    }
}
