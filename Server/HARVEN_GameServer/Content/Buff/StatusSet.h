#pragma once

#include <array>
#include <cstdint>
#include "Database/GameData/CombatData.h"
#include "Base/ObjID/ObjID.h"

// ─────────────────────────────────────────────────────────────────────────────
// 개체(플레이어/몬스터) 하나에 걸린 상태이상. 소유 Zone 스레드에서만 읽고 쓴다.
// 시각은 GetNowTime()(ms) 기준. 슬롯이 적어 선형 탐색이면 충분하다.
// ─────────────────────────────────────────────────────────────────────────────
struct StatusSlot
{
    StatusId id = StatusId::None;
    UInt8    stacks = 0;
    UInt32   until = 0;
    UInt32 nextTick = 0;
    UInt16   tickDamage = 0;     // 중첩 하나당 틱 피해(걸 때의 공격력으로 고정)
    UInt16   power = 0;          // 강화 세기(%) — Empower · Fortify · Swift(거는 쪽이 정한다 — 더 센 쪽이 남는다)
    ObjID    source{};
};

class CStatusSet
{
public:
    static constexpr int kMax = 6;

    [[nodiscard]] const StatusSlot* Get(StatusId id, UInt32 now) const noexcept
    {
        for (const auto& s : m_Slots)
            if (s.id == id && s.stacks > 0 && now < s.until)
                return &s;
        return nullptr;
    }
    [[nodiscard]] bool Has(StatusId id, UInt32 now) const noexcept { return Get(id, now) != nullptr; }
    [[nodiscard]] UInt8 Stacks(StatusId id, UInt32 now) const noexcept
    {
        const StatusSlot* s = Get(id, now);
        return s ? s->stacks : 0;
    }
    [[nodiscard]] UInt32 Remaining(StatusId id, UInt32 now) const noexcept
    {
        const StatusSlot* s = Get(id, now);
        return s ? s->until - now : 0;
    }

    // 강화 세기(%) — 걸려 있지 않으면 0
    [[nodiscard]] UInt16 Power(StatusId id, UInt32 now) const noexcept
    {
        const StatusSlot* s = Get(id, now);
        return s ? s->power : 0;
    }

    // 걸기: 같은 상태면 시간 갱신 + 중첩(최대치까지) · 세기는 더 센 쪽. 반환값 = 결과 슬롯.
    StatusSlot& Apply(StatusId id, UInt32 now, UInt32 durationMs, UInt8 stacks, const ObjID& source, UInt16 tickDamage, UInt16 power = 0) noexcept
    {
        const StatusRow& row = StatusTable::Get(id);
        StatusSlot* slot = nullptr;
        for (auto& s : m_Slots)
            if (s.id == id) { slot = &s; break; }
        if (slot == nullptr)
        {
            // 빈(또는 만료된) 슬롯, 없으면 가장 먼저 끝나는 슬롯을 덮어쓴다.
            for (auto& s : m_Slots)
                if (s.id == StatusId::None || s.stacks == 0 || now >= s.until) { slot = &s; break; }
            if (slot == nullptr)
            {
                slot = &m_Slots[0];
                for (auto& s : m_Slots)
                    if (s.until < slot->until) slot = &s;
            }
            *slot = StatusSlot{};
            slot->id = id;
        }
        const bool alive = slot->stacks > 0 && now < slot->until;
        const UInt8 maxStacks = row.maxStacks > 0 ? row.maxStacks : 1;
        const UInt8 base = alive ? slot->stacks : 0;
        slot->stacks = static_cast<UInt8>(base + stacks > maxStacks ? maxStacks : base + stacks);
        slot->until = now + durationMs;
        slot->source = source;
        if (tickDamage > 0) slot->tickDamage = tickDamage;
        slot->power = alive && slot->power > power ? slot->power : power;
        if (!alive && row.tickMs > 0)
            slot->nextTick = now + row.tickMs;
        return *slot;
    }

    void Remove(StatusId id) noexcept
    {
        for (auto& s : m_Slots)
            if (s.id == id) s = StatusSlot{};
    }

    void Clear() noexcept { m_Slots = {}; }

    [[nodiscard]] bool BlocksMove(UInt32 now) const noexcept
    {
        for (const auto& s : m_Slots)
            if (s.stacks > 0 && now < s.until && StatusTable::Get(s.id).blocksMove) return true;
        return false;
    }
    [[nodiscard]] bool BlocksAttack(UInt32 now) const noexcept
    {
        for (const auto& s : m_Slots)
            if (s.stacks > 0 && now < s.until && StatusTable::Get(s.id).blocksAttack) return true;
        return false;
    }
    [[nodiscard]] UInt32 SlowPercent(UInt32 now) const noexcept
    {
        UInt32 best = 0;
        for (const auto& s : m_Slots)
            if (s.stacks > 0 && now < s.until)
            {
                const UInt32 p = StatusTable::Get(s.id).slowPercent;
                if (p > best) best = p;
            }
        return best;
    }
    [[nodiscard]] bool Any(UInt32 now) const noexcept
    {
        for (const auto& s : m_Slots)
            if (s.stacks > 0 && now < s.until) return true;
        return false;
    }

    std::array<StatusSlot, kMax>& Slots() noexcept { return m_Slots; }

private:
    std::array<StatusSlot, kMax> m_Slots{};
};
