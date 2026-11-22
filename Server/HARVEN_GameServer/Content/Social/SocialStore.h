#pragma once
#include "Protocol/PlayerSocial.h"
#include <map>
#include <set>
#include <string>
#include <vector>
#include <mutex>

namespace Social
{
    struct Profile
    {
        std::string name;
        std::set<int> friends, blocks, incoming;
    };
    struct Entry { SocialEntry kind; int id; std::string name; };
    class Store
    {
    public:
        explicit Store(std::string path);
        void Register(int id, const std::string& name);
        int Find(const std::string& name) const;
        bool Blocked(int a,int b) const;
        SocialResult Change(int a,int b,SocialOp op);
        std::vector<Entry> List(int id) const;
        SocialResult Report(int a,int b,ReportReason reason,const std::string& note,
                            const std::string& evidence,uint64_t& receipt);
    private:
        bool Save() const;
        bool BlockedUnlocked(int a,int b) const;
        std::string m_path;
        mutable std::mutex m_lock;
        std::map<int,Profile> m_profiles;
        std::map<int,uint64_t> m_reports;
        uint64_t m_receipt=0;
        bool m_ready=true;
    };
    Store& Global();
}
