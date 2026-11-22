#include "pch.h"
#include "Content/Social/SocialStore.h"
#include "ThirdParty/nlohmann/json.hpp"
#include <filesystem>
#include <fstream>
#include <chrono>

namespace Social
{
    using nlohmann::json;
    namespace fs=std::filesystem;
    Store::Store(std::string path):m_path(std::move(path))
    {
        try
        {
            if(fs::exists(m_path))
            {
                std::ifstream in(m_path); const auto root=json::parse(in);
                for(const auto& p:root.at("players"))
                    m_profiles.emplace(p.at("id").get<int>(),Profile{p.at("name"),p.at("friends").get<std::set<int>>(),
                        p.at("blocks").get<std::set<int>>(),p.at("incoming").get<std::set<int>>()});
            }
            std::ifstream reports(m_path+".reports.jsonl"); std::string line;
            while(std::getline(reports,line))
            {
                const auto r=json::parse(line,nullptr,false); if(r.is_discarded()) continue;
                m_reports[r.at("reporter").get<int>()]=r.at("time").get<uint64_t>();
                m_receipt=std::max(m_receipt,r.at("receipt").get<uint64_t>());
            }
        }
        catch(const std::exception& e)
        { m_ready=false; std::fprintf(stderr,"[Social] cannot load %s: %s\n",m_path.c_str(),e.what()); }
    }
    bool Store::Save() const
    {
        try
        {
            fs::create_directories(fs::path(m_path).parent_path()); json rows=json::array();
            for(const auto& [id,p]:m_profiles)
                rows.push_back({{"id",id},{"name",p.name},{"friends",p.friends},{"blocks",p.blocks},{"incoming",p.incoming}});
            const std::string temp=m_path+".tmp";
            std::ofstream out(temp,std::ios::trunc); out<<json{{"version",1},{"players",rows}}.dump(); out.flush();
            if(!out.good()) return false; out.close(); fs::rename(temp,m_path); return true;
        }
        catch(const std::exception& e)
        { std::fprintf(stderr,"[Social] save failed: %s\n",e.what()); return false; }
    }
    void Store::Register(int id,const std::string& name)
    { std::lock_guard guard(m_lock); if(m_ready && id>0) m_profiles[id].name=name; }
    int Store::Find(const std::string& name) const
    {
        std::lock_guard guard(m_lock);
        auto key=[](std::string s){for(char& c:s) if(c>='A' && c<='Z') c=static_cast<char>(c-'A'+'a'); return s;};
        const auto wanted=key(name); for(const auto& [id,p]:m_profiles) if(key(p.name)==wanted) return id; return 0;
    }
    bool Store::BlockedUnlocked(int a,int b) const
    {
        if(!m_ready) return true;
        if(a==b) return false;
        const auto x=m_profiles.find(a),y=m_profiles.find(b);
        return (x!=m_profiles.end() && x->second.blocks.count(b)) || (y!=m_profiles.end() && y->second.blocks.count(a));
    }
    bool Store::Blocked(int a,int b) const
    { std::lock_guard guard(m_lock); return BlockedUnlocked(a,b); }
    SocialResult Store::Change(int a,int b,SocialOp op)
    {
        std::lock_guard guard(m_lock);
        if(!m_ready) return SocialResult::StorageError;
        if(a<=0 || a==b) return SocialResult::Invalid;
        if(!m_profiles.count(a) || !m_profiles.count(b)) return SocialResult::NotFound;
        auto& x=m_profiles.at(a); auto& y=m_profiles.at(b);
        if(op!=SocialOp::Block && op!=SocialOp::Unblock && op!=SocialOp::FriendRemove && BlockedUnlocked(a,b))
            return SocialResult::Blocked;
        const auto oldX=x,oldY=y;
        switch(op)
        {
        case SocialOp::Block:
            if(x.blocks.size()>=200 && !x.blocks.count(b)) return SocialResult::Full;
            x.blocks.insert(b); x.friends.erase(b); y.friends.erase(a); x.incoming.erase(b); y.incoming.erase(a); break;
        case SocialOp::Unblock: x.blocks.erase(b); break;
        case SocialOp::FriendRequest:
            if(x.friends.count(b)) return SocialResult::Ok;
            if(x.friends.size()>=100 || y.friends.size()>=100 || y.incoming.size()>=100) return SocialResult::Full;
            if(!y.incoming.count(a))
            {
                size_t outgoing=0; for(const auto& [id,p]:m_profiles) { (void)id; if(p.incoming.count(a)) ++outgoing; }
                if(outgoing>=100) return SocialResult::Full;
            }
            y.incoming.insert(a); break;
        case SocialOp::FriendAccept:
            if(!x.incoming.count(b)) return SocialResult::NoRequest;
            if(x.friends.size()>=100 || y.friends.size()>=100) return SocialResult::Full;
            x.friends.insert(b); y.friends.insert(a); x.incoming.erase(b); y.incoming.erase(a); break;
        case SocialOp::FriendDecline: x.incoming.erase(b); break;
        case SocialOp::FriendRemove: x.friends.erase(b); y.friends.erase(a); break;
        default: return SocialResult::Invalid;
        }
        if(!Save()) { x=oldX; y=oldY; return SocialResult::StorageError; }
        return SocialResult::Ok;
    }
    std::vector<Entry> Store::List(int id) const
    {
        std::lock_guard guard(m_lock); std::vector<Entry> out; const auto it=m_profiles.find(id);
        if(it==m_profiles.end()) return out;
        auto add=[&](SocialEntry kind,const std::set<int>& ids)
        { for(int other:ids) if(m_profiles.count(other)) out.push_back({kind,other,m_profiles.at(other).name}); };
        add(SocialEntry::Friend,it->second.friends); add(SocialEntry::Block,it->second.blocks); add(SocialEntry::Incoming,it->second.incoming);
        for(const auto& [other,p]:m_profiles) if(p.incoming.count(id)) out.push_back({SocialEntry::Outgoing,other,p.name});
        return out;
    }
    SocialResult Store::Report(int a,int b,ReportReason reason,const std::string& note,const std::string& evidence,uint64_t& receipt)
    {
        std::lock_guard guard(m_lock);
        if(!m_ready) return SocialResult::StorageError;
        if(a==b || a<=0 || static_cast<uint8_t>(reason)>3 || note.size()>127) return SocialResult::Invalid;
        if(!m_profiles.count(a) || !m_profiles.count(b)) return SocialResult::NotFound;
        const uint64_t now=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        if(m_reports.count(a) && now<m_reports[a]+60) return SocialResult::TooFast;
        try
        {
            fs::create_directories(fs::path(m_path).parent_path());
            const uint64_t next=std::max(now*1000,m_receipt+1);
            std::ofstream out(m_path+".reports.jsonl",std::ios::app);
            out<<json{{"receipt",next},{"time",now},{"reporter",a},{"reporter_name",m_profiles.at(a).name},
                {"target",b},{"target_name",m_profiles.at(b).name},{"reason",static_cast<int>(reason)},
                {"reporter_note",note},{"server_chat_evidence",evidence}}.dump()<<'\n'; out.flush();
            if(!out.good()) return SocialResult::StorageError;
            m_reports[a]=now; m_receipt=next; receipt=next; return SocialResult::Ok;
        }
        catch(...) { return SocialResult::StorageError; }
    }
    Store& Global()
    {
        static Store store(std::getenv("HARVEN_SOCIAL_STORE")?std::getenv("HARVEN_SOCIAL_STORE"):"Bin/Social/state.json");
        return store;
    }
}
