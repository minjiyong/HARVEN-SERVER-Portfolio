#include "pch.h"
#include "Entity/Subject/Skill/AreaOfEffect.h"

#include <array>
#include <vector>

#include "Base/Core/CoreTLS.h"
#include "Content/Combat/CombatHelper.h"
#include "Content/ContentHelper.h"
#include "Content/Field/Sector.h"
#include "Content/Field/ZoneLayout.h"
#include "Content/Skill/MonsterSkillHelper.h"
#include "Database/GameData/MonsterAITable.h"
#include "Database/GameData/WorldData.h"
#include "Entity/Component/Behavior/EntityBehavior.h"
#include "Entity/Component/Behavior/Task/Monster/MonsterAgent.h"
#include "Entity/Subject/Monster/Monster.h"
#include "User/User.h"
#include "Worker/TimerThread.h"

namespace
{
    // Zone 스레드마다 하나 — 칸은 다시 쓴다(빈 칸 목록). 핸들 = (세대 << 16) | (칸 + 1).
    struct AoeTable
    {
        std::vector<CAreaOfEffect> slots;
        std::vector<UInt16>        freeSlots;
        size_t                     alive = 0;
    };
    thread_local AoeTable LAoe;

    // 한 틱에 닿는 대상 상한(반경 8까지 — 넘으면 앞에서부터)
    constexpr int kMaxTouch = 24;
    // 동료 강화: 남은 시간이 (간격 + 이만큼)보다 길면 다시 걸지 않는다 — 틱마다 STATUS_NFY를 뿌리지 않게
    constexpr UInt32 kRefreshSlackMs = 500;

    [[nodiscard]] int Cheb(int ax, int ay, int bx, int by) noexcept { return std::max(std::abs(ax - bx), std::abs(ay - by)); }

    [[nodiscard]] CAreaOfEffect* Find(UInt32 handle)
    {
        const UInt32 index = handle & 0xFFFFu;
        if (index == 0 || index > LAoe.slots.size())
            return nullptr;
        CAreaOfEffect& s = LAoe.slots[index - 1];
        return s.handle == handle ? &s : nullptr;
    }

    void Free(CAreaOfEffect& s)
    {
        const UInt32 index = s.handle & 0xFFFFu;
        const UInt16 gen = s.gen;
        s = CAreaOfEffect{};
        s.gen = gen;   // 세대는 남긴다 — 다음에 이 칸을 쓰면 하나 올려 낡은 예약을 거른다
        LAoe.freeSlots.push_back(static_cast<UInt16>(index - 1));
        if (LAoe.alive > 0)
            --LAoe.alive;
    }

    void ScheduleTick(const CAreaOfEffect& s, UInt32 delayMs)
    {
        GTimerThread->ScheduleAfter(s.ownerId, std::chrono::milliseconds(delayMs), TIMER_EVENT_TYPE::EV_MONSTER_AOE,
            ObjID(EnumCategory::eNone, static_cast<UInt64>(s.handle)));
    }

    [[nodiscard]] bool HittablePlayer(const UserSharedPtr& u)
    {
        if (u == nullptr || u->GetStat()->IsDead() || u->IsTransferring() || u->GetZoneId() != LCurrentZoneId)
            return false;
        const auto session = u->GetGameSession();
        return session && session->m_eState == SOCKET_STATE::ST_INGAME && !WorldData::IsSafeZone(u->GetX(), u->GetY());
    }

    void Apply(const MonsterAoeRow& row, const CAreaOfEffect& s, const MonsterSharedPtr& owner)
    {
        const short sx = static_cast<short>(s.x / SECTOR_RANGE), sy = static_cast<short>(s.y / SECTOR_RANGE);
        if (!GSector->IsValidSector(sx, sy))
            return;
        const int r = row.radius;
        const ObjID ownerId = s.ownerId;
        const SubjectSharedPtr fxCenter = owner;   // 효과를 보낼 섹터 기준(주인이 없으면 대상 기준)

        // ── 적: 틱 피해 · 해로운 상태 ──
        if (row.affect != AoeAffect::Allies && (row.coeff > 0 || row.status != StatusId::None))
        {
            std::array<UserSharedPtr, kMaxTouch> victims{};
            int count = 0;
            GSector->ForEachObjectWithin(sx, sy, s.x, s.y, r, EnumCategory::eUser, [&](const SubjectSharedPtr& object)
            {
                if (count >= kMaxTouch || Cheb(object->GetX(), object->GetY(), s.x, s.y) > r)
                    return;
                const auto u = static_pointer_cast<CUser>(object);
                if (HittablePlayer(u))
                    victims[static_cast<size_t>(count++)] = u;
            });
            const StatusRow& st = StatusTable::Get(row.status);
            const UInt32 now = GetNowTime();
            // 틱 피해 숫자 모양 — 불(화상 · 불 효과)이면 화상, 그 밖은 중독(클라 DamageStyle::DotBurn · DotPoison)
            const UInt16 dotStyle = row.status == StatusId::Burn || row.fx == MonsterFx::Fire ? 9 : 15;
            for (int i = 0; i < count; ++i)
            {
                const UserSharedPtr& u = victims[static_cast<size_t>(i)];
                if (row.coeff > 0)
                {
                    // ±10%
                    const UInt32 raw = std::max<UInt32>(1, static_cast<UInt32>(s.attack) * row.coeff / 100u * (90u + static_cast<UInt32>(LRng() % 21u)) / 100u);
                    bool landed = false;
                    if (CCombatHelper::HitPlayerFromAoE(ownerId, u, raw, dotStyle, &landed))
                    {
                        CContentHelper::HandlePlayerDeath(u, ownerId, false);
                        continue;
                    }
                    if (!landed)
                        continue;   // 잔상 회피 · 무적 — 상태이상도 없다
                }
                else if (CCombatHelper::IsUntouchable(u, now))
                    continue;
                if (row.status != StatusId::None && row.statusMs > 0)
                {
                    const UInt16 tick = st.tickMs > 0 ? static_cast<UInt16>(std::max<UInt32>(1, static_cast<UInt32>(s.attack) * st.tickPercent / 100u)) : 0;
                    CCombatHelper::ApplyStatus(u, row.status, row.statusMs, std::max<UInt8>(1, row.statusStacks), ownerId, tick, row.statusPower);
                }
                if (i < 4)
                    CMonsterSkillHelper::SendFx(u, ownerId, row.fx, u->GetX(), u->GetY(), 0);
            }
            if (row.fx != MonsterFx::None)
                CMonsterSkillHelper::SendFx(fxCenter ? fxCenter : (count > 0 ? SubjectSharedPtr(victims[0]) : nullptr), ownerId, row.fx, s.x, s.y, r);
        }

        // ── 동료: 회복 · 강화 ──
        if (row.affect != AoeAffect::Enemies && (row.healPercent > 0 || row.status != StatusId::None))
        {
            const UInt32 now = GetNowTime();
            int shown = 0;
            GSector->ForEachObjectWithin(sx, sy, s.x, s.y, r, EnumCategory::eMonster, [&](const SubjectSharedPtr& object)
            {
                if (object->GetStat()->IsDead() || Cheb(object->GetX(), object->GetY(), s.x, s.y) > r)
                    return;
                const auto ally = static_pointer_cast<CMonster>(object);
                if (ally->IsDummy())
                    return;
                bool touched = false;
                Int32 healed = 0;
                if (row.healPercent > 0 && ally->GetStat()->GetHp() < ally->GetStat()->GetMaxHp())
                {
                    const UInt16 before = ally->GetStat()->GetHp();
                    const UInt32 amount = std::max<UInt32>(1, static_cast<UInt32>(ally->GetStat()->GetMaxHp()) * row.healPercent / 100u);
                    ally->GetStat()->HealHp(static_cast<UInt16>(std::min<UInt32>(amount, 65535)), ally->GetStat()->GetMaxHp());
                    healed = static_cast<Int32>(ally->GetStat()->GetHp()) - before;
                    touched = healed > 0;
                }
                if (row.status != StatusId::None && row.statusMs > 0)
                {
                    const StatusSlot* have = ally->Status().Get(row.status, now);
                    const bool fresh = have != nullptr && have->until - now > static_cast<UInt32>(row.intervalMs) + kRefreshSlackMs && have->power >= row.statusPower;
                    if (!fresh)
                    {
                        CCombatHelper::ApplyStatus(ally, row.status, row.statusMs, std::max<UInt8>(1, row.statusStacks), ownerId, 0, row.statusPower);
                        touched = true;
                    }
                }
                if (touched && shown < 6)
                {
                    ++shown;
                    CMonsterSkillHelper::SendFx(ally, ally->GetObjID(), row.fx, ally->GetX(), ally->GetY(), healed);
                }
            });
        }
    }
}

namespace CAreaOfEffectHelper
{
    UInt32 Spawn(const MonsterSharedPtr& owner, UInt16 aoeId, short x, short y)
    {
        const MonsterAoeRow* row = MonsterAoeTable::Find(aoeId);
        if (row == nullptr || owner == nullptr || owner->GetStat()->IsDead())
            return 0;
        if (!row->follow)
        {
            if (!CZoneLayout::IsValidWorldPosition(x, y) || WorldData::IsSafeZone(x, y) || CZoneLayout::GetZoneIdByWorld(x, y) != owner->GetZoneId())
                return 0;
        }
        else
        {
            x = owner->GetX();
            y = owner->GetY();
        }

        UInt16 index = 0;
        if (!LAoe.freeSlots.empty())
        {
            index = LAoe.freeSlots.back();
            LAoe.freeSlots.pop_back();
        }
        else
        {
            if (LAoe.slots.size() >= 0xFFFF)
                return 0;
            index = static_cast<UInt16>(LAoe.slots.size());
            LAoe.slots.emplace_back();
        }
        CAreaOfEffect& s = LAoe.slots[index];
        s.gen = static_cast<UInt16>(s.gen + 1 == 0 ? 1 : s.gen + 1);
        s.handle = (static_cast<UInt32>(s.gen) << 16) | static_cast<UInt32>(index + 1);
        s.aoeId = aoeId;
        s.ownerId = owner->GetObjID();
        const UInt32 empower = owner->Status().Power(StatusId::Empower, GetNowTime());
        s.attack = static_cast<UInt16>(std::min<UInt32>(65535, static_cast<UInt32>(owner->GetStat()->GetOffensive()) * (100u + empower) / 100u));
        s.x = x;
        s.y = y;
        s.endAt = GetNowTime() + row->durationMs;
        ++LAoe.alive;
        if (CEntityBehavior* b = owner->GetBehavior())
            b->m_bOwnsAoe = true;   // 잠들 때 주인에 묶인 장판을 거두러 간다
        ScheduleTick(s, row->firstMs);
        return s.handle;
    }

    void Remove(UInt32 handle)
    {
        if (CAreaOfEffect* s = Find(handle))
            Free(*s);
    }

    void RemoveByOwner(const ObjID& ownerId)
    {
        for (CAreaOfEffect& s : LAoe.slots)
        {
            if (s.handle == 0 || s.ownerId != ownerId)
                continue;
            const MonsterAoeRow* row = MonsterAoeTable::Find(s.aoeId);
            if (row == nullptr || row->ownerBound)
                Free(s);
        }
    }

    void HandleTick(const ObjID& ownerId, UInt32 handle)
    {
        CAreaOfEffect* s = Find(handle);
        if (s == nullptr || s->ownerId != ownerId)
            return;   // 이미 거뒀다(주인이 잠들었거나 다른 장판이 칸을 받았다)
        const MonsterAoeRow* row = MonsterAoeTable::Find(s->aoeId);
        if (row == nullptr)
        {
            Free(*s);
            return;
        }
        const auto owner = ::GetGameObject<CMonster>(ownerId);
        const bool ownerAwake = owner != nullptr && !owner->GetStat()->IsDead() && owner->IsActive() && owner->GetSectorX() >= 0;
        if (row->ownerBound && !ownerAwake)
        {
            Free(*s);
            return;
        }
        if (row->follow && owner != nullptr && owner->GetSectorX() >= 0)
        {
            s->x = owner->GetX();
            s->y = owner->GetY();
        }
        const UInt32 now = GetNowTime();
        const bool aura = row->follow && row->ownerBound;   // 오라는 주인이 깨어 있는 동안 — 시간으로 끝나지 않는다
        if (!aura && TimeReached(now, s->endAt))
        {
            Free(*s);
            return;
        }
        const CAreaOfEffect snapshot = *s;   // Apply가 다른 장판을 건드려도(지금은 없다) 이 값은 그대로
        Apply(*row, snapshot, ownerAwake ? owner : nullptr);
        if (CAreaOfEffect* again = Find(handle))
            ScheduleTick(*again, std::max<UInt32>(row->intervalMs, MonsterAoeTable::kMinIntervalMs));
    }

    size_t ActiveCount()
    {
        return LAoe.alive;
    }
}
