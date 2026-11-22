#include "pch.h"
#include "Content/Combat/CombatHelper.h"
#include "Content/Skill/UserSkillHelper.h"
#include "User/User.h"
#include "Entity/Subject/Monster/Monster.h"
#include "User/UserHelper.h"
#include "Entity/Subject/Monster/MonsterHelper.h"
#include "Entity/Subject/SubjectHelper.h"
#include "Content/Field/SectorHelper.h"
#include "Content/Field/Sector.h"
#include "Worker/TimerThread.h"
#include "Base/Core/CoreTLS.h"
#include "Content/Navigator/Collision.h"
#include "Content/ContentHelper.h"
#include "Content/Field/ZoneLayout.h"
#include "Database/GameData/WorldData.h"
#include "Database/GameData/MoveRule.h"
#include "Database/GameData/MonsterTable.h"
#include "Database/GameData/MonsterAITable.h"
#include "Content/Skill/MonsterSkillHelper.h"
#include "Entity/Component/Behavior/EntityBehaviorHelper.h"

namespace
{
    // 몬스터 → 플레이어 피해 배율(%). 플레이어 HP가 직업·능력치로 커진 만큼
    // 몬스터 위협을 맞춘다.
    constexpr UInt32 kMonsterDamageScaleV2 = 170;

    // 기본 공격으로 MP가 조금씩 찬다(스킬과 평타를 섞어 쓰게).
    constexpr UInt32 kCounterIntervalMs = 450;

    SessionSharedPtr SessionOf(const UserSharedPtr& u)
    {
        return u ? u->GetGameSession() : nullptr;
    }

    bool IsInGameUser(const SubjectSharedPtr& s)
    {
        if (!s || s->GetObjID().GetCategory<EnumCategory>() != EnumCategory::eUser)
            return false;
        auto user = static_pointer_cast<CUser>(s);
        auto session = user->GetGameSession();
        return session && session->m_eState == SOCKET_STATE::ST_INGAME;
    }

    void BroadcastStatus(const SubjectSharedPtr& target, StatusId id, UInt8 stacks, UInt32 remainMs, const ObjID& source)
    {
        if (target == nullptr || target->GetSectorX() < 0)
            return;
        STATUS_NFY_PACKET packet{};
        packet.size = sizeof(packet);
        packet.type = static_cast<char>(PacketType::STATUS_NFY);
        packet.subjectId = target->GetObjID();
        packet.status = static_cast<UInt8>(id);
        packet.stacks = stacks;
        packet.durationMs = remainMs;
        packet.sourceId = source;
        // 시야(CanSee — 체비셰프 GViewRange) 안만 훑는다: 칸에 적어 둔 좌표로 걸러 범위 밖 플레이어는 약한 참조를 풀지 않는다
        GSector->ForEachObjectWithin(target->GetSectorX(), target->GetSectorY(), target->GetX(), target->GetY(), GViewRange, EnumCategory::eUser,
            [&](const SubjectSharedPtr& object)
        {
            if (!IsInGameUser(object) || !CSubjectHelper::CanSee(object, target))
                return;
            if (auto session = static_pointer_cast<CUser>(object)->GetGameSession())
                session->PostSend(packet);
        });
    }

    int Sign(int v) { return (v > 0) - (v < 0); }
}

namespace CCombatHelper
{
    bool MonsterCanReach(const MonsterSharedPtr& monster, const UserSharedPtr& target)
    {
        if (!monster || !target)
            return false;
        const int dx = std::abs(target->GetX() - monster->GetX());
        const int dy = std::abs(target->GetY() - monster->GetY());
        return (dx | dy) != 0 && dx <= 1 && dy <= 1;
    }

    SubjectSharedPtr FindNear(const SubjectSharedPtr& me, const ObjID& id)
    {
        if (!me || me->GetSectorX() < 0)
            return nullptr;
        return GSector->FindNeighbor(me->GetSectorX(), me->GetSectorY(), id);
    }

    std::vector<MonsterSharedPtr> MonstersAround(const SubjectSharedPtr& me, int cx, int cy, int radius)
    {
        std::vector<MonsterSharedPtr> out;
        if (!me || me->GetSectorX() < 0)
            return out;
        // 중심 칸이 속한 섹터 기준으로 이웃을 훑는다(시전자와 같은 Zone 안일 때).
        short sx = static_cast<short>(cx / SECTOR_RANGE), sy = static_cast<short>(cy / SECTOR_RANGE);
        if (!GSector->IsValidSector(sx, sy)) { sx = me->GetSectorX(); sy = me->GetSectorY(); }
        GSector->ForEachObjectWithin(sx, sy, cx, cy, radius, EnumCategory::eMonster, [&](const SubjectSharedPtr& object)
        {
            if (object->GetStat()->IsDead())
                return;
            if (std::abs(object->GetX() - cx) > radius || std::abs(object->GetY() - cy) > radius)
                return;
            out.push_back(static_pointer_cast<CMonster>(object));
        });
        return out;
    }

    UInt32 RollUserDamage(const UserSharedPtr& u, DamageKind kind, UInt32 coeffPercent, bool forceCrit, bool& outCrit)
    {
        const DerivedStats& d = u->m_Derived;
        const UInt32 atk = kind == DamageKind::Magic ? d.magicAttack : d.attack;
        // ±10%
        const UInt32 roll = 900 + static_cast<UInt32>(LRng() % 201);
        UInt64 dmg = static_cast<UInt64>(atk) * coeffPercent * roll / 100000u;
        outCrit = forceCrit || (static_cast<UInt32>(LRng() % 1000) < d.critChance);
        if (outCrit)
            dmg = dmg * d.critDamage / 100u;
        if (dmg < 1) dmg = 1;
        return static_cast<UInt32>(std::min<UInt64>(dmg, 60000));
    }

    UInt32 HitMonster(const UserSharedPtr& attacker, const MonsterSharedPtr& monster, UInt32 damage, const HitCtx& ctx)
    {
        if (!attacker || !monster || monster->GetStat()->IsDead())
            return 0;
        const int level = monster->CombatLevel();   // 정예면 + elite_level_bonus
        UInt32 mitigated = StatRule::Mitigate(damage, StatRule::MonsterDefense(level), attacker->m_Derived.penetration);
        // 굳건(몬스터 장판 · 기술의 강화) — 받는 피해 −세기%(최대 MonsterAoeTable::kMaxFortifyPower)
        if (const UInt32 fortify = std::min<UInt32>(monster->Status().Power(StatusId::Fortify, GetNowTime()), MonsterAoeTable::kMaxFortifyPower); fortify > 0)
            mitigated = std::max<UInt32>(1, mitigated * (100u - fortify) / 100u);
        ObjID monsterId = monster->GetObjID();
        ObjID attackerId = attacker->GetObjID();
        attacker->m_LastCombatAt = GetNowTime();
        UInt8 flags = ctx.flags;
        if (const SkillRow* row = SkillTable::Find(ctx.skillId); row != nullptr && row->slot != 0 && (flags & DAMAGE_DOT) == 0)
            flags |= DAMAGE_SKILL;
        CUserHelper::AttackMonster(monsterId, attackerId, static_cast<int>(mitigated), flags,
            ctx.skillId, ctx.comboId, ctx.flags2, ctx.hitIndex);
        return mitigated;
    }

    void ApplyGearProcs(const UserSharedPtr& u, UInt16 skillId, bool basic, DamageKind kind,
        const MonsterSharedPtr& m, UInt32 dealt, UInt8 hitIndex)
    {
        if (!u || !m)
            return;
        const GearExtras& gx = u->m_GearExtras;
        auto power = [&](ItemProc p) { return gx.procPower[static_cast<size_t>(p)]; };
        auto fires = [&](ItemProc p)
        {
            const UInt8 pw = power(p);
            return pw > 0 && static_cast<int>(LRng() % 100u) < ItemRules::ProcChance(p, pw);
        };
        const UInt32 now = GetNowTime();

        // 생명 흡수 — 준 피해의 일부를 HP로(쓰러뜨린 한 방도 포함)
        if (const UInt8 pw = power(ItemProc::Lifesteal); pw > 0 && dealt > 0 && u->GetStat()->GetHp() < u->GetStat()->GetMaxHp())
        {
            const UInt32 heal = std::max<UInt32>(1, dealt * static_cast<UInt32>(ItemRules::ProcPower(ItemProc::Lifesteal, pw)) / 100u);
            u->GetStat()->HealHp(static_cast<UInt16>(std::min<UInt32>(heal, 65535)), u->GetStat()->GetMaxHp());
            SendResource(u);
            if (now - u->m_LifestealFxAt >= 700)
            {
                u->m_LifestealFxAt = now;
                CUserHelper::BroadcastEffect(u, u->GetObjID(), EffectId::ProcLifesteal, u->GetX(), u->GetY(), static_cast<Int32>(heal));
            }
        }
        if (m->GetStat()->IsDead())
            return;

        const DerivedStats& d = u->m_Derived;
        const UInt32 atk = kind == DamageKind::Magic ? d.magicAttack : d.attack;

        // 천둥의 일격 — 벼락(마법 피해)
        if (fires(ItemProc::Thunder))
        {
            const UInt32 base = std::max<UInt32>(d.attack, d.magicAttack);
            const UInt32 dmg = std::max<UInt32>(1, base * static_cast<UInt32>(ItemRules::ProcPower(ItemProc::Thunder, power(ItemProc::Thunder))) / 100u
                * (90u + static_cast<UInt32>(LRng() % 21u)) / 100u);
            CUserHelper::BroadcastEffect(u, u->GetObjID(), EffectId::ProcThunder, m->GetX(), m->GetY(), 0);
            HitCtx ctx;
            ctx.flags = DAMAGE_SKILL;
            ctx.flags2 = DAMAGE2_PROC;
            ctx.hitIndex = static_cast<UInt8>(hitIndex + 1);
            HitMonster(u, m, dmg, ctx);
            if (m->GetStat()->IsDead())
                return;
        }
        // 메아리 — 기술(기본 공격 제외)이 한 번 더
        if (!basic && dealt > 0 && fires(ItemProc::Echo))
        {
            const UInt32 dmg = std::max<UInt32>(1, dealt * static_cast<UInt32>(ItemRules::ProcPower(ItemProc::Echo, power(ItemProc::Echo))) / 100u);
            CUserHelper::BroadcastEffect(u, u->GetObjID(), EffectId::ProcEcho, m->GetX(), m->GetY(), 0);
            HitCtx ctx;
            ctx.skillId = skillId;
            ctx.flags = DAMAGE_SKILL;
            ctx.flags2 = DAMAGE2_PROC;
            ctx.hitIndex = static_cast<UInt8>(hitIndex + 2);
            // 이미 방어가 적용된 값을 다시 방어에 넣지 않도록 관통 없이 그대로(조금 약해지는 건 의도)
            HitMonster(u, m, dmg, ctx);
            if (m->GetStat()->IsDead())
                return;
        }
        // 서리 손길 — 둔화(신화: 빙결)
        if (fires(ItemProc::FrostTouch))
        {
            if (power(ItemProc::FrostTouch) >= 2)
                ApplyStatus(m, StatusId::Freeze, 1200, 1, u->GetObjID());
            else
                ApplyStatus(m, StatusId::Slow, 2500, 1, u->GetObjID());
            CUserHelper::BroadcastEffect(u, u->GetObjID(), EffectId::ProcFrost, m->GetX(), m->GetY(), 0);
        }
        // 잔불 — 화상
        if (fires(ItemProc::Ember))
        {
            const UInt16 tick = static_cast<UInt16>(std::max<UInt32>(1, atk * StatusTable::Get(StatusId::Burn).tickPercent / 100u));
            ApplyStatus(m, StatusId::Burn, 3000, 1, u->GetObjID(), tick);
            CUserHelper::BroadcastEffect(u, u->GetObjID(), EffectId::ProcEmber, m->GetX(), m->GetY(), 0);
        }
        // 맹독 — 중독 1중첩
        if (fires(ItemProc::Venom))
        {
            const UInt16 tick = static_cast<UInt16>(std::max<UInt32>(1, atk * StatusTable::Get(StatusId::Poison).tickPercent / 100u));
            ApplyStatus(m, StatusId::Poison, 5000, 1, u->GetObjID(), tick);
            CUserHelper::BroadcastEffect(u, u->GetObjID(), EffectId::ProcVenom, m->GetX(), m->GetY(), 0);
        }
    }

    bool HitPlayerFromMonster(const MonsterSharedPtr& monster, const UserSharedPtr& victim, UInt32 raw, UInt8 flags, bool* outLanded)
    {
        const UInt32 now = GetNowTime();
        const ObjID monsterId = monster->GetObjID();
        victim->m_LastCombatAt = now;
        if (outLanded != nullptr)
            *outLanded = false;

        // 잔상 회피 · 무적(이동기 · 각성기) 중이면 전부 피한다. 잔상 회피 직후(0.3초 안)면 완벽 회피.
        if (victim->Status().Has(StatusId::Evade, now) || now < victim->m_InvulnUntil)
        {
            CUserHelper::BroadcastDamage(victim, monsterId, 0, flags, 0, 0, DAMAGE2_EVADE);
            CUserSkillHelper::OnEvaded(victim, now);
            return false;
        }
        const int monsterLevel = monster->CombatLevel();   // 정예면 + elite_level_bonus
        if (static_cast<UInt32>(LRng() % 100) < StatRule::EvadeChance(victim->m_Derived.evasion, monsterLevel))
        {
            CUserHelper::BroadcastDamage(victim, monsterId, 0, flags, 0, 0, DAMAGE2_EVADE);
            return false;
        }
        if (outLanded != nullptr)
            *outLanded = true;

        UInt32 damage = raw * kMonsterDamageScaleV2 / 100u;
        damage = StatRule::Mitigate(damage, victim->m_Derived.defense, 0);
        UInt8 flags2 = 0;
        const bool guarding = victim->Status().Has(StatusId::Guard, now);
        if (guarding)
        {
            damage = std::max<UInt32>(1, damage / 2);
            flags2 |= DAMAGE2_GUARD;
            CUserSkillHelper::AddIdentity(victim, 60);   // 서약기사: 막으면 서약이 찬다
        }

        victim->GetStat()->TakeDamage(static_cast<UInt16>(std::min<UInt32>(damage, 65535)));
        const bool dead = victim->GetStat()->IsDead();
        CUserHelper::BroadcastDamage(victim, monsterId, static_cast<Int32>(damage), static_cast<UInt8>(flags | (dead ? DAMAGE_KILLED : 0)), 0, 0, flags2);

        // 서약의 방패(장비 특수 효과) — 맞을 때 확률로 철벽 태세(재발동 대기 있음)
        if (!dead && !guarding)
        {
            const UInt8 pw = victim->m_GearExtras.procPower[static_cast<size_t>(ItemProc::Guardian)];
            if (pw > 0 && now >= victim->m_GuardianReadyAt && static_cast<int>(LRng() % 100u) < ItemRules::ProcChance(ItemProc::Guardian, pw))
            {
                victim->m_GuardianReadyAt = now + ItemRules::kGuardianCooldownMs;
                ApplyStatus(victim, StatusId::Guard, pw >= 2 ? 4000u : 3000u, 1, victim->GetObjID());
                CUserHelper::BroadcastEffect(victim, victim->GetObjID(), EffectId::ProcGuardian, victim->GetX(), victim->GetY(), 0);
            }
        }

        // 철벽 태세 반격 — 살아 있고, 간격이 지났으면 때린 몬스터에게 되돌려 준다.
        if (!dead && guarding && now - victim->m_LastCounterAt >= kCounterIntervalMs && !monster->GetStat()->IsDead())
        {
            victim->m_LastCounterAt = now;
            bool crit = false;
            // Phase 2-D 철벽 태세 갈래: 「날 선 반격」 피해 +30% · 「불꽃 반격」「울림 반격」 상태이상
            const SkillGrowth::State gs = victim->GrowState(5);
            const SkillGrowth::Variant* sharp = SkillGrowth::FindMod(5, gs, SkillGrowth::Mod::Heavy);
            const SkillGrowth::Variant* elem = SkillGrowth::FindMod(5, gs, SkillGrowth::Mod::Element);
            UInt32 counter = RollUserDamage(victim, DamageKind::Physical, sharp ? 70u * (100u + sharp->value) / 100u : 70u, false, crit);
            counter = counter * SkillGrowth::DamagePercent(gs.level) / 100u;
            CUserHelper::BroadcastEffect(victim, victim->GetObjID(), EffectId::Counter, monster->GetX(), monster->GetY(), 0);
            HitCtx ctx;
            ctx.skillId = 5;
            ctx.comboId = 8;
            ctx.flags = static_cast<UInt8>(DAMAGE_COMBO | (crit ? DAMAGE_CRIT : 0));
            ctx.flags2 = DAMAGE2_COUNTER;
            HitMonster(victim, monster, counter, ctx);
            if (elem && !monster->GetStat()->IsDead())
            {
                const StatusRow& st = StatusTable::Get(elem->status);
                const UInt16 tick = st.tickMs > 0 ? static_cast<UInt16>(std::max<UInt32>(1, victim->m_Derived.attack * st.tickPercent / 100u)) : 0;
                ApplyStatus(monster, elem->status, SkillGrowth::DefaultStatusMs(elem->status), 1, victim->GetObjID(), tick);
            }
        }
        return dead;
    }

    bool IsUntouchable(const UserSharedPtr& victim, UInt32 now)
    {
        return victim->Status().Has(StatusId::Evade, now) || now < victim->m_InvulnUntil;
    }

    bool HitPlayerFromAoE(const ObjID& sourceId, const UserSharedPtr& victim, UInt32 raw, UInt16 dotStyle, bool* outLanded)
    {
        const UInt32 now = GetNowTime();
        victim->m_LastCombatAt = now;
        if (outLanded != nullptr)
            *outLanded = false;
        // 무적(이동기 · 각성기) · 잔상 회피 중이면 닿지 않는다
        if (IsUntouchable(victim, now))
        {
            CUserHelper::BroadcastDamage(victim, sourceId, 0, DAMAGE_DOT, dotStyle, 0, DAMAGE2_EVADE);
            return false;
        }
        if (outLanded != nullptr)
            *outLanded = true;
        UInt32 damage = raw * kMonsterDamageScaleV2 / 100u;
        damage = StatRule::Mitigate(damage, victim->m_Derived.defense, 0);
        UInt8 flags2 = 0;
        if (victim->Status().Has(StatusId::Guard, now))
        {
            damage = std::max<UInt32>(1, damage / 2);
            flags2 |= DAMAGE2_GUARD;
        }
        victim->GetStat()->TakeDamage(static_cast<UInt16>(std::min<UInt32>(damage, 65535)));
        const bool dead = victim->GetStat()->IsDead();
        CUserHelper::BroadcastDamage(victim, sourceId, static_cast<Int32>(damage), static_cast<UInt8>(DAMAGE_DOT | (dead ? DAMAGE_KILLED : 0)), dotStyle, 0, flags2);
        return dead;
    }

    void ApplyStatus(const SubjectSharedPtr& target, StatusId id, UInt32 durationMs, UInt8 stacks, const ObjID& source, UInt16 tickDamage, UInt16 power)
    {
        if (!target || target->GetStat()->IsDead() || id == StatusId::None || durationMs == 0)
            return;
        const UInt32 now = GetNowTime();
        // 상태이상 저항(플레이어) — 해로운 상태의 지속 시간을 줄인다.
        if (StatusTable::Get(id).debuff && target->GetObjID().GetCategory<EnumCategory>() == EnumCategory::eUser)
        {
            const auto user = static_pointer_cast<CUser>(target);
            // Phase 2-B 슈퍼아머(홀딩 · 차지 · 서약 해방) · 무적: 몬스터가 거는 기절 · 빙결을 받지 않는다
            if ((id == StatusId::Stun || id == StatusId::Freeze) && source.GetCategory<EnumCategory>() == EnumCategory::eMonster
                && (now < user->m_SuperArmorUntil || now < user->m_InvulnUntil || user->Status().Has(StatusId::Oath, now)))
                return;
            durationMs = durationMs * (100u - std::min<UInt32>(user->m_Derived.statusResist, 60)) / 100u;
        }
        // 보스는 기절·빙결을 짧게 받는다(연계는 되지만 무한 제어는 안 되게).
        if (target->GetObjID().GetCategory<EnumCategory>() == EnumCategory::eMonster && (id == StatusId::Stun || id == StatusId::Freeze))
        {
            const auto monster = static_pointer_cast<CMonster>(target);
            if (monster->IsBoss())
                durationMs /= 3;
        }
        const bool hadTicks = [&]()
        {
            for (auto& s : target->Status().Slots())
                if (s.stacks > 0 && now < s.until && StatusTable::Get(s.id).tickMs > 0) return true;
            return false;
        }();
        StatusSlot& slot = target->Status().Apply(id, now, durationMs, stacks, source, tickDamage, power);
        BroadcastStatus(target, id, slot.stacks, durationMs, source);

        // 지속 피해가 새로 시작됐으면 틱 타이머를 건다(이미 돌고 있으면 그대로 둔다).
        if (StatusTable::Get(id).tickMs > 0 && !hadTicks)
            GTimerThread->ScheduleAfter(target->GetObjID(), std::chrono::milliseconds(StatusTable::Get(id).tickMs), TIMER_EVENT_TYPE::EV_STATUS_TICK);

        // 제어기에 걸린 몬스터는 경로를 다시 찾게 하고(밀려났거나 멈췄으므로), 시전 중이면 곧바로 생각해 끊는다
        if (target->GetObjID().GetCategory<EnumCategory>() == EnumCategory::eMonster && (StatusTable::Get(id).blocksMove || StatusTable::Get(id).blocksAttack))
        {
            const auto monster = static_pointer_cast<CMonster>(target);
            monster->ClearPath();
            CEntityBehaviorHelper::OnDisabled(monster);
        }
    }

    void RemoveStatus(const SubjectSharedPtr& target, StatusId id)
    {
        if (!target)
            return;
        const UInt32 now = GetNowTime();
        if (!target->Status().Has(id, now))
            return;
        target->Status().Remove(id);
        BroadcastStatus(target, id, 0, 0, ObjID{});
    }

    void HandleStatusTick(const ObjID& subjectId)
    {
        auto target = ::GetGameObject<CSubject>(subjectId);
        if (!target || target->GetStat()->IsDead() || target->GetSectorX() < 0)
            return;
        const UInt32 now = GetNowTime();
        UInt32 nextAt = 0;

        // 틱 피해를 모은 뒤 적용(적용 중에 죽으면 슬롯이 지워지므로)
        struct Tick { ObjID source; UInt32 damage; StatusId id; };
        std::vector<Tick> ticks;
        for (auto& s : target->Status().Slots())
        {
            const StatusRow& row = StatusTable::Get(s.id);
            if (s.stacks == 0 || now >= s.until || row.tickMs == 0)
                continue;
            while (s.nextTick != 0 && s.nextTick <= now && s.nextTick <= s.until)
            {
                ticks.push_back(Tick{ s.source, static_cast<UInt32>(s.tickDamage) * s.stacks, s.id });
                s.nextTick += row.tickMs;
            }
            if (s.nextTick <= s.until && (nextAt == 0 || s.nextTick < nextAt))
                nextAt = s.nextTick;
        }

        for (const Tick& t : ticks)
        {
            if (target->GetStat()->IsDead())
                break;
            if (target->GetObjID().GetCategory<EnumCategory>() == EnumCategory::eMonster)
            {
                auto attacker = ::GetGameObject<CUser>(t.source);
                if (!attacker || attacker->GetZoneId() != target->GetZoneId())
                    continue;
                HitCtx ctx;
                ctx.flags = DAMAGE_DOT;
                ctx.skillId = t.id == StatusId::Burn ? 9 : 15;
                HitMonster(attacker, static_pointer_cast<CMonster>(target), std::max<UInt32>(1, t.damage), ctx);
            }
            else if (target->GetObjID().GetCategory<EnumCategory>() == EnumCategory::eUser
                && t.source.GetCategory<EnumCategory>() == EnumCategory::eMonster)
            {
                // 몬스터가 건 화상 · 중독(기술 · 장판) — 방어만 적용(마을에서는 닳지 않는다)
                const auto victim = static_pointer_cast<CUser>(target);
                if (WorldData::IsSafeZone(victim->GetX(), victim->GetY()))
                    continue;
                const UInt32 damage = std::max<UInt32>(1, StatRule::Mitigate(t.damage, victim->m_Derived.defense, 0));
                victim->GetStat()->TakeDamage(static_cast<UInt16>(std::min<UInt32>(damage, 65535)));
                const bool dead = victim->GetStat()->IsDead();
                CUserHelper::BroadcastDamage(victim, t.source, static_cast<Int32>(damage), static_cast<UInt8>(DAMAGE_DOT | (dead ? DAMAGE_KILLED : 0)),
                    static_cast<UInt16>(t.id == StatusId::Burn ? 9 : 15));
                if (dead)
                {
                    CContentHelper::HandlePlayerDeath(victim, t.source, false);
                    return;
                }
            }
        }

        if (!target->GetStat()->IsDead() && nextAt != 0)
        {
            const UInt32 wait = nextAt > now ? nextAt - now : 1;
            GTimerThread->ScheduleAfter(subjectId, std::chrono::milliseconds(wait), TIMER_EVENT_TYPE::EV_STATUS_TICK);
        }
    }

    bool Knockback(const MonsterSharedPtr& target, int fromX, int fromY, int tiles, bool& outWall)
    {
        outWall = false;
        if (!target || target->GetStat()->IsDead() || tiles <= 0)
            return false;
        if (target->IsBoss() || target->IsDummy())
            return false;   // 보스 · 허수아비는 밀리지 않는다(허수아비는 안전지대 안이라 늘 "벽"이 된다)
        int dx = Sign(target->GetX() - fromX), dy = Sign(target->GetY() - fromY);
        if (dx == 0 && dy == 0)
            dx = target->IsFacingLeft() ? 1 : -1;
        int x = target->GetX(), y = target->GetY();
        for (int i = 0; i < tiles; ++i)
        {
            const short nx = static_cast<short>(x + dx);
            const short ny = static_cast<short>(y + dy);
            // 대각이면 두 옆도 열려야 한다(Phase 4-A 모서리 규칙) — 막힌 두 칸 사이로 빠지지 않고 벽에 부딪힌다
            if (!CZoneLayout::IsValidWorldPosition(nx, ny) || !MoveRule::CleanStep(x, y, dx, dy, &CollisionAt)
                || WorldData::IsSafeZone(nx, ny) || CZoneLayout::GetZoneIdByWorld(nx, ny) != target->GetZoneId())
            {
                outWall = true;
                break;
            }
            x = nx;
            y = ny;
        }
        if (x == target->GetX() && y == target->GetY())
            return false;
        // 한 칸 95ms로 한 번에 — 알림(FORCED_MOVE)이 먼저 간다(예전엔 칸마다 걷기 속도로 덮여 느리게 밀렸다)
        const UInt32 steps = static_cast<UInt32>(std::max(std::abs(x - target->GetX()), std::abs(y - target->GetY())));
        CMonsterSkillHelper::ForcedMove(target, static_cast<short>(x), static_cast<short>(y), 95u * steps, ForcedMoveKind::Knockback);
        return true;
    }

    bool Pull(const MonsterSharedPtr& target, int toX, int toY, int tiles)
    {
        if (!target || target->GetStat()->IsDead() || tiles <= 0 || target->IsBoss() || target->IsDummy())
            return false;
        int x = target->GetX(), y = target->GetY();
        for (int i = 0; i < tiles; ++i)
        {
            const int dx = Sign(toX - x), dy = Sign(toY - y);
            if (dx == 0 && dy == 0) break;
            const short nx = static_cast<short>(x + dx);
            const short ny = static_cast<short>(y + dy);
            if (nx == toX && ny == toY) break;   // 시전자 칸 바로 앞에서 멈춘다
            if (!CZoneLayout::IsValidWorldPosition(nx, ny) || !MoveRule::CleanStep(x, y, dx, dy, &CollisionAt)
                || WorldData::IsSafeZone(nx, ny) || CZoneLayout::GetZoneIdByWorld(nx, ny) != target->GetZoneId())
                break;
            x = nx;
            y = ny;
        }
        if (x == target->GetX() && y == target->GetY())
            return false;
        const UInt32 steps = static_cast<UInt32>(std::max(std::abs(x - target->GetX()), std::abs(y - target->GetY())));
        CMonsterSkillHelper::ForcedMove(target, static_cast<short>(x), static_cast<short>(y), 90u * steps, ForcedMoveKind::Pull);
        return true;
    }

    bool MovePlayerTo(const UserSharedPtr& player, short x, short y, UInt32 stepMs)
    {
        if (!player || player->GetStat()->IsDead() || player->IsTransferring())
            return false;
        if (!CZoneLayout::IsValidWorldPosition(x, y) || isCollision(x, y))
            return false;
        // 기술 이동은 Zone을 넘지 않는다 — 넘으면 이 스레드가 이어서 만지는 CUser가 다른 Zone 스레드 것이 된다
        if (CZoneLayout::GetZoneIdByWorld(x, y) != player->GetZoneId())
            return false;
        if (stepMs > 0)   // 미끄러지는 이동(돌진 · 구르기)만 — 순간이동(stepMs 0)은 선을 긋지 않는다
            GuardTownMove("dash", player->GetX(), player->GetY(), x, y);
        player->SetLastStepMs(stepMs);
        player->m_MoveClock = GetNowTime();   // 돌진 뒤 이동 예산을 새로 시작
        CSectorHelper::HandlePlayerMove(player, x, y, false);
        return true;
    }

    void SendResource(const UserSharedPtr& u)
    {
        auto session = SessionOf(u);
        if (!session)
            return;
        USER_RESOURCE_INF_PACKET packet{};
        packet.size = sizeof(packet);
        packet.type = static_cast<char>(PacketType::USER_RESOURCE_INF);
        packet.hp = u->GetStat()->GetHp();
        packet.maxhp = u->GetStat()->GetMaxHp();
        packet.mp = u->m_Mp;
        packet.maxmp = u->m_Derived.maxMp;
        session->PostSend(packet);
    }

    void SendStats(const UserSharedPtr& u)
    {
        auto session = SessionOf(u);
        if (!session)
            return;
        USER_STATS_INF_PACKET packet{};
        packet.size = sizeof(packet);
        packet.type = static_cast<char>(PacketType::USER_STATS_INF);
        packet.classId = static_cast<UInt8>(u->m_eClassId);
        packet.level = u->GetStat()->GetLevel();
        packet.base = ClassTable::BaseAt(u->m_eClassId, packet.level);
        packet.alloc = u->m_Alloc;
        for (int i = 0; i < 6; ++i) packet.gear.v[i] = u->m_Gear.stats[i];
        packet.freePoints = u->FreePoints();
        packet.stats = u->m_Derived;
        packet.mp = u->m_Mp;
        session->PostSend(packet);
    }

    void GainMp(const UserSharedPtr& u, int amount)
    {
        if (!u || amount == 0)
            return;
        const int mp = std::clamp<int>(static_cast<int>(u->m_Mp) + amount, 0, u->m_Derived.maxMp);
        if (mp == u->m_Mp)
            return;
        u->m_Mp = static_cast<UInt16>(mp);
        SendResource(u);
    }

    void RefreshStats(const UserSharedPtr& u, bool fullHeal)
    {
        if (!u)
            return;
        u->RecomputeStats(fullHeal);
        SendStats(u);
        SendResource(u);
        CUserHelper::BroadcastInfo(u);
    }

    void HandleStatAlloc(const UserSharedPtr& u, UInt8 stat, UInt8 amount)
    {
        if (!u || stat >= static_cast<UInt8>(StatKind::Count) || amount == 0)
            return;
        const UInt16 free = u->FreePoints();
        const UInt16 use = std::min<UInt16>(free, std::min<UInt8>(amount, 10));
        if (use == 0)
        {
            SendStats(u);
            return;
        }
        u->m_Alloc.v[stat] = static_cast<Int16>(u->m_Alloc.v[stat] + use);
        RefreshStats(u, false);
        CUserHelper::SaveProgress(u);
    }
}
