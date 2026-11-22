#pragma once

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "Database/GameData/AchieveRules.h"

// ─────────────────────────────────────────────────────────────────────────────
// 개인 업적 — 사람 한 명의 진척(통계) · 달성 · 받음 · 들어가 본 지역. 순수 논리(서버 전역 · GD()를 보지 않는다 — 표 색인은 인자로).
// CUser가 하나 들고(소유 Zone 스레드만 만진다), 진행 기록(UserProgress 판 4)의 끝 절로 저장한다. 시험: AchieveRuleTests.
//
//  · 통계 = (kind, param) → u32. 표가 세는 키(Catalog::keys)만 만든다 — 표에 없는 키의 사건은 버린다.
//  · 누적(Add)은 u32에서 멈추고, 최댓값(Raise — 레벨 · 강화 단계)은 큰 값만 남는다. 지역 수는 지역 비트의 개수(따로 적지 않는다).
//  · 통계가 키의 업적 목표에 닿으면 그 업적을 달성으로(한 번만) — 새로 달성한 id를 돌려준다(점수 · 알림은 부르는 쪽).
//  · 점수 · 달성 수 · 받을 수는 표와 비트셋으로 그때 센다(저장하지 않는다 — 표의 points가 바뀌어도 맞다).
// 설계: Shared/Document/ACHIEVEMENT.md.
// ─────────────────────────────────────────────────────────────────────────────
class AchieveBook
{
public:
    using Catalog = AchieveRule::Catalog;
    using StatKey = AchieveRule::StatKey;
    using DoneList = std::vector<uint16_t>;   // 새로 달성한 업적 id(달성한 차례)

    struct Stat
    {
        StatKey  key{};
        uint32_t value = 0;
    };
    struct Summary
    {
        uint32_t points = 0;
        uint16_t done = 0;       // 달성(받음 포함) — 표에 있는 업적만
        uint16_t total = 0;
        uint16_t claimable = 0;  // 달성 · 아직 받지 않음
    };

    // ── 사건 → 통계(표가 세는 키만) ──
    void Add(const Catalog& cat, StatKey key, uint32_t delta, DoneList& done);     // 누적(u32에서 멈춤)
    void Raise(const Catalog& cat, StatKey key, uint32_t value, DoneList& done);   // 최댓값
    bool EnterRegion(const Catalog& cat, int region, DoneList& done);              // 처음 들어간 지역이면 true

    // 처치 한 번(퀘스트 처치와 같은 자격): 처치(0 · 종) · 정예 · 보스(0 · 종) · 파티 처치
    void OnKill(const Catalog& cat, uint16_t species, bool elite, bool boss, bool party, DoneList& done);
    // 장비 count개(등급 grade) — 그 등급 이하의 모든 eGear param에 더한다(param = 「그 등급 이상」)
    void OnGear(const Catalog& cat, uint8_t grade, uint32_t count, DoneList& done);
    // 퀘스트 완료(0 · 그 퀘스트)
    void OnQuest(const Catalog& cat, uint16_t questId, DoneList& done);

    // 불러온 뒤 · 표가 바뀐 뒤: 지금 통계로 닿은 업적을 모두 달성으로
    void Recheck(const Catalog& cat, DoneList& done);

    // ── 읽기 ──
    [[nodiscard]] uint32_t Value(StatKey key) const noexcept;
    [[nodiscard]] bool IsDone(uint16_t id) const noexcept { return id <= AchieveRule::kMaxId && m_Done.test(id); }
    [[nodiscard]] bool IsClaimed(uint16_t id) const noexcept { return id <= AchieveRule::kMaxId && m_Claimed.test(id); }
    [[nodiscard]] uint8_t State(uint16_t id) const noexcept { return IsClaimed(id) ? 2 : (IsDone(id) ? 1 : 0); }
    [[nodiscard]] uint32_t Progress(const AchieveRule::Row& row) const noexcept;   // 목표에서 자른 진척(달성이면 목표)
    [[nodiscard]] Summary Sum(const Catalog& cat) const noexcept;
    [[nodiscard]] uint16_t Regions() const noexcept { return m_Regions; }
    [[nodiscard]] const std::vector<Stat>& Stats() const noexcept { return m_Stats; }

    // 받기 — 달성했고 아직 받지 않았을 때만 true(보상 지급은 부르는 쪽이 먼저 — 다 들어간 뒤에 부른다)
    bool MarkClaimed(uint16_t id) noexcept;

    // ── 진행 기록(UserProgress 판 4의 끝 절) ──
    //   u8 통계 수 · {u8 kind, u16 param, u32 값} × 수 · u8 달성 바이트 수 · 달성 비트 · u8 받음 바이트 수 · 받음 비트 · u16 지역 비트
    //   (비트 i = 업적 id i, 바이트 수는 마지막으로 켜진 비트까지 — 0~128). 값 0인 통계 · cat이 세지 않는 키는 쓰지 않는다(cat = nullptr면 모두).
    //   모르는 kind의 통계(새 서버가 쓴 것)는 그 7바이트를 그대로 되쓴다(앞으로의 판과 섞여도 잃지 않는다). 통계는 모두 합쳐 kMaxStatKeys줄까지 — 아는 것 먼저.
    //   얼어 있으면(Frozen) 읽은 원본 바이트를 그대로 쓴다.
    void Write(std::vector<uint8_t>& out, const Catalog* cat) const;
    // 모르는 kind의 통계 줄은 건너뛰고(보관) 계속 읽는다. 정말 깨졌으면(잘림 · 비트 바이트 수 > 128) false — 그때 이 책은
    // **얼린다**: 메모리는 빈 채, 바꾸는 함수는 모두 아무것도 하지 않고, Write는 [at, size)의 원본 바이트를 그대로 되쓴다
    // (깨진 기록을 빈 기록으로 덮어 받음 비트를 잃고 → 소급 · 다시 받기가 되는 길을 막는다). at은 size로 간다.
    bool Read(const uint8_t* data, size_t size, size_t& at);
    // 원본 바이트를 그대로 들고 읽기 전용으로(뒤에 남은 바이트 · 이 서버보다 새 판 — ProgressCodec이 부른다)
    enum class FreezeReason : uint8_t { None, Truncated, BadBits, Trailing, NewerVersion };
    void Freeze(const uint8_t* data, size_t size, FreezeReason why, uint8_t version = 0);
    [[nodiscard]] bool Frozen() const noexcept { return m_Freeze != FreezeReason::None; }
    [[nodiscard]] FreezeReason FrozenWhy() const noexcept { return m_Freeze; }
    [[nodiscard]] uint8_t FrozenVersion() const noexcept { return m_FrozenVersion; }      // NewerVersion: 원본 판(되쓸 때 그대로)
    [[nodiscard]] size_t UnknownStats() const noexcept { return m_Unknown.size(); }
    // Write가 쓰는 가장 큰 바이트(표가 세는 키 kMaxStatKeys개 · 비트셋 둘 가득)
    static constexpr size_t kMaxSectionBytes = 1 + static_cast<size_t>(AchieveRule::kMaxStatKeys) * 7 + 1 + AchieveRule::kBitBytes + 1 + AchieveRule::kBitBytes + 2;
    static_assert(kMaxSectionBytes == 1157);

    void Clear() noexcept;

    // 소유 Zone이 쓰는 저장 표시 — 달성 · 받기는 바로 저장, 그 밖 진척은 더러움 표시 뒤 60초에(CAchieveHelper)
    bool dirty = false;

private:
    Stat* Find(StatKey key) noexcept;
    void Check(const Catalog& cat, int keyIndex, uint32_t value, DoneList& done);

    std::vector<Stat> m_Stats;                              // key 순
    std::vector<std::array<uint8_t, 7>> m_Unknown;          // 모르는 kind의 통계 줄(읽은 그대로 — Write가 되쓴다)
    FreezeReason m_Freeze = FreezeReason::None;
    uint8_t m_FrozenVersion = 0;
    std::vector<uint8_t> m_Raw;                             // 얼었을 때의 원본 절 바이트
    std::bitset<AchieveRule::kMaxId + 1> m_Done;
    std::bitset<AchieveRule::kMaxId + 1> m_Claimed;         // ⊆ m_Done
    uint16_t m_Regions = 0;                                 // 들어가 본 지역(비트 = Region zone_id)
};
