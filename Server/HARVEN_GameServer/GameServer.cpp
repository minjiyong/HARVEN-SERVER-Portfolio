#include "pch.h"
#include <iostream>
#include <atomic>
#include <mutex>
#include <new>
#include <future>
#include "WorkerThread/ThreadManager.h"
#include "Base/Core/CrashHandler.h"
#include "Base/Core/LocalEnv.h"
#include "User/User.h"
#include "Entity/Subject/Monster/Monster.h"
#include "Worker/GameLogicThread.h"
#include "Worker/WorkerThread.h"
#include "Database/DBConnectionPool.h"
#include "Network/SocketManager.h"
#include "Database/DBThread.h"
#include "Worker/TimerThread.h"
#include "Content/Navigator/Collision.h"
#include "Content/Field/ZoneLayout.h"
#include "Content/Field/ZoneManager.h"
#include "Content/Guild/GuildService.h"
#include "Content/Auction/AuctionService.h"
#include "Content/Party/PartyService.h"
#include "Content/Economy/EconomyLog.h"
#include "Content/ServiceCommon.h"
#include "Database/GameData/Load/GameData.h"


int main(int argc, char* argv[])
{
    // 죽을 때 crash.log에 신호 · 호출 스택 · 메모리를 남긴다(Base/Core/CrashHandler)
    CCrashHandler::Install();

    // 개발 PC의 harven.env(클라우드 DB 접속 정보 등 — Git에 올리지 않는다)를 환경 변수로 올린다.
    // 이미 있는 환경 변수가 먼저다(운영 · CI). 스레드를 띄우기 전에 한 번(Base/Core/LocalEnv)
    CLocalEnv::Load(argc > 0 ? argv[0] : nullptr);

    // ── 기획 데이터(GameData/ — 시트 HARVEN_DATA에서 datatool이 만든다) ─────────────
    // 스레드를 시작하기 전에 한 번 읽는다. 틀린 데이터면 원본 위치를 모두 찍고 뜨지 않는다(SHEET_DATA_PROMPT [7]).
    //   --data-dir <폴더>   기본: 실행 파일 옆 GameData/, 없으면 위로 올라가며 GameData/manifest.json이 있는 곳
    //   --check-data <폴더> 읽고 검증만 하고 끝낸다(datatool check · D2 시험). 오류 줄: ERROR<tab>위치<tab>이유<tab>R번호
    //   --dump-data <폴더>  읽은 메모리를 정본 JSON으로 쓰고 끝낸다(datatool golden — 3자 일치의 셋째)
    //   --dev               개발 서버: GameData/manifest.json이 바뀌면 다시 읽는다(값만 바뀐 판 — 핫 리로드, 운영 기본 끔).
    //                       대화창 /dataver(판 · 세대) · /reloaddata(바로 다시 읽기)도 --dev에서만.
    {
        std::string dataDir, dumpDir, checkDir;
        bool dev = false;
        for (int i = 1; i < argc; ++i)
            if (std::string(argv[i]) == "--dev") dev = true;
        for (int i = 1; i + 1 < argc; ++i)
        {
            const std::string a = argv[i];
            if (a == "--data-dir") dataDir = argv[i + 1];
            else if (a == "--dump-data") dumpDir = argv[i + 1];
            else if (a == "--check-data") checkDir = argv[i + 1];
        }
        if (!checkDir.empty())
        {
            const GameData::LoadResult r = GameData::CheckDir(checkDir);
            for (const auto& e : r.errors)
                std::cout << "ERROR\t" << e.where << "\t" << e.what << "\t" << e.rule << "\n";
            std::cout << GameData::Describe(r, 0) << std::endl;
            return r.ok ? 0 : 1;
        }
        const std::filesystem::path dir = GameData::ResolveDir(dataDir, argc > 0 ? argv[0] : nullptr);
        const GameData::LoadResult r = GameData::LoadFromDir(dir);
        std::cout << GameData::Describe(r) << " dir=" << dir.string() << std::endl;
        if (!r.ok)
            return 1;
        if (!dumpDir.empty())
        {
            std::string err;
            if (!GameData::DumpToDir(dumpDir, err))
            {
                std::cout << "ERROR\t--dump-data\t" << err << "\t" << std::endl;
                return 1;
            }
            std::cout << "[GameData] dumped to " << dumpDir << std::endl;
            return 0;
        }
        if (dev)
            GameData::StartDevWatch(dir);
    }

    // --view-range N : 시야 범위(기본 9). 포트폴리오 부하 테스트 조건은 5.
    // --load-test : 부하 시험 — 새 캐릭터를 Zone들에 흩어 세우고, 죽으면 그 Zone 안에서 다시 세운다.
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--load-test")
            GLoadTestMode = true;
    for (int i = 1; i + 1 < argc; ++i)
    {
        if (std::string(argv[i]) == "--view-range")
        {
            const int range = std::atoi(argv[i + 1]);
            GViewRange = std::clamp(range, 3, SECTOR_RANGE);
        }
        else if (std::string(argv[i]) == "--port")
        {
            const int port = std::atoi(argv[i + 1]);
            if (port > 0 && port < 65536) GServerPort = port;
        }
    }
    std::cout << "View range: " << GViewRange << std::endl;
    if (GLoadTestMode) std::cout << "Load test mode: new users and respawns spread over zones" << std::endl;
    if (GServerPort != PORT_NUM) std::cout << "Port: " << GServerPort << std::endl;

    //소켓API이용해서 네트워크 초기설정 해주기
    CSocketManager::Init();
    if (!CSocketManager::MakeListenSocket() || !CSocketManager::Bind() ||
        !CSocketManager::Listen() || !CSocketManager::StartAccepting())
    {
        std::cerr << "Network startup failed\n";
        return 1;
    }

    InitCollisionTile();

    // 몬스터 초기화는 각 Zone 스레드의 CZone::Run() 안에서 GSector 설정 후 수행된다.
    // (메인 스레드에서 호출하면 GSector=nullptr로 크래시 발생)

    //DB풀 초기화 — DB 샤드(스레드) 하나가 동시에 커넥션 하나를 쓰므로 같은 수로 맞춘다.
    const bool dbConnected = GDBConnectionPool->Connect(CDBThread::kShardCount);

    // Phase 5 결사 · 경매장: 접속을 받기 전(워커 스레드를 띄우기 전) 메인 스레드에서 DB로 서비스 상태를 만든다.
    // 못 읽으면 두 서비스를 켜지 않는다(요청에는 Busy) — 번호가 겹친 채 거래를 받지 않기 위해.
    {
        auto db = dbConnected ? GDBConnectionPool->Pop() : nullptr;   // 연결이 없으면 Pop은 기다리기만 한다
        Int64 maxListing = 0, maxSettlement = 0;
        int maxGuild = 0;
        auto guild = std::make_shared<CGuildService>();
        auto auction = std::make_shared<CAuctionService>();
        const bool ok = db != nullptr && db->AuctionMaxIds(maxListing, maxSettlement, maxGuild)
            && guild->LoadFromDB(*db, maxGuild) && auction->LoadFromDB(*db, maxListing, maxSettlement);
        if (ok)
        {
            GGuildService = guild;
            GAuctionService = auction;
            std::cout << "[Phase5] guilds=" << guild->GuildCount() << " members=" << guild->MemberCount()
                << " listings=" << auction->ListingCount() << " settlements=" << auction->SettlementCount()
                << " hourSec=" << CAuctionService::HourSec() << "\n" << std::flush;
        }
        else
            std::cout << "[Phase5] 결사 · 경매장 DB 불러오기 실패 — 두 서비스를 끈다\n" << std::flush;
        // 장애 시험의 기준: 그로시 = 지갑 + 받지 않은 대금 + 보증금 · 수수료, 물건 = 소지품 + 판매 중 + 받지 않은 물건
        UInt64 wallet = 0, settleAdena = 0, invUnits = 0, listingUnits = 0, settleUnits = 0, sink = 0;
        if (db != nullptr && db->AuditTotals(wallet, settleAdena, invUnits, listingUnits, settleUnits, sink))
            std::cout << "[Audit] adena wallet=" << wallet << " settle=" << settleAdena << " sink=" << sink
                << " total=" << (wallet + settleAdena + sink) << " | items inv=" << invUnits << " listing=" << listingUnits
                << " settle=" << settleUnits << " total=" << (invUnits + listingUnits + settleUnits) << "\n" << std::flush;
        if (db != nullptr)
            GDBConnectionPool->Push(db);
    }
    // 파티 서비스 — DB가 없다(서버를 다시 켜면 파티는 흩어진다). Zone 스레드가 뜨기 전에 만든다(Zone 틱 · 처치 보상이 부른다)
    GPartyService = std::make_shared<CPartyService>();

    // IO 스레드 — 코어마다 하나, 각자 io_context 하나(CSocketManager)
    for (UInt32 ioIndex = 0; ioIndex < CSocketManager::GetIoThreadCount(); ++ioIndex)
    {
        const std::string name = "io" + std::to_string(ioIndex);
        GThreadManager->Launch([ioIndex]()
        {
            GWorkerThread->DoWork(ioIndex);
        }, name.c_str());
    }

    // defaultThread: Zone 없이 Enqueue되는 작업(로그인 전 세션 정리 등) 처리
    GThreadManager->Launch([]()
    {
        GGameLogicThread->Run();
    }, "logic-default");

    // Zone 전용 로직 스레드 시작 (Zone당 독립 CGameLogicThread)
    for (ZoneId zoneId = 0; zoneId < static_cast<ZoneId>(CZoneLayout::ZoneCount); ++zoneId)
    {
        const std::string name = "zone" + std::to_string(zoneId);
        GThreadManager->Launch([zoneId]()
        {
            GZoneManager->RunZone(zoneId);
        }, name.c_str());
    }

    //DB스레드 생성 — 스레드 하나가 샤드 하나를 전담한다(DBThread.h 참고).
    // 샤드마다 담당 스레드가 정확히 하나씩 있어야 하므로 kShardCount를 그대로 쓴다.
    for (int shardIndex = 0; shardIndex < CDBThread::kShardCount; ++shardIndex)
    {
        const std::string name = "db" + std::to_string(shardIndex);
        GThreadManager->Launch([shardIndex]()
        {
            GDBThread->DoDataBase(shardIndex);
        }, name.c_str());
    }


    // 서비스 스레드(결사 · 경매장 · 파티) — 각자 작업 큐 하나. 1초 틱(초대 만료 · 경매 만료 · 끊긴 파티원 · 모집 글)은 따로 한 스레드가 넣는다.
    // 결사 · 경매장은 DB를 읽었을 때만 켠다. 파티는 늘 켠다.
    const bool phase5 = GGuildService != nullptr && GAuctionService != nullptr;
    if (phase5)
    {
        GThreadManager->Launch([]()
        {
            GGuildService->Run();
        }, "svc-guild");
        GThreadManager->Launch([]()
        {
            GAuctionService->Run();
        }, "svc-auction");
    }
    GThreadManager->Launch([]()
    {
        GPartyService->Run();
    }, "svc-party");
    GThreadManager->Launch([phase5]()
    {
        while (true)
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            if (phase5)
            {
                GGuildService->Post([]() { GGuildService->Tick(); });
                GAuctionService->Post([]() { GAuctionService->Tick(); });
            }
            GPartyService->Post([]() { GPartyService->Tick(); });
            // 경제 로그 하루 경계(06:00 KST) — 원자 카운터라 이 스레드에서 바로 쓴다(Content/Economy/EconomyLog)
            CEconomyLog::Tick(Service::UnixNow());
        }
    }, "svc-tick");

    //TImer스레드 생성
    GThreadManager->Launch([]()
    {
        GTimerThread->DoTimer();
    }, "timer");

    GThreadManager->Join();
}

