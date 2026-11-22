#pragma once

#include <atomic>
#include "Content/Field/ZoneTypes.h"

class CTransform
{
public:
    using SharedPtr = shared_ptr<CTransform>;
    using WeakPtr = weak_ptr<CTransform>;

private:
    short m_X       = -1;
    short m_Y       = -1;
    short m_SectorX = -1;
    short m_SectorY = -1;
    // 소유 Zone 스레드가 쓰지만, 읽는 쪽이 항상 소유자인 것은 아니다 —
    // 전투 중인 몬스터는 대상이 자기 Zone을 벗어났는지 보려고 대상 플레이어의
    // 이 값을 읽는데(몬스터 행동 트리의 대상 고르기 — C_InCombat), 그 플레이어가 이미 다른
    // Zone으로 넘어갔다면 새 소유 스레드가 같은 값을 쓰고 있을 수 있다.
    // 생 정수로 두면 그 조합이 데이터 레이스가 되므로 원자적으로 다룬다.
    // 순서를 맞춰야 할 다른 데이터가 없는 단일 값이고 실제 동기화는 Zone 큐
    // 전달(뮤텍스)이 담당하므로 relaxed로 충분하다.
    std::atomic<ZoneId> m_ZoneId{ InvalidZoneId };
    std::atomic<bool> m_bTransferring{ false };

public:
    CTransform() = default;

    // ── Getters ──────────────────────────────────────────────
    short GetX()       const noexcept { return m_X; }
    short GetY()       const noexcept { return m_Y; }
    short GetSectorX() const noexcept { return m_SectorX; }
    short GetSectorY() const noexcept { return m_SectorY; }
    ZoneId GetZoneId() const noexcept { return m_ZoneId.load(std::memory_order_relaxed); }
    bool IsTransferring() const noexcept { return m_bTransferring.load(std::memory_order_acquire); }

    // ── Setters ──────────────────────────────────────────────
    void SetX(short x)           noexcept { m_X = x; }
    void SetY(short y)           noexcept { m_Y = y; }
    void SetPosition(short x, short y) noexcept { m_X = x; m_Y = y; }
    void SetSectorX(short sx)    noexcept { m_SectorX = sx; }
    void SetSectorY(short sy)    noexcept { m_SectorY = sy; }
    void SetSector(short sx, short sy) noexcept { m_SectorX = sx; m_SectorY = sy; }
    void SetZoneId(ZoneId zoneId) noexcept { m_ZoneId.store(zoneId, std::memory_order_relaxed); }
    void SetTransferring(bool transferring) noexcept { m_bTransferring.store(transferring, std::memory_order_release); }

    // ── In-out references (CSector 업데이트 함수 전달용) ──
    short& RefX()       noexcept { return m_X; }
    short& RefY()       noexcept { return m_Y; }
    short& RefSectorX() noexcept { return m_SectorX; }
    short& RefSectorY() noexcept { return m_SectorY; }

    // ── Utility ──────────────────────────────────────────────
    void Reset() noexcept;
};
