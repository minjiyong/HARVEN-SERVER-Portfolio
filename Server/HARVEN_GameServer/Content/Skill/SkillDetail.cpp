#include "pch.h"
#include "Content/Skill/SkillDetail.h"
#include "Content/Skill/UserSkillHelper.h"
#include "Content/Combat/CombatHelper.h"
#include "User/User.h"
#include "Entity/Subject/Monster/Monster.h"
#include "User/UserHelper.h"
#include "Entity/Subject/Monster/MonsterHelper.h"
#include "Entity/Subject/SubjectHelper.h"
#include "Content/Field/Sector.h"
#include "Worker/TimerThread.h"
#include "Base/Core/CoreTLS.h"
#include "Content/Navigator/Collision.h"
#include "Content/ContentHelper.h"
#include "Content/Field/ZoneLayout.h"
#include "Database/GameData/WorldData.h"
#include "Database/GameData/MoveRule.h"
#include "Entity/Component/Behavior/EntityBehaviorHelper.h"

namespace
{
    using namespace CCombatHelper;

    int Cheb(int ax, int ay, int bx, int by) { return (std::max)(std::abs(ax - bx), std::abs(ay - by)); }
    int Sign(int v) { return (v > 0) - (v < 0); }
}

namespace SkillDetail
{
    using namespace CCombatHelper;

    namespace
    {
        thread_local const HitSpec* t_legacy = nullptr;
    }
    CLegacyScope::CLegacyScope(const HitSpec& ctx) : m_pPrev(t_legacy) { t_legacy = &ctx; }
    CLegacyScope::~CLegacyScope() { t_legacy = m_pPrev; }

    void SendResult(const UserSharedPtr& u, UInt16 seq, UInt16 skillId, SkillResult r, UInt32 ms)
    {
        auto session = u->GetGameSession();
        if (!session)
            return;
        SKILL_RESULT_ACK_PACKET packet{};
        packet.size = sizeof(packet);
        packet.type = static_cast<char>(PacketType::SKILL_RESULT_ACK);
        packet.seq = seq;
        packet.skillId = skillId;
        packet.result = static_cast<UInt8>(r);
        packet.cooldownMs = ms;
        packet.mp = u->m_Mp;
        session->PostSend(packet);
    }

}

namespace SkillDetail
{
    // 대상 지정 스킬의 대상: 몬스터 또는(강제 공격한) 다른 플레이어
    SubjectSharedPtr ResolveEnemy(const UserSharedPtr& u, const ObjID& targetId)
    {
        if (targetId == ObjID{} || targetId == u->GetObjID())
            return nullptr;
        auto target = FindNear(u, targetId);
        if (!target)
            return nullptr;
        const auto cat = target->GetObjID().GetCategory<EnumCategory>();
        if (cat != EnumCategory::eMonster && cat != EnumCategory::eUser)
            return nullptr;
        if (target->GetStat()->IsDead())
            return nullptr;
        return target;
    }

    bool IsMonster(const SubjectSharedPtr& s) { return s && s->GetObjID().GetCategory<EnumCategory>() == EnumCategory::eMonster; }

    UInt32 DealHit(const UserSharedPtr& u, const SkillRow& row, const SubjectSharedPtr& target, HitSpec spec)
    {
        if (!target || target->GetStat()->IsDead())
            return 0;
        if (t_legacy != nullptr && !spec.extras)
        {
            // Legacy 사건의 예전 판정: 표의 무력화 · 반격 · 반응 · 게이지를 싣는다
            spec.stagger = (std::max)(spec.stagger, t_legacy->stagger);
            spec.counter = spec.counter || t_legacy->counter;
            if (spec.react == HitReact::None && spec.applyKnockback) spec.react = t_legacy->react;
            spec.extras = true;
            spec.castSeq = t_legacy->castSeq;
        }
        const UInt32 now = GetNowTime();
        UInt8 comboId = spec.comboId;
        UInt32 bonus = spec.bonusPercent;

        // 표의 연계: 대상이 comboStatus 상태면 추가 피해(+ 연계 표시)
        if (spec.evaluateCombo && row.comboStatus != StatusId::None && row.comboStatus != StatusId::Ambush)
        {
            const bool has = target->Status().Has(row.comboStatus, now)
                || (row.id == 6 && target->GetStat()->GetHp() * 100u <= target->GetStat()->GetMaxHp() * 30u)
                || (row.id == 11 && (target->Status().Has(StatusId::Marked, now) || target->Status().Has(StatusId::Freeze, now)));
            if (has)
            {
                bonus += row.comboBonus;
                if (row.comboId != 0) comboId = row.comboId;
                if (row.consumeCombo)
                    RemoveStatus(target, row.comboStatus);
            }
        }

        // Phase 2-B: 함성(+15%) · 후방(+20%) · 무력화된 보스(+30%) · 기습/밤 장막(급소)
        if (spec.extras)
        {
            if (u->Status().Has(StatusId::Rally, now)) bonus += 15;
            if (u->Status().Has(StatusId::Ambush, now) && row.slot != 0)
            {
                spec.forceCrit = true;
                spec.flags2 |= DAMAGE2_BACKSTAB;
            }
            else if (IsMonster(target))
            {
                const int side = u->GetX() - target->GetX();
                const bool behind = side != 0 && ((target->IsFacingLeft() && side > 0) || (!target->IsFacingLeft() && side < 0));
                if (behind) { bonus += 20; spec.flags2 |= DAMAGE2_BACK; }
            }
        }
        if (target->Status().Has(StatusId::Broken, now)) bonus += 30;
        if (spec.counter) spec.flags2 |= DAMAGE2_COUNTER;

        bool crit = false;
        UInt32 damage = RollUserDamage(u, row.kind, spec.coeff, spec.forceCrit, crit);
        damage = damage * (100u + bonus) / 100u;
        // Phase 2-D 기술 레벨: 레벨마다 +5%(레벨 1 = 그대로)
        if (const UInt8 lv = u->GrowState(row.id).level; lv > 1)
            damage = damage * SkillGrowth::DamagePercent(lv) / 100u;

        // V2 장비: 기술 강화(그 기술 피해 +%) · 처형자(HP 30% 이하 대상 피해 +%)
        const GearExtras& gx = u->m_GearExtras;
        if (row.id < sizeof(gx.skillBoost) && gx.skillBoost[row.id] > 0)
            damage = damage * (100u + gx.skillBoost[row.id]) / 100u;
        if (const UInt8 pw = gx.procPower[static_cast<size_t>(ItemProc::Execution)];
            pw > 0 && IsMonster(target) && target->GetStat()->GetHp() * 100u <= target->GetStat()->GetMaxHp() * 30u)
        {
            damage = damage * static_cast<UInt32>(100 + ItemRules::ProcPower(ItemProc::Execution, pw)) / 100u;
            CUserHelper::BroadcastEffect(u, u->GetObjID(), EffectId::ProcExecute, target->GetX(), target->GetY(), 0);
        }

        HitCtx ctx;
        ctx.skillId = row.id;
        ctx.comboId = comboId;
        ctx.flags = static_cast<UInt8>((crit ? DAMAGE_CRIT : 0) | (comboId != 0 ? DAMAGE_COMBO : 0));
        ctx.flags2 = spec.flags2;
        ctx.hitIndex = spec.hitIndex;

        // 넉백은 피해보다 먼저 계산해 표시 플래그를 싣는다(벽 충돌이면 추가 피해·기절).
        // 확장 타격은 표의 반응(react)을 쓴다: 넉백 1칸(표에 칸이 있으면 그만큼) · 끌어당김 2칸 · 띄우기 · 넘어뜨리기(짧은 기절)
        bool knocked = false, wall = false;
        if (spec.react != HitReact::None && IsMonster(target) && !target->GetStat()->IsDead())
        {
            const auto m = static_pointer_cast<CMonster>(target);
            const short fx = spec.fromX != 0 || spec.fromY != 0 ? spec.fromX : u->GetX();
            const short fy = spec.fromX != 0 || spec.fromY != 0 ? spec.fromY : u->GetY();
            switch (spec.react)
            {
            case HitReact::Knockback:
                knocked = Knockback(m, fx, fy, row.knockback > 0 ? row.knockback : 1, wall);
                break;
            case HitReact::Pull:
                knocked = Pull(m, fx, fy, 2);
                break;
            default:
                break;
            }
            if (knocked) ctx.flags |= DAMAGE_KNOCKBACK;
        }
        else if (spec.applyKnockback && row.knockback > 0 && IsMonster(target))
        {
            knocked = Knockback(static_pointer_cast<CMonster>(target), u->GetX(), u->GetY(), row.knockback, wall);
            if (knocked) ctx.flags |= DAMAGE_KNOCKBACK;
            if (wall)
            {
                damage = damage * 130u / 100u;
                ctx.flags2 |= DAMAGE2_WALL;
                if (comboId == 0) { ctx.comboId = 9; ctx.flags |= DAMAGE_COMBO; }
                CUserHelper::BroadcastEffect(u, target->GetObjID(), EffectId::WallSlam, target->GetX(), target->GetY(), 0);
            }
        }

        UInt32 dealt = 0;
        if (IsMonster(target))
        {
            const auto monster = static_pointer_cast<CMonster>(target);
            // 반격: 끊을 수 있는 기술을 예고 중이면 끊긴다(곧바로 생각해 취소 — CMonsterSkillHelper::Advance)
            if (spec.counter)
                CEntityBehaviorHelper::OnCountered(monster);
            dealt = HitMonster(u, monster, damage, ctx);
            ApplyGearProcs(u, row.id, row.slot == 0, row.kind, monster, dealt, spec.hitIndex);
            if (spec.stagger > 0)
                CUserSkillHelper::ApplyStagger(u, monster, spec.stagger);
        }
        else
        {
            dealt = damage * 7u / 10u;
            CContentHelper::ApplyPvpDamage(u, static_pointer_cast<CUser>(target), dealt, ctx.flags, ctx.skillId, ctx.comboId, ctx.flags2, ctx.hitIndex);
        }
        if (spec.extras)
            CUserSkillHelper::OnHitDealt(u, row, target, dealt, ctx.flags2, spec.castSeq);

        if (target->GetStat()->IsDead())
            return dealt;
        if (spec.applyStatus && row.status != StatusId::None && row.status != StatusId::Ambush && row.status != StatusId::Guard && row.status != StatusId::Evade)
        {
            const StatusRow& st = StatusTable::Get(row.status);
            const UInt16 tick = st.tickMs > 0
                ? static_cast<UInt16>(std::max<UInt32>(1, (row.kind == DamageKind::Magic ? u->m_Derived.magicAttack : u->m_Derived.attack) * st.tickPercent / 100u))
                : 0;
            ApplyStatus(target, row.status, row.statusMs, std::max<UInt8>(1, row.statusStacks), u->GetObjID(), tick);
        }
        if (wall)
            ApplyStatus(target, StatusId::Stun, 900, 1, u->GetObjID());
        // 띄우기 · 넘어뜨리기: 잠깐 움직이지 못한다(보스는 무력화 게이지로만)
        if (IsMonster(target) && !static_pointer_cast<CMonster>(target)->IsBoss())
        {
            if (spec.react == HitReact::Launch) ApplyStatus(target, StatusId::Stun, 550, 1, u->GetObjID());
            else if (spec.react == HitReact::Knockdown) ApplyStatus(target, StatusId::Stun, 800, 1, u->GetObjID());
        }
        return dealt;
    }

    // 방향 단위 벡터(8방향)
    void DirTo(int fromX, int fromY, int toX, int toY, int& dx, int& dy)
    {
        dx = Sign(toX - fromX);
        dy = Sign(toY - fromY);
    }

    bool Walkable(int x, int y)
    {
        return CZoneLayout::IsValidWorldPosition(static_cast<short>(x), static_cast<short>(y)) && !isCollision(static_cast<short>(x), static_cast<short>(y));
    }

    bool StepOk(int x, int y, int dx, int dy)
    {
        // Phase 4-A: 도착 칸만 보던 것을 모서리 규칙으로 — 막힌 두 칸 사이 · 절벽 가장자리 모서리를 대각으로 빠져나가지 않는다
        return MoveRule::CleanStep(x, y, dx, dy, [](int bx, int by) { return !Walkable(bx, by); });
    }

    bool InSameZone(const UserSharedPtr& u, int x, int y)
    {
        return CZoneLayout::IsValidWorldPosition(x, y) && CZoneLayout::GetZoneIdByWorld(x, y) == u->GetZoneId();
    }

    MonsterSharedPtr MonsterAt(const UserSharedPtr& u, int x, int y)
    {
        for (auto& m : MonstersAround(u, x, y, 0))
            if (m->GetX() == x && m->GetY() == y)
                return m;
        return nullptr;
    }

    // ── 판정 ────────────────────────────────────────────────────────────────
    void Execute(const UserSharedPtr& u, const SkillRow& row, const ObjID& targetId, short tx, short ty)
    {
        const UInt32 now = GetNowTime();
        const int ux = u->GetX(), uy = u->GetY();
        switch (row.id)
        {
        // ── 기본 공격 ─────────────────────────────────────────────
        case 1:   // 검격
        case 7:   // 각인탄
        {
            auto target = ResolveEnemy(u, targetId);
            if (!target) return;
            HitSpec spec;
            spec.coeff = row.coeff;
            DealHit(u, row, target, spec);
            GainMp(u, row.id == 7 ? 3 : 2);
            return;
        }
        case 13:  // 연속 찌르기(2연타) — 기습 중이면 둘 다 확정 치명타
        {
            auto target = ResolveEnemy(u, targetId);
            if (!target) return;
            const bool ambush = u->Status().Has(StatusId::Ambush, now);
            for (UInt8 i = 0; i < row.hits && !target->GetStat()->IsDead(); ++i)
            {
                HitSpec spec;
                spec.coeff = row.coeff;
                spec.hitIndex = i;
                spec.forceCrit = ambush;
                if (ambush) { spec.comboId = 6; spec.flags2 = DAMAGE2_BACKSTAB; }
                DealHit(u, row, target, spec);
            }
            if (ambush) RemoveStatus(u, StatusId::Ambush);
            GainMp(u, 2);
            return;
        }

        // ── 서약기사 ───────────────────────────────────────────
        case 2:   // 방패 강타
        case 15:  // 독 칼날
        case 6:   // 단죄(예고 뒤)
        {
            auto target = ResolveEnemy(u, targetId);
            if (!target) return;
            if (Cheb(ux, uy, target->GetX(), target->GetY()) > row.range + 1)
                return;   // 예고 사이 멀어졌다 — 헛침(피한 것)
            HitSpec spec;
            spec.coeff = row.coeff;
            if (row.id == 6) spec.flags2 = DAMAGE2_HEAVY;
            DealHit(u, row, target, spec);
            return;
        }
        case 3:   // 회전 베기
        case 9:   // 화염 고리
        case 18:  // 그믐 베기
        {
            auto targets = MonstersAround(u, ux, uy, row.radius);
            if (row.id == 18)
            {
                // 다섯 번 벤다. 첫 적중에 대상의 중독 중첩을 터뜨린다(중첩당 +30%).
                std::vector<UInt8> poison(targets.size());
                for (size_t k = 0; k < targets.size(); ++k)
                {
                    poison[k] = targets[k]->Status().Stacks(StatusId::Poison, now);
                    if (poison[k] > 0)
                    {
                        RemoveStatus(targets[k], StatusId::Poison);
                        CUserHelper::BroadcastEffect(u, targets[k]->GetObjID(), EffectId::PoisonBurst, targets[k]->GetX(), targets[k]->GetY(), poison[k]);
                    }
                }
                for (UInt8 i = 0; i < row.hits; ++i)
                    for (size_t k = 0; k < targets.size(); ++k)
                    {
                        HitSpec spec;
                        spec.coeff = row.coeff;
                        spec.hitIndex = i;
                        spec.evaluateCombo = false;
                        if (i == 0 && poison[k] > 0)
                        {
                            spec.bonusPercent = 30u * poison[k] * 2u;   // 첫 타에 몰아서
                            spec.comboId = 7;
                        }
                        DealHit(u, row, targets[k], spec);
                    }
                return;
            }
            for (auto& m : targets)
            {
                const bool frozen = row.id == 9 && m->Status().Has(StatusId::Freeze, now);
                HitSpec spec;
                spec.coeff = row.coeff;
                DealHit(u, row, m, spec);
                if (frozen)
                {
                    // 증발 폭발 — 얼어 있던 적 주변으로 터진다
                    CUserHelper::BroadcastEffect(u, m->GetObjID(), EffectId::SteamBurst, m->GetX(), m->GetY(), 1);
                    for (auto& n : MonstersAround(u, m->GetX(), m->GetY(), 1))
                    {
                        if (n == m) continue;
                        HitSpec burst;
                        burst.coeff = 60;
                        burst.comboId = 3;
                        burst.evaluateCombo = false;
                        burst.applyStatus = false;
                        burst.hitIndex = 1;
                        DealHit(u, row, n, burst);
                    }
                }
            }
            return;
        }
        case 4:   // 돌진 — 방향으로 최대 dash칸, 처음 부딪힌 적을 밀친다
        case 16:  // 잔상 회피 — 방향으로 dash칸 빠르게 이동 + 회피
        {
            int dx, dy;
            DirTo(ux, uy, tx, ty, dx, dy);
            if (dx == 0 && dy == 0) dx = u->IsFacingLeft() ? -1 : 1;
            int x = ux, y = uy;
            MonsterSharedPtr hit;
            for (int i = 0; i < row.dash; ++i)
            {
                const int nx = x + dx, ny = y + dy;
                if (!StepOk(x, y, dx, dy) || !InSameZone(u, nx, ny) || (row.id == 4 && WorldData::IsSafeZone(nx, ny) && !WorldData::IsTrainingYard(nx, ny)))
                    break;
                if (row.id == 4)
                {
                    if (auto m = MonsterAt(u, nx, ny)) { hit = m; break; }
                }
                x = nx; y = ny;
            }
            if (x != ux || y != uy)
                MovePlayerTo(u, static_cast<short>(x), static_cast<short>(y), 60u * static_cast<UInt32>(Cheb(ux, uy, x, y)));
            if (row.id == 16)
            {
                ApplyStatus(u, StatusId::Evade, row.statusMs, 1, u->GetObjID());
                return;
            }
            // 돌진 끝에서 한 칸 앞 적(또는 바로 옆)을 친다
            if (!hit)
                hit = MonsterAt(u, x + dx, y + dy);
            if (hit)
            {
                HitSpec spec;
                spec.coeff = row.coeff;
                spec.flags2 = DAMAGE2_HEAVY;
                DealHit(u, row, hit, spec);
            }
            return;
        }
        case 5:   // 철벽 태세
            ApplyStatus(u, StatusId::Guard, row.statusMs, 1, u->GetObjID());
            return;

        // ── 각인술사 ───────────────────────────────────────────────
        case 8:   // 서리 각인
        case 12:  // 낙성(예고 뒤)
        {
            for (auto& m : MonstersAround(u, tx, ty, row.radius))
            {
                HitSpec spec;
                spec.coeff = row.coeff;
                if (row.id == 12) spec.flags2 = DAMAGE2_HEAVY;
                DealHit(u, row, m, spec);
            }
            return;
        }
        case 10:  // 옛길 도약 — 도착은 시전 때 끝났다. 여기는 떠난 자리의 각인 폭발.
        {
            CUserHelper::BroadcastEffect(u, u->GetObjID(), EffectId::RuneExplosion, tx, ty, row.radius);
            for (auto& m : MonstersAround(u, tx, ty, row.radius))
            {
                HitSpec spec;
                spec.coeff = row.coeff;
                spec.applyKnockback = false;
                DealHit(u, row, m, spec);
            }
            return;
        }
        case 11:  // 벼락 사슬 — 대상에서 주변으로 연쇄
        {
            auto first = ResolveEnemy(u, targetId);
            if (!first) return;
            std::vector<SubjectSharedPtr> hitList{ first };
            // 튀는 수 = 2 + 행의 타수(표 1 → 3번, 예전과 같다). 갈래 「일곱 번 튐」은 ApplyRow가 타수를 늘린다
            int chains = 2 + static_cast<int>(row.hits);
            SubjectSharedPtr cur = first;
            UInt8 index = 0;
            while (cur)
            {
                HitSpec spec;
                spec.coeff = row.coeff;
                spec.hitIndex = index++;
                const bool conduct = cur->Status().Has(StatusId::Burn, now) || cur->Status().Has(StatusId::Marked, now) || cur->Status().Has(StatusId::Freeze, now);
                if (conduct) chains += 1;   // 전도 대상이면 한 번 더 튄다
                DealHit(u, row, cur, spec);
                if (--chains < 0 || hitList.size() > static_cast<size_t>(6 + row.hits))
                    break;
                // 다음: 아직 안 맞은 가장 가까운 적(반경 radius)
                SubjectSharedPtr next;
                int best = 99;
                for (auto& m : MonstersAround(u, cur->GetX(), cur->GetY(), row.radius))
                {
                    if (std::find(hitList.begin(), hitList.end(), static_pointer_cast<CSubject>(m)) != hitList.end())
                        continue;
                    const int d = Cheb(cur->GetX(), cur->GetY(), m->GetX(), m->GetY());
                    if (d < best) { best = d; next = m; }
                }
                if (!next) break;
                hitList.push_back(next);
                cur = next;
            }
            return;
        }

        // ── 밤사냥꾼 ─────────────────────────────────────────
        case 14:  // 그늘 걸음 — 대상의 등 뒤로
        {
            auto target = ResolveEnemy(u, targetId);
            if (!target) return;
            // 대상 기준으로 나와 반대편 칸 → 막혔으면 대상 주변의 빈 칸
            int dx = Sign(target->GetX() - ux), dy = Sign(target->GetY() - uy);
            if (dx == 0 && dy == 0) dx = 1;
            int bx = target->GetX() + dx, by = target->GetY() + dy;
            // 대상 칸에서 한 걸음(모서리 규칙) — 대상과 다른 층(절벽 아래 · 위)으로 내려앉지 않는다(Phase 4-A)
            const int ox0 = target->GetX(), oy0 = target->GetY();
            auto landable = [&u, ox0, oy0](int x, int y) { return StepOk(ox0, oy0, x - ox0, y - oy0) && InSameZone(u, x, y) && (!WorldData::IsSafeZone(x, y) || WorldData::IsTrainingYard(x, y)); };   // 훈련장은 예외(Phase 2-E)
            if (!landable(bx, by))
            {
                bool found = false;
                for (int oy = -1; oy <= 1 && !found; ++oy)
                    for (int ox = -1; ox <= 1 && !found; ++ox)
                    {
                        if (ox == 0 && oy == 0) continue;
                        const int cx = target->GetX() + ox, cy = target->GetY() + oy;
                        if (landable(cx, cy)) { bx = cx; by = cy; found = true; }
                    }
                if (!found) return;
            }
            MovePlayerTo(u, static_cast<short>(bx), static_cast<short>(by), 0);
            u->UpdateFacing(target->GetX() - bx);
            ApplyStatus(u, StatusId::Ambush, row.statusMs, 1, u->GetObjID());
            HitSpec spec;
            spec.coeff = row.coeff;
            spec.applyStatus = false;
            DealHit(u, row, target, spec);
            return;
        }
        case 17:  // 후방 습격 — 등 뒤(또는 기습 중)면 확정 치명타 ×1.5
        {
            auto target = ResolveEnemy(u, targetId);
            if (!target) return;
            const bool ambush = u->Status().Has(StatusId::Ambush, now);
            // 몬스터는 좌우로만 바라본다: 바라보는 쪽의 반대편에 서 있으면 "등 뒤"
            const int side = u->GetX() - target->GetX();
            const bool behind = side != 0 && ((target->IsFacingLeft() && side > 0) || (!target->IsFacingLeft() && side < 0));
            HitSpec spec;
            spec.coeff = row.coeff;
            spec.evaluateCombo = false;
            if (ambush || behind)
            {
                spec.forceCrit = true;
                spec.bonusPercent = row.comboBonus;
                spec.comboId = row.comboId;
                spec.flags2 = DAMAGE2_BACKSTAB;
            }
            DealHit(u, row, target, spec);
            if (ambush) RemoveStatus(u, StatusId::Ambush);
            return;
        }
        default:
            return;
        }
    }

    bool BlinkLanding(const UserSharedPtr& u, const SkillRow& row, short& tx, short& ty)
    {
        // 옛길 도약: 도착 칸이 걸을 수 있어야 한다. 막혔으면 가까운 쪽으로 당긴다.
        const int ux = u->GetX(), uy = u->GetY();
        int dx = Sign(tx - ux), dy = Sign(ty - uy);
        int bx = ux, by = uy;
        for (int i = 0; i < row.dash; ++i)
        {
            const int nx = bx + dx * (std::abs(tx - bx) > 0 ? 1 : 0);
            const int ny = by + dy * (std::abs(ty - by) > 0 ? 1 : 0);
            if (nx == bx && ny == by) break;
            if (!StepOk(bx, by, nx - bx, ny - by) || !InSameZone(u, nx, ny)) break;
            bx = nx; by = ny;
        }
        if (bx == ux && by == uy) return false;
        tx = static_cast<short>(bx);
        ty = static_cast<short>(by);
        return true;
    }
}
