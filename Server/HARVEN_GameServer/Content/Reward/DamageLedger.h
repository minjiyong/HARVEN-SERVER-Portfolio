#pragma once

#include <array>
#include <span>

#include "Base/Core/Types.h"
#include "Base/ObjID/ObjID.h"
#include "Database/GameData/RewardRules.h"

// ─────────────────────────────────────────────────────────────────────────────
// CDamageLedger — 몬스터 하나가 지금 생애에 플레이어에게서 받은 피해(그로시 · 경험치 나눔과 퀘스트 인정의 근거, Shared/Document/PARTY.md 3).
//  · 위협 표(CAggro)와 따로 둔다 — 위협은 도발 · 감지 · 동료 도움(1부터)이 섞이고 깨어 있는 동안만 있다. 여기는 "실제로 깎은 HP"만.
//  · 넘친 피해(남은 HP보다 큰 마지막 타격)는 남은 HP만큼만 센다 — 막타가 큰 기술이라고 몫이 부풀지 않게.
//  · 칸 kMax개 고정 배열 — 넘치면 가장 적게 준 칸을 더 크게 준 새 사람이 바꾼다(그 사람은 몫에서 빠진다).
//  · 다시 태어날 때 · 집에 돌아가 HP를 다 채울 때 지운다. 소유 Zone 스레드 전용 — 잠금 없음.
// ─────────────────────────────────────────────────────────────────────────────
class CDamageLedger
{
public:
    static constexpr int kMax = 16;
    static_assert(kMax + 1 <= static_cast<int>(RewardRule::kMaxShares), "기록 칸 + 막타가 한 번에 나눌 수 있는 사람 수 안에 든다");

    struct Entry
    {
        ObjID  id{};
        UInt32 damage = 0;    // 32비트에서 멈춘다(RewardRule::SplitByWeight의 곱이 64비트 안에 든다)
    };

    void Add(const ObjID& id, UInt32 damage) noexcept
    {
        if (damage == 0)
            return;
        for (int i = 0; i < m_Count; ++i)
            if (m_Entries[i].id == id)
            {
                const UInt64 sum = static_cast<UInt64>(m_Entries[i].damage) + damage;
                m_Entries[i].damage = sum > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<UInt32>(sum);
                return;
            }
        if (m_Count < kMax)
        {
            m_Entries[m_Count++] = Entry{ id, damage };
            return;
        }
        int weakest = 0;
        for (int i = 1; i < m_Count; ++i)
            if (m_Entries[i].damage < m_Entries[weakest].damage)
                weakest = i;
        if (m_Entries[weakest].damage < damage)
            m_Entries[weakest] = Entry{ id, damage };
    }

    void Clear() noexcept
    {
        m_Entries = {};
        m_Count = 0;
    }

    [[nodiscard]] bool Empty() const noexcept { return m_Count == 0; }
    [[nodiscard]] std::span<const Entry> Entries() const noexcept { return { m_Entries.data(), static_cast<size_t>(m_Count) }; }

    [[nodiscard]] UInt32 DamageOf(const ObjID& id) const noexcept
    {
        for (int i = 0; i < m_Count; ++i)
            if (m_Entries[i].id == id)
                return m_Entries[i].damage;
        return 0;
    }

private:
    std::array<Entry, kMax> m_Entries{};
    UInt8                   m_Count = 0;
};
