// 결사 성장 규칙 시험 — Database/GameData/GuildGrowth.h(순수 함수) · Content/Guild/GuildProgress.cpp(저장 · 규칙 적용 GuildModel).
//   cmake --build <폴더> --target GuildRuleTests && <폴더>/Server/HARVEN_GameServer/tests/GuildRuleTests
// 1) 저장 왕복 — 무작위 진행(결사원 0~60 · 임무 참여 · 업적 · 상점 구매)을 Serialize → Deserialize하면 같다. 자르거나 바이트를 바꾼 덩어리는
//    false 또는 다른 값일 뿐 터지지 않는다(ASan 빌드에서 함께 돌린다).
// 2) PickMissions — 같은 결사 · 주면 같은 임무, 겹침 없음, min_level · weight 지킴, 뽑힌 수 = min(칸, 뽑을 수 있는 수)
// 3) 처치 규칙 — 여럿이 함께 쓰러뜨린 한 마리 = 임무 진행 1 · 참여자 모두 몫 · 달성 보상에 덧보상 · 출석 단계 · 하루 경험치 한도 ·
//    협력 처치(새 결사원) · 업적 · 칭호 · 레벨 · 주 넘김(이번 주 기여 · 구매 비움, 누적은 그대로)
// 4) 나갔다 다시 들어온 결사원 — 오늘 · 이번 주 한도 몫을 잇는다(Depart · Rejoin)
// 5) 권한 · 글 검사 · 주/날 번호
#include <algorithm>
#include <cstdio>
#include <random>
#include <vector>

#include "Content/Guild/GuildProgress.h"

ObjID ObjID::npos;

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

    bool Same(const GuildProgress& a, const GuildProgress& b)
    {
        auto memEq = [](const GuildMemberProgress& x, const GuildMemberProgress& y)
        {
            return x.playerId == y.playerId && x.joinedAt == y.joinedAt && x.contribWeek == y.contribWeek && x.contribTotal == y.contribTotal
                && x.activeWeek == y.activeWeek && x.activeToday == y.activeToday && x.lastActiveMin == y.lastActiveMin && x.expToday == y.expToday
                && x.attendedToday == y.attendedToday && x.bought == y.bought;
        };
        auto misEq = [](const GuildMissionProgress& x, const GuildMissionProgress& y)
        {
            return x.id == y.id && x.progress == y.progress && x.done == y.done && x.who == y.who;
        };
        if (a.members.size() != b.members.size() || a.missions.size() != b.missions.size()) return false;
        for (size_t i = 0; i < a.members.size(); ++i) if (!memEq(a.members[i], b.members[i])) return false;
        for (size_t i = 0; i < a.missions.size(); ++i) if (!misEq(a.missions[i], b.missions[i])) return false;
        return a.level == b.level && a.exp == b.exp && a.title == b.title && a.recruiting == b.recruiting && a.recruitMinLevel == b.recruitMinLevel
            && a.intro == b.intro && a.permsSet == b.permsSet && std::equal(a.perms, a.perms + 4, b.perms) && a.kills == b.kills
            && a.bossKills == b.bossKills && a.coopKills == b.coopKills && a.missionsDone == b.missionsDone && a.attendDays == b.attendDays
            && a.peakMembers == b.peakMembers && a.achievements == b.achievements && a.weekId == b.weekId && a.dayId == b.dayId
            && a.attendCount == b.attendCount && a.attendTier == b.attendTier && a.claimantId == b.claimantId && a.claimEndsAt == b.claimEndsAt;
    }

    GuildProgress RandomProgress(std::mt19937& rng)
    {
        std::uniform_int_distribution<uint32_t> u32;
        auto pick = [&](uint32_t lo, uint32_t hi) { return std::uniform_int_distribution<uint32_t>(lo, hi)(rng); };
        GuildProgress p;
        p.level = static_cast<uint8_t>(pick(1, 20));
        p.exp = u32(rng);
        p.title = static_cast<uint8_t>(pick(0, 31));
        p.recruiting = static_cast<uint8_t>(pick(0, 1));
        p.recruitMinLevel = static_cast<uint8_t>(pick(1, 50));
        p.intro = std::string(pick(0, 120), 'a');
        p.permsSet = static_cast<uint8_t>(pick(0, 1));
        for (auto& v : p.perms) v = static_cast<uint8_t>(pick(0, 127));
        p.kills = u32(rng); p.bossKills = u32(rng); p.coopKills = u32(rng); p.missionsDone = u32(rng); p.attendDays = u32(rng);
        p.peakMembers = static_cast<uint8_t>(pick(0, 60));
        for (uint32_t i = 0, n = pick(0, 64); i < n; ++i) p.achievements.emplace_back(static_cast<uint16_t>(i + 1), u32(rng));
        p.weekId = u32(rng); p.dayId = u32(rng);
        p.attendCount = static_cast<uint8_t>(pick(0, 60)); p.attendTier = static_cast<uint8_t>(pick(0, 5));
        p.claimantId = static_cast<int32_t>(pick(0, 1000)); p.claimEndsAt = u32(rng);
        const uint32_t nm = pick(0, 60);
        for (uint32_t i = 0; i < nm; ++i)
        {
            GuildMemberProgress m;
            m.playerId = static_cast<int32_t>(i + 1);
            m.joinedAt = u32(rng); m.contribWeek = u32(rng); m.contribTotal = u32(rng);
            m.activeWeek = static_cast<uint16_t>(pick(0, 65535)); m.activeToday = static_cast<uint16_t>(pick(0, 1440));
            m.lastActiveMin = u32(rng); m.expToday = u32(rng); m.attendedToday = static_cast<uint8_t>(pick(0, 1));
            for (uint32_t k = 0, nb = pick(0, 10); k < nb; ++k) m.bought.emplace_back(static_cast<uint16_t>(k + 1), static_cast<uint8_t>(pick(1, 255)));
            p.members.push_back(std::move(m));
        }
        for (uint32_t i = 0, n = pick(0, 3); i < n; ++i)
        {
            GuildMissionProgress mp;
            mp.id = static_cast<uint16_t>(i + 1); mp.progress = u32(rng); mp.done = static_cast<uint8_t>(pick(0, 1));
            for (uint32_t k = 0, nw = pick(0, nm); k < nw; ++k) mp.who.emplace_back(static_cast<int32_t>(k + 1), u32(rng));
            p.missions.push_back(std::move(mp));
        }
        return p;
    }

    void TestSerialize()
    {
        std::mt19937 rng(7);
        size_t maxBytes = 0;
        for (int seed = 0; seed < 3000; ++seed)
        {
            rng.seed(static_cast<unsigned>(seed));
            const GuildProgress p = RandomProgress(rng);
            const std::vector<uint8_t> bytes = GuildModel::Serialize(p);
            maxBytes = std::max(maxBytes, bytes.size());
            GuildProgress q;
            Check(GuildModel::Deserialize(bytes, q), "왕복 — 읽기 성공", seed);
            Check(Same(p, q), "왕복 — 같은 값", seed);
            // 자르기 · 바이트 바꾸기 — 터지지 않는다(false 또는 다른 값)
            for (int k = 0; k < 8; ++k)
            {
                std::vector<uint8_t> broken = bytes;
                if (k < 4 && !broken.empty()) broken.resize(std::uniform_int_distribution<size_t>(0, broken.size() - 1)(rng));
                else if (!broken.empty()) broken[std::uniform_int_distribution<size_t>(0, broken.size() - 1)(rng)] ^= static_cast<uint8_t>(1 + rng() % 255);
                GuildProgress r;
                (void)GuildModel::Deserialize(broken, r);
            }
            if (!bytes.empty())
            {
                std::vector<uint8_t> extra = bytes;
                extra.push_back(0);
                GuildProgress r;
                Check(!GuildModel::Deserialize(extra, r), "꼬리가 붙은 덩어리는 거절", seed);
            }
        }
        // 가장 큰 결사(60명 · 임무 셋에 모두 참여 · 업적 64 · 사람마다 구매 10줄)도 DB 칸(16000) 안
        Check(maxBytes <= GuildModel::kMaxBytes, "가장 큰 진행 덩어리 ≤ 16000바이트");
        std::printf("  serialize: 3000 rounds, max %zu bytes\n", maxBytes);
    }

    std::vector<GuildGrowth::MissionRow> Pool()
    {
        std::vector<GuildGrowth::MissionRow> v;
        for (uint16_t i = 1; i <= 14; ++i)
        {
            GuildGrowth::MissionRow r;
            r.id = i;
            r.name = "m";
            r.kind = static_cast<GuildMissionKind>(i % 6);
            r.target = 3;
            r.minLevel = static_cast<uint8_t>(1 + (i % 5));
            r.weight = static_cast<uint16_t>(i % 4 == 0 ? 0 : i);
            v.push_back(r);
        }
        return v;
    }

    void TestPickMissions()
    {
        const auto pool = Pool();
        for (uint32_t gid = 1; gid < 300; ++gid)
            for (uint32_t week = 0; week < 20; ++week)
                for (uint8_t level = 1; level <= 6; ++level)
                {
                    uint16_t a[3]{}, b[3]{};
                    const size_t na = GuildGrowth::PickMissions(pool, level, gid, week, 3, a);
                    const size_t nb = GuildGrowth::PickMissions(pool, level, gid, week, 3, b);
                    Check(na == nb && std::equal(a, a + na, b), "같은 결사 · 주 → 같은 임무", static_cast<int>(gid));
                    size_t eligible = 0;
                    for (const auto& r : pool) if (r.minLevel <= level && r.weight > 0) ++eligible;
                    Check(na == std::min<size_t>(3, eligible), "뽑힌 수 = min(칸, 뽑을 수 있는 수)", static_cast<int>(gid));
                    for (size_t i = 0; i < na; ++i)
                    {
                        const auto& r = pool[a[i] - 1];
                        Check(r.minLevel <= level && r.weight > 0, "min_level · weight 지킴", static_cast<int>(gid));
                        for (size_t j = i + 1; j < na; ++j) Check(a[i] != a[j], "겹침 없음", static_cast<int>(gid));
                    }
                }
    }

    // 시험 표(임무: Kill 3 · Gear 2 · Active 2명×1분 / 레벨 2 = 1500 / 출석 1 · 2명 / 업적 처치 1 · 레벨 2)
    struct World
    {
        std::vector<GuildGrowth::LevelRow> levels{ { 1, 0, 3, 0, 0 }, { 2, 1500, 5, 1, 1 }, { 3, 900000, 6, 2, 0 } };
        std::vector<GuildGrowth::MissionRow> missions;
        std::vector<GuildGrowth::AchRow> achs;
        std::vector<GuildGrowth::AttendRow> attend{ { 1, 1, 5, 10, 1 }, { 2, 2, 10, 20, 1 } };
        GuildModel::Tables t;
        World()
        {
            missions.push_back({ 1, "kill", GuildMissionKind::Kill, 3, 0, 1, 100, 30, 5, 10 });
            missions.push_back({ 2, "gear", GuildMissionKind::Gear, 2, 0, 1, 100, 20, 5, 10 });
            missions.push_back({ 3, "active", GuildMissionKind::Active, 2, 1, 1, 100, 20, 5, 10 });
            achs.push_back({ 1, "first", GuildAchKind::Kills, 1, 200, 0, 2 });
            achs.push_back({ 2, "lv2", GuildAchKind::Level, 2, 50, 0, 0 });
            t.c.weekPeriodSec = 604800; t.c.weekOffsetSec = 334800; t.c.dayPeriodSec = 86400; t.c.dayOffsetSec = 75600;
            t.c.killExpPct = 50; t.c.memberDailyExpCap = 600; t.c.killContrib = 1; t.c.bossContrib = 30; t.c.coopNewDays = 7; t.c.coopContribBonus = 2;
            t.c.attendActiveMin = 1; t.c.missionSlots = 3; t.c.missionBonusMembers = 2; t.c.missionBonusPct = 50;
            t.levels = levels; t.missions = missions; t.achs = achs; t.attend = attend;
        }
    };

    void TestKillRules()
    {
        World w;
        const int64_t now = 1'800'000'000;
        GuildProgress p;
        const int32_t ids[] = { 1, 2, 3 };
        GuildModel::SyncMembers(p, ids, static_cast<uint32_t>(now - 3600));   // 한 시간 전에 들어옴 = 새 결사원
        GuildModel::Roll(p, 9, now, w.t);
        Check(p.missions.size() == 3, "1레벨 — 임무 셋");
        int64_t t = now;
        uint32_t sealsIn = 0;
        std::vector<uint16_t> doneIds;
        bool leveled = false;
        for (int k = 0; k < 3; ++k)
        {
            GuildModel::Effects fx;
            GuildModel::KillIn kill;
            kill.players = { 1, 2 };
            kill.monsterExp = 1000;
            kill.monsterLevel = 10;
            GuildModel::OnKill(p, kill, t, w.t, fx);
            GuildModel::OnLoot(p, 1, 100, 1, false, true, 2, t, w.t, fx);   // 막타(1)가 장비 하나
            for (const auto& s : fx.seals) sealsIn += static_cast<uint32_t>(s.amount);
            doneIds.insert(doneIds.end(), fx.missionsDone.begin(), fx.missionsDone.end());
            leveled = leveled || fx.Leveled();
            if (k == 0)
            {
                Check(p.missions[0].progress == 1 || p.missions[1].progress == 1 || p.missions[2].progress == 1, "둘이 한 마리 = 진행 1");
                Check(std::find(fx.attendTiers.begin(), fx.attendTiers.end(), 2) != fx.attendTiers.end(), "둘 다 1분 활동 → 출석 2단계");
                Check(p.coopKills == 1, "새 결사원과 함께 = 협력 처치");
                Check(std::find(fx.achievementsDone.begin(), fx.achievementsDone.end(), 1) != fx.achievementsDone.end(), "처치 1 업적");
                Check(std::find(fx.titlesUnlocked.begin(), fx.titlesUnlocked.end(), 2) != fx.titlesUnlocked.end(), "업적 칭호 2");
            }
            t += 61;   // 다음 분
        }
        std::sort(doneIds.begin(), doneIds.end());
        Check(doneIds == std::vector<uint16_t>{ 1, 2, 3 }, "세 마리 → 임무 셋 달성");
        for (const auto& mp : p.missions)
        {
            const bool killMission = mp.id == 1;
            if (killMission)
            {
                Check(mp.progress == 3, "Kill 진행 = 마리 수(사람 수가 아니다)");
                Check(mp.who.size() == 2, "Kill — 둘 다 참여");
            }
        }
        // 덧보상: Kill 인장 30 + 50% = 45, Gear(혼자) 20 그대로, Active(둘) 20 + 50% = 30, 업적 200 + 50, 출석 5 + 10
        Check(sealsIn == 45 + 20 + 30 + 200 + 50 + 5 + 10, "금고 들어올 것 = 임무(덧보상) + 업적 + 출석");
        Check(leveled && p.level == 2, "레벨 2");
        Check((GuildModel::TitlesUnlocked(p, w.t) & 0b110) == 0b110, "칭호 1(레벨) · 2(업적)");
        // 하루 경험치 한도 600: 사람마다 처치 몫 250씩 — 셋째 마리에 한도
        const GuildMemberProgress* m1 = GuildModel::Member(p, 1);
        Check(m1 != nullptr && m1->expToday == 600, "하루 경험치 한도");
        Check(m1 != nullptr && m1->contribWeek == m1->contribTotal && m1->contribWeek > 0, "기여 — 이번 주 = 누적(첫 주)");
        // 주 넘김: 이번 주 기여 · 구매 비움, 누적 그대로, 임무 새로
        GuildModel::AddBought(*GuildModel::Member(p, 1), 5);
        const uint32_t total = m1->contribTotal;
        GuildModel::Roll(p, 9, t + 604800, w.t);
        m1 = GuildModel::Member(p, 1);
        Check(m1->contribWeek == 0 && m1->contribTotal == total && m1->bought.empty(), "주 넘김 — 이번 주만 비운다");
        Check(m1->expToday == 0 && p.attendCount == 0, "날 넘김 — 오늘 경험치 · 출석 비움");
        Check(p.missions.size() == 3 && std::all_of(p.missions.begin(), p.missions.end(), [](const auto& m) { return m.progress == 0 && !m.done; }),
            "새 주의 임무");
        // 오래된 결사원(들어온 지 30일)끼리는 협력이 아니다
        GuildProgress old;
        GuildModel::SyncMembers(old, ids, static_cast<uint32_t>(now - 30 * 86400));
        GuildModel::Roll(old, 9, now, w.t);
        GuildModel::Effects fx;
        GuildModel::KillIn kill;
        kill.players = { 1, 2 };
        kill.monsterExp = 1000;
        GuildModel::OnKill(old, kill, now, w.t, fx);
        Check(old.coopKills == 0, "오래된 결사원끼리는 협력 처치가 아니다");
        // 결사원이 아닌 사람은 무시
        GuildModel::Effects fx2;
        GuildModel::KillIn stranger;
        stranger.players = { 77 };
        stranger.monsterExp = 1000;
        const uint32_t expBefore = old.exp;
        GuildModel::OnKill(old, stranger, now + 60, w.t, fx2);
        Check(old.exp == expBefore && fx2.contrib.empty(), "결사원이 아니면 경험치 · 기여 없음");
    }

    // 나갔다 다시 들어온 결사원 — 오늘 · 이번 주 한도 몫을 잇는다(하루 경험치 한도 · 출석 · 주 상점 구매), 기여도는 잇지 않는다
    void TestRejoin()
    {
        World w;
        const int64_t now = 1'800'000'000;
        GuildProgress p;
        const int32_t ids[] = { 1, 2 };
        GuildModel::SyncMembers(p, ids, static_cast<uint32_t>(now - 30 * 86400));
        GuildModel::Roll(p, 9, now, w.t);
        int64_t t = now;
        for (int k = 0; k < 4; ++k, t += 61)   // 한 사람 몫 500 × 4 → 하루 한도 600에서 멈춘다 · 1분 활동 → 출석
        {
            GuildModel::Effects fx;
            GuildModel::KillIn kill;
            kill.players = { 1 };
            kill.monsterExp = 1000;
            GuildModel::OnKill(p, kill, t, w.t, fx);
        }
        GuildModel::AddBought(*GuildModel::Member(p, 1), 7);
        GuildModel::AddBought(*GuildModel::Member(p, 1), 7);
        const GuildMemberProgress before = *GuildModel::Member(p, 1);
        Check(before.expToday == 600 && before.attendedToday == 1 && GuildModel::BoughtThisWeek(before, 7) == 2, "나가기 전 — 한도 · 출석 · 구매");
        const uint8_t attendBefore = p.attendCount;

        // 같은 날 나갔다 다시: 한도 몫 그대로 → 사냥해도 결사 경험치가 늘지 않고 · 출석을 다시 세지 않는다
        GuildModel::Departed d = GuildModel::Depart(p, 1);
        GuildModel::RemoveMember(p, 1);
        Check(GuildModel::Member(p, 1) == nullptr, "나감");
        GuildMemberProgress& back = GuildModel::EnsureMember(p, 1, static_cast<uint32_t>(t));
        GuildModel::Rejoin(p, back, d);
        Check(back.expToday == 600 && back.attendedToday == 1 && GuildModel::BoughtThisWeek(back, 7) == 2, "같은 날 다시 — 한도 몫을 잇는다");
        Check(back.contribTotal == 0 && back.joinedAt == static_cast<uint32_t>(t), "기여도 · 가입일은 잇지 않는다");
        const uint32_t expBefore = p.exp;
        for (int k = 0; k < 3; ++k, t += 61)
        {
            GuildModel::Effects fx;
            GuildModel::KillIn kill;
            kill.players = { 1 };
            kill.monsterExp = 1000;
            GuildModel::OnKill(p, kill, t, w.t, fx);
        }
        Check(p.exp == expBefore, "다시 들어와도 하루 한도 — 결사 경험치 그대로");
        Check(p.attendCount == attendBefore, "다시 들어와도 출석을 다시 세지 않는다");

        // 다음 날(같은 주) 나갔다 다시: 오늘 몫은 새로, 주 상점 구매는 잇는다
        d = GuildModel::Depart(p, 1);
        GuildModel::RemoveMember(p, 1);
        const int64_t nextDay = t + 86400;
        GuildModel::Roll(p, 9, nextDay, w.t);
        GuildMemberProgress& back2 = GuildModel::EnsureMember(p, 1, static_cast<uint32_t>(nextDay));
        GuildModel::Rejoin(p, back2, d);
        const bool sameWeek = d.weekId == p.weekId;
        Check(back2.expToday == 0 && back2.attendedToday == 0, "다음 날 — 오늘 몫은 새로");
        Check(GuildModel::BoughtThisWeek(back2, 7) == (sameWeek ? 2 : 0), "같은 주면 상점 구매를 잇는다");
        // 다음 주: 아무것도 잇지 않는다
        d = GuildModel::Depart(p, 1);
        GuildModel::RemoveMember(p, 1);
        GuildModel::Roll(p, 9, nextDay + 604800, w.t);
        GuildMemberProgress& back3 = GuildModel::EnsureMember(p, 1, static_cast<uint32_t>(nextDay + 604800));
        GuildModel::Rejoin(p, back3, d);
        Check(back3.expToday == 0 && back3.bought.empty(), "다음 주 — 잇지 않는다");
        // 진행이 없던 사람 · 다른 사람의 몫은 잇지 않는다
        GuildModel::Departed none = GuildModel::Depart(p, 99);
        GuildMemberProgress& other = GuildModel::EnsureMember(p, 2, static_cast<uint32_t>(nextDay));
        const GuildMemberProgress otherBefore = other;
        GuildModel::Rejoin(p, other, none);
        GuildModel::Rejoin(p, other, before.playerId == 1 ? GuildModel::Departed{ p.dayId, p.weekId, before } : none);
        Check(other.expToday == otherBefore.expToday && other.bought == otherBefore.bought, "다른 사람의 몫은 잇지 않는다");
    }

    void TestPermsAndText()
    {
        GuildGrowth::ConstRow c;
        c.permOfficer = GuildGrowth::kLegacyOfficer;
        c.permElite = GuildGrowth::kLegacyElite;
        c.permMember = GuildGrowth::kLegacyMember;
        GuildProgress p;
        Check(GuildModel::PermsOf(p, c, 0) == GuildGrowth::kAllPerms, "결사장 = 전부");
        Check(GuildModel::PermsOf(p, c, 2) == GuildGrowth::kLegacyElite, "정하기 전 = 시트 기본값");
        p.permsSet = 1;
        p.perms[2] = static_cast<uint8_t>(GuildPerm::Kick);
        Check(GuildModel::PermsOf(p, c, 2) == static_cast<uint8_t>(GuildPerm::Kick), "결사가 정한 값");
        const uint8_t masks[4] = { 0, 0, static_cast<uint8_t>(GuildPerm::Rank), 0 };
        using GuildRule::Rank;
        Check(!GuildGrowth::CanSetRank(Rank::Elite, Rank::Elite, Rank::Member, masks), "같은 직위는 못 바꾼다");
        Check(GuildGrowth::CanSetRank(Rank::Elite, Rank::Member, Rank::Member, masks), "낮은 직위끼리는 된다");
        Check(!GuildGrowth::CanSetRank(Rank::Elite, Rank::Member, Rank::Officer, masks), "자기보다 높게는 못 올린다");
        Check(GuildGrowth::CheckText("이번 주 목표", 2, 10) == GuildGrowth::TextCheck::Ok, "글 — 보통");
        Check(GuildGrowth::CheckText("일이삼사오육칠팔구십일", 2, 10) == GuildGrowth::TextCheck::TooLong, "글 — 11자");
        Check(GuildGrowth::CheckText("씨8발", 2, 10) == GuildGrowth::TextCheck::Banned, "글 — 금칙어(숫자 끼움)");
        Check(GuildGrowth::CheckText("", 0, 40) == GuildGrowth::TextCheck::Ok, "가입 안내는 비워도 된다");
        Check(GuildGrowth::PeriodId(334800 + 604800 * 5 + 3, 604800, 334800) == 5, "주 번호");
        Check(GuildGrowth::PeriodLeft(334800 + 604800 * 5 + 3, 604800, 334800) == 604797, "주가 끝날 때까지");
        Check(GuildGrowth::LeaderAbsent(false, 1000, 1000 + 1209600, 1209600) && !GuildGrowth::LeaderAbsent(true, 1000, 99999999, 10)
            && !GuildGrowth::LeaderAbsent(false, 0, 99999999, 10), "결사장 부재 판정(접속 중 · 기록 없음은 아니다)");
    }
}

int main()
{
    TestSerialize();
    TestPickMissions();
    TestKillRules();
    TestRejoin();
    TestPermsAndText();
    std::printf("[guild_rule_tests] checks=%d failures=%d\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
