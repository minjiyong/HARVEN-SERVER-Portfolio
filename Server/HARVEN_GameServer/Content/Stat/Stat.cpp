#include "pch.h"
#include "Content/Stat/Stat.h"

UInt16 CStat::TakeDamage(UInt16 amount) noexcept
{
    const UInt16 current = m_Hp.load();
    const UInt16 next    = (current > amount) ? static_cast<UInt16>(current - amount) : 0;
    m_Hp.store(next);
    if (next == 0)
        m_bDie.store(true);
    return next;
}

void CStat::HealHp(UInt16 amount, UInt16 maxCap) noexcept
{
    const UInt32 current = m_Hp.load();
    const UInt16 next    = (current + amount >= maxCap)
                            ? maxCap
                            : static_cast<UInt16>(current + amount);
    m_Hp.store(next);
}

UInt8 CStat::AddExp(UInt32 amount) noexcept
{
    if (m_Level >= MAX_LEVEL)
        return 0;

    UInt8 gained = 0;
    m_Exp += amount;
    while (m_Level < MAX_LEVEL)
    {
        const UInt32 needed = LevelTable::ExpToNext(m_Level);
        if (m_Exp < needed)
            break;
        m_Exp -= needed;
        ++m_Level;
        ++gained;
    }

    // 만렙에 도달하면 남은 경험치는 의미가 없으므로 0으로 둔다(표시가 꼬이지 않게).
    if (m_Level >= MAX_LEVEL)
        m_Exp = 0;

    return gained;
}

UInt32 CStat::LoseExp(UInt32 amount) noexcept
{
    const UInt32 lost = (amount < m_Exp) ? amount : m_Exp;
    m_Exp -= lost;
    return lost;
}
