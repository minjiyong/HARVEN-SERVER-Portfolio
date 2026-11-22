// 개인 업적 규칙 시험 — Database/GameData/AchieveRules.h(색인 BuildCatalog) · Content/Achieve/AchieveBook(진척 → 달성 · 받음 · 저장 절) ·
// User/ProgressCodec(진행 기록 판 4).
//   cmake --build <폴더> --target AchieveRuleTests && <폴더>/Server/HARVEN_GameServer/tests/AchieveRuleTests
// 1) 색인 — id → 행 · 키(kind, param) 정렬 · 키의 업적은 target 오름차순 · 점수 합
// 2) 통계 → 달성 — 누적(처치 0 · 종 · 정예 · 보스 · 파티) · 최댓값(레벨 · 강화) · 장비(그 등급 이상) · 퀘스트(0 · id) · 지역 수 ·
//    한 번만 달성 · 표가 세지 않는 키는 만들지 않는다 · u32에서 멈춤 · 묶음(단계 1~4가 차례로)
// 3) 받기 — 달성 전 · 두 번 · 요약(점수 · 달성 · 받을 수)
// 4) 저장 — 업적 절 왕복(무작위) · 진행 기록 판 4 왕복 · 판 3 기록(업적 없음)을 읽으면 업적은 빈 채 · 깨진 · 잘린 덩어리는 터지지 않는다 ·
//    가장 큰 기록 ≤ VARBINARY(4000)의 절반
// 5) 불러온 뒤 다시 보기(Recheck) — 표 목표가 낮아지면 달성
#include <algorithm>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include "Content/Achieve/AchieveBook.h"
#include "User/ProgressCodec.h"

namespace
{
    int g_failures = 0;
    int g_checks = 0;
    void Check(bool ok, const char* what, int seed = -1)
    {
        ++g_checks;
        if (!ok)
        {
            ++g_failures;
            if (g_failures < 30)
                std::printf("FAIL: %s (seed %d)\n", what, seed);
        }
    }

    using AchieveRule::Row;
    using AchieveRule::StatKey;

    Row R(uint16_t id, AchCategory cat, uint16_t group, uint8_t step, AchKind kind, uint16_t param, uint32_t target, uint16_t points,
          uint16_t item = 0, uint16_t count = 0, uint32_t adena = 0)
    {
        Row r;
        r.id = id; r.category = cat; r.group = group; r.step = step; r.name = "업적"; r.desc = "설명";
        r.kind = kind; r.param = param; r.target = target; r.points = points; r.rewardItem = item; r.rewardCount = count; r.rewardAdena = adena;
        return r;
    }

    // 명세 1차 표의 모양(묶음 · 키) — 49행 중 시험에 필요한 것
    AchieveRule::Catalog SpecCatalog()
    {
        AchieveRule::Catalog c;
        using K = AchKind;
        using C = AchCategory;
        c.rows = {
            R(1, C::eHunt, 1, 1, K::eKill, 0, 1, 5, 1, 10, 100), R(2, C::eHunt, 1, 2, K::eKill, 0, 100, 10, 8, 10, 500),
            R(3, C::eHunt, 1, 3, K::eKill, 0, 1000, 20, 61, 50, 2000), R(4, C::eHunt, 1, 4, K::eKill, 0, 10000, 40, 59, 1, 10000),
            R(5, C::eHunt, 2, 1, K::eKillElite, 0, 1, 10, 64, 2), R(6, C::eHunt, 2, 2, K::eKillElite, 0, 10, 20, 64, 5),
            R(8, C::eHunt, 3, 1, K::ePartyKill, 0, 100, 10, 8, 20),
            R(10, C::eBoss, 4, 1, K::eKillBoss, 29, 1, 20, 13, 1), R(16, C::eBoss, 10, 1, K::eKillBoss, 0, 10, 30, 63, 30),
            R(18, C::eGrowth, 11, 1, K::eLevel, 0, 5, 5, 10, 5, 500), R(19, C::eGrowth, 11, 2, K::eLevel, 0, 10, 10, 11, 5, 2000),
            R(23, C::eGrowth, 11, 6, K::eLevel, 0, 50, 60, 60, 2, 50000),
            R(24, C::eGrowth, 12, 1, K::eDeath, 0, 1, 5, 1, 10),
            R(25, C::eGrowth, 13, 1, K::eAdena, 0, 100000, 10, 8, 10),
            R(27, C::eGear, 14, 1, K::eGear, 1, 1, 5, 61, 20), R(28, C::eGear, 14, 2, K::eGear, 2, 1, 10, 62, 20),
            R(29, C::eGear, 14, 3, K::eGear, 3, 1, 25, 63, 20), R(30, C::eGear, 14, 4, K::eGear, 4, 1, 50, 60, 1),
            R(31, C::eGear, 15, 1, K::eEnchant, 0, 1, 5, 13, 1), R(32, C::eGear, 15, 2, K::eEnchant, 0, 5, 15, 59, 1),
            R(35, C::eCraft, 16, 1, K::eCraft, 0, 1, 5, 66, 10), R(38, C::eCraft, 17, 1, K::eDismantle, 0, 1, 5, 61, 20),
            R(40, C::eCraft, 18, 1, K::eEssence, 0, 500, 15, 64, 3),
            R(41, C::eWorld, 19, 1, K::eQuest, 0, 1, 5, 1, 10), R(42, C::eWorld, 19, 2, K::eQuest, 0, 20, 20, 11, 10),
            R(43, C::eWorld, 20, 1, K::eGuildJoin, 0, 1, 10, 12, 5), R(44, C::eWorld, 21, 1, K::eAuctionSell, 0, 1, 5, 0, 0, 500),
            R(45, C::eWorld, 22, 1, K::eAuctionBuy, 0, 10, 10, 0, 0, 2000), R(46, C::eWorld, 23, 1, K::eTrade, 0, 1, 5, 8, 5),
            R(47, C::eWorld, 24, 1, K::eZone, 0, 5, 10, 11, 5), R(48, C::eWorld, 24, 2, K::eZone, 0, 10, 20, 12, 10),
            R(49, C::eWorld, 24, 3, K::eZone, 0, 16, 40, 59, 1),
            R(60, C::eWorld, 30, 1, K::eQuest, 7, 1, 3, 1, 1),   // 특정 퀘스트
        };
        AchieveRule::BuildCatalog(c);
        return c;
    }

    bool Has(const AchieveBook::DoneList& v, uint16_t id)
    {
        for (const uint16_t x : v) if (x == id) return true;
        return false;
    }

    std::vector<uint8_t> Section(const AchieveBook& b, const AchieveRule::Catalog* cat)
    {
        std::vector<uint8_t> out;
        b.Write(out, cat);
        return out;
    }

    void TestCatalog()
    {
        const auto c = SpecCatalog();
        Check(c.Find(1) != nullptr && c.Find(1)->target == 1, "Find(1)");
        Check(c.Find(49) != nullptr && c.Find(49)->kind == AchKind::eZone, "Find(49)");
        Check(c.Find(7) == nullptr && c.Find(0) == nullptr && c.Find(1023) == nullptr && c.Find(5000) == nullptr, "없는 id");
        bool sorted = true;
        for (size_t i = 1; i < c.keys.size(); ++i) sorted &= c.keys[i - 1] < c.keys[i];
        Check(sorted, "키 정렬 · 겹침 없음");
        const int k = c.KeyIndex(StatKey{ AchKind::eKill, 0 });
        Check(k >= 0 && c.RowsOfKey(k).size() == 4, "eKill 0의 업적 넷");
        uint32_t last = 0;
        bool asc = true;
        for (const uint16_t at : c.RowsOfKey(k)) { asc &= c.rows[at].target >= last; last = c.rows[at].target; }
        Check(asc, "키의 업적은 target 오름차순");
        Check(c.KeyIndex(StatKey{ AchKind::eKill, 5 }) < 0, "표에 없는 키");
        uint32_t pts = 0;
        for (const auto& r : c.rows) pts += r.points;
        Check(c.totalPoints == pts, "점수 합");
    }

    void TestCounting()
    {
        const auto c = SpecCatalog();
        AchieveBook b;
        AchieveBook::DoneList d;
        // 첫 처치 → 1
        b.OnKill(c, 3, false, false, false, d);
        Check(d.size() == 1 && d[0] == 1, "첫 처치 → 업적 1");
        Check(b.Value(StatKey{ AchKind::eKill, 0 }) == 1, "처치 수 1");
        Check(b.Value(StatKey{ AchKind::eKill, 3 }) == 0 && b.Stats().size() == 1, "표가 세지 않는 키(eKill 3)는 만들지 않는다");
        d.clear();
        for (int i = 0; i < 98; ++i) b.OnKill(c, 3, false, false, false, d);
        Check(d.empty() && b.Value(StatKey{ AchKind::eKill, 0 }) == 99, "99 — 아직");
        b.OnKill(c, 3, false, false, false, d);
        Check(d.size() == 1 && d[0] == 2, "100 → 업적 2(묶음 단계 2)");
        d.clear();
        // 정예 · 보스 · 파티
        b.OnKill(c, 29, true, true, true, d);
        Check(Has(d, 5) && Has(d, 10) && !Has(d, 16) && !Has(d, 8), "정예 1 · 모로즈 1 · 보스 10은 아직 · 파티 100은 아직");
        Check(b.Value(StatKey{ AchKind::eKillBoss, 0 }) == 1 && b.Value(StatKey{ AchKind::eKillBoss, 29 }) == 1, "보스 0 · 29");
        Check(b.Value(StatKey{ AchKind::ePartyKill, 0 }) == 1, "파티 처치 1");
        d.clear();
        b.OnKill(c, 29, true, true, true, d);
        Check(!Has(d, 10) && !Has(d, 5), "한 번만 달성");
        // 레벨(최댓값)
        d.clear();
        b.Raise(c, StatKey{ AchKind::eLevel, 0 }, 12, d);
        Check(Has(d, 18) && Has(d, 19) && !Has(d, 23) && d.size() == 2, "레벨 12 → 5 · 10");
        b.Raise(c, StatKey{ AchKind::eLevel, 0 }, 7, d);
        Check(b.Value(StatKey{ AchKind::eLevel, 0 }) == 12, "최댓값은 내려가지 않는다");
        // 장비 — 영웅(3) 하나면 param 1 · 2 · 3
        d.clear();
        b.OnGear(c, 3, 1, d);
        Check(Has(d, 27) && Has(d, 28) && Has(d, 29) && !Has(d, 30), "영웅 장비 → 고급 · 희귀 · 영웅 이상");
        d.clear();
        b.OnGear(c, 0, 5, d);
        Check(d.empty(), "일반 장비는 param 1 이상을 세지 않는다");
        // 퀘스트(0 · 7)
        d.clear();
        b.OnQuest(c, 7, d);
        Check(Has(d, 41) && Has(d, 60), "퀘스트 7 → 아무거나 1 · 퀘스트 7");
        d.clear();
        b.OnQuest(c, 8, d);
        Check(d.empty() && b.Value(StatKey{ AchKind::eQuest, 0 }) == 2 && b.Value(StatKey{ AchKind::eQuest, 7 }) == 1, "다른 퀘스트는 0만");
        // 지역
        d.clear();
        for (int z = 0; z < 4; ++z) Check(b.EnterRegion(c, z, d), "새 지역");
        Check(!b.EnterRegion(c, 2, d) && d.empty(), "같은 지역 두 번");
        b.EnterRegion(c, 9, d);
        Check(d.size() == 1 && d[0] == 47 && b.Value(StatKey{ AchKind::eZone, 0 }) == 5, "지역 5곳 → 47");
        Check(!b.EnterRegion(c, 16, d) && !b.EnterRegion(c, -1, d), "범위 밖 지역");
        for (int z = 0; z < 16; ++z) b.EnterRegion(c, z, d);
        Check(Has(d, 48) && Has(d, 49) && b.Regions() == 0xFFFF, "16곳 → 48 · 49");
        // 그로시 누적 · u32에서 멈춤
        d.clear();
        b.Add(c, StatKey{ AchKind::eAdena, 0 }, 0xFFFFFFF0u, d);
        b.Add(c, StatKey{ AchKind::eAdena, 0 }, 1000, d);
        Check(b.Value(StatKey{ AchKind::eAdena, 0 }) == 0xFFFFFFFFu && Has(d, 25), "u32에서 멈춤");
        // 강화(최댓값)
        d.clear();
        b.Raise(c, StatKey{ AchKind::eEnchant, 0 }, 6, d);
        Check(Has(d, 31) && Has(d, 32), "강화 +6 → +1 · +5");
        // 진척(목표에서 자른다)
        Check(b.Progress(*c.Find(3)) == 102 && b.Progress(*c.Find(4)) == 102, "진척 102(목표 1000 · 10000)");
        Check(b.Progress(*c.Find(1)) == 1, "달성이면 목표");
        Check(b.Progress(*c.Find(16)) == 2, "보스 2 / 10");
    }

    void TestClaim()
    {
        const auto c = SpecCatalog();
        AchieveBook b;
        AchieveBook::DoneList d;
        Check(!b.MarkClaimed(1), "달성 전 받기");
        b.OnKill(c, 1, false, false, false, d);
        auto s = b.Sum(c);
        Check(s.points == 5 && s.done == 1 && s.claimable == 1 && s.total == c.rows.size(), "요약 — 점수 5 · 1 · 1");
        Check(b.State(1) == 1 && b.State(2) == 0, "상태 1 · 0");
        Check(b.MarkClaimed(1) && b.State(1) == 2, "받기");
        Check(!b.MarkClaimed(1), "두 번 받기");
        s = b.Sum(c);
        Check(s.points == 5 && s.done == 1 && s.claimable == 0, "받은 뒤 요약(점수는 달성 때 — 그대로)");
    }

    AchieveBook RandomBook(std::mt19937& rng, const AchieveRule::Catalog& c)
    {
        AchieveBook b;
        AchieveBook::DoneList d;
        std::uniform_int_distribution<int> pick(0, 9);
        for (int i = 0, n = pick(rng) * 20; i < n; ++i)
        {
            switch (pick(rng))
            {
            case 0: b.OnKill(c, static_cast<uint16_t>(rng() % 40), rng() % 2, rng() % 2, rng() % 2, d); break;
            case 1: b.Raise(c, StatKey{ AchKind::eLevel, 0 }, rng() % 51, d); break;
            case 2: b.OnGear(c, static_cast<uint8_t>(rng() % 6), 1 + rng() % 3, d); break;
            case 3: b.OnQuest(c, static_cast<uint16_t>(rng() % 10), d); break;
            case 4: b.EnterRegion(c, static_cast<int>(rng() % 16), d); break;
            case 5: b.Add(c, StatKey{ AchKind::eAdena, 0 }, rng() % 50000, d); break;
            case 6: b.Raise(c, StatKey{ AchKind::eEnchant, 0 }, rng() % 12, d); break;
            case 7: b.Add(c, StatKey{ static_cast<AchKind>(rng() % static_cast<unsigned>(AchKind::Count)), 0 }, 1, d); break;
            default:
                for (const auto& r : c.rows) if (rng() % 3 == 0) (void)b.MarkClaimed(r.id);
            }
        }
        return b;
    }

    void TestSection()
    {
        const auto c = SpecCatalog();
        for (int seed = 0; seed < 300; ++seed)
        {
            std::mt19937 rng(static_cast<unsigned>(seed));
            const AchieveBook a = RandomBook(rng, c);
            const auto bytes = Section(a, &c);
            Check(bytes.size() <= AchieveBook::kMaxSectionBytes, "절 크기 ≤ kMaxSectionBytes", seed);
            AchieveBook b;
            size_t at = 0;
            Check(b.Read(bytes.data(), bytes.size(), at) && at == bytes.size(), "절 읽기", seed);
            Check(Section(b, &c) == bytes, "절 왕복(같은 바이트)", seed);
            const auto sa = a.Sum(c), sb = b.Sum(c);
            Check(sa.points == sb.points && sa.done == sb.done && sa.claimable == sb.claimable, "요약 같음", seed);
            for (const auto& r : c.rows)
                Check(a.State(r.id) == b.State(r.id) && a.Progress(r) == b.Progress(r), "상태 · 진척 같음", seed);
            // 잘린 · 깨진 덩어리 — false 또는 다른 값일 뿐 터지지 않는다(ASan 빌드에서 함께 돈다)
            for (size_t cut = 0; cut < bytes.size(); cut += 1 + bytes.size() / 17)
            {
                AchieveBook t;
                size_t p = 0;
                Check(!t.Read(bytes.data(), cut, p) && t.Stats().empty() && t.Regions() == 0 && t.Frozen(), "잘린 절 → false · 얼림(메모리는 빈 채)", seed);
                const std::vector<uint8_t> kept = Section(t, &c);
                Check(kept == std::vector<uint8_t>(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(cut)), "얼린 책은 원본 바이트를 그대로 되쓴다", seed);
                AchieveBook::DoneList dd;
                t.Add(c, StatKey{ AchKind::eKill, 0 }, 5, dd);
                t.EnterRegion(c, 3, dd);
                Check(dd.empty() && !t.MarkClaimed(1) && !t.dirty && Section(t, &c) == kept, "얼린 책은 바뀌지 않는다", seed);
            }
            auto bad = bytes;
            if (!bad.empty())
            {
                bad[static_cast<size_t>(rng()) % bad.size()] ^= static_cast<uint8_t>(1 + rng() % 255);
                AchieveBook t;
                size_t p = 0;
                (void)t.Read(bad.data(), bad.size(), p);
                Check(p <= bad.size(), "바꾼 바이트 — 범위 안", seed);
            }
        }
        // 표가 세지 않는 키 · 값 0은 쓰지 않는다
        AchieveBook a;
        std::vector<uint8_t> raw = { 2, static_cast<uint8_t>(AchKind::eKill), 5, 0, 7, 0, 0, 0,   // eKill 5 = 7(표에 없음)
                                     static_cast<uint8_t>(AchKind::eKill), 0, 0, 3, 0, 0, 0,      // eKill 0 = 3
                                     0, 0, 0, 0 };
        size_t at = 0;
        Check(a.Read(raw.data(), raw.size(), at) && a.Value(StatKey{ AchKind::eKill, 5 }) == 7, "읽기는 모든 키");
        const auto w = Section(a, &c);
        Check(w.size() == 1 + 7 + 1 + 1 + 2 && w[0] == 1, "쓰기는 표가 세는 키만");
        // 받음 ⊆ 달성 · id 0
        std::vector<uint8_t> raw2 = { 0, 1, 0x01, 1, 0x07, 0, 0 };   // 달성 {0} · 받음 {0, 1, 2}
        AchieveBook e;
        at = 0;
        Check(e.Read(raw2.data(), raw2.size(), at) && !e.IsDone(0) && !e.IsClaimed(1) && !e.IsClaimed(2), "받음 ⊆ 달성 · id 0 없음");
        // 모르는 kind(새 판의 표가 센 것) — 건너뛰고 계속 읽고, 그 줄은 그대로 되쓴다 · 아는 줄 · 받음 비트는 남는다
        std::vector<uint8_t> raw3 = { 3,
                                      static_cast<uint8_t>(AchKind::Count) + 4, 9, 0, 1, 2, 3, 4,          // 모르는 kind 22
                                      static_cast<uint8_t>(AchKind::eKill), 0, 0, 7, 0, 0, 0,              // eKill 0 = 7
                                      static_cast<uint8_t>(AchKind::eZone), 0, 0, 5, 0, 0, 0,              // eZone 통계(쓰지 않는 줄) — 모르는 것처럼 보관
                                      1, 0x06, 1, 0x02, 0x0F, 0x00 };                                      // 달성 {1, 2} · 받음 {1} · 지역 4곳
        at = 0;
        AchieveBook u;
        Check(u.Read(raw3.data(), raw3.size(), at) && at == raw3.size() && !u.Frozen(), "모르는 kind — 건너뛰고 끝까지 읽는다");
        Check(u.UnknownStats() == 2 && u.Value(StatKey{ AchKind::eKill, 0 }) == 7 && u.IsClaimed(1) && u.IsDone(2) && !u.IsClaimed(2)
              && u.Value(StatKey{ AchKind::eZone, 0 }) == 4, "아는 줄 · 받음 비트 · 지역은 그대로");
        const auto rewritten = Section(u, &c);
        Check(rewritten.size() == raw3.size() && rewritten[0] == 3
              && std::search(rewritten.begin(), rewritten.end(), raw3.begin() + 1, raw3.begin() + 8) != rewritten.end(),
              "되쓰기 — 모르는 줄 7바이트를 그대로");
        AchieveBook u2;
        at = 0;
        Check(u2.Read(rewritten.data(), rewritten.size(), at) && Section(u2, &c) == rewritten, "되쓴 절도 같은 바이트(왕복)");
        // 비트 바이트 수 > 128 — 얼린다
        std::vector<uint8_t> raw4 = { 0, 129 };
        raw4.resize(2 + 129 + 3, 0xFF);
        at = 0;
        AchieveBook f;
        Check(!f.Read(raw4.data(), raw4.size(), at) && f.Frozen() && f.FrozenWhy() == AchieveBook::FreezeReason::BadBits && Section(f, &c) == raw4,
              "비트셋 > 128바이트 → 얼림 · 원본 되쓰기");
    }

    ProgressCodec::Data RandomData(std::mt19937& rng, bool full)
    {
        ProgressCodec::Data d;
        d.classId = static_cast<uint8_t>(rng() % 3);
        for (auto& v : d.alloc) v = static_cast<int16_t>(rng() % 1000);
        d.pityRare = static_cast<uint16_t>(rng() % 1001);
        d.pityHeroic = static_cast<uint16_t>(rng() % 1001);
        const size_t nq = full ? ProgressCodec::kMaxQuests : rng() % (ProgressCodec::kMaxQuests + 1);
        for (size_t i = 0; i < nq; ++i)
            d.quests.push_back({ static_cast<uint16_t>(1 + rng() % 255), { static_cast<uint16_t>(rng()), static_cast<uint16_t>(rng()), static_cast<uint16_t>(rng()) } });
        for (auto& b : d.questDone) b = static_cast<uint8_t>(rng());
        const size_t nc = full ? ProgressCodec::kMaxCooldowns : rng() % 40;
        for (size_t i = 0; i < nc; ++i)
            d.cooldowns.push_back({ static_cast<uint16_t>(1 + rng() % 255), static_cast<uint32_t>(rng()) });
        return d;
    }

    bool SameData(const ProgressCodec::Data& a, const ProgressCodec::Data& b)
    {
        std::vector<ProgressCodec::Cooldown> ca(a.cooldowns.begin(), a.cooldowns.begin() + static_cast<std::ptrdiff_t>(std::min(a.cooldowns.size(), ProgressCodec::kMaxCooldowns)));
        return a.classId == b.classId && a.alloc == b.alloc && a.pityRare == b.pityRare && a.pityHeroic == b.pityHeroic && a.quests == b.quests
            && a.questDone == b.questDone && ca == b.cooldowns;
    }

    void TestBlob()
    {
        const auto c = SpecCatalog();
        size_t biggest = 0;
        for (int seed = 0; seed < 300; ++seed)
        {
            std::mt19937 rng(static_cast<unsigned>(1000 + seed));
            const ProgressCodec::Data d = RandomData(rng, false);
            const AchieveBook book = RandomBook(rng, c);
            const auto blob = ProgressCodec::Encode(d, book, &c);
            Check(blob[0] == 'P' && blob[1] == '2' && blob[2] == 4, "판 4", seed);
            biggest = std::max(biggest, blob.size());
            // v4 왕복
            ProgressCodec::Data d2;
            AchieveBook b2;
            Check(ProgressCodec::Decode(blob, d2, b2) && d2.version == 4, "판 4 읽기", seed);
            Check(SameData(d, d2), "판 4 왕복 — 퀘스트 · 천장 · 능력치", seed);
            Check(Section(b2, &c) == Section(book, &c), "판 4 왕복 — 업적", seed);
            Check(ProgressCodec::Encode(d2, b2, &c) == blob, "판 4 다시 쓰기 = 같은 바이트", seed);
            // 판 3(업적 절 없음) — 업적은 빈 채, 나머지는 같다
            auto v3 = blob;
            v3.resize(blob.size() - Section(book, &c).size());
            v3[2] = 3;
            ProgressCodec::Data d3;
            AchieveBook b3 = RandomBook(rng, c);   // 앞 내용이 남지 않는지
            Check(ProgressCodec::Decode(v3, d3, b3) && d3.version == 3, "판 3 읽기", seed);
            Check(SameData(d, d3), "판 3 → 같은 퀘스트 · 천장", seed);
            Check(b3.Stats().empty() && b3.Sum(c).done == 0 && b3.Regions() == 0, "판 3 → 업적 빈 채", seed);
            // 판 3 → 판 4로 다시 쓰면 업적 절(빈 것)이 붙는다
            const auto up = ProgressCodec::Encode(d3, b3, &c);
            Check(up[2] == 4 && up.size() == v3.size() + 5, "판 3 → 판 4(빈 업적 절 5바이트)", seed);
            // 잘린 v4 — 터지지 않는다
            for (size_t cut = 0; cut < blob.size(); cut += 1 + blob.size() / 23)
            {
                ProgressCodec::Data t;
                AchieveBook tb;
                const std::vector<uint8_t> part(blob.begin(), blob.begin() + static_cast<std::ptrdiff_t>(cut));
                const bool ok = ProgressCodec::Decode(part, t, tb);
                Check(ok == (cut >= 16), "잘린 기록 — 16바이트 미만만 false", seed);
                Check(tb.Stats().size() <= book.Stats().size(), "잘린 기록 — 업적 절은 비거나 그대로", seed);
                if (cut >= v3.size())
                {
                    // 업적 절 안에서 잘렸다 — 얼려서 다음 저장이 남은 원본 바이트를 그대로 쓴다(빈 절로 덮어 받음 비트를 잃지 않는다)
                    Check(tb.Frozen() && ProgressCodec::Encode(t, tb, &c) == part, "업적 절이 잘린 기록 — 저장해도 원본 그대로", seed);
                }
            }
            // 모르는 kind 줄이 섞인 판 4 기록 — 아는 부분 · 받음 비트가 왕복한다
            {
                auto withUnknown = v3;
                withUnknown[2] = 4;
                std::vector<uint8_t> sec;
                book.Write(sec, &c);
                std::vector<uint8_t> extra = { static_cast<uint8_t>(AchKind::Count) + 1, 1, 0, 9, 9, 9, 9 };
                sec.insert(sec.begin() + 1 + sec[0] * 7, extra.begin(), extra.end());
                ++sec[0];
                withUnknown.insert(withUnknown.end(), sec.begin(), sec.end());
                ProgressCodec::Data du;
                AchieveBook bu;
                Check(ProgressCodec::Decode(withUnknown, du, bu) && !bu.Frozen() && bu.UnknownStats() == 1, "모르는 kind 섞인 기록 읽기", seed);
                bool same = SameData(d, du);
                for (const auto& r : c.rows)
                    same &= bu.State(r.id) == book.State(r.id) && bu.Progress(r) == book.Progress(r);
                Check(same, "모르는 kind 섞인 기록 — 아는 부분 · 받음 비트 그대로", seed);
                ProgressCodec::Data dz;
                AchieveBook bz;
                Check(ProgressCodec::Decode(ProgressCodec::Encode(du, bu, &c), dz, bz) && bz.UnknownStats() == 1 && Section(bz, &c) == Section(bu, &c),
                      "모르는 kind 섞인 기록 — 다시 써도 모르는 줄이 남는다", seed);
            }
            // 절 뒤에 바이트가 남았다 · 새 판 — 얼려서 원본 그대로
            {
                auto trailing = blob;
                trailing.push_back(0xAB);
                ProgressCodec::Data dt;
                AchieveBook bt;
                Check(ProgressCodec::Decode(trailing, dt, bt) && bt.Frozen() && bt.FrozenWhy() == AchieveBook::FreezeReason::Trailing
                      && ProgressCodec::Encode(dt, bt, &c) == trailing, "절 뒤 남은 바이트 — 얼림 · 원본 그대로", seed);
                auto newer = blob;
                newer[2] = 5;
                ProgressCodec::Data dn;
                AchieveBook bn;
                Check(ProgressCodec::Decode(newer, dn, bn) && bn.Frozen() && bn.FrozenVersion() == 5 && ProgressCodec::Encode(dn, bn, &c) == newer,
                      "새 판(5) — 얼림 · 판 번호와 원본 그대로", seed);
            }
        }
        // 판 1 · 2(옛 기록)
        std::vector<uint8_t> v1 = { 'P', '2', 1, 2, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
        ProgressCodec::Data d1;
        AchieveBook b1;
        Check(ProgressCodec::Decode(v1, d1, b1) && d1.classId == 2 && d1.alloc[0] == 5 && d1.quests.empty() && b1.Stats().empty(), "판 1");
        Check(!ProgressCodec::Decode({ 'P', '1', 1 }, d1, b1), "V2가 아닌 기록");
        // 가장 큰 기록
        std::mt19937 rng(7);
        const ProgressCodec::Data full = RandomData(rng, true);
        AchieveRule::Catalog big;
        for (uint16_t k = 0; k < AchieveRule::kMaxStatKeys; ++k)
            big.rows.push_back(R(static_cast<uint16_t>(AchieveRule::kMaxId - k), AchCategory::eHunt, static_cast<uint16_t>(k + 1), 1, AchKind::eKill, k, 1, 1));
        std::sort(big.rows.begin(), big.rows.end(), [](const Row& a, const Row& b) { return a.id < b.id; });
        AchieveRule::BuildCatalog(big);
        AchieveBook maxed;
        AchieveBook::DoneList d;
        for (uint16_t k = 0; k < AchieveRule::kMaxStatKeys; ++k)
            maxed.Add(big, StatKey{ AchKind::eKill, k }, 0xFFFFFFFFu, d);
        for (const auto& r : big.rows) (void)maxed.MarkClaimed(r.id);
        for (int z = 0; z < 16; ++z) maxed.EnterRegion(big, z, d);
        const auto maxBlob = ProgressCodec::Encode(full, maxed, &big);
        std::printf("진행 기록 크기: 무작위 최대 %zu B · 가득 찬 기록 %zu B · 상한 kMaxBytes %zu B · DB %zu B\n",
            biggest, maxBlob.size(), ProgressCodec::kMaxBytes, ProgressCodec::kDbBytes);
        Check(maxBlob.size() <= ProgressCodec::kMaxBytes, "가득 찬 기록 ≤ kMaxBytes");
        Check(maxBlob.size() <= ProgressCodec::kDbBytes / 2, "가득 찬 기록 ≤ VARBINARY(4000)의 절반");
        ProgressCodec::Data back;
        AchieveBook mb;
        Check(ProgressCodec::Decode(maxBlob, back, mb) && Section(mb, &big) == Section(maxed, &big), "가득 찬 기록 왕복");
    }

    void TestRecheck()
    {
        auto c = SpecCatalog();
        AchieveBook b;
        AchieveBook::DoneList d;
        for (int i = 0; i < 50; ++i) b.OnKill(c, 1, false, false, false, d);
        Check(!b.IsDone(2), "50 — 업적 2 아직");
        // 표가 바뀌어 업적 2의 목표가 50으로(개발 서버 핫 리로드 · 다음 판)
        for (auto& r : c.rows) if (r.id == 2) r.target = 50;
        AchieveRule::BuildCatalog(c);
        d.clear();
        b.Recheck(c, d);
        Check(d.size() == 1 && d[0] == 2 && b.IsDone(2), "Recheck → 업적 2");
        d.clear();
        b.Recheck(c, d);
        Check(d.empty(), "Recheck 두 번 — 그대로");
    }
}

int main()
{
    TestCatalog();
    TestCounting();
    TestClaim();
    TestSection();
    TestBlob();
    TestRecheck();
    std::printf("AchieveRuleTests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
