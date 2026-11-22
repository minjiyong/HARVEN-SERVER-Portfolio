#pragma once
#include "Base/GameObject.h"
#include "Database/GameData/LevelTable.h"

class CStat : public CGameObject
{
public:
    using SharedPtr = shared_ptr<CStat>;
    using WeakPtr = weak_ptr<CStat>;

    // 레벨 규칙은 Data/LevelTable.h(서버/클라 공용)로 옮겼다. 예전 상수
    // (EXP_TABLE[4] = {30,50,70,90}, MAX_LEVEL = 5)는 LevelTable::ExpToNext(1) == 30에서
    // 시작하도록 맞춰 두었다.
    static constexpr UInt8 MAX_LEVEL = LevelTable::kMaxLevel;

private:
    UInt16         m_MaxHp        = 0;
    std::atomic<UInt16> m_Hp      = 0;
    UInt16         m_Offensive    = 0;
    std::atomic<bool>     m_bDie  = true;
    UInt8          m_Level        = 1;
    UInt32         m_Exp          = 0;

public:
    CStat() = default;
    // atomic 멤버가 있으므로 복사 금지
    CStat(const ObjID& ownerID)
    {
        SetOwnerID(ownerID);
    }

    CStat(const CStat&)            = delete;
    CStat& operator=(const CStat&) = delete;

    // ── Getters ──────────────────────────────────────────────
    UInt16 GetMaxHp()    const noexcept { return m_MaxHp; }
    UInt16 GetHp()       const noexcept { return m_Hp.load(); }
    UInt16 GetOffensive()const noexcept { return m_Offensive; }
    bool   IsDead()      const noexcept { return m_bDie.load(); }
    UInt8  GetLevel()    const noexcept { return m_Level; }
    UInt32 GetExp()      const noexcept { return m_Exp; }

    // ── Setters ──────────────────────────────────────────────
    void SetMaxHp(UInt16 maxHp)          noexcept { m_MaxHp = maxHp; }
    void SetHp(UInt16 hp)                noexcept { m_Hp.store(hp); }
    void SetOffensive(UInt16 offensive)  noexcept { m_Offensive = offensive; }
    void SetDead(bool dead)              noexcept { m_bDie.store(dead); }
    void SetLevel(UInt8 level)           noexcept { m_Level = level; }
    void SetExp(UInt32 exp)              noexcept { m_Exp = exp; }

    // ── Wrapper functions ─────────────────────────────────────
    // 데미지 적용 후 남은 HP 반환. HP가 0이 되면 m_bDie를 true로 설정
    UInt16 TakeDamage(UInt16 amount) noexcept;
    // HP 회복. maxCap을 초과하지 않도록 제한
    void HealHp(UInt16 amount, UInt16 maxCap) noexcept;

    // 경험치 추가. 이번 호출로 오른 레벨 수를 반환한다(0 = 레벨업 없음).
    // 보스처럼 큰 경험치를 한 번에 주면 여러 레벨이 한꺼번에 오를 수 있어서
    // 예전처럼 bool이 아니라 횟수를 돌려준다.
    UInt8 AddExp(UInt32 amount) noexcept;

    // 경험치 차감(사망 페널티). 현재 레벨의 누적 경험치 안에서만 깎고 레벨은 내리지
    // 않는다. 실제로 깎인 양을 반환한다.
    UInt32 LoseExp(UInt32 amount) noexcept;

    // 모든 수치를 초기값으로 되돌림
    void Reset() noexcept;
};
