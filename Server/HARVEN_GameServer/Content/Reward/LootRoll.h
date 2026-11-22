#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "Database/GameData/Item/ItemTableRow.h"   // ItemGrade · ItemTableId

// ─────────────────────────────────────────────────────────────────────────────
// 처치 보상 굴림 — 순수 함수(시트 값 · 난수를 인자로 받는다, GD() · 서버 객체 없음). 서버(Content/Reward/LootHelper.cpp)와
// 시험(tests/loot_rule_tests.cpp — LootRuleTests)이 같이 쓴다. 설계: Shared/Document/ECONOMY.md 「처치 보상」 · 「정예」 · 「보스 개인 굴림」.
//
//  · 필드 장비: TierLoot bp × (100 + 아이템 찾기)/100으로 일반 → 고급 → 희귀를 한 번 굴린다(PickFieldGrade) → 희귀 천장(ApplyRarePity).
//  · 직업 무기 바꾸기: 남의 직업 무기가 뽑히면 class_swap_percent로 같은 (등급, 단, 경로)의 자기 직업 무기(ClassSwap).
//  · 정수 · 결정: 나눠 받는 사람 중 피해 비례 추첨 한 명(PickByDamage), 몬스터가 레벨 차 이상 낮으면 확률 절반(GapPermille).
//  · 정예 · 보스 자격(PickClaimants): 피해 몫 ≥ N%(정예는 공유 범위 안 · 살아 있음까지, 보스는 그런 사람의 파티원 중 공유 범위 안도) — 계정(playerId)당 한 번.
//  · 정예 장비 칸(PickEliteCell) · 보스 굴림(RollBoss): 영웅 천장(heroic_pity).
// rng() → uint32_t 난수(서버는 LRng). 난수는 필요할 때만 쓴다(확률 0 · 후보 없음이면 굴리지 않는다).
// ─────────────────────────────────────────────────────────────────────────────
namespace LootRoll
{
    inline constexpr uint32_t kBpScale   = 10000;   // 만분율
    inline constexpr uint32_t kPermille  = 1000;
    inline constexpr uint16_t kPityCap   = 1000;    // 천장 카운터 위(진행 기록 읽기 CUser::DeserializeProgress의 상한과 같다)
    inline constexpr size_t   kNone      = static_cast<size_t>(-1);

    template <class Rng>
    [[nodiscard]] uint32_t Below(Rng&& rng, uint32_t n)
    {
        return n == 0 ? 0u : static_cast<uint32_t>(rng()) % n;
    }

    // v × percent / 100 — [lo, hi]로 자른다(정예 최대 HP · 공격력: 1 ~ 65535, CStat이 16비트)
    [[nodiscard]] constexpr uint32_t ScalePercent(uint32_t v, uint32_t percent, uint32_t lo, uint32_t hi) noexcept
    {
        const uint64_t x = static_cast<uint64_t>(v) * percent / 100u;
        return x < lo ? lo : (x > hi ? hi : static_cast<uint32_t>(x));
    }

    // ── 필드 장비 등급 ───────────────────────────────────────────────────────
    enum class FieldPick : uint8_t { None, Common, Uncommon, Rare };

    [[nodiscard]] constexpr ItemGrade GradeOf(FieldPick p) noexcept
    {
        switch (p)
        {
        case FieldPick::Uncommon: return ItemGrade::eRare;    // 고급
        case FieldPick::Rare:     return ItemGrade::eEpic;    // 희귀
        default:                  return ItemGrade::eNone;    // 일반(None은 부르는 쪽이 거른다)
        }
    }

    // bp × (100 + 아이템 찾기)/100(음수 찾기는 0으로 본다)
    [[nodiscard]] constexpr uint32_t ScaleBp(uint32_t bp, int itemFind) noexcept
    {
        const uint32_t f = itemFind > 0 ? static_cast<uint32_t>(itemFind) : 0u;
        return static_cast<uint32_t>(static_cast<uint64_t>(bp) * (100u + f) / 100u);
    }

    // roll ∈ [0, 10000): 일반 → 고급 → 희귀 순서로 (찾기를 곱한) bp를 빼 간다 — 합이 10000을 넘으면 뒤 등급이 줄어든다.
    [[nodiscard]] constexpr FieldPick PickFieldGrade(uint32_t commonBp, uint32_t uncommonBp, uint32_t rareBp, int itemFind, uint32_t roll) noexcept
    {
        const uint32_t bp[3] = { ScaleBp(commonBp, itemFind), ScaleBp(uncommonBp, itemFind), ScaleBp(rareBp, itemFind) };
        for (int i = 0; i < 3; ++i)
        {
            if (roll < bp[i])
                return static_cast<FieldPick>(i + 1);
            roll -= bp[i];
        }
        return FieldPick::None;
    }

    // 희귀 천장 — 장비가 나왔을 때만(None은 그대로 · 카운터도 그대로). 카운터 + 1 ≥ 천장이고 이 단계가 희귀를 굴리면(rare_bp > 0) 희귀로 올린다.
    // 카운터: 희귀면 0, 아니면 +1(kPityCap에서 멈춘다 — 희귀가 없는 1단계에서도 센다: 2단계 첫 장비가 희귀).
    [[nodiscard]] constexpr FieldPick ApplyRarePity(FieldPick p, bool rareAllowed, uint16_t pityLimit, uint16_t& counter) noexcept
    {
        if (p == FieldPick::None)
            return p;
        if (p != FieldPick::Rare && rareAllowed && static_cast<uint32_t>(counter) + 1u >= pityLimit)
            p = FieldPick::Rare;
        counter = p == FieldPick::Rare ? 0 : static_cast<uint16_t>(counter < kPityCap ? counter + 1 : kPityCap);
        return p;
    }

    // ── 가중치 고르기 ────────────────────────────────────────────────────────
    // weight(i) — 뺄 칸은 0. roll ∈ [0, 합) → 칸 번호(합 0이면 kNone)
    template <class WeightFn>
    [[nodiscard]] constexpr uint64_t TotalWeight(size_t n, WeightFn&& weight)
    {
        uint64_t total = 0;
        for (size_t i = 0; i < n; ++i)
            total += weight(i);
        return total;
    }
    template <class WeightFn>
    [[nodiscard]] constexpr size_t PickByWeight(size_t n, WeightFn&& weight, uint64_t roll)
    {
        for (size_t i = 0; i < n; ++i)
        {
            const uint64_t w = weight(i);
            if (roll < w)
                return i;
            roll -= w;
        }
        return kNone;
    }
    template <class Rng, class WeightFn>
    [[nodiscard]] size_t RollByWeight(size_t n, WeightFn&& weight, Rng&& rng)
    {
        const uint64_t total = TotalWeight(n, weight);
        if (total == 0)
            return kNone;
        const uint64_t roll = (static_cast<uint64_t>(static_cast<uint32_t>(rng())) << 32 | static_cast<uint32_t>(rng())) % total;
        return PickByWeight(n, weight, roll);
    }

    // ── 직업 무기 바꾸기 ─────────────────────────────────────────────────────
    // picked가 남의 직업 무기(otherClassWeapon)이고 같은 (등급, 단, 경로)에 자기 직업 무기(own)가 있으면 swapPercent 확률로 그중 하나(균등).
    template <class Rng>
    [[nodiscard]] ItemTableId ClassSwap(ItemTableId picked, bool otherClassWeapon, std::span<const ItemTableId> own, uint8_t swapPercent, Rng&& rng)
    {
        if (!otherClassWeapon || own.empty() || swapPercent == 0)
            return picked;
        if (Below(rng, 100u) >= swapPercent)
            return picked;
        return own[Below(rng, static_cast<uint32_t>(own.size()))];
    }

    // ── 정수 · 결정 ──────────────────────────────────────────────────────────
    // 피해에 비례해 한 명(피해 0은 1로 본다 — RewardRule::SplitByWeight와 같다). 빈 목록이면 kNone.
    template <class Rng>
    [[nodiscard]] size_t PickByDamage(std::span<const uint32_t> damage, Rng&& rng)
    {
        return RollByWeight(damage.size(), [&](size_t i) -> uint64_t { return damage[i] > 0 ? damage[i] : 1u; }, rng);
    }

    // 몬스터가 받는 사람보다 gap 레벨 이상 낮으면 확률 절반(gap 0 = 끔)
    [[nodiscard]] constexpr uint32_t GapPermille(uint32_t permille, int receiverLevel, int monsterLevel, uint8_t gap) noexcept
    {
        return gap > 0 && receiverLevel - monsterLevel >= static_cast<int>(gap) ? permille / 2u : permille;
    }

    // ── 정예 · 보스 자격 ─────────────────────────────────────────────────────
    // 피해 몫 ≥ percent%(damage × 100 ≥ total × percent). total 0이면 늘 참이다(장부가 비면 막타 한 사람만 목록에 있다).
    [[nodiscard]] constexpr bool HasShare(uint64_t damage, uint64_t total, uint8_t percent) noexcept
    {
        return damage * 100u >= total * percent;
    }

    struct Claimant
    {
        int32_t  playerId = 0;          // 계정(User 행 하나 = 계정 하나)
        uint32_t damage = 0;            // 이번 생애의 피해(기록에 없는 사람 0)
        uint32_t partyId = 0;           // 0 = 파티 없음
        bool     sharing = false;       // 살아 있고 죽은 자리에서 exp_share_range 안
        bool     contributor = true;    // 피해 기록에 있는 사람(아니면 곁에 있던 사람 — 보스 파티원 후보)
    };
    // 자격자 칸 번호를 out에(앞에서부터) — 돌려주는 값 = 자격자 수(out 크기까지).
    //  · 기본: contributor · 피해 몫 ≥ percent(needSharing이면 sharing도)
    //  · partyJoins: 위 자격자와 같은 파티(≠ 0)이면서 sharing인 사람도(피해와 상관없이)
    //  · 같은 playerId는 한 번(앞 칸)
    [[nodiscard]] constexpr size_t PickClaimants(std::span<const Claimant> c, uint64_t total, uint8_t percent, bool needSharing, bool partyJoins,
        std::span<size_t> out) noexcept
    {
        auto direct = [&](size_t i) { return c[i].contributor && HasShare(c[i].damage, total, percent) && (!needSharing || c[i].sharing); };
        auto viaParty = [&](size_t i)
        {
            if (!partyJoins || c[i].partyId == 0 || !c[i].sharing)
                return false;
            for (size_t j = 0; j < c.size(); ++j)
                if (j != i && c[j].partyId == c[i].partyId && direct(j))
                    return true;
            return false;
        };
        size_t n = 0;
        for (size_t i = 0; i < c.size() && n < out.size(); ++i)
        {
            if (!direct(i) && !viaParty(i))
                continue;
            bool seen = false;
            for (size_t k = 0; k < n && !seen; ++k)
                seen = c[out[k]].playerId == c[i].playerId;
            if (!seen)
                out[n++] = i;
        }
        return n;
    }

    // ── 정예 장비(EliteGear 칸) ──────────────────────────────────────────────
    [[nodiscard]] constexpr bool IsHeroicOrAbove(ItemGrade g) noexcept { return static_cast<int>(g) >= static_cast<int>(ItemGrade::eUnique); }

    struct GradeCell
    {
        ItemGrade grade = ItemGrade::eNone;
        uint8_t   rank = 0;
        uint16_t  weight = 0;
    };
    // 가중치로 칸 하나. 영웅 천장: 카운터 + 1 ≥ 천장이고 영웅 이상 칸(가중치 > 0)이 있으면 그 칸들 중에서만.
    // 카운터: 고른 칸이 영웅 이상이면 0, 아니면 +1(kPityCap까지). 고를 칸이 없으면 kNone(카운터 그대로).
    template <class Rng>
    [[nodiscard]] size_t PickEliteCell(std::span<const GradeCell> cells, uint16_t pityLimit, uint16_t& counter, Rng&& rng)
    {
        const bool forced = static_cast<uint32_t>(counter) + 1u >= pityLimit
            && TotalWeight(cells.size(), [&](size_t i) -> uint64_t { return IsHeroicOrAbove(cells[i].grade) ? cells[i].weight : 0u; }) > 0;
        const size_t k = RollByWeight(cells.size(),
            [&](size_t i) -> uint64_t { return !forced || IsHeroicOrAbove(cells[i].grade) ? cells[i].weight : 0u; }, rng);
        if (k == kNone)
            return k;
        counter = IsHeroicOrAbove(cells[k].grade) ? 0 : static_cast<uint16_t>(counter < kPityCap ? counter + 1 : kPityCap);
        return k;
    }

    // ── 보스 개인 굴림(BossLoot) ─────────────────────────────────────────────
    struct BossRow
    {
        ItemTableId item = ITEM_TABLE_ID_NONE;   // 0 = 없음
        uint16_t    weight = 0;
        ItemGrade   grade = ItemGrade::eNone;    // 장비 등급(장비가 아니면 무시)
        bool        gear = false;                // 장비
        bool        usable = true;               // 이 사람이 쓸 수 있다(무기의 직업 제한) — 못 쓰면 뺀다
    };
    // 못 쓰는 행을 빼고 가중치를 다시 합해 하나 → 아이템(0 = 없음). 장비가 나오면 영웅 천장:
    // 영웅 미만 장비이고 카운터 + 1 ≥ 천장이면 영웅 이상 장비 행(쓸 수 있는 것) 중에서 다시 고른다. 카운터: 영웅 이상 장비 0 · 그 밖 장비 +1(없음이면 그대로).
    template <class Rng>
    [[nodiscard]] ItemTableId RollBoss(std::span<const BossRow> rows, uint16_t pityLimit, uint16_t& counter, Rng&& rng)
    {
        size_t k = RollByWeight(rows.size(), [&](size_t i) -> uint64_t { return rows[i].usable ? rows[i].weight : 0u; }, rng);
        if (k == kNone || rows[k].item == ITEM_TABLE_ID_NONE)
            return ITEM_TABLE_ID_NONE;
        if (!rows[k].gear)
            return rows[k].item;
        if (!IsHeroicOrAbove(rows[k].grade) && static_cast<uint32_t>(counter) + 1u >= pityLimit)
        {
            const size_t h = RollByWeight(rows.size(), [&](size_t i) -> uint64_t
            {
                return rows[i].usable && rows[i].gear && rows[i].item != ITEM_TABLE_ID_NONE && IsHeroicOrAbove(rows[i].grade) ? rows[i].weight : 0u;
            }, rng);
            if (h != kNone)
                k = h;
        }
        counter = IsHeroicOrAbove(rows[k].grade) ? 0 : static_cast<uint16_t>(counter < kPityCap ? counter + 1 : kPityCap);
        return rows[k].item;
    }

    // ── 정예 몬스터 ──────────────────────────────────────────────────────────
    // 보스 · 허수아비가 아니고 지역 단계 ≥ elite_min_tier인 몬스터만 정예 후보
    [[nodiscard]] constexpr bool EliteCandidate(bool boss, bool dummy, int regionTier, int minTier) noexcept
    {
        return !boss && !dummy && regionTier >= minTier;
    }
    template <class Rng>
    [[nodiscard]] bool RollElite(bool candidate, uint32_t permille, Rng&& rng)
    {
        if (!candidate || permille == 0)
            return false;
        return permille >= kPermille || Below(rng, kPermille) < permille;
    }
}
