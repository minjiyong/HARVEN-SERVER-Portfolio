#pragma once

#include <cstdint>
#include <limits>
#include <random>

#include "Database/GameData/Gen/NpcIds.h"   // NpcIds::* — Npc.code_name에서 생성(datatool export)

// 대장간 강화 규칙. 값(단계별 성공 몫 · 하르벤의 가호 · 단계별 성능 · 알림 수치 · 장신구 배율)은 시트 Enchant · EnchantConst 표
// (→ Bin/GameData)에서 온다 — 여기에는 구조 상수(판정 눈금 · 자료형 상한)와 함수만 둔다. 데이터를 읽는 함수의 구현은
// Database/GameData/Load/Bind_Item.cpp. 강화권 · 보호권 아이템 = 시트 Item의 code_name(ItemTableData::Id::EnhancementTicket ·
// EnhancementProtection, 값은 buy_price), 대장장이 = 시트 Npc의 code_name(NpcIds::Blacksmith), 진열 = 시트 Shop.
namespace EnchantRule
{
    using Level = uint32_t;
    // 게임상 상한은 없다. 자료형 끝에서만 넘침을 방지한다.
    inline constexpr Level kMaxLevel = std::numeric_limits<Level>::max();
    inline constexpr uint16_t kBlacksmithNpcId = NpcIds::Blacksmith;   // 대장장이 도브란 — 강화는 그의 곁에서(Npc 탭 code_name)
    // 판정 눈금 — 성공 몫(success_tickets)의 분모 · 패킷 successTickets의 분모(1조 분의 1까지)
    inline constexpr uint64_t kRollScale = 1'000'000'000'000ull;

    // 다음 단계로 오를 성공 몫(Enchant 표 — 마지막 행이 그 위 단계 모두)
    [[nodiscard]] uint64_t SuccessTickets(Level current) noexcept;
    [[nodiscard]] inline double SuccessPercent(Level current) noexcept
    {
        return static_cast<double>(SuccessTickets(current)) * 100.0 / static_cast<double>(kRollScale);
    }
    [[nodiscard]] inline bool Succeeds(Level current, uint64_t roll) noexcept
    {
        return current < kMaxLevel && roll < kRollScale && roll < SuccessTickets(current);
    }

    // 하르벤의 가호: 지금 단계의 보장 시도 번호(0 = 없음). N-1번 연속 실패 뒤 N번째는 100%.
    [[nodiscard]] uint16_t GraceAttempts(Level current) noexcept;
    [[nodiscard]] inline uint64_t GraceSuccessTickets(Level current, uint16_t failures) noexcept
    {
        const auto limit = GraceAttempts(current);
        return limit != 0 && static_cast<uint32_t>(failures) + 1 >= limit ? kRollScale : SuccessTickets(current);
    }
    [[nodiscard]] inline bool SucceedsWithGrace(Level current, uint16_t failures, uint64_t roll) noexcept
    {
        return current < kMaxLevel && roll < kRollScale && roll < GraceSuccessTickets(current, failures);
    }
    [[nodiscard]] inline bool ValidGraceChange(Level current, uint16_t before, uint16_t after, bool success) noexcept
    {
        const auto limit = GraceAttempts(current);
        if (limit == 0) return before == 0 && after == 0;
        if (success) return after == 0;
        return static_cast<uint32_t>(before) + 1 < limit && after == before + 1;
    }
    template<class Rng> [[nodiscard]] uint64_t Roll(Rng& rng)
    {
        // 균등 분포의 rejection sampling: % 연산 편향 없이 1조 분의 1까지 판정.
        return std::uniform_int_distribution<uint64_t>(0, kRollScale - 1)(rng);
    }

    // 도달한 단계의 누적 성능(무기 공격력 · 방어구 방어력) = Enchant.stat_gain을 +0부터 더한 값.
    // 표 마지막 행 위로는 한 단계마다 (마지막 행 stat_gain + EnchantConst.gain_per_further_level × n). 65535에서 멈춘다.
    [[nodiscard]] uint16_t AttackBonus(Level level) noexcept;
    [[nodiscard]] Level WorldNoticeLevel() noexcept;      // 이 수치 이상 성공 → 서버 전체 알림
    [[nodiscard]] Level FirstNoticeLevel() noexcept;      // 이 수치 이상 → 아이템별 최초 도달 기록
    [[nodiscard]] uint16_t AccessoryHpPerPoint() noexcept;
    [[nodiscard]] uint16_t AccessoryMpPerPoint() noexcept;

    // 패킷 · 옛 DB 열(uint8_t)로 보낼 때
    [[nodiscard]] constexpr uint8_t LegacyLevel(Level level) noexcept
    {
        return static_cast<uint8_t>(level > 255 ? 255 : level);
    }
}
