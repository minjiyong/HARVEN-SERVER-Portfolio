#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// 개인 업적 — 분야 · 세는 것(AchKind) · 표 색인(Catalog). 값은 시트 achieve 묶음(Achievement → Bin/GameData/Achievement.json)에서 온다.
// 열거형 값 = 순서 — 진행 기록(UserProgress 판 4)과 클라 생성 표가 이 값을 쓴다. 끝에만 더한다(바꾸거나 지우지 않는다).
// 표를 읽는 파사드(Get · IsHuntMaterial)의 구현은 Database/GameData/Load/Bind_Achieve.cpp, 색인 만들기(BuildCatalog)는 이 헤더
// (순수 — 시험 AchieveRuleTests가 같은 함수를 쓴다). 사람마다의 진척 · 달성 · 받음은 Content/Achieve/AchieveBook.
// 설계: Shared/Document/ACHIEVEMENT.md · 규칙 R136~R140(DATA_RULES.md).
// ─────────────────────────────────────────────────────────────────────────────

// 분야(업적 창의 탭 — ACHIEVE_REQ List의 category 값)
enum class AchCategory : uint8_t
{
    eHunt,     // 사냥
    eBoss,     // 보스
    eGrowth,   // 성장
    eGear,     // 장비
    eCraft,    // 제작
    eWorld,    // 세계
    Count
};

// 무엇을 세는가(param 뜻 · 진척 값은 ACHIEVEMENT.md 1절)
enum class AchKind : uint8_t
{
    eKill,         // 몬스터 처치(피해를 준 모두 — 퀘스트 처치와 같은 자격). param = 종 id(0 = 아무 몬스터)
    eKillElite,    // 정예 처치
    eKillBoss,     // 보스 처치. param = 보스 종 id(0 = 아무 보스)
    ePartyKill,    // 파티에 든 채 처치
    eLevel,        // 레벨(최댓값)
    eDeath,        // 쓰러짐
    eAdena,        // 사냥 그로시 획득(AdenaReason::Pickup) 누적 합
    eGear,         // 장비 획득(드롭 · 정예 · 보스 · 제작 · 퀘스트). param = 등급(ItemGrade) — 그 등급 이상
    eEnchant,      // 강화 성공으로 도달한 단계(최고)
    eCraft,        // 제작 성공
    eDismantle,    // 분해 성공
    eEssence,      // 사냥 정수 · 결정 획득 개수(재료 61~65, 처치 · 정예 · 보스 보상)
    eQuest,        // 퀘스트 완료. param = 퀘스트 id(0 = 아무거나)
    eGuildJoin,    // 결사 가입 · 창설
    eAuctionSell,  // 경매 판매 성립
    eAuctionBuy,   // 경매 구매 성립
    eTrade,        // 교환 성립
    eZone,         // 서로 다른 지역(Region) 들어가 본 수
    Count
};

namespace AchieveRule
{
    // ── 구조 상수(진행 기록 · 패킷) ──
    inline constexpr int      kMaxId        = 1023;                 // 업적 id 1~1023 — 달성 · 받음 비트셋 칸(R136)
    inline constexpr int      kBitBytes     = (kMaxId + 1) / 8;     // 비트셋 하나의 바이트(128)
    inline constexpr int      kNameBytes    = 30;                   // 이름(한글 10자) — R136
    inline constexpr int      kDescBytes    = 90;                   // 설명(한글 30자) — R136
    inline constexpr int      kMaxStatKeys  = 128;                  // 표가 세는 (kind, param) 종류 수 상한 — 진행 기록 크기(R136)
    inline constexpr int      kRegionBits   = 16;                   // 들어가 본 지역 비트(u16) — WorldData::kRegionCount와 같다(Bind_Achieve static_assert)
    inline constexpr int      kPageEntries  = 30;                   // ACHIEVE_INF_PACKET::entries
    inline constexpr uint8_t  kAllCategories = 0xFF;                // List: 모든 분야
    inline constexpr uint32_t kListIntervalMs = 500;                // List 묻기 간격(사람마다)
    inline constexpr uint32_t kSaveDelayMs  = 60000;                // 달성 · 받기가 아닌 진척은 더러움 표시 뒤 60초에 저장
    inline constexpr int      kMaxGradeParam = 5;                   // eGear param 위(ItemGrade::eMythic) — R137
    inline constexpr uint32_t kMaxRewardAdena = 2'000'000'000u;     // reward_adena 위 = 지갑 상한(DB INT · CInventory::AddAdena) — R140
    static_assert(kMaxId < 0xFFFF && kBitBytes == 128);

    struct Row
    {
        uint16_t    id = 0;
        AchCategory category = AchCategory::eHunt;
        uint16_t    group = 0;
        uint8_t     step = 0;
        const char* name = "";
        const char* desc = "";
        AchKind     kind = AchKind::eKill;
        uint16_t    param = 0;
        uint32_t    target = 0;
        uint16_t    points = 0;
        uint16_t    rewardItem = 0;     // 0 = 없음
        uint16_t    rewardCount = 0;
        uint32_t    rewardAdena = 0;
    };

    // 세는 단위 — (kind, param). 같은 키의 업적은 같은 진척 값을 본다.
    struct StatKey
    {
        AchKind  kind = AchKind::eKill;
        uint16_t param = 0;
        [[nodiscard]] constexpr bool operator==(const StatKey&) const noexcept = default;
        [[nodiscard]] constexpr bool operator<(const StatKey& o) const noexcept
        {
            return kind != o.kind ? static_cast<uint8_t>(kind) < static_cast<uint8_t>(o.kind) : param < o.param;
        }
    };

    // param을 쓰는 kind(그 밖은 param 0 — R137)
    [[nodiscard]] constexpr bool UsesParam(AchKind k) noexcept
    {
        return k == AchKind::eKill || k == AchKind::eKillBoss || k == AchKind::eGear || k == AchKind::eQuest;
    }
    // 진척이 최댓값인 kind(레벨 · 강화 단계 · 지역 수) — 나머지는 누적
    [[nodiscard]] constexpr bool IsMaxKind(AchKind k) noexcept
    {
        return k == AchKind::eLevel || k == AchKind::eEnchant || k == AchKind::eZone;
    }
    [[nodiscard]] constexpr bool ValidCategory(uint8_t c) noexcept { return c < static_cast<uint8_t>(AchCategory::Count); }

    // ── 표 색인(로드 때 한 번 — 스냅샷이 들고 있다) ──
    struct Catalog
    {
        std::vector<Row> rows;                       // id 순(BuildCatalog 앞에 정렬해 넣는다)
        std::array<uint16_t, kMaxId + 1> at{};       // id → rows 위치 + 1(0 = 없음)
        std::vector<StatKey> keys;                   // 표가 세는 (kind, param) — 정렬 · 겹침 없음
        std::vector<uint16_t> keyRows;               // keys[k]의 업적(rows 위치) — target 오름차순, 같으면 id 순
        std::vector<uint32_t> keyStart;              // keys.size() + 1 — keyRows의 [start, end)
        uint32_t totalPoints = 0;

        [[nodiscard]] const Row* Find(uint16_t id) const noexcept
        {
            return id <= kMaxId && at[id] != 0 ? &rows[at[id] - 1u] : nullptr;
        }
        [[nodiscard]] int KeyIndex(StatKey k) const noexcept
        {
            const auto it = std::lower_bound(keys.begin(), keys.end(), k);
            return it != keys.end() && *it == k ? static_cast<int>(it - keys.begin()) : -1;
        }
        [[nodiscard]] bool Tracks(StatKey k) const noexcept { return KeyIndex(k) >= 0; }
        [[nodiscard]] std::span<const uint16_t> RowsOfKey(int k) const noexcept
        {
            if (k < 0 || static_cast<size_t>(k) + 1 >= keyStart.size())
                return {};
            return std::span<const uint16_t>(keyRows.data() + keyStart[static_cast<size_t>(k)], keyStart[static_cast<size_t>(k) + 1] - keyStart[static_cast<size_t>(k)]);
        }
    };

    // rows(id 순)로 나머지 색인을 만든다. 틀린 표(id 범위 밖 · 겹침)여도 멈추지 않는다 — 규칙 검사(R136~)가 따로 막는다.
    inline void BuildCatalog(Catalog& c)
    {
        c.at.fill(0);
        c.keys.clear();
        c.keyRows.clear();
        c.keyStart.clear();
        c.totalPoints = 0;
        for (size_t i = 0; i < c.rows.size() && i < 0xFFFF; ++i)
        {
            const Row& r = c.rows[i];
            if (r.id <= kMaxId && c.at[r.id] == 0)
                c.at[r.id] = static_cast<uint16_t>(i + 1);
            c.keys.push_back(StatKey{ r.kind, r.param });
            c.totalPoints += r.points;
        }
        std::sort(c.keys.begin(), c.keys.end());
        c.keys.erase(std::unique(c.keys.begin(), c.keys.end()), c.keys.end());
        c.keyStart.assign(c.keys.size() + 1, 0);
        std::vector<std::vector<uint16_t>> per(c.keys.size());
        for (size_t i = 0; i < c.rows.size() && i < 0xFFFF; ++i)
            per[static_cast<size_t>(c.KeyIndex(StatKey{ c.rows[i].kind, c.rows[i].param }))].push_back(static_cast<uint16_t>(i));
        for (size_t k = 0; k < per.size(); ++k)
        {
            std::stable_sort(per[k].begin(), per[k].end(), [&](uint16_t a, uint16_t b) { return c.rows[a].target < c.rows[b].target; });
            c.keyStart[k] = static_cast<uint32_t>(c.keyRows.size());
            c.keyRows.insert(c.keyRows.end(), per[k].begin(), per[k].end());
        }
        c.keyStart[c.keys.size()] = static_cast<uint32_t>(c.keyRows.size());
    }

    // ── 파사드(GD() — 지금 판) ──
    [[nodiscard]] const Catalog& Get() noexcept;
    // 사냥 정수 · 결정(eEssence) — TierLoot의 essence_item · crystal_item · elite_crystal_item(지금 재료 61~65)
    [[nodiscard]] bool IsHuntMaterial(uint16_t itemId) noexcept;
}
