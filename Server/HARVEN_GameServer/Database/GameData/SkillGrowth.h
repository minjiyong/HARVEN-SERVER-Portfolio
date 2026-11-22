#pragma once

#include "Database/GameData/CombatData.h"
#include "Database/GameData/SkillData.h"

#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// 기술 성장(Phase 2-D) — 기술 레벨 · 기술 점수 · 갈래. 서버 · 클라 공용.
//
//  · 기술 레벨 1~10(QWERASDF 여덟 칸의 기술만). 캐릭터 레벨이 오를 때마다 기술 점수 2점.
//    레벨을 올리는 값: 1→4 한 단계 1점 · 4→7 2점 · 7→10 3점(10까지 18점). 레벨마다 그 기술 피해 +5%(DealHit).
//    쓴 점수는 따로 저장하지 않는다 — 기술 레벨들에서 다시 계산한다(점수 ↔ 레벨이 어긋날 수 없다).
//  · 갈래(레벨 4 · 7에서 둘 중 하나): 로스트아크 트라이포드의 축소판. 갈래 하나 = 변형 한 줄(Mod + 값).
//    ApplyDef · ApplyRow가 기술 표(SkillDef · SkillRow)의 "복사본"을 바꾼다 — 서버 판정 · 클라 예측 · 연출이 같은 복사본을 쓴다.
//    표로 나타낼 수 없는 것(튕김 수 · 함성 시간 · 안개 틱 · 남는 장판 · 반격)은 서버가 FindMod로 읽어 처리한다.
//  · 레벨 · 갈래는 서버 권위(DB PlayerSkills), 어느 키에 둘지는 클라 설정.
//  · 주변 사람에게는 SKILL_NFY.flags의 비트 1~4로 켜진 갈래만 알린다(레벨은 연출과 상관없다).
// ─────────────────────────────────────────────────────────────────────────────
namespace SkillGrowth
{
    // 구조 상수(코드에 남는다): 상태 1바이트 포장(레벨 4비트) · 서버 · 클라 배열 크기 · 자라는 칸 수
    inline constexpr uint8_t  kMaxLevel = 10;
    inline constexpr uint16_t kIdSpan = 40;            // 기술 id 상한(서버 · 클라 배열 크기)
    inline constexpr int      kSlots = 8;              // 자라는 칸: Q W E R A S D F

    // 기획 값 — GameData/GrowthConst.json(시트 HARVEN_DATA의 GrowthConst 탭, 한 행). 로더: Data/Load/Bind_Growth.cpp
    [[nodiscard]] uint8_t TierLevel(int tier) noexcept;   // 갈래 Ⅰ · Ⅱ가 열리는 기술 레벨(이관 전 kTierLevel[tier]) — tier 0 · 1
    [[nodiscard]] uint32_t DamagePerLevel() noexcept;     // 레벨마다 피해 +%(이관 전 kDamagePerLevel)

    // 레벨 from → from+1 값
    [[nodiscard]] constexpr uint8_t StepCost(uint8_t from) noexcept { return from < 4 ? 1 : (from < 7 ? 2 : 3); }
    // 레벨 1 → level 까지 쓴 점수
    [[nodiscard]] constexpr uint16_t CostTo(uint8_t level) noexcept
    {
        uint16_t c = 0;
        for (uint8_t l = 1; l < level && l < kMaxLevel; ++l) c = static_cast<uint16_t>(c + StepCost(l));
        return c;
    }
    // 캐릭터 레벨의 기술 점수(레벨 1 = 0점)
    [[nodiscard]] constexpr uint16_t PointsAt(uint8_t charLevel) noexcept { return charLevel > 1 ? static_cast<uint16_t>(2u * (charLevel - 1u)) : 0; }
    [[nodiscard]] inline uint32_t DamagePercent(uint8_t level) noexcept { return 100u + DamagePerLevel() * (level > 1 ? level - 1u : 0u); }

    // 기술 하나의 상태 — 한 바이트: 레벨-1(4비트) · 갈래 Ⅰ(2비트) · 갈래 Ⅱ(2비트). 0 = 레벨 1 · 갈래 없음
    struct State
    {
        uint8_t level = 1;
        uint8_t pick[2] = { 0, 0 };   // 0 = 고르지 않음, 1 · 2
    };
    [[nodiscard]] constexpr State Unpack(uint8_t b) noexcept
    {
        State s;
        s.level = static_cast<uint8_t>((b & 0x0F) + 1);
        if (s.level > kMaxLevel) s.level = kMaxLevel;
        s.pick[0] = static_cast<uint8_t>((b >> 4) & 3);
        s.pick[1] = static_cast<uint8_t>((b >> 6) & 3);
        if (s.pick[0] > 2) s.pick[0] = 0;
        if (s.pick[1] > 2) s.pick[1] = 0;
        return s;
    }
    [[nodiscard]] constexpr uint8_t Pack(const State& s) noexcept
    {
        const uint8_t lv = s.level < 1 ? 1 : (s.level > kMaxLevel ? kMaxLevel : s.level);
        return static_cast<uint8_t>(((lv - 1) & 0x0F) | ((s.pick[0] & 3) << 4) | ((s.pick[1] & 3) << 6));
    }
    // 켜진 갈래(레벨이 모자라면 골라 둔 것도 꺼진다)
    [[nodiscard]] inline uint8_t Active(const State& s, int tier) noexcept { return s.level >= TierLevel(tier) ? s.pick[tier] : 0; }
    // SKILL_NFY.flags 비트 1~2 = 갈래 Ⅰ, 3~4 = 갈래 Ⅱ(SkillNfyFlag)
    [[nodiscard]] inline uint8_t NfyBits(const State& s) noexcept { return static_cast<uint8_t>((Active(s, 0) << 1) | (Active(s, 1) << 3)); }
    [[nodiscard]] constexpr State FromNfy(uint8_t flags) noexcept
    {
        State s;
        s.level = kMaxLevel;
        s.pick[0] = static_cast<uint8_t>((flags >> 1) & 3);
        s.pick[1] = static_cast<uint8_t>((flags >> 3) & 3);
        if (s.pick[0] > 2) s.pick[0] = 0;
        if (s.pick[1] > 2) s.pick[1] = 0;
        return s;
    }

    // ── 변형 ────────────────────────────────────────────────────────────────
    enum class Mod : uint8_t
    {
        None,
        Wider,    // 범위 +value칸(원 · 고리 · 부채꼴 반경, 직선은 길이 +value · 폭 +2·value) — 예전 판정은 SkillRow.radius
        Echo,     // 주 타격이 value×10ms 뒤 한 번 더(피해 · 무력화 50%) — 예전 판정 기술은 서버가 Execute를 한 번 더
        Heavy,    // 피해 +value% · 무력화 +50% · 주 타격이 강타 · 재사용 대기 +15%
        Swift,    // 재사용 대기 −value% · 동작(준비 · 타격 시각 · 후딜) −25% · 모으기 단계 · 홀딩 틱 −20%
        Twin,     // 직선: ±value° 두 줄기 더(50%) · 부채꼴: value°만큼 넓게
        Pull,     // 맞은 적을 모양 가운데(시전자 기준 모양이면 시전자)로 끌어당긴다
        Element,  // 모든 타격이 상태이상 status를 건다(원래 상태이상 대신) — 연출 색 color
        Lasting,  // 주 타격 자리에 value초 동안 장판(0.5초마다 20% · status)
        Longer,   // 지속 +value%(홀딩 · 켜고 끄기 최대 · 상태이상 · 함성 · 안개 · 표식)
        Extra,    // 더 많이: 튕김 · 전도 +value · 상태이상 중첩 +value · 따라 하기 +value
        Steady,   // 동작 내내 경직 면역
        Count
    };

    struct Variant
    {
        Mod         mod = Mod::None;
        uint8_t     value = 0;
        StatusId    status = StatusId::None;   // Element · Lasting
        uint32_t    color = 0;                 // 연출 색(0 = 기술 색)
        const char* name = "";
        const char* desc = "";
    };
    struct Tree
    {
        uint16_t skillId = 0;
        Variant  v[2][2] = {};                 // [갈래 Ⅰ · Ⅱ][첫째 · 둘째]
    };

    // 갈래 표 — 기술마다 갈래 넷(이름 · 설명은 WORLD.md 용어집 「갈래」). 순서 = 레벨 4 [첫째, 둘째] · 레벨 7 [첫째, 둘째].
    // 값은 GameData/GrowthTree.json(skill_id → 갈래 id 넷) + GrowthVariant.json(갈래 값 — 공용 프리셋은 한 행).
    // 로더가 id로 값을 복사해 Tree를 이관 전과 같은 모양(v[2][2])으로 다시 만든다 — ApplyDef · FindMod가 보는 값이 같다.
    // 규칙(예전 static_assert(Detail::Validate()) → 부팅 검증 R011~R019): skill_id < kIdSpan · 기술 표에 있다 · 칸이 QWERASDF ·
    // 갈래 mod ≠ None · 이름 · 설명 있음 · Element면 상태이상 · 같은 단의 두 갈래는 다르다 · 모든 조합에서 타격 ≤ 8 · skill_id 중복 없음 ·
    // 세 직업 × QWERASDF 기본 기술이 모두 갈래를 가진다.
    [[nodiscard]] int TreeCount() noexcept;                 // 이관 전 kTreeCount
    [[nodiscard]] const Tree* FindTree(uint16_t id) noexcept; // 없으면 nullptr. O(1)
    [[nodiscard]] inline bool Growable(uint16_t id) noexcept { return FindTree(id) != nullptr; }
    // 켜진 갈래(없으면 nullptr)
    [[nodiscard]] inline const Variant* ActiveVariant(uint16_t id, const State& s, int tier) noexcept
    {
        const Tree* t = FindTree(id);
        const uint8_t p = Active(s, tier);
        return (t && p >= 1 && p <= 2) ? &t->v[tier][p - 1] : nullptr;
    }
    // 켜진 갈래 중 이 변형(없으면 nullptr)
    [[nodiscard]] inline const Variant* FindMod(uint16_t id, const State& s, Mod m) noexcept
    {
        for (int tier = 0; tier < 2; ++tier)
            if (const Variant* v = ActiveVariant(id, s, tier); v && v->mod == m) return v;
        return nullptr;
    }
    [[nodiscard]] constexpr uint32_t Scale(uint32_t v, uint32_t pct) noexcept { return v * pct / 100u; }
    [[nodiscard]] constexpr uint16_t Scale16(uint32_t v, uint32_t pct) noexcept { const uint32_t r = v * pct / 100u; return static_cast<uint16_t>(r > 65535u ? 65535u : r); }
    [[nodiscard]] constexpr uint8_t Clamp8(uint32_t v) noexcept { return static_cast<uint8_t>(v > 255u ? 255u : v); }

    // 주 타격(메아리 · 강타 · 장판이 따르는 것): 틱 · 완벽 구간 전용이 아닌 마지막 타격, 없으면 마지막
    [[nodiscard]] constexpr int MainHit(const SkillDef& d) noexcept
    {
        for (int i = d.hitCount - 1; i >= 0; --i)
            if ((d.hits[i].flags & (HF_Tick | HF_Perfect)) == 0 && d.hits[i].tag == HT_None) return i;
        return d.hitCount - 1;
    }

    // 상태이상을 새로 걸 때 기본 시간(ms)
    [[nodiscard]] constexpr uint16_t DefaultStatusMs(StatusId st) noexcept
    {
        switch (st)
        {
        case StatusId::Stun: return 600;
        case StatusId::Freeze: return 1500;
        case StatusId::Burn: return 3000;
        case StatusId::Poison: return 5000;
        case StatusId::Slow: return 2500;
        case StatusId::Marked: return 5000;
        default: return 2000;
        }
    }

    // 기술 표(SkillDef) 복사본에 켜진 갈래를 입힌다 — 서버 판정 · 클라 예측 · 연출 공용
    [[nodiscard]] inline SkillDef ApplyDef(const SkillDef& base, const SkillRow& row, const State& s) noexcept
    {
        SkillDef d = base;
        for (int tier = 0; tier < 2; ++tier)
        {
            const Variant* vp = ActiveVariant(base.id, s, tier);
            if (!vp) continue;
            const Variant& v = *vp;
            const uint32_t baseCd = d.cooldownMs > 0 ? d.cooldownMs : row.cooldownMs;
            switch (v.mod)
            {
            case Mod::Wider:
                for (int i = 0; i < d.hitCount; ++i)
                {
                    SkillHit& h = d.hits[i];
                    if (h.shape == HitShape::Target) continue;
                    h.a = Clamp8(h.a + v.value);
                    if (h.shape == HitShape::Line) h.b = Clamp8(h.b + 2u * v.value);
                }
                break;
            case Mod::Echo:
                // 예전 판정 기술(Legacy)은 서버가 Execute를 한 번 더 부른다(표의 타격은 시각 표시용 하나뿐)
                if (d.special != SkillSpecial::Legacy && d.hitCount > 0 && d.hitCount < 8)
                {
                    const int m = MainHit(d);
                    SkillHit e = d.hits[m];
                    e.atMs = static_cast<uint16_t>(e.atMs + v.value * 10u);
                    e.coeff = static_cast<uint16_t>(e.coeff / 2u);
                    e.stagger = static_cast<uint8_t>(e.stagger / 2u);
                    e.flags = static_cast<uint8_t>(e.flags & ~(HF_Counter | HF_Final));
                    e.tag = HT_Echo;
                    e.src = static_cast<uint8_t>(m);
                    d.hits[d.hitCount++] = e;
                }
                break;
            case Mod::Heavy:
                for (int i = 0; i < d.hitCount; ++i)
                {
                    d.hits[i].coeff = Scale16(d.hits[i].coeff, 100u + v.value);
                    d.hits[i].stagger = Clamp8(d.hits[i].stagger * 3u / 2u);
                }
                if (d.hitCount > 0) d.hits[MainHit(d)].flags = static_cast<uint8_t>(d.hits[MainHit(d)].flags | HF_Heavy);
                d.cooldownMs = Scale(baseCd, 115);
                break;
            case Mod::Swift:
                d.cooldownMs = Scale(baseCd, 100u - v.value);
                d.lockMs = Scale16(d.lockMs, 75);
                d.castMs = Scale16(d.castMs, 75);
                for (int i = 0; i < d.hitCount; ++i) d.hits[i].atMs = Scale16(d.hits[i].atMs, 75);
                if (d.kind == SkillKind::Charge) { d.stageMs = Scale16(d.stageMs, 80); }
                if (d.kind == SkillKind::Hold) { d.tickMs = Scale16(d.tickMs, 80); }
                break;
            case Mod::Twin:
            {
                const int n = d.hitCount;
                for (int i = 0; i < n; ++i)
                {
                    SkillHit& h = d.hits[i];
                    if (h.flags & HF_Tick) continue;
                    if (h.shape == HitShape::Cone)
                        h.b = Clamp8(h.b + v.value > 90u ? 90u : h.b + v.value);
                    else if (h.shape == HitShape::Line && d.hitCount + 2 <= 8)
                    {
                        for (int k = 0; k < 2; ++k)
                        {
                            SkillHit t = h;
                            t.turn = static_cast<int8_t>(k == 0 ? -static_cast<int>(v.value) : static_cast<int>(v.value));
                            t.coeff = static_cast<uint16_t>(t.coeff / 2u);
                            t.stagger = static_cast<uint8_t>(t.stagger / 2u);
                            t.flags = static_cast<uint8_t>(t.flags & ~(HF_Final | HF_Counter));
                            t.tag = HT_Twin;
                            t.src = static_cast<uint8_t>(i);
                            d.hits[d.hitCount++] = t;
                        }
                    }
                }
                break;
            }
            case Mod::Pull:
                for (int i = 0; i < d.hitCount; ++i) d.hits[i].react = HitReact::Pull;
                break;
            case Mod::Element:
                for (int i = 0; i < d.hitCount; ++i) d.hits[i].flags = static_cast<uint8_t>(d.hits[i].flags | HF_Status);
                break;
            case Mod::Longer:
                if (d.kind == SkillKind::Hold || d.kind == SkillKind::Toggle)
                {
                    const uint16_t more = static_cast<uint16_t>(Scale16(d.holdMaxMs, 100u + v.value) - d.holdMaxMs);
                    d.holdMaxMs = static_cast<uint16_t>(d.holdMaxMs + more);
                    if (d.superArmorMs > 0) d.superArmorMs = static_cast<uint16_t>(d.superArmorMs + more);
                }
                break;
            case Mod::Steady:
            {
                uint32_t last = 0;
                for (int i = 0; i < d.hitCount; ++i) if (d.hits[i].atMs > last) last = d.hits[i].atMs;
                const uint32_t armor = d.castMs + d.lockMs + last + 300u;
                if (d.superArmorMs < armor) d.superArmorMs = static_cast<uint16_t>(armor > 65535u ? 65535u : armor);
                break;
            }
            case Mod::Lasting:   // 서버 UserSkillHelper(주 타격 뒤 장판) · 클라 연출
            case Mod::Extra:     // ApplyRow(중첩 · 전도) + 서버 UserSkillHelper(튕김)
            case Mod::None:
            case Mod::Count:
                break;
            }
        }
        return d;
    }

    // 기술 행(SkillRow) 복사본 — 예전 판정(Execute) · DealHit(상태이상)이 읽는 값
    [[nodiscard]] inline SkillRow ApplyRow(const SkillRow& base, const State& s) noexcept
    {
        SkillRow r = base;
        for (int tier = 0; tier < 2; ++tier)
        {
            const Variant* vp = ActiveVariant(base.id, s, tier);
            if (!vp) continue;
            const Variant& v = *vp;
            switch (v.mod)
            {
            case Mod::Wider: r.radius = Clamp8(r.radius + v.value); break;
            case Mod::Heavy: r.coeff = Scale16(r.coeff, 100u + v.value); break;
            case Mod::Element:
                r.status = v.status;
                if (r.statusMs == 0 || base.status != v.status) r.statusMs = DefaultStatusMs(v.status);
                if (r.statusStacks == 0) r.statusStacks = 1;
                break;
            case Mod::Longer: r.statusMs = Scale16(r.statusMs, 100u + v.value); break;
            case Mod::Extra:
                if (r.status != StatusId::None) r.statusStacks = Clamp8(r.statusStacks + v.value);
                r.hits = Clamp8(r.hits + v.value);
                break;
            default: break;
            }
        }
        return r;
    }

    // 설명 한 줄(툴팁 · 기술 창) — 이름 · 설명은 표에 있다
    [[nodiscard]] constexpr const char* TierName(int tier) noexcept { return tier == 0 ? "갈래 Ⅰ" : "갈래 Ⅱ"; }

    // R020 — 코드에 남는 규칙(구조 상수 kMaxLevel과 공식 StepCost만 쓴다 — 데이터가 아니다)
    static_assert(CostTo(kMaxLevel) == 18, "SkillGrowth: 기술 레벨 1 → 10에 드는 점수는 18점(StepCost · kMaxLevel)");
    static_assert(sizeof(State) == 3, "State는 3바이트(레벨 · 갈래 둘)");
}
