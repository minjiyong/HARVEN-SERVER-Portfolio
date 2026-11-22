#include "pch.h"
#include "Database/GameData/MoveRule.h"
#include "User/User.h"
#include "User/ProgressCodec.h"
#include "Worker/TimerThread.h"

void CUser::InitInstance()
{
    CSubject::InitInstance();

    GetStat()->SetMaxHp(PLAYER_MAX_HP);
    GetStat()->SetHp(PLAYER_MAX_HP);
    GetStat()->SetOffensive(PLAYER_OFFENSIVE);
    GetStat()->SetDead(false);

    m_pInventory = std::make_shared<CInventory>();
    m_pInventory->Attach(self<CUser>());
}

SessionSharedPtr CUser::GetGameSession() const
{
    return m_pSession;
}

void CUser::SetGameSession(const SessionSharedPtr& session)
{
    m_pSession = session;
}

UInt32 CUser::GetMoveSpeedPercent(UInt32 now) const noexcept
{
    // 능력치(DEX)·장비(이동 속도 옵션·세트)가 반영된 이동속도. 클라이언트는 USER_STATS_INF의 stats.moveSpeed로 같은 값을 받아 예측한다.
    UInt32 speed = m_Derived.moveSpeed;
    if (IsHasted(now))
        speed += MoveRule::kHasteBonusPercent;
    return MoveRule::ClampSpeed(speed);
}

// ─────────────────────────────────────────────────────────────────────────────
// V2 능력치
// ─────────────────────────────────────────────────────────────────────────────
BaseStats CUser::TotalBase() const noexcept
{
    BaseStats b = ClassTable::BaseAt(m_eClassId, GetStatConst()->GetLevel());
    for (int i = 0; i < 6; ++i)
        b.v[i] = static_cast<Int16>(b.v[i] + m_Alloc.v[i]);
    return b;
}

UInt16 CUser::FreePoints() const noexcept
{
    // 레벨업마다 1포인트(2레벨부터). 찍은 만큼 뺀다.
    const int earned = GetStatConst()->GetLevel() > 1 ? GetStatConst()->GetLevel() - 1 : 0;
    int spent = 0;
    for (int i = 0; i < 6; ++i) spent += m_Alloc.v[i];
    return static_cast<UInt16>(earned > spent ? earned - spent : 0);
}

bool CUser::RecomputeStats(bool fullHeal)
{

    // 장비 보너스 — 착용 중인 모든 부위(무기+강화 · 방어구 · 장신구 · 옵션 · 세트).
    GearBonus gear{};
    GearExtras extras{};
    if (m_pInventory != nullptr)
        m_pInventory->CollectGearBonus(gear, &extras);
    m_GearExtras = extras;

    const DerivedStats before = m_Derived;
    m_Gear = gear;
    m_Derived = StatRule::Compute(m_eClassId, GetStat()->GetLevel(), TotalBase(), gear);

    GetStat()->SetMaxHp(m_Derived.maxHp);
    if (fullHeal || GetStat()->GetHp() > m_Derived.maxHp)
        GetStat()->SetHp(m_Derived.maxHp);
    // 공격력 하나(CStat 공격력 → USER_INFO_INF.attack)도 같은 값을 보게 한다.
    GetStat()->SetOffensive(m_eClassId == ClassId::Engraver ? m_Derived.magicAttack : m_Derived.attack);
    if (fullHeal || m_Mp > m_Derived.maxMp)
        m_Mp = m_Derived.maxMp;

    return std::memcmp(&before, &m_Derived, sizeof(DerivedStats)) != 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// V2 진행 데이터 — 바이트 형식은 User/ProgressCodec(판 4: + 개인 업적). 여기서는 표로 거르고 CUser에 옮긴다.
// ─────────────────────────────────────────────────────────────────────────────
std::vector<UInt8> CUser::SerializeProgress() const
{
    ProgressCodec::Data d;
    d.classId = static_cast<UInt8>(m_eClassId);
    for (int i = 0; i < 6; ++i)
        d.alloc[static_cast<size_t>(i)] = m_Alloc.v[i];
    d.pityRare = m_PityRare;
    d.pityHeroic = m_PityHeroic;
    for (const auto& q : m_Quests)
    {
        if (q.id == 0) continue;
        ProgressCodec::Quest c;
        c.id = q.id;
        for (int o = 0; o < QuestData::kMaxObjs; ++o) c.progress[static_cast<size_t>(o)] = q.progress[static_cast<size_t>(o)];
        d.quests.push_back(c);
    }
    d.questDone = m_QuestDone;
    for (const auto& c : m_QuestCooldowns)
        d.cooldowns.push_back({ c.id, c.readyAtUnix });
    return ProgressCodec::Encode(d, m_Achieve, &AchieveRule::Get());
}

bool CUser::DeserializeProgress(const std::vector<UInt8>& in)
{
    ProgressCodec::Data d;
    m_LoadedProgressVersion = 0;
    if (!ProgressCodec::Decode(in, d, m_Achieve))
        return false;
    m_LoadedProgressVersion = d.version;
    if (ClassTable::IsValid(d.classId))
        m_eClassId = static_cast<ClassId>(d.classId);
    for (int i = 0; i < 6; ++i)
        m_Alloc.v[i] = d.alloc[static_cast<size_t>(i)];
    m_PityRare = d.pityRare;
    m_PityHeroic = d.pityHeroic;
    // v3: 퀘스트 — 표에 없는 id(지워진 퀘스트)는 버리고, 진행도는 목표 수로 자른다.
    m_Quests = {};
    m_QuestDone = d.questDone;
    m_QuestCooldowns.clear();
    size_t slot = 0;
    for (const auto& q : d.quests)
    {
        const QuestData::QuestRow* row = QuestData::Find(q.id);
        if (row == nullptr || slot >= m_Quests.size())
            continue;
        bool dup = false;
        for (size_t k = 0; k < slot; ++k) dup |= m_Quests[k].id == q.id;
        if (dup)
            continue;
        m_Quests[slot].id = q.id;
        for (int o = 0; o < QuestData::kMaxObjs; ++o)
            m_Quests[slot].progress[static_cast<size_t>(o)] = static_cast<UInt16>((std::min)(q.progress[static_cast<size_t>(o)], row->objs[o].count));
        ++slot;
    }
    for (const auto& c : d.cooldowns)
        if (QuestData::Find(c.id) != nullptr)
            m_QuestCooldowns.push_back({ c.id, c.readyAtUnix });
    return true;
}
