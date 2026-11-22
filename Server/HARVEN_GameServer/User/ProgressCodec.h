#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "Content/Achieve/AchieveBook.h"

// ─────────────────────────────────────────────────────────────────────────────
// 진행 기록(DB UserProgress · VARBINARY(4000)) 바이트 — 'P' '2' 판 | classId | alloc[6](i16) | 판 2~: 천장 둘 | 판 3~: 퀘스트 | 판 4~: 개인 업적.
// 순수(GD() · CUser를 보지 않는다) — 표로 거르는 일(없는 퀘스트 · 직업)은 CUser::DeserializeProgress가 한다. 시험: AchieveRuleTests.
//   1: classId + alloc[6]
//   2: + 드롭 천장 카운터(pityRare, pityHeroic)
//   3: + 퀘스트(진행 중 칸 · 끝낸 퀘스트 비트 · 반복 퀘스트 대기 시각)
//   4: + 개인 업적(AchieveBook::Write — 통계 · 달성 · 받음 · 들어가 본 지역). 옛 판(1~3)을 읽으면 업적은 빈 채
// ─────────────────────────────────────────────────────────────────────────────
namespace ProgressCodec
{
    inline constexpr uint8_t kVersion   = 4;
    inline constexpr size_t  kDbBytes   = 4000;   // UserProgress.data VARBINARY(4000)
    inline constexpr size_t  kMaxQuests = 8;      // QuestData::kMaxActive(쓰는 쪽이 지킨다 — 읽기는 255까지 받아 CUser가 거른다)
    inline constexpr size_t  kMaxObjs   = 3;      // QuestData::kMaxObjs
    inline constexpr size_t  kMaxCooldowns = 32;  // 쓰는 줄 수 상한

    struct Quest
    {
        uint16_t id = 0;
        std::array<uint16_t, kMaxObjs> progress{};
        [[nodiscard]] bool operator==(const Quest&) const = default;
    };
    struct Cooldown
    {
        uint16_t id = 0;
        uint32_t readyAtUnix = 0;
        [[nodiscard]] bool operator==(const Cooldown&) const = default;
    };
    struct Data
    {
        uint8_t version = kVersion;               // 읽은 판(쓸 때는 보지 않는다 — 언제나 kVersion)
        uint8_t classId = 0;                      // 그대로(직업 검사는 CUser)
        std::array<int16_t, 6> alloc{};           // 읽을 때 0~999로 자른다
        uint16_t pityRare = 0, pityHeroic = 0;    // 읽을 때 0~1000으로 자른다
        std::vector<Quest> quests;                // 진행 중(쓰는 쪽은 id ≠ 0만)
        std::array<uint8_t, 32> questDone{};
        std::vector<Cooldown> cooldowns;          // 쓸 때 앞 kMaxCooldowns줄만
    };

    // 판 kVersion으로 쓴다. 업적 절은 book.Write(cat — 표가 세는 키만)
    [[nodiscard]] std::vector<uint8_t> Encode(const Data& d, const AchieveBook& book, const AchieveRule::Catalog* cat);
    // false = V2 기록이 아니다('P' '2'가 아니거나 너무 짧다 — 기존 캐릭터의 첫 V2 접속). 판 4 미만이면 book은 빈 채,
    // 깨진 업적 절(잘림 · 비트 길이 · 뒤에 남은 바이트 · 새 판)이면 book을 얼린다(원본 절 바이트를 되쓴다 — AchieveBook::Read)
    bool Decode(const std::vector<uint8_t>& in, Data& d, AchieveBook& book);
    // 쓸 수 있는 가장 큰 기록(퀘스트 8 · 대기 32 · 업적 절 가득) — kDbBytes보다 한참 작아야 한다(시험이 본다)
    inline constexpr size_t kMaxBytes = 4 + 12 + 4 + 1 + kMaxQuests * (2 + kMaxObjs * 2) + 32 + 1 + kMaxCooldowns * 6 + AchieveBook::kMaxSectionBytes;
    static_assert(kMaxBytes <= kDbBytes / 2, "진행 기록이 VARBINARY(4000)의 절반을 넘는다 — 칸을 줄이거나 DB 열을 늘린다");
}
