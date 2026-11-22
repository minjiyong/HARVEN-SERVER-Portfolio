#pragma once

#include <array>

#include "Base/Core/Types.h"
#include "Base/ObjID/ObjID.h"

// ─────────────────────────────────────────────────────────────────────────────
// CAggro — 몬스터 하나의 위협 표(REIW Content/Aggro). 깨어 있는 몬스터의 행동(CEntityBehavior)만 든다.
//  · 칸 kMax개 고정 배열 — 한 몬스터를 동시에 때리는 사람은 많지 않다. 넘치면 위협이 가장 작은 칸을 바꾼다.
//  · 위협 = 준 피해(맞을 때 더한다). 감지 · 동료의 도움 요청으로 들어온 사람은 1부터.
//  · 도발(Taunt 상태)은 표보다 먼저 본다(T_Combat — 건 사람이 유효하면 그 사람).
//  · 소유 Zone 스레드 전용 — 잠금 없음.
// ─────────────────────────────────────────────────────────────────────────────
class CAggro
{
public:
    static constexpr int kMax = 8;

    struct Entry
    {
        ObjID  id{};
        UInt32 threat = 0;
    };

    void Add(const ObjID& id, UInt32 threat) noexcept
    {
        if (threat == 0)
            threat = 1;
        for (int i = 0; i < m_Count; ++i)
            if (m_Entries[i].id == id)
            {
                const UInt64 sum = static_cast<UInt64>(m_Entries[i].threat) + threat;
                m_Entries[i].threat = sum > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<UInt32>(sum);
                return;
            }
        if (m_Count < kMax)
        {
            m_Entries[m_Count++] = Entry{ id, threat };
            return;
        }
        int weakest = 0;
        for (int i = 1; i < m_Count; ++i)
            if (m_Entries[i].threat < m_Entries[weakest].threat)
                weakest = i;
        if (m_Entries[weakest].threat < threat)
            m_Entries[weakest] = Entry{ id, threat };
    }

    void Remove(const ObjID& id) noexcept
    {
        for (int i = 0; i < m_Count; ++i)
            if (m_Entries[i].id == id)
            {
                m_Entries[i] = m_Entries[--m_Count];
                m_Entries[m_Count] = Entry{};
                return;
            }
    }

    void Clear() noexcept
    {
        m_Entries = {};
        m_Count = 0;
    }

    [[nodiscard]] bool Empty() const noexcept { return m_Count == 0; }
    [[nodiscard]] int Count() const noexcept { return m_Count; }

    [[nodiscard]] bool Has(const ObjID& id) const noexcept
    {
        return Threat(id) > 0;
    }

    // 그 사람의 위협(없으면 0 — 칸에 든 사람은 늘 1 이상)
    [[nodiscard]] UInt32 Threat(const ObjID& id) const noexcept
    {
        for (int i = 0; i < m_Count; ++i)
            if (m_Entries[i].id == id)
                return m_Entries[i].threat;
        return 0;
    }

    // 위협이 큰 순서로 valid(id)를 묻는다 — 처음 참인 id. 거짓인 칸은 지운다(죽음 · 마을 · Zone 밖 · 은신 …).
    template <class Fn>
    [[nodiscard]] ObjID PickValid(Fn&& valid)
    {
        while (m_Count > 0)
        {
            int best = 0;
            for (int i = 1; i < m_Count; ++i)
                if (m_Entries[i].threat > m_Entries[best].threat)
                    best = i;
            const ObjID id = m_Entries[best].id;
            if (valid(id))
                return id;
            Remove(id);
        }
        return ObjID{};
    }

private:
    std::array<Entry, kMax> m_Entries{};
    UInt8                   m_Count = 0;
};
