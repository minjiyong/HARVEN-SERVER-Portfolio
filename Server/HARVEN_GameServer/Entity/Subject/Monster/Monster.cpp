#include "pch.h"
#include "Entity/Subject/Monster/Monster.h"
#include "Base/GameObjectManager.h"
#include "Base/Core/ObjectPool.h"
#include "Entity/Component/Behavior/EntityBehavior.h"
#include "Database/GameData/EconRules.h"

CMonster::~CMonster()
{
    // 몬스터는 서버가 끝날 때만 사라진다 — 그때 깨어 있던 행동을 풀에 돌려준다
    if (m_pBehavior != nullptr)
    {
        CObjectPool<CEntityBehavior>::Release(m_pBehavior);
        m_pBehavior = nullptr;
    }
}

void CMonster::InitInstance()
{
    CSubject::InitInstance();

    // 종족 id는 ObjID의 KIND에 실려 온다(CMonsterHelper::InitForZone이 생성 시 지정).
    // KIND가 0이거나 테이블에 없으면 예전 동작(고정 스탯, ID 구간으로 선공 여부)으로 둔다.
    m_pSpecies = MonsterTable::Find(GetKIND());
    if (m_pSpecies != nullptr)
    {
        GetStat()->SetMaxHp(m_pSpecies->maxHp);
        GetStat()->SetHp(m_pSpecies->maxHp);
        GetStat()->SetOffensive(m_pSpecies->attack);
        GetStat()->SetLevel(m_pSpecies->level);
        SetType(m_pSpecies->aggressive ? MONSTER_TYPE::AGGRO : MONSTER_TYPE::PASSIVE);
        SetName(m_pSpecies->name);
    }
    else
    {
        GetStat()->SetMaxHp(MONSTER_MAX_HP);
        GetStat()->SetHp(MONSTER_MAX_HP);
        GetStat()->SetOffensive(MONSTER_OFFENSIVE);
        SetType(GetDatabaseID() <= AGGRO_MONSTER_BOUNDARY ? MONSTER_TYPE::AGGRO : MONSTER_TYPE::PASSIVE);
        SetName(std::format("Monster{}", GetDatabaseID()));
    }
    GetStat()->SetDead(false);
    m_BaseMaxHp = GetStat()->GetMaxHp();
    m_BaseOffensive = GetStat()->GetOffensive();
    m_bElite = false;

    ClearPath();
}

void CMonster::SetElite(bool elite) noexcept
{
    m_bElite = elite;
    const auto scale = [elite](UInt16 base, UInt32 percent) -> UInt16
    {
        if (!elite)
            return base;
        const UInt32 v = static_cast<UInt32>(base) * percent / 100u;
        return static_cast<UInt16>(std::clamp<UInt32>(v, 1u, 65535u));
    };
    GetStat()->SetMaxHp(scale(m_BaseMaxHp, EconRule::EliteHpPercent()));
    GetStat()->SetOffensive(scale(m_BaseOffensive, EconRule::EliteAttackPercent()));
    if (GetStat()->GetHp() > GetStat()->GetMaxHp())
        GetStat()->SetHp(GetStat()->GetMaxHp());
}

int CMonster::CombatLevel() noexcept
{
    const int level = m_pSpecies != nullptr ? m_pSpecies->level : GetStat()->GetLevel();
    return m_bElite ? level + EconRule::EliteLevelBonus() : level;
}
