#include "Content/Social/SocialStore.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>
#define CHECK(x) do { if(!(x)){std::cerr<<"FAIL line "<<__LINE__<<" "<<#x<<'\n'; return 1;} }while(false)
int main()
{
    const auto dir=std::filesystem::temp_directory_path()/("harven-social-"+std::to_string(getpid()));
    const auto path=(dir/"state.json").string(); uint64_t receipt=0;
    {
        Social::Store s(path); s.Register(1,"Alice"); s.Register(2,"Bob"); s.Register(3,"Carol");
        CHECK(s.Find("ALICE")==1);
        CHECK(s.Change(1,2,SocialOp::FriendRequest)==SocialResult::Ok);
        CHECK(s.Change(2,1,SocialOp::FriendAccept)==SocialResult::Ok);
        CHECK(s.List(1).size()==1 && s.List(2).size()==1);
        CHECK(s.Change(1,2,SocialOp::Block)==SocialResult::Ok);
        CHECK(s.Blocked(1,2) && s.Blocked(2,1) && !s.Blocked(1,3));
        CHECK(s.List(2).empty());
        CHECK(s.Change(2,1,SocialOp::FriendRequest)==SocialResult::Blocked);
        CHECK(s.Change(1,1,SocialOp::Block)==SocialResult::Invalid);
        CHECK(s.Report(1,2,ReportReason::Abuse,"욕설 신고","server evidence",receipt)==SocialResult::Ok);
        CHECK(receipt>0 && std::filesystem::file_size(path+".reports.jsonl")>0);
        CHECK(s.Report(1,2,ReportReason::Abuse,"","",receipt)==SocialResult::TooFast);
    }
    {
        Social::Store s(path); CHECK(s.Blocked(1,2));
        CHECK(s.Report(1,2,ReportReason::Abuse,"","",receipt)==SocialResult::TooFast);
        CHECK(s.Change(1,2,SocialOp::Unblock)==SocialResult::Ok);
        CHECK(!s.Blocked(1,2));
        CHECK(s.Change(2,1,SocialOp::FriendAccept)==SocialResult::NoRequest);
        CHECK(s.Change(2,1,SocialOp::Block)==SocialResult::Ok);
        CHECK(s.Change(1,2,SocialOp::Unblock)==SocialResult::Ok && s.Blocked(1,2));
    }
    std::ofstream(path)<<"broken";
    { Social::Store s(path); CHECK(s.Blocked(1,2)); CHECK(s.Change(1,2,SocialOp::Unblock)==SocialResult::StorageError); }
    std::filesystem::remove_all(dir); std::cout<<"PASS social persistence / reciprocal block / friends / report receipt / fail-closed\n";
}
