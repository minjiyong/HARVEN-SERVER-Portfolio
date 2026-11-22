// 처치 보상 굴림 시험 — Content/Reward/LootRoll.h(순수 함수 — 시트 값 · 난수를 인자로).
//   cmake --build <폴더> --target LootRuleTests && <폴더>/Server/HARVEN_GameServer/tests/LootRuleTests
// 1) 필드 장비 등급: bp 경계(roll이 일반 · 고급 · 희귀 · 없음 칸의 끝/처음) · 아이템 찾기 배율 · 합이 만분율을 넘을 때 · 무작위 빈도
// 2) 희귀 천장: N-1개 동안 희귀가 없으면 N번째는 희귀 · 희귀가 없는 단계(rare_bp 0)에서는 올리지 않고 세기만 · 상한 · 없음은 세지 않음
// 3) 직업 무기 바꾸기: 남의 직업 무기만 · 확률 0/100 · 자기 무기 목록 안에서만 · 빈도
// 4) 피해 가중 추첨: 비례 빈도 · 피해 0은 1 · 빈 목록
// 5) 레벨 차 절반: 경계(gap-1 · gap) · gap 0 끔
// 6) 자격자: 피해 몫 경계(%) · 공유 범위 · 파티원 끼기(보스) · 계정당 한 번 · 때리지 않은 남은 안 된다
// 7) 정예 장비 칸 · 영웅 천장 / 8) 보스 굴림: 못 쓰는 무기 행 빼기 · 0 = 없음 · 영웅 천장(영웅 미만 장비 → 영웅 이상 행) · 카운터
// 9) 정예 후보 · 확률 · 배율 자르기
#include <cstdio>
#include <map>
#include <random>
#include <vector>

#include "Content/Reward/LootRoll.h"

ObjID ObjID::npos;

namespace
{
    int g_failures = 0;
    int g_checks = 0;
    void Check(bool ok, const char* what)
    {
        ++g_checks;
        if (!ok)
        {
            ++g_failures;
            if (g_failures < 40)
                std::printf("FAIL: %s\n", what);
        }
    }

    // 정해 둔 값을 차례로 돌려주는 난수(경계 시험)
    struct Seq
    {
        std::vector<uint32_t> v;
        size_t i = 0;
        uint32_t operator()() { return i < v.size() ? v[i++] : 0u; }
    };
}

int main()
{
    using namespace LootRoll;
    std::mt19937 mt(20261012);
    auto rng = [&] { return static_cast<uint32_t>(mt()); };

    // ── 1) 필드 장비 등급 ──
    {
        // T2: 일반 45 · 고급 30 · 희귀 12(만분율)
        Check(PickFieldGrade(45, 30, 12, 0, 0) == FieldPick::Common, "roll 0 → 일반");
        Check(PickFieldGrade(45, 30, 12, 0, 44) == FieldPick::Common, "roll 44 → 일반(끝)");
        Check(PickFieldGrade(45, 30, 12, 0, 45) == FieldPick::Uncommon, "roll 45 → 고급(처음)");
        Check(PickFieldGrade(45, 30, 12, 0, 74) == FieldPick::Uncommon, "roll 74 → 고급(끝)");
        Check(PickFieldGrade(45, 30, 12, 0, 75) == FieldPick::Rare, "roll 75 → 희귀(처음)");
        Check(PickFieldGrade(45, 30, 12, 0, 86) == FieldPick::Rare, "roll 86 → 희귀(끝)");
        Check(PickFieldGrade(45, 30, 12, 0, 87) == FieldPick::None, "roll 87 → 없음");
        Check(PickFieldGrade(45, 30, 12, 0, 9999) == FieldPick::None, "roll 9999 → 없음");
        // 아이템 찾기 +100% → bp 두 배(90 · 60 · 24)
        Check(ScaleBp(45, 100) == 90 && ScaleBp(45, -50) == 45 && ScaleBp(12, 25) == 15, "ScaleBp");
        Check(PickFieldGrade(45, 30, 12, 100, 89) == FieldPick::Common && PickFieldGrade(45, 30, 12, 100, 90) == FieldPick::Uncommon, "찾기 +100% 일반 끝");
        Check(PickFieldGrade(45, 30, 12, 100, 173) == FieldPick::Rare && PickFieldGrade(45, 30, 12, 100, 174) == FieldPick::None, "찾기 +100% 희귀 끝");
        // 합이 10000을 넘으면 뒤 등급이 준다
        Check(PickFieldGrade(10000, 0, 10000, 0, 9999) == FieldPick::Common, "일반 10000이면 희귀 없음");
        Check(PickFieldGrade(0, 0, 10000, 0, 9999) == FieldPick::Rare, "희귀만 10000");
        Check(PickFieldGrade(0, 0, 0, 500, 0) == FieldPick::None, "모두 0");
        Check(GradeOf(FieldPick::Common) == ItemGrade::eNone && GradeOf(FieldPick::Uncommon) == ItemGrade::eRare && GradeOf(FieldPick::Rare) == ItemGrade::eEpic,
            "GradeOf");
        // 빈도(무작위 100만 번): 기대 만분율 ± 여유
        std::map<FieldPick, int> n;
        constexpr int kN = 1000000;
        for (int i = 0; i < kN; ++i)
            ++n[PickFieldGrade(45, 30, 12, 0, Below(rng, kBpScale))];
        const auto near = [](int got, double want) { return got > want * 0.9 - 50 && got < want * 1.1 + 50; };
        Check(near(n[FieldPick::Common], kN * 0.0045) && near(n[FieldPick::Uncommon], kN * 0.0030) && near(n[FieldPick::Rare], kN * 0.0012), "빈도");
    }

    // ── 2) 희귀 천장 ──
    {
        uint16_t c = 0;
        int forcedAt = -1;
        for (int i = 1; i <= 25 && forcedAt < 0; ++i)
            if (ApplyRarePity(FieldPick::Common, true, 20, c) == FieldPick::Rare)
                forcedAt = i;
        Check(forcedAt == 20, "천장 20 → 20번째 장비가 희귀");
        Check(c == 0, "희귀 뒤 카운터 0");
        c = 5;
        Check(ApplyRarePity(FieldPick::Rare, true, 20, c) == FieldPick::Rare && c == 0, "희귀가 나오면 0");
        c = 7;
        Check(ApplyRarePity(FieldPick::None, true, 20, c) == FieldPick::None && c == 7, "장비 없음은 세지 않는다");
        c = 30;
        Check(ApplyRarePity(FieldPick::Uncommon, false, 20, c) == FieldPick::Uncommon && c == 31, "희귀 없는 단계는 올리지 않고 센다");
        Check(ApplyRarePity(FieldPick::Common, true, 20, c) == FieldPick::Rare && c == 0, "다음 단계 첫 장비는 희귀");
        c = kPityCap;
        (void)ApplyRarePity(FieldPick::Common, false, 20, c);
        Check(c == kPityCap, "카운터 상한");
        c = 0;
        Check(ApplyRarePity(FieldPick::Common, true, 1, c) == FieldPick::Rare, "천장 1 = 늘 희귀");
    }

    // ── 3) 직업 무기 바꾸기 ──
    {
        const ItemTableId own[] = { 21, 22, 23 };
        Check(ClassSwap(3, false, own, 100, rng) == 3, "내 직업 무기 · 방어구는 그대로");
        Check(ClassSwap(3, true, {}, 100, rng) == 3, "자기 무기가 없으면 그대로");
        Check(ClassSwap(3, true, own, 0, rng) == 3, "확률 0");
        Seq s{ { 0, 4 } };   // 0 % 100 = 0 < 60 → 바꾼다, 4 % 3 = 1 → 22
        Check(ClassSwap(3, true, own, 60, s) == 22 && s.i == 2, "확률 안 → 자기 무기(균등 고르기)");
        Seq t{ { 60 } };     // 60 ≥ 60 → 그대로, 난수 한 번만
        Check(ClassSwap(3, true, own, 60, t) == 3 && t.i == 1, "확률 밖 → 그대로");
        int swapped = 0;
        std::map<ItemTableId, int> got;
        for (int i = 0; i < 100000; ++i)
        {
            const ItemTableId id = ClassSwap(3, true, own, 60, rng);
            if (id != 3) ++swapped;
            ++got[id];
        }
        Check(swapped > 58000 && swapped < 62000, "바꾸기 빈도 60%");
        Check(got.size() == 4 && got[21] > 18000 && got[22] > 18000 && got[23] > 18000, "자기 무기 안에서 균등");
    }

    // ── 4) 피해 가중 추첨 ──
    {
        const uint32_t dmg[] = { 100, 300, 600 };
        std::map<size_t, int> n;
        for (int i = 0; i < 200000; ++i)
            ++n[PickByDamage(dmg, rng)];
        Check(n.size() == 3 && n[0] > 18000 && n[0] < 22000 && n[1] > 58000 && n[1] < 62000 && n[2] > 118000 && n[2] < 122000, "피해 비례 빈도");
        const uint32_t zero[] = { 0, 0 };
        std::map<size_t, int> z;
        for (int i = 0; i < 20000; ++i)
            ++z[PickByDamage(zero, rng)];
        Check(z[0] > 9000 && z[1] > 9000, "피해 0은 1로 본다");
        Check(PickByDamage(std::span<const uint32_t>(), rng) == kNone, "빈 목록");
        const uint32_t one[] = { 5 };
        Check(PickByDamage(one, rng) == 0, "한 명");
    }

    // ── 5) 레벨 차 절반 ──
    {
        Check(GapPermille(300, 40, 31, 10) == 300, "레벨 차 9 → 그대로");
        Check(GapPermille(300, 40, 30, 10) == 150, "레벨 차 10 → 절반");
        Check(GapPermille(301, 50, 1, 10) == 150, "절반(내림)");
        Check(GapPermille(300, 10, 40, 10) == 300, "몬스터가 높다 → 그대로");
        Check(GapPermille(300, 60, 1, 0) == 300, "gap 0 = 끔");
    }

    // ── 6) 자격자 ──
    {
        // 피해 몫 10%: 합 1000 → 100 이상
        std::vector<Claimant> c = {
            { 1, 600, 0, true, true },   // 0: 자격
            { 2, 100, 0, true, true },   // 1: 딱 10% — 자격
            { 3, 99, 0, true, true },    // 2: 9.9% — 아님
            { 4, 201, 0, false, true },  // 3: 멀리(공유 밖) — 정예는 아님 · 보스는 자격
        };
        std::vector<size_t> out(8);
        size_t m = PickClaimants(c, 1000, 10, true, false, out);
        Check(m == 2 && out[0] == 0 && out[1] == 1, "정예: 10% 경계 · 공유 범위");
        m = PickClaimants(c, 1000, 10, false, true, out);
        Check(m == 3 && out[2] == 3, "보스: 공유 범위와 상관없이 피해 몫");
        // 보스 파티원: 3% 자격자(파티 7)의 파티원 중 공유 범위 안 · 때리지 않음
        std::vector<Claimant> b = {
            { 10, 970, 0, true, true },   // 0: 혼자 97%
            { 11, 30, 7, true, true },    // 1: 3% — 자격(파티 7)
            { 12, 0, 7, true, false },    // 2: 파티원 · 공유 범위 → 자격
            { 13, 0, 7, false, false },   // 3: 파티원 · 멀리 → 아님
            { 14, 0, 9, true, false },    // 4: 다른 파티 → 아님
            { 15, 0, 0, true, false },    // 5: 파티 없는 구경꾼 → 아님
            { 11, 0, 7, true, false },    // 6: 같은 계정 둘째 칸 → 한 번만
        };
        m = PickClaimants(b, 1000, 3, false, true, out);
        Check(m == 3 && out[0] == 0 && out[1] == 1 && out[2] == 2, "보스: 3% · 그 파티원(공유 범위) · 계정당 한 번");
        std::vector<Claimant> low = { { 20, 980, 0, true, true }, { 21, 20, 5, true, true }, { 22, 0, 5, true, false } };
        m = PickClaimants(low, 1000, 3, false, true, out);
        Check(m == 1 && out[0] == 0, "2%의 파티원은 자격이 없다(자격자의 파티원만)");
        std::vector<Claimant> dup = { { 30, 500, 0, true, true }, { 30, 500, 0, true, true } };
        m = PickClaimants(dup, 1000, 10, true, false, out);
        Check(m == 1, "같은 playerId 두 칸 → 한 번");
        Check(HasShare(0, 0, 0) && HasShare(1, 0, 10) && !HasShare(2, 100, 3) && HasShare(3, 100, 3), "HasShare 경계");
    }

    // ── 7) 정예 장비 칸 · 영웅 천장 ──
    {
        const GradeCell t3[] = { { ItemGrade::eEpic, 3, 50 }, { ItemGrade::eEpic, 1, 30 }, { ItemGrade::eUnique, 1, 20 } };
        std::map<size_t, int> n;
        uint16_t c = 0;
        for (int i = 0; i < 100000; ++i)
        {
            c = 0;
            ++n[PickEliteCell(t3, 1000, c, rng)];
        }
        Check(n[0] > 48000 && n[0] < 52000 && n[1] > 28000 && n[1] < 32000 && n[2] > 18000 && n[2] < 22000, "칸 가중치 50 · 30 · 20");
        c = 18;
        Seq low{ { 0, 0 } };   // 가중치 roll 0 → 첫 칸(희귀-상)
        Check(PickEliteCell(t3, 20, c, low) == 0 && c == 19, "천장 전: 희귀 → +1");
        Seq again{ { 0, 0 } };
        Check(PickEliteCell(t3, 20, c, again) == 2 && c == 0, "천장(20번째): 영웅 칸만 → 0");
        const GradeCell t2[] = { { ItemGrade::eRare, 3, 60 }, { ItemGrade::eEpic, 1, 40 } };
        c = 50;
        const size_t k = PickEliteCell(t2, 20, c, rng);
        Check(k != kNone && c == 51, "영웅 칸이 없는 단계 — 올리지 않고 센다");
        const GradeCell none[] = { { ItemGrade::eEpic, 1, 0 } };
        c = 3;
        Check(PickEliteCell(none, 20, c, rng) == kNone && c == 3, "가중치 0 — 없음 · 카운터 그대로");
    }

    // ── 8) 보스 굴림 ──
    {
        // 무기(못 씀) 30 · 갑옷(영웅) 30 · 없음 940
        const BossRow rows[] = {
            { 18, 30, ItemGrade::eLegendary, true, false },
            { 47, 30, ItemGrade::eUnique, true, true },
            { 0, 940, ItemGrade::eNone, false, true },
        };
        std::map<ItemTableId, int> n;
        uint16_t c = 0;
        for (int i = 0; i < 200000; ++i)
            ++n[RollBoss(rows, 1000, c, rng)];
        Check(n.count(18) == 0, "못 쓰는 무기 행은 나오지 않는다");
        Check(n[47] > 5300 && n[47] < 7000, "다시 합한 가중치(30 / 970 ≈ 3.1%)");
        Check(n[0] > 190000, "0 = 없음");
        // 영웅 천장: 고급 장비(영웅 미만)가 뽑혀도 카운터 + 1 ≥ 천장이면 영웅 이상 행으로
        const BossRow mix[] = {
            { 3, 100, ItemGrade::eRare, true, true },
            { 47, 1, ItemGrade::eUnique, true, true },
            { 18, 1, ItemGrade::eLegendary, true, false },
        };
        c = 5;
        Seq s{ { 0, 0 } };
        Check(RollBoss(mix, 20, c, s) == 3 && c == 6, "천장 전: 고급 → +1");
        c = 19;
        Seq f{ { 0, 0, 0, 0 } };
        Check(RollBoss(mix, 20, c, f) == 47 && c == 0, "천장: 영웅 이상(쓸 수 있는) 행으로 → 0");
        c = 9;
        Seq e{ { 0, 0 } };
        const BossRow nothing[] = { { 0, 10, ItemGrade::eNone, false, true } };
        Check(RollBoss(nothing, 20, c, e) == ITEM_TABLE_ID_NONE && c == 9, "없음 — 카운터 그대로");
        const BossRow unusable[] = { { 18, 10, ItemGrade::eLegendary, true, false } };
        Check(RollBoss(unusable, 20, c, rng) == ITEM_TABLE_ID_NONE, "쓸 수 있는 행이 없으면 없음");
        Check(RollBoss(std::span<const BossRow>(), 20, c, rng) == ITEM_TABLE_ID_NONE, "빈 표");
    }

    // ── 9) 정예 후보 · 확률 · 배율 ──
    {
        Check(EliteCandidate(false, false, 2, 2) && !EliteCandidate(false, false, 1, 2), "단계 ≥ elite_min_tier");
        Check(!EliteCandidate(true, false, 5, 2) && !EliteCandidate(false, true, 5, 2), "보스 · 허수아비 아님");
        int n = 0;
        for (int i = 0; i < 200000; ++i)
            n += RollElite(true, 20, rng) ? 1 : 0;
        Check(n > 3500 && n < 4500, "20‰");
        Seq never{ {} };
        Check(!RollElite(false, 1000, never) && never.i == 0 && !RollElite(true, 0, never) && never.i == 0, "후보 아님 · 0‰ — 난수를 쓰지 않는다");
        Check(RollElite(true, 1000, never) && never.i == 0, "1000‰ — 늘");
        Check(ScalePercent(60, 400, 1, 65535) == 240 && ScalePercent(30000, 400, 1, 65535) == 65535 && ScalePercent(0, 150, 1, 65535) == 1,
            "배율 · 65535 상한 · 1 하한");
    }

    std::printf("LootRuleTests: 검사 %d · 실패 %d\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
