#pragma once
#include "Content/Social/SocialStore.h"
namespace CSocialHelper
{
    void OnEnterWorld(const UserSharedPtr& user);
    void HandleRequest(const UserSharedPtr& user,const SOCIAL_REQ_PACKET& req);
    bool Blocked(int a,int b);
    bool BlockedNames(const std::string& a,const std::string& b);
}
