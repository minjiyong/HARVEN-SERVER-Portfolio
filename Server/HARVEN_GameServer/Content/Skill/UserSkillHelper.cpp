#include "pch.h"
#include "Content/Skill/UserSkillHelper.h"
#include "Content/Skill/SkillDetail.h"
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
#include "Database/GameData/SkillGrowth.h"
#include "Entity/Component/Behavior/EntityBehaviorHelper.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

// ─────────────────────────────────────────────────────────────────────────────
// 판정 규칙(칸 공간)
//   원(Circle) = 체비셰프 반경(예전 MonstersAround와 같다) · 부채꼴(Cone) = 유클리드 반경 + 0.5 안, 조준 방향과의 각 ≤ b°
//   직선(Line) = 조준 방향 투영 0 < t ≤ a + 0.5, 옆 거리 ≤ b/2 + 0.35 · 고리(Ring) = b ≤ 체비셰프 ≤ a
//   maxTargets가 있으면 모양 중심에서 가까운 순서로 자른다(각인탄 · 비수 투척: 먼저 닿는 적).
// 수명(REIW CSkillBase BT와 같은 차례 — UserSkillScheduler.h)
//   ExecuteSkill(검증 · 넣기 · 재사용 대기) → Cast(알림 · 시전 창) → Fire(이동 · 특수 동작) → Hit(일정) → Terminate
// ─────────────────────────────────────────────────────────────────────────────
namespace
{
    using namespace CCombatHelper;
    using namespace SkillDetail;

    constexpr UInt32 kGraceMs = 60;         // 재사용 대기 지터 여유
    constexpr UInt32 kChargeSlackMs = 80;   // 차지 완벽 구간 판정 여유(지연 흔들림)
    constexpr UInt32 kRuneDelayMs = 1000;   // 옛길 도약 각인이 터지기까지(예전과 같다)
    constexpr UInt8  kEvLast = 0x40;        // UserCastStep.flags: 이 시전의 마지막 타격(갈래 「장판」이 여기서 남는다) — SkillReqFlag와 겹치지 않는 비트
    constexpr UInt8  kEchoStage = 0xE0;     // Legacy 사건의 stage: 갈래 「메아리」로 한 번 더(피해 50%)
    constexpr UInt8  kLingerHit = 0x7E;     // 장판 틱의 타격 번호(표 밖 — 클라는 반응 없이 숫자만)
    constexpr float kPiF = 3.14159265f;

    int Cheb(int ax, int ay, int bx, int by) { return (std::max)(std::abs(ax - bx), std::abs(ay - by)); }
    int Sign(int v) { return (v > 0) - (v < 0); }

    // 시험용 추적(HARVEN_SKILL_TRACE=1): 요청 · 타격 판정을 stderr로 — 샌드박스 녹화와 서버 시각을 맞춰 본다
    bool Trace()
    {
        static const bool on = std::getenv("HARVEN_SKILL_TRACE") != nullptr;
        return on;
    }

    struct Aim { short x = 0, y = 0; float dx = 1.f, dy = 0.f; };

    UInt8 Dir256(float dx, float dy)
    {
        const float sx = dx - dy, sy = (dx + dy) * 0.5f;
        float a = std::atan2(sy, sx);
        if (a < 0.f) a += 2.f * kPiF;
        return static_cast<UInt8>(static_cast<int>(a / (2.f * kPiF) * 256.f) & 255);
    }

    void Normalize(float& dx, float& dy, const UserSharedPtr& u)
    {
        const float l = std::sqrt(dx * dx + dy * dy);
        if (l < 1e-3f) { dx = u->IsFacingLeft() ? -1.f : 1.f; dy = 0.f; return; }
        dx /= l; dy /= l;
    }

    UInt32 CooldownOf(const UserSharedPtr& u, const SkillDef& def, const SkillRow& row)
    {
        if (def.kind == SkillKind::Basic)
            return SkillTable::BasicIntervalMs(row.cls, u->m_Derived.attackSpeed);
        return SkillDefTable::CooldownMs(def, row, u->m_Derived.cooldownReduction);
    }

    CUserSkillScheduler& Sched(const UserSharedPtr& u) { return u->GetSkillScheduler(); }

    // REIW IsCooldown — 칸(SkillKey)마다 다시 쓸 수 있는 시각
    UInt32& ReadyAt(const UserSharedPtr& u, const SkillDef& def) { return Sched(u).keyReadyAt[static_cast<size_t>(def.key)]; }

    // 누르고 있기 · 켜 두기 틱 간격 — 표는 R103(≥ 50ms)이 막지만, 검증을 건너뛴 판(핫 리로드)이나 갈래(Mod::Swift — 틱 ×0.8)로 줄어도
    // 지연 없는 틱이 연달아 걸리지 않게 거는 곳에서 한 번 더 자른다
    UInt32 TickMsOf(const SkillDef& def) { return (std::max)(static_cast<UInt32>(def.tickMs), static_cast<UInt32>(SkillDefTable::kMinTickMs)); }

    // ── 알림 ────────────────────────────────────────────────────────────────
    template <typename Packet>
    void ToViewers(const SubjectSharedPtr& center, const Packet& packet, bool includeSelf)
    {
        if (center->GetSectorX() < 0)
            return;
        // 시야(CanSee — 체비셰프 GViewRange) 안만 훑는다: 칸에 적어 둔 좌표로 걸러 범위 밖 플레이어는 약한 참조를 풀지 않는다
        GSector->ForEachObjectWithin(center->GetSectorX(), center->GetSectorY(), center->GetX(), center->GetY(), GViewRange, EnumCategory::eUser,
            [&](const SubjectSharedPtr& object)
        {
            if (object->GetObjID().GetCategory<EnumCategory>() != EnumCategory::eUser)
                return;
            if (!includeSelf && object->GetObjID() == center->GetObjID())
                return;
            auto viewer = static_pointer_cast<CUser>(object);
            auto session = viewer->GetGameSession();
            if (!session || session->m_eState != SOCKET_STATE::ST_INGAME)
                return;
            if (object != center && !CSubjectHelper::CanSee(object, center))
                return;
            session->PostSend(packet);
        });
    }

    void Notify(const UserSharedPtr& u, const SkillDef& def, SkillCastPhase phase, UInt8 stage, UInt8 flags, const Aim& aim, const ObjID& target, UInt16 castMs)
    {
        SKILL_NFY_PACKET p{};
        p.size = sizeof(p);
        p.type = static_cast<char>(PacketType::SKILL_NFY);
        p.casterId = u->GetObjID();
        p.skillId = def.id;
        p.phase = static_cast<UInt8>(phase);
        p.stage = stage;
        // 켜진 갈래(비트 1~4) — 주변 클라가 같은 복사본(SkillGrowth::ApplyDef)으로 시각표 · 연출을 만든다
        p.flags = static_cast<UInt8>((flags & ~SKILLNFY_VARIANT_MASK) | SkillGrowth::NfyBits(u->GrowState(def.id)));
        p.dir = Dir256(aim.dx, aim.dy);
        p.fromX = u->GetX();
        p.fromY = u->GetY();
        p.aimX = aim.x;
        p.aimY = aim.y;
        p.targetId = target;
        p.castMs = castMs;
        p.serverTime = GetNowTime();
        ToViewers(u, p, true);
    }

    void NotifyForcedMove(const SubjectSharedPtr& s, short fx, short fy, short tx, short ty, UInt16 ms, ForcedMoveKind kind)
    {
        SUBJECT_FORCED_MOVE_NFY_PACKET p{};
        p.size = sizeof(p);
        p.type = static_cast<char>(PacketType::SUBJECT_FORCED_MOVE_NFY);
        p.id = s->GetObjID();
        p.fromX = fx; p.fromY = fy;
        p.toX = tx; p.toY = ty;
        p.durationMs = ms;
        p.kind = static_cast<UInt8>(kind);
        ToViewers(s, p, true);
    }

    // ── 일정(REIW SetTimer) ──────────────────────────────────────────────────
    // 시각 비교는 감김(49.7일)에 안전하게
    bool Due(UInt32 now, UInt32 at) { return static_cast<Int32>(now - at) >= 0; }
    // 먼저 할 일정: 이른 시각 → 같은 시각이면 먼저 넣은 것(예전 타이머 큐가 같은 시각을 넣은 순서로 꺼내던 것과 같다)
    bool Before(const UserCastStep& a, const UserCastStep& b)
    {
        if (a.dueAt != b.dueAt)
            return static_cast<Int32>(a.dueAt - b.dueAt) < 0;
        return a.order < b.order;
    }

    // 시전이 일정 한 줄을 남긴다. 깨우기는 요청 · 깨우기 처리가 끝날 때 한 번 건다(ArmWake).
    void Schedule(const UserSharedPtr& u, UserCastStep step, UInt32 delayMs)
    {
        auto& s = Sched(u);
        step.dueAt = GetNowTime() + delayMs;
        step.order = ++s.nextOrder;
        if (UserCast* c = s.Find(step.castId))
            ++c->pending;
        s.steps.push_back(step);
    }

    UserCastStep MakeStep(const UserSharedPtr& u, UInt32 castId, const SkillDef& def, EnumSkillStep kind, const Aim& aim, const ObjID& target, UInt8 stage, UInt8 flags)
    {
        UserCastStep ev{};
        ev.castId = castId;
        ev.skillId = def.id;
        ev.target = target;
        ev.x = aim.x;
        ev.y = aim.y;
        ev.kind = kind;
        ev.stage = stage;
        ev.flags = flags;
        ev.castSeq = Sched(u).castSeq;
        ev.ox = u->GetX();
        ev.oy = u->GetY();
        ev.dx = aim.dx;
        ev.dy = aim.dy;
        ev.grow = def.id < SkillGrowth::kIdSpan ? u->m_SkillGrow[def.id] : 0;
        return ev;
    }

    // 가장 이른 일정에 깨우기를 건다 — 이미 그 전에 깨우게 걸려 있으면 그대로, 아니면 새로 건다(세대 +1: 앞의 깨우기는 낡는다)
    void ArmWake(const UserSharedPtr& u)
    {
        auto& s = Sched(u);
        if (s.steps.empty())
            return;
        UInt32 at = s.steps.front().dueAt;
        for (const UserCastStep& st : s.steps)
            if (static_cast<Int32>(st.dueAt - at) < 0)
                at = st.dueAt;
        if (s.wakeArmed && Due(at, s.wakeAt))
            return;
        s.wakeArmed = true;
        s.wakeAt = at;
        ++s.wakeEpoch;
        const UInt32 now = GetNowTime();
        const UInt32 delay = Due(now, at) ? 0u : at - now;
        GTimerThread->ScheduleAfter(u->GetObjID(), std::chrono::milliseconds(delay), TIMER_EVENT_TYPE::EV_SKILL_IMPACT,
            ObjID(EnumCategory::eNone, static_cast<UInt64>(s.wakeEpoch)));
    }

    // ── 시전 수명(REIW MakeNewSkill · Insert · T_Skill_Terminate) ────────────────
    // 시전 하나를 일정에 넣고 번호를 돌려준다
    UInt32 Begin(const UserSharedPtr& u, const SkillDef& def, EnumSkillStage stage, UInt32 now)
    {
        auto& s = Sched(u);
        UserCast c{};
        c.id = ++s.nextCastId;
        if (c.id == 0)
            c.id = ++s.nextCastId;   // 0은 "없음"
        c.skillId = def.id;
        c.kind = def.kind;
        c.stage = stage;
        c.castSeq = s.castSeq;
        c.beginAt = now;
        s.casts.push_back(c);
        return c.id;
    }

    // 남은 일정이 없고 시전 창이 닫혔으면 일정에서 뺀다(REIW OnDestroy)
    void TryTerminate(const UserSharedPtr& u, UInt32 castId)
    {
        auto& s = Sched(u);
        for (auto it = s.casts.begin(); it != s.casts.end(); ++it)
        {
            if (it->id != castId)
                continue;
            if (it->pending == 0 && it->stage != EnumSkillStage::Cast)
                s.casts.erase(it);
            return;
        }
    }

    // 넣었지만 쓰지 못한 시전(이동기가 막혔다)을 뺀다 — 일정을 남기기 전에만
    void Discard(const UserSharedPtr& u, UInt32 castId)
    {
        if (UserCast* c = Sched(u).Find(castId))
        {
            c->stage = EnumSkillStage::Terminate;
            TryTerminate(u, castId);
        }
    }

    // 발사를 마쳤다 — 남은 일정이 있으면 타격 단계, 없으면 끝
    void Fired(const UserSharedPtr& u, UInt32 castId)
    {
        if (UserCast* c = Sched(u).Find(castId))
        {
            c->stage = c->pending > 0 ? EnumSkillStage::Hit : EnumSkillStage::Terminate;
            TryTerminate(u, castId);
        }
    }

    // 시전 창의 일정(차지 시간 끝 · 홀딩 틱 · 홀딩 시간 끝 · 토글 틱)을 거둔다 — 창이 닫힌 뒤의 이 일정은
    // 예전에도 도착해서 "이미 그 시전이 아니다"로 아무 일 없이 버려졌다.
    void DropWindowSteps(const UserSharedPtr& u, UInt32 castId)
    {
        auto& s = Sched(u);
        UserCast* c = s.Find(castId);
        std::erase_if(s.steps, [&](const UserCastStep& st)
        {
            const bool window = st.castId == castId &&
                (st.kind == EnumSkillStep::ChargeTimeout || st.kind == EnumSkillStep::HoldTick ||
                 st.kind == EnumSkillStep::HoldTimeout || st.kind == EnumSkillStep::ToggleTick);
            if (window && c != nullptr && c->pending > 0)
                --c->pending;
            return window;
        });
    }

    // 캐스팅 기술의 시전 창(castMs)이 닫혔으면 타격 단계로 — 창은 일정의 castingUntil · castingSeq가 센다
    void SweepCasting(const UserSharedPtr& u, UInt32 now)
    {
        auto& s = Sched(u);
        for (size_t i = 0; i < s.casts.size();)
        {
            UserCast& c = s.casts[i];
            if (c.kind == SkillKind::Cast && c.stage == EnumSkillStage::Cast && (s.castingSeq != c.castSeq || !(s.castingUntil > now)))
            {
                c.stage = c.pending > 0 ? EnumSkillStage::Hit : EnumSkillStage::Terminate;
                if (c.pending == 0)
                {
                    s.casts.erase(s.casts.begin() + static_cast<std::ptrdiff_t>(i));
                    continue;
                }
            }
            ++i;
        }
    }

    UserCast* Charging(const UserSharedPtr& u) { return Sched(u).FindCasting(SkillKind::Charge); }
    UserCast* Holding(const UserSharedPtr& u) { return Sched(u).FindCasting(SkillKind::Hold); }
    UserCast* Toggled(const UserSharedPtr& u) { return Sched(u).FindCasting(SkillKind::Toggle); }

    // ── 모양 ────────────────────────────────────────────────────────────────
    bool Inside(const SkillHit& h, int cx, int cy, float dx, float dy, int x, int y)
    {
        return SkillDefTable::InsideHit(h, static_cast<float>(cx), static_cast<float>(cy), dx, dy, static_cast<float>(x), static_cast<float>(y));
    }

    int Reach(const SkillHit& h) { return (h.shape == HitShape::Line || h.shape == HitShape::Cone) ? h.a + 1 : (std::max)(1, static_cast<int>(h.a)); }

    // 모양 안의 적(몬스터 + 강제 공격이면 마을 밖 다른 플레이어)
    std::vector<SubjectSharedPtr> Collect(const UserSharedPtr& u, const SkillHit& h, int cx, int cy, float dx, float dy, bool force)
    {
        std::vector<SubjectSharedPtr> out;
        for (auto& m : MonstersAround(u, cx, cy, Reach(h)))
            if (Inside(h, cx, cy, dx, dy, m->GetX(), m->GetY()))
                out.push_back(m);
        if (force && u->GetSectorX() >= 0 && !WorldData::IsSafeZone(u->GetX(), u->GetY()))
        {
            GSector->ForEachNeighborObject(u->GetSectorX(), u->GetSectorY(), EnumCategory::eUser, [&](const SubjectSharedPtr& object)
            {
                if (object->GetObjID().GetCategory<EnumCategory>() != EnumCategory::eUser || object == u)
                    return;
                if (object->GetStat()->IsDead() || WorldData::IsSafeZone(object->GetX(), object->GetY()))
                    return;
                if (Inside(h, cx, cy, dx, dy, object->GetX(), object->GetY()))
                    out.push_back(object);
            });
        }
        if (h.maxTargets > 0 && out.size() > h.maxTargets)
        {
            std::sort(out.begin(), out.end(), [&](const SubjectSharedPtr& a, const SubjectSharedPtr& b)
            {
                const int da = (a->GetX() - cx) * (a->GetX() - cx) + (a->GetY() - cy) * (a->GetY() - cy);
                const int db = (b->GetX() - cx) * (b->GetX() - cx) + (b->GetY() - cy) * (b->GetY() - cy);
                return da < db;
            });
            out.resize(h.maxTargets);
        }
        return out;
    }

    HitSpec SpecOf(const SkillHit& h, int hitIdx, UInt32 castSeq, int fromX, int fromY)
    {
        HitSpec s;
        s.coeff = h.coeff;
        s.hitIndex = static_cast<UInt8>(0x80 | (hitIdx & 0x7F));
        s.applyStatus = (h.flags & HF_Status) != 0;
        s.applyKnockback = false;
        s.react = h.react;
        s.stagger = h.stagger;
        s.counter = (h.flags & HF_Counter) != 0;
        s.extras = true;
        s.castSeq = castSeq;
        s.fromX = static_cast<short>(fromX);
        s.fromY = static_cast<short>(fromY);
        if (h.flags & HF_Heavy) s.flags2 |= DAMAGE2_HEAVY;
        return s;
    }

    // 타격표 한 줄을 지금 판정한다. 돌려주는 값 = 모양 가운데 칸(갈래 「장판」이 남는 자리)
    std::pair<int, int> ApplyHitRow(const UserSharedPtr& u, const SkillRow& row, const SkillDef& def, int hitIdx, const UserCastStep& ev)
    {
        const SkillHit& h = def.hits[hitIdx];
        // 세 줄기(갈래)로 덧붙은 타격은 조준 방향을 돌린다
        float dx = ev.dx, dy = ev.dy;
        SkillDefTable::TurnDir(h, dx, dy);
        int cx = ev.x, cy = ev.y;
        switch (h.anchor)
        {
        case HitAnchor::Caster: cx = u->GetX(); cy = u->GetY(); break;
        case HitAnchor::Aim: break;
        case HitAnchor::Target:
            if (auto t = ResolveEnemy(u, ev.target); t && Cheb(t->GetX(), t->GetY(), u->GetX(), u->GetY()) <= row.range + 3)
            {
                cx = t->GetX(); cy = t->GetY();
            }
            else if (h.shape == HitShape::Target)
                return { cx, cy };   // 대상이 없어졌다(멀어졌다) — 헛침
            break;
        }
        if (h.along != 0)
        {
            cx += static_cast<int>(std::lround(dx * h.along));
            cy += static_cast<int>(std::lround(dy * h.along));
        }
        const bool force = (ev.flags & SKILLREQ_FORCE) != 0;
        // 넉백 기준: 시전자 기준 타격은 시전자, 지점 타격은 지점 가운데
        const int fx = h.anchor == HitAnchor::Caster ? u->GetX() : cx;
        const int fy = h.anchor == HitAnchor::Caster ? u->GetY() : cy;
        const auto targets = Collect(u, h, cx, cy, dx, dy, force);
        if (Trace())
            std::fprintf(stderr, "[skillcast] t=%u hit id=%u row=%d center=%d,%d dir=%.2f,%.2f targets=%zu tag=%u\n", GetNowTime(), def.id, hitIdx, cx, cy, dx, dy, targets.size(), h.tag);
        for (auto& t : targets)
            DealHit(u, row, t, SpecOf(h, hitIdx, ev.castSeq, fx, fy));
        return { cx, cy };
    }

    // ── 도움 ────────────────────────────────────────────────────────────────
    Aim ResolveAim(const UserSharedPtr& u, const SkillRow& row, const SKILL_REQ_PACKET& req, const SubjectSharedPtr& target)
    {
        Aim a;
        const SkillDef* def = SkillDefTable::Find(row.id);
        const int ux = u->GetX(), uy = u->GetY();
        int ax = req.aimX, ay = req.aimY;
        if (target && (row.target == SkillTarget::Enemy || row.target == SkillTarget::Direction))
        {
            ax = target->GetX();
            ay = target->GetY();
        }
        if (!CZoneLayout::IsValidWorldPosition(static_cast<short>(ax), static_cast<short>(ay)))
        {
            ax = ux; ay = uy;
        }
        const int reach = (std::max)(1, static_cast<int>(def && def->range > 0 ? def->range : row.range));
        const int d = Cheb(ux, uy, ax, ay);
        if (row.target == SkillTarget::Self)
        {
            ax = ux; ay = uy;
        }
        else if (d > reach)
        {
            const float k = static_cast<float>(reach) / static_cast<float>(d);
            ax = ux + static_cast<int>(std::lround((ax - ux) * k));
            ay = uy + static_cast<int>(std::lround((ay - uy) * k));
        }
        a.x = static_cast<short>(ax);
        a.y = static_cast<short>(ay);
        a.dx = static_cast<float>(req.aimX - ux);
        a.dy = static_cast<float>(req.aimY - uy);
        if (target) { a.dx = static_cast<float>(target->GetX() - ux); a.dy = static_cast<float>(target->GetY() - uy); }
        Normalize(a.dx, a.dy, u);
        if (std::fabs(a.dx) > 0.2f) u->UpdateFacing(a.dx > 0.f ? 1 : -1);
        return a;
    }

    // 타격표를 시전 castId의 일정으로 남긴다(REIW T_Skill_Hit — 줄마다 표의 시각에)
    void ScheduleHits(const UserSharedPtr& u, UInt32 castId, const SkillDef& def, const Aim& aim, const ObjID& target, UInt8 stageEq, UInt8 stageMax, bool perfect, UInt8 flags, bool releaseOnly, bool skipRelease, UInt32 offsetMs)
    {
        auto active = [&](const SkillHit& h)
        {
            if (h.flags & HF_Tick) return false;
            if (releaseOnly != ((h.flags & HF_Release) != 0)) return false;
            if (skipRelease && (h.flags & HF_Release)) return false;
            if ((h.flags & HF_Perfect) && !perfect) return false;
            if (stageEq != 0 && h.stage != stageEq) return false;
            if (stageMax != 0 && h.stage != 0 && h.stage > stageMax) return false;
            return true;
        };
        // 이 시전의 마지막 타격(덧붙은 메아리 · 세 줄기는 빼고) — 갈래 「장판」이 그 자리에 남는다
        int last = -1;
        for (int i = 0; i < def.hitCount; ++i)
            if (active(def.hits[i]) && def.hits[i].tag == HT_None && (last < 0 || def.hits[i].atMs >= def.hits[last].atMs)) last = i;
        for (int i = 0; i < def.hitCount; ++i)
        {
            const SkillHit& h = def.hits[i];
            if (!active(h)) continue;
            auto ev = MakeStep(u, castId, def, EnumSkillStep::Hit, aim, target, stageEq ? stageEq : stageMax, flags);
            ev.hit = static_cast<UInt8>(i);
            if (i == last) ev.flags = static_cast<UInt8>(ev.flags | kEvLast);
            Schedule(u, ev, offsetMs + h.atMs);
        }
    }

    // 갈래 「장판」: 주 타격 자리에 value초 동안 0.5초마다(20% · 상태이상). 반경 = radius(최소 1)
    // (REIW ExecuteNextSkill — 이 시전이 남기는 장판. 같은 시전의 일정이라 다 돌 때까지 시전이 남는다)
    void StartLinger(const UserSharedPtr& u, UInt32 castId, const SkillDef& def, UInt8 grow, int cx, int cy, int radius, UInt32 castSeq, UInt8 reqFlags)
    {
        const SkillGrowth::Variant* v = SkillGrowth::FindMod(def.id, SkillGrowth::Unpack(grow), SkillGrowth::Mod::Lasting);
        if (!v) return;
        UserCastStep ev{};
        ev.castId = castId;
        ev.skillId = def.id;
        ev.x = static_cast<short>(cx);
        ev.y = static_cast<short>(cy);
        ev.kind = EnumSkillStep::Linger;
        ev.stage = static_cast<UInt8>((std::max)(1, (std::min)(4, radius)));
        ev.flags = static_cast<UInt8>(reqFlags & SKILLREQ_FORCE);
        ev.castSeq = castSeq;
        ev.ox = u->GetX(); ev.oy = u->GetY();
        ev.grow = grow;
        ev.extra = static_cast<UInt32>(v->value) * 2u;   // 0.5초 틱 수
        Schedule(u, ev, 500);
    }

    void ApplyWindows(const UserSharedPtr& u, const SkillDef& def, UInt32 extraMs = 0)
    {
        const UInt32 now = GetNowTime();
        if (def.superArmorMs > 0) u->m_SuperArmorUntil = (std::max)(u->m_SuperArmorUntil, now + def.superArmorMs + extraMs);
        if (def.invulnMs > 0) u->m_InvulnUntil = (std::max)(u->m_InvulnUntil, now + def.invulnMs);
    }

    // ── 시전 창 닫기(REIW CancelCasting · 단계 넘김) ─────────────────────────────
    // 차지를 거둔다 — 놓으면 발사(Fire)로, 끊기면 끝(Terminate)
    void EndCharge(const UserSharedPtr& u, UserCast& c, EnumSkillStage next)
    {
        const UInt32 castId = c.id;
        c.stage = next;
        DropWindowSteps(u, castId);
        if (next == EnumSkillStage::Terminate)
            TryTerminate(u, castId);
    }

    // 홀딩을 거둔다 — 마무리(놓음 · 시간 끝)든 끊김이든 HoldEnd를 알리고 발사(Fire) 단계를 거친다
    void EndHold(const UserSharedPtr& u, UserCast& c)
    {
        c.stage = EnumSkillStage::Fire;
        DropWindowSteps(u, c.id);
    }

    // 토글을 끈다 — 재사용 대기는 끌 때부터(REIW CooldownStartType::End)
    void ToggleOff(const UserSharedPtr& u, const SkillDef& def, const SkillRow& row)
    {
        if (UserCast* t = Toggled(u))
        {
            const UInt32 castId = t->id;
            t->stage = EnumSkillStage::Terminate;
            DropWindowSteps(u, castId);
            TryTerminate(u, castId);
        }
        RemoveStatus(u, StatusId::Guard);
        const UInt32 cd = CooldownOf(u, def, row);
        ReadyAt(u, def) = GetNowTime() + cd;
        Aim a; a.x = u->GetX(); a.y = u->GetY();
        Notify(u, def, SkillCastPhase::ToggleOff, 0, 0, a, ObjID{}, 0);
    }

    // 차지를 놓는다(요청 또는 최대 시간) — 시전 창(Cast) → 발사(Fire): 놓음 알림 · 달 가르기 돌진 → 타격(Hit)
    void ReleaseCharge(const UserSharedPtr& u, const SkillDef& def, const SkillRow& row, Aim aim, bool timedOut, UInt8 reqFlags)
    {
        UserCast* c = Charging(u);
        if (c == nullptr)
            return;   // 부르는 쪽이 이미 확인했다
        const UInt32 castId = c->id;
        const UInt32 now = GetNowTime();
        const UInt32 held = now - c->beginAt;
        const UInt8 stage = SkillDefTable::ChargeStage(def, held);
        const bool perfect = !timedOut && SkillDefTable::ChargePerfect(def, held, kChargeSlackMs);
        EndCharge(u, *c, EnumSkillStage::Fire);
        auto& s = Sched(u);
        ++s.castSeq;
        ApplyWindows(u, def, 400);
        const UInt8 flags = static_cast<UInt8>(perfect ? SKILLNFY_PERFECT : 0);
        Notify(u, def, SkillCastPhase::ChargeRelease, stage, flags, aim, ObjID{}, 0);
        if (def.special == SkillSpecial::MoonCleave)
        {
            // 달 가르기: 직선 위 적을 기억하고 끝까지 돌진 → 0.3초 뒤 한꺼번에
            s.marks.clear();
            SkillHit line = def.hits[0];
            for (auto& t : Collect(u, line, u->GetX(), u->GetY(), aim.dx, aim.dy, (reqFlags & SKILLREQ_FORCE) != 0))
                s.marks.push_back(t->GetObjID());
            const int ux = u->GetX(), uy = u->GetY();
            const int sdx = static_cast<int>(std::lround(aim.dx)), sdy = static_cast<int>(std::lround(aim.dy));
            int x = ux, y = uy;
            for (int i = 0; i < row.dash; ++i)
            {
                const int nx = x + sdx, ny = y + sdy;
                if (!StepOk(x, y, sdx, sdy) || !InSameZone(u, nx, ny) || (WorldData::IsSafeZone(nx, ny) && !WorldData::IsTrainingYard(nx, ny))) break;
                x = nx; y = ny;
            }
            if (x != ux || y != uy)
            {
                MovePlayerTo(u, static_cast<short>(x), static_cast<short>(y), def.moveMs * static_cast<UInt32>(Cheb(ux, uy, x, y)));
                NotifyForcedMove(u, static_cast<short>(ux), static_cast<short>(uy), static_cast<short>(x), static_cast<short>(y),
                    static_cast<UInt16>(def.moveMs * Cheb(ux, uy, x, y)), ForcedMoveKind::Dash);
            }
            for (int i = 0; i < def.hitCount; ++i)
            {
                const SkillHit& h = def.hits[i];
                if ((h.flags & HF_Perfect) && !perfect) continue;
                if (h.stage != 0 && h.stage > stage) continue;
                auto ev = MakeStep(u, castId, def, EnumSkillStep::MarkHit, aim, ObjID{}, stage, reqFlags);
                ev.hit = static_cast<UInt8>(i);
                Schedule(u, ev, h.atMs);
            }
            Fired(u, castId);
            return;
        }
        ScheduleHits(u, castId, def, aim, ObjID{}, 0, stage, perfect, reqFlags, false, false, 0);
        Fired(u, castId);
        (void)row;
    }

    // 홀딩을 끝낸다 — 시전 창(Cast) → 발사(Fire): HoldEnd 알림 → 놓음이면 마무리 타격(Hit)
    void EndHoldWithRelease(const UserSharedPtr& u, const SkillDef& def, bool release)
    {
        UserCast* h = Holding(u);
        if (h == nullptr)
            return;   // 부르는 쪽이 이미 확인했다
        const UInt32 castId = h->id;
        Aim a; a.x = u->GetX(); a.y = u->GetY();
        EndHold(u, *h);
        ++Sched(u).castSeq;
        Notify(u, def, SkillCastPhase::HoldEnd, 0, 0, a, ObjID{}, 0);
        // 마무리 타격: 시전자 기준이면 지금 자리에서
        if (release)
            ScheduleHits(u, castId, def, a, ObjID{}, 0, 0, false, 0, true, false, 0);
        Fired(u, castId);
    }

    // ── 발사(REIW T_Skill_Fire · ExecuteBehavior) ──────────────────────────────
    // 이동기 — 못 움직이면 false(요청은 Blocked로 거절되고 아무것도 쓰지 않는다)
    bool DoMove(const UserSharedPtr& u, UInt32 castId, const SkillDef& def, const SkillRow& row, Aim& aim, const SubjectSharedPtr& target, const ObjID& targetId)
    {
        const int ux = u->GetX(), uy = u->GetY();
        switch (def.special)
        {
        case SkillSpecial::Roll:
        {
            int dx = static_cast<int>(std::lround(aim.dx)), dy = static_cast<int>(std::lround(aim.dy));
            if (dx == 0 && dy == 0) dx = u->IsFacingLeft() ? -1 : 1;
            int x = ux, y = uy;
            for (int i = 0; i < row.dash; ++i)
            {
                const int nx = x + dx, ny = y + dy;
                if (!StepOk(x, y, dx, dy) || !InSameZone(u, nx, ny)) break;
                x = nx; y = ny;
            }
            if (x != ux || y != uy)
            {
                MovePlayerTo(u, static_cast<short>(x), static_cast<short>(y), def.moveMs * static_cast<UInt32>(Cheb(ux, uy, x, y)));
                NotifyForcedMove(u, static_cast<short>(ux), static_cast<short>(uy), static_cast<short>(x), static_cast<short>(y),
                    static_cast<UInt16>(def.moveMs * Cheb(ux, uy, x, y)), ForcedMoveKind::Roll);
            }
            aim.x = static_cast<short>(x); aim.y = static_cast<short>(y);
            ApplyStatus(u, StatusId::Evade, def.invulnMs, 1, u->GetObjID());
            return true;
        }
        case SkillSpecial::Blink:
        {
            short tx = aim.x, ty = aim.y;
            if (!BlinkLanding(u, row, tx, ty)) return false;
            aim.x = tx; aim.y = ty;
            MovePlayerTo(u, tx, ty, 0);
            NotifyForcedMove(u, static_cast<short>(ux), static_cast<short>(uy), tx, ty, 0, ForcedMoveKind::Blink);
            // 떠난 자리 각인 폭발(예전 판정 그대로)
            Aim from; from.x = static_cast<short>(ux); from.y = static_cast<short>(uy);
            auto ev = MakeStep(u, castId, def, EnumSkillStep::Legacy, from, ObjID{}, 0, 0);
            Schedule(u, ev, kRuneDelayMs);
            return true;
        }
        case SkillSpecial::Dodge:
            Execute(u, row, targetId, aim.x, aim.y);
            Sched(u).dodgeAt = GetNowTime();
            aim.x = u->GetX(); aim.y = u->GetY();
            NotifyForcedMove(u, static_cast<short>(ux), static_cast<short>(uy), aim.x, aim.y, static_cast<UInt16>(def.moveMs * Cheb(ux, uy, aim.x, aim.y)), ForcedMoveKind::Dash);
            return true;
        default:
            (void)target;
            return false;
        }
    }

    // 고유 기술
    void DoIdentity(const UserSharedPtr& u, UInt32 castId, const SkillDef& def, const SkillRow& row, const Aim& aim, UInt8 flags)
    {
        const UInt32 now = GetNowTime();
        switch (def.special)
        {
        case SkillSpecial::OathRelease:
        {
            ApplyStatus(u, StatusId::Oath, row.statusMs, 1, u->GetObjID());
            u->m_SuperArmorUntil = (std::max)(u->m_SuperArmorUntil, now + row.statusMs);
            // 단죄 재사용 대기 절반
            UInt32& v = Sched(u).keyReadyAt[static_cast<size_t>(SkillKey::V)];
            if (v > now) v = now + (v - now) / 2;
            CUserHelper::BroadcastEffect(u, u->GetObjID(), EffectId::OathRelease, u->GetX(), u->GetY(), 0);
            ScheduleHits(u, castId, def, aim, ObjID{}, 0, 0, false, flags, false, false, 0);
            break;
        }
        case SkillSpecial::Resonance:
        {
            // 8칸 안 표식 대상이 0.05초 간격으로 터진다(없으면 가까운 셋)
            std::vector<MonsterSharedPtr> marked, all = MonstersAround(u, u->GetX(), u->GetY(), row.radius);
            for (auto& m : all)
                if (m->Status().Has(StatusId::Marked, now)) marked.push_back(m);
            if (marked.empty())
            {
                std::sort(all.begin(), all.end(), [&](const MonsterSharedPtr& a, const MonsterSharedPtr& b)
                {
                    return Cheb(a->GetX(), a->GetY(), u->GetX(), u->GetY()) < Cheb(b->GetX(), b->GetY(), u->GetX(), u->GetY());
                });
                for (size_t i = 0; i < all.size() && i < 3; ++i) marked.push_back(all[i]);
            }
            UInt32 delay = 250;
            UInt8 order = 0;
            for (auto& m : marked)
            {
                Aim at; at.x = m->GetX(); at.y = m->GetY();
                auto ev = MakeStep(u, castId, def, EnumSkillStep::Resonance, at, m->GetObjID(), order++, flags);
                Schedule(u, ev, delay);
                delay += 50;
            }
            break;
        }
        case SkillSpecial::Veil:
        {
            ApplyStatus(u, StatusId::Veil, row.statusMs, 1, u->GetObjID());
            ApplyStatus(u, StatusId::Ambush, row.statusMs + 2000, 1, u->GetObjID());
            // 나를 쫓던 몬스터는 놓친다
            for (auto& m : MonstersAround(u, u->GetX(), u->GetY(), 8))
                CEntityBehaviorHelper::Forget(m, u->GetObjID());
            CCombatHelper::SendStats(u);   // 이동 속도가 바뀐다
            break;
        }
        default:
            break;
        }
    }

    void HandleStep(const UserSharedPtr& u, const UserCastStep& ev);

    // 일반 · 캐스팅 · 각성기 — 특수 동작(함성 · 분신 · 안개 · 그믐 · 결계 · 돌진 · 그늘 걸음 · 예전 판정) 또는 타격표
    void DoSpecialStart(const UserSharedPtr& u, UInt32 castId, const SkillDef& def, const SkillRow& row, const Aim& aim, const ObjID& targetId, UInt8 flags)
    {
        const UInt32 now = GetNowTime();
        switch (def.special)
        {
        case SkillSpecial::Rally:
        {
            // 갈래 「긴 함성」: 8초 → 12초
            const SkillGrowth::Variant* longer = SkillGrowth::FindMod(def.id, u->GrowState(def.id), SkillGrowth::Mod::Longer);
            ApplyStatus(u, StatusId::Rally, longer ? 8000u * (100u + longer->value) / 100u : 8000u, 1, u->GetObjID());
            int n = 0;
            for (auto& m : MonstersAround(u, u->GetX(), u->GetY(), row.radius))
            {
                ApplyStatus(m, StatusId::Taunt, row.statusMs, 1, u->GetObjID());
                CEntityBehaviorHelper::OnTaunted(m, u);
                ++n;
            }
            CUserHelper::BroadcastEffect(u, u->GetObjID(), EffectId::Taunted, u->GetX(), u->GetY(), n);
            ScheduleHits(u, castId, def, aim, targetId, 0, 0, false, flags, false, false, 0);
            break;
        }
        case SkillSpecial::Clones:
            ApplyStatus(u, StatusId::Clone, row.statusMs, row.statusStacks, u->GetObjID());
            Sched(u).lastCloneCast = Sched(u).castSeq;   // 이번 시전은 세지 않는다
            break;
        case SkillSpecial::PoisonMist:
        {
            ScheduleHits(u, castId, def, aim, targetId, 0, 0, false, flags, false, false, 0);
            auto ev = MakeStep(u, castId, def, EnumSkillStep::MistTick, aim, ObjID{}, 0, flags);
            // 갈래 「긴 안개」: 틱 8번(약 4초) → 12번
            const SkillGrowth::Variant* longer = SkillGrowth::FindMod(def.id, u->GrowState(def.id), SkillGrowth::Mod::Longer);
            ev.extra = longer ? 8u * (100u + longer->value) / 100u : 8u;
            Schedule(u, ev, 700);
            break;
        }
        case SkillSpecial::DuskAwaken:
        {
            // 그믐 베기: 최대 7대상을 기억하고(원 4칸) 표의 시각에 한꺼번에
            auto& marks = Sched(u).marks;
            marks.clear();
            const SkillHit& first = def.hits[0];
            for (auto& t : Collect(u, first, u->GetX(), u->GetY(), aim.dx, aim.dy, (flags & SKILLREQ_FORCE) != 0))
                marks.push_back(t->GetObjID());
            for (int i = 0; i < def.hitCount; ++i)
            {
                auto ev = MakeStep(u, castId, def, EnumSkillStep::MarkHit, aim, ObjID{}, 0, flags);
                ev.hit = static_cast<UInt8>(i);
                Schedule(u, ev, def.hits[i].atMs);
            }
            break;
        }
        case SkillSpecial::Barrier:
            ScheduleHits(u, castId, def, aim, targetId, 0, 0, false, flags, false, false, 0);
            break;
        case SkillSpecial::Dash:
        case SkillSpecial::ShadowStep:
        {
            // 예전 판정이 곧바로 옮기고 때린다
            const int ux = u->GetX(), uy = u->GetY();
            Execute(u, row, targetId, aim.x, aim.y);
            if (u->GetX() != ux || u->GetY() != uy)
                NotifyForcedMove(u, static_cast<short>(ux), static_cast<short>(uy), u->GetX(), u->GetY(),
                    static_cast<UInt16>(def.moveMs * Cheb(ux, uy, u->GetX(), u->GetY())),
                    def.special == SkillSpecial::Dash ? ForcedMoveKind::Dash : ForcedMoveKind::Blink);
            // 갈래 「불씨 자국」 · 「먹물 웅덩이」: 도착한 자리에
            StartLinger(u, castId, def, def.id < SkillGrowth::kIdSpan ? u->m_SkillGrow[def.id] : 0, u->GetX(), u->GetY(), 1, Sched(u).castSeq, flags);
            break;
        }
        case SkillSpecial::Legacy:
        {
            auto ev = MakeStep(u, castId, def, EnumSkillStep::Legacy, aim, targetId, 0, flags);
            const UInt32 at = def.hitCount > 0 ? def.hits[0].atMs : def.castMs;
            // 갈래 「메아리」(예전 판정 기술): 같은 판정을 value×10ms 뒤 한 번 더
            if (const SkillGrowth::Variant* echo = SkillGrowth::FindMod(def.id, u->GrowState(def.id), SkillGrowth::Mod::Echo))
            {
                auto again = ev;
                again.stage = kEchoStage;
                Schedule(u, again, at + echo->value * 10u);
            }
            if (at == 0) HandleStep(u, ev);   // 시각 0이면 지금 판정한다(일정에 넣지 않는다)
            else Schedule(u, ev, at);
            break;
        }
        default:
            ScheduleHits(u, castId, def, aim, targetId, 0, 0, false, flags, false, false, 0);
            break;
        }
        (void)now;
    }
    // 일정 한 줄(REIW T_Skill_Hit · 시전 창의 틱 · 시간 끝)
    void HandleStep(const UserSharedPtr& u, const UserCastStep& ev)
    {
        const SkillRow* rowBase = SkillTable::Find(ev.skillId);
        const SkillDef* defBase = SkillDefTable::Find(ev.skillId);
        if (!rowBase || !defBase)
            return;
        // 시전 때의 갈래(ev.grow)로 — 도중에 바꿔도 이 시전은 그대로
        const SkillGrowth::State grow = SkillGrowth::Unpack(ev.grow);
        const SkillRow rowEff = SkillGrowth::ApplyRow(*rowBase, grow);
        const SkillDef defEff = SkillGrowth::ApplyDef(*defBase, *rowBase, grow);
        const SkillRow* row = &rowEff;
        const SkillDef* def = &defEff;
        const UInt32 now = GetNowTime();
        auto& s = Sched(u);
        switch (ev.kind)
        {
        case EnumSkillStep::Hit:
            if (ev.hit >= def->hitCount) return;
            // 캐스팅이 끊겼으면(이동) 캐스팅 기술의 예약은 버린다
            if (def->kind == SkillKind::Cast && s.castingSeq != ev.castSeq && def->hits[ev.hit].atMs >= def->castMs) return;
            if (u->Status().BlocksAttack(now) && def->kind != SkillKind::Awaken) return;   // 기절하면 남은 타격도 끊긴다(각성기는 무적이라 그대로)
            {
                const auto [cx, cy] = ApplyHitRow(u, *row, *def, ev.hit, ev);
                // 갈래 「장판」: 이 시전의 마지막 타격 자리에 남는다
                if (ev.flags & kEvLast)
                    StartLinger(u, ev.castId, *def, ev.grow, cx, cy, def->hits[ev.hit].shape == HitShape::Target ? 1 : def->hits[ev.hit].a, ev.castSeq, ev.flags);
            }
            if (def->special == SkillSpecial::Barrier)
            {
                // 결계 고리: 안의 적에게 표식
                SkillHit inner = def->hits[ev.hit];
                inner.shape = HitShape::Circle;
                const SkillGrowth::Variant* longer = SkillGrowth::FindMod(def->id, grow, SkillGrowth::Mod::Longer);
                for (auto& t : Collect(u, inner, ev.x, ev.y, ev.dx, ev.dy, false))
                    ApplyStatus(t, StatusId::Marked, longer ? 5000u * (100u + longer->value) / 100u : 5000u, 1, u->GetObjID());
            }
            if (def->hits[ev.hit].flags & HF_Final)
                if (u->Status().Has(StatusId::Ambush, now) && ev.skillId != 17 && ev.skillId != 13) RemoveStatus(u, StatusId::Ambush);
            return;
        case EnumSkillStep::Legacy:
        {
            // 예전 판정 — 표의 무력화 · 반격을 함께 싣는다(DealHit이 읽는 확장 문맥)
            SkillDetail::HitSpec ctx;
            if (def->hitCount > 0)
            {
                ctx.stagger = def->hits[0].stagger;
                ctx.counter = (def->hits[0].flags & HF_Counter) != 0;
                ctx.react = def->hits[0].react;
            }
            ctx.extras = true;
            ctx.castSeq = ev.castSeq;
            SkillDetail::CLegacyScope scope(ctx);
            if (Trace())
                std::fprintf(stderr, "[skillcast] t=%u legacy id=%u echo=%d at=%d,%d grow=0x%02X\n", GetNowTime(), ev.skillId, ev.stage == kEchoStage ? 1 : 0, ev.x, ev.y, ev.grow);
            if (ev.stage == kEchoStage)
            {
                // 갈래 「메아리」: 같은 판정을 한 번 더(피해 50% · 밀치기 없음)
                SkillRow echo = *row;
                echo.coeff = static_cast<UInt16>(echo.coeff / 2u);
                echo.knockback = 0;
                Execute(u, echo, ev.target, ev.x, ev.y);
                return;
            }
            Execute(u, *row, ev.target, ev.x, ev.y);
            // 갈래 「장판」(서리 각인 · 화염 고리 — 예전 판정 기술은 한 번뿐이라 여기서)
            if (ev.skillId != 10)
                StartLinger(u, ev.castId, *def, ev.grow, ev.x, ev.y, row->radius, ev.castSeq, ev.flags);
            return;
        }
        case EnumSkillStep::HoldTick:
        {
            const UserCast* h = Holding(u);
            if (h == nullptr || h->id != ev.castId) return;   // 홀딩이 끝났다(그 시전이 아니다)
            const UInt32 holdStart = h->beginAt;
            if (u->Status().BlocksAttack(now)) { EndHoldWithRelease(u, *def, false); return; }
            for (int i = 0; i < def->hitCount; ++i)
                if (def->hits[i].flags & HF_Tick)
                    ApplyHitRow(u, *row, *def, i, ev);
            if (now + TickMsOf(*def) <= holdStart + def->holdMaxMs)
            {
                UserCastStep next = ev;
                Schedule(u, next, TickMsOf(*def));
            }
            return;
        }
        case EnumSkillStep::HoldTimeout:
            if (const UserCast* h = Holding(u); h != nullptr && h->id == ev.castId)
                EndHoldWithRelease(u, *def, true);
            return;
        case EnumSkillStep::ChargeTimeout:
            if (const UserCast* c = Charging(u); c != nullptr && c->id == ev.castId)
            {
                Aim a; a.x = ev.x; a.y = ev.y; a.dx = ev.dx; a.dy = ev.dy;
                ReleaseCharge(u, *def, *row, a, true, ev.flags);
            }
            return;
        case EnumSkillStep::ToggleTick:
        {
            if (const UserCast* t = Toggled(u); t == nullptr || t->id != ev.castId) return;   // 꺼졌다
            if (!u->Status().Has(StatusId::Guard, now) || u->m_Mp < def->tickMp) { ToggleOff(u, *def, *row); return; }
            u->m_Mp = static_cast<UInt16>(u->m_Mp - def->tickMp);
            SendResource(u);
            Schedule(u, ev, TickMsOf(*def));
            return;
        }
        case EnumSkillStep::MistTick:
        {
            SkillHit h = def->hits[0];
            h.coeff = 12;
            h.stagger = 1;
            for (auto& t : Collect(u, h, ev.x, ev.y, ev.dx, ev.dy, (ev.flags & SKILLREQ_FORCE) != 0))
            {
                DealHit(u, *row, t, SpecOf(h, 0, ev.castSeq, ev.x, ev.y));
                ApplyStatus(t, StatusId::Slow, 1200, 1, u->GetObjID());
            }
            if (ev.extra > 1)
            {
                UserCastStep next = ev;
                next.extra = ev.extra - 1;
                Schedule(u, next, 450);
            }
            return;
        }
        case EnumSkillStep::Resonance:
        {
            auto t = ResolveEnemy(u, ev.target);
            const int cx = t ? t->GetX() : ev.x, cy = t ? t->GetY() : ev.y;
            CUserHelper::BroadcastEffect(u, u->GetObjID(), EffectId::Resonance, static_cast<short>(cx), static_cast<short>(cy), ev.stage);
            SkillHit h{};
            h.coeff = row->coeff;
            h.shape = HitShape::Circle;
            h.a = 1;
            h.react = HitReact::Launch;
            h.stagger = 20;
            for (auto& v : Collect(u, h, cx, cy, ev.dx, ev.dy, false))
            {
                DealHit(u, *row, v, SpecOf(h, 0, ev.castSeq, cx, cy));
                RemoveStatus(v, StatusId::Marked);
            }
            return;
        }
        case EnumSkillStep::Bounce:
        {
            // 방패 투척 1단: 맞히고 다음 적(3칸 안, 아직 안 맞은 가장 가까운)으로 튄다 — 최대 3번
            auto t = ResolveEnemy(u, ev.target);
            if (!t) return;
            s.marks.push_back(t->GetObjID());
            DealHit(u, *row, t, SpecOf(def->hits[0], 0, ev.castSeq, u->GetX(), u->GetY()));
            // 갈래 「다섯 번 튕김」
            const SkillGrowth::Variant* more = SkillGrowth::FindMod(def->id, grow, SkillGrowth::Mod::Extra);
            if (ev.extra + 1 >= 3u + (more ? more->value : 0u)) return;
            SubjectSharedPtr next;
            int best = 99;
            for (auto& m : MonstersAround(u, t->GetX(), t->GetY(), row->radius))
            {
                if (std::find(s.marks.begin(), s.marks.end(), m->GetObjID()) != s.marks.end()) continue;
                const int d = Cheb(t->GetX(), t->GetY(), m->GetX(), m->GetY());
                if (d < best) { best = d; next = m; }
            }
            if (!next) return;
            UserCastStep n = ev;
            n.target = next->GetObjID();
            n.x = next->GetX();
            n.y = next->GetY();
            n.extra = ev.extra + 1;
            Schedule(u, n, 110);
            return;
        }
        case EnumSkillStep::MarkHit:
        {
            if (ev.hit >= def->hitCount) return;
            const SkillHit& h = def->hits[ev.hit];
            // 그믐 베기 첫 타: 중독을 터뜨린다(중첩당 +60% — 예전 독 폭발 규칙)
            const bool dusk = def->special == SkillSpecial::DuskAwaken;
            auto hitOne = [&](const ObjID& id)
            {
                auto t = ResolveEnemy(u, id);
                if (!t || Cheb(t->GetX(), t->GetY(), u->GetX(), u->GetY()) > 7) return;
                HitSpec spec = SpecOf(h, ev.hit, ev.castSeq, u->GetX(), u->GetY());
                if (dusk && ev.hit == 0)
                {
                    const UInt8 poison = t->Status().Stacks(StatusId::Poison, now);
                    if (poison > 0)
                    {
                        RemoveStatus(t, StatusId::Poison);
                        CUserHelper::BroadcastEffect(u, t->GetObjID(), EffectId::PoisonBurst, t->GetX(), t->GetY(), poison);
                        spec.bonusPercent = 60u * poison;
                        spec.comboId = 7;
                        spec.evaluateCombo = false;
                    }
                }
                // 방패 되튕김의 끌어당김 알림(Pull)은 CCombatHelper::Pull이 옮기기 전에 직접 보낸다
                DealHit(u, *row, t, spec);
            };
            if (def->special == SkillSpecial::ShieldBounce)
                hitOne(ev.target);
            else
                for (const ObjID& id : std::vector<ObjID>(s.marks)) hitOne(id);
            return;
        }
        case EnumSkillStep::Linger:
        {
            // 갈래 「장판」 틱: 반경 ev.stage · 20% · 갈래의 상태이상(연계 없음 · 밀치기 없음)
            const SkillGrowth::Variant* v = SkillGrowth::FindMod(def->id, grow, SkillGrowth::Mod::Lasting);
            if (!v) return;
            SkillRow lr = *row;
            lr.status = v->status;
            lr.statusMs = v->status != StatusId::None ? SkillGrowth::DefaultStatusMs(v->status) : 0;
            lr.statusStacks = 1;
            lr.comboStatus = StatusId::None;
            lr.knockback = 0;
            SkillHit h{};
            h.shape = HitShape::Circle;
            h.a = ev.stage;
            h.coeff = 20;
            h.stagger = 1;
            h.flags = v->status != StatusId::None ? HF_Status : 0;
            const auto lingerTargets = Collect(u, h, ev.x, ev.y, ev.dx, ev.dy, (ev.flags & SKILLREQ_FORCE) != 0);
            if (Trace())
                std::fprintf(stderr, "[skillcast] t=%u linger id=%u center=%d,%d r=%u left=%u targets=%zu\n", GetNowTime(), ev.skillId, ev.x, ev.y, ev.stage, ev.extra, lingerTargets.size());
            for (auto& t : lingerTargets)
            {
                HitSpec spec = SpecOf(h, kLingerHit, ev.castSeq, ev.x, ev.y);
                spec.evaluateCombo = false;
                spec.applyKnockback = false;
                DealHit(u, lr, t, spec);
            }
            if (ev.extra > 1)
            {
                UserCastStep next = ev;
                next.extra = ev.extra - 1;
                Schedule(u, next, 500);
            }
            return;
        }
        case EnumSkillStep::None:
            return;
        }
    }

    // REIW CSkillBase::OnUpdate — 때가 된 일정을 시각 순서대로(같은 시각이면 넣은 순서) 처리한다
    void Advance(const UserSharedPtr& u)
    {
        auto& s = Sched(u);
        SweepCasting(u, GetNowTime());
        for (;;)
        {
            const UInt32 now = GetNowTime();
            size_t best = s.steps.size();
            for (size_t i = 0; i < s.steps.size(); ++i)
                if (Due(now, s.steps[i].dueAt) && (best == s.steps.size() || Before(s.steps[i], s.steps[best])))
                    best = i;
            if (best == s.steps.size())
                break;
            const UserCastStep step = s.steps[best];
            s.steps.erase(s.steps.begin() + static_cast<std::ptrdiff_t>(best));
            if (UserCast* c = s.Find(step.castId); c != nullptr && c->pending > 0)
                --c->pending;
            // 죽었거나 Zone을 옮기는 중이면 그 일정은 버린다(예전 타격 예약과 같다)
            if (!u->GetStat()->IsDead() && !u->IsTransferring() && u->GetSectorX() >= 0)
                HandleStep(u, step);
            TryTerminate(u, step.castId);
        }
    }

    // 요청 · 깨우기 처리가 끝나면 남은 일정 중 가장 이른 것에 깨우기를 건다
    class CArmWakeOnExit
    {
    public:
        explicit CArmWakeOnExit(const UserSharedPtr& u) noexcept : m_User(u) {}
        ~CArmWakeOnExit() { ArmWake(m_User); }
        CArmWakeOnExit(const CArmWakeOnExit&) = delete;
        CArmWakeOnExit& operator=(const CArmWakeOnExit&) = delete;
    private:
        const UserSharedPtr& m_User;
    };
}

namespace CUserSkillHelper
{
    void ExecuteSkill(const UserSharedPtr& u, const SKILL_REQ_PACKET& req)
    {
        if (!u)
            return;
        const SkillRow* rowBase = SkillTable::Find(req.skillId);
        const SkillDef* defBase = SkillDefTable::Find(req.skillId);
        if (rowBase == nullptr || defBase == nullptr)
            return;
        // Phase 2-D: 켜진 갈래를 입힌 복사본으로 판정한다(클라 예측 · 연출도 같은 복사본 — SkillGrowth::ApplyDef)
        const SkillGrowth::State grow = u->GrowState(req.skillId);
        const SkillRow rowEff = SkillGrowth::ApplyRow(*rowBase, grow);
        const SkillDef defEff = SkillGrowth::ApplyDef(*defBase, *rowBase, grow);
        const SkillRow* row = &rowEff;
        const SkillDef* def = &defEff;
        const UInt32 now = GetNowTime();
        auto fail = [&](SkillResult r, UInt32 ms = 0) { SendResult(u, req.seq, req.skillId, r, ms); };
        const auto phase = static_cast<SkillPhase>(req.phase);
        if (Trace())
            std::fprintf(stderr, "[skillcast] t=%u req id=%u phase=%u seq=%u at=%d,%d aim=%d,%d\n", now, req.skillId, req.phase, req.seq, u->GetX(), u->GetY(), req.aimX, req.aimY);
        auto& s = Sched(u);
        const CArmWakeOnExit arm(u);
        SweepCasting(u, now);

        // ── 놓음 · 취소(차지 · 홀딩) — 재사용 대기 · 자원은 누를 때 냈다 ──
        if (phase == SkillPhase::Release || phase == SkillPhase::Cancel)
        {
            if (UserCast* c = Charging(u); c != nullptr && c->skillId == def->id)
            {
                if (phase == SkillPhase::Cancel) { EndCharge(u, *c, EnumSkillStage::Terminate); Aim a; a.x = u->GetX(); a.y = u->GetY(); Notify(u, *def, SkillCastPhase::Cancel, 0, 0, a, ObjID{}, 0); return; }
                auto target = req.targetId != ObjID{} ? ResolveEnemy(u, req.targetId) : nullptr;
                ReleaseCharge(u, *def, *row, ResolveAim(u, *row, req, target), false, req.flags);
            }
            else if (const UserCast* h = Holding(u); h != nullptr && h->skillId == def->id)
                EndHoldWithRelease(u, *def, phase == SkillPhase::Release);
            return;
        }

        // ── 검증(REIW ExecuteSkill 1~3) ──
        if (u->GetStat()->IsDead() || u->IsTransferring() || u->GetSectorX() < 0) return fail(SkillResult::Busy);
        if (row->cls != u->m_eClassId) return fail(SkillResult::WrongClass);
        if (u->GetStat()->GetLevel() < row->unlockLevel) return fail(SkillResult::Locked, row->unlockLevel);
        // 안전지대에서는 싸우지 않는다 — 훈련장(Phase 2-E)만 예외(맞는 것은 허수아비뿐)
        if (WorldData::IsSafeZone(u->GetX(), u->GetY()) && !WorldData::IsTrainingYard(u->GetX(), u->GetY())) return fail(SkillResult::SafeZone);
        if (u->Status().BlocksAttack(now)) return fail(SkillResult::Stunned);
        if ((row->dash > 0 || def->kind == SkillKind::Move) && u->Status().BlocksMove(now)) return fail(SkillResult::Stunned);

        // 토글 끄기 · 체인 다음 단은 재사용 대기와 상관없다
        if (def->kind == SkillKind::Toggle)
            if (const UserCast* t = Toggled(u); t != nullptr && t->skillId == def->id)
            {
                ToggleOff(u, *def, *row);
                SendResult(u, req.seq, def->id, SkillResult::Ok, CooldownOf(u, *def, *row));
                return;
            }
        auto target = req.targetId != ObjID{} ? ResolveEnemy(u, req.targetId) : nullptr;
        if (def->kind == SkillKind::Chain && s.chainSkill == def->id && now < s.chainUntil && s.chainStage < def->stages)
        {
            // 체인 다음 단(REIW CSkillSequence 입력 창) — 새 시전 하나
            const UInt8 stage = ++s.chainStage;
            const Aim aim = ResolveAim(u, *row, req, target);
            ++s.castSeq;
            const UInt32 castId = Begin(u, *def, EnumSkillStage::Fire, now);
            Notify(u, *def, SkillCastPhase::ChainStep, stage, 0, aim, req.targetId, 0);
            if (def->special == SkillSpecial::ShieldBounce)
            {
                // 방패 투척 2단: 튕긴 적을 끌어당기며 되돌아온다
                for (size_t i = 0; i < s.marks.size(); ++i)
                {
                    auto ev = MakeStep(u, castId, *def, EnumSkillStep::MarkHit, aim, s.marks[i], stage, req.flags);
                    ev.hit = 1;
                    ev.extra = static_cast<UInt32>(i);
                    Schedule(u, ev, def->hits[1].atMs + static_cast<UInt32>(i) * 60u);
                }
            }
            else
                ScheduleHits(u, castId, *def, aim, req.targetId, stage, 0, false, req.flags, false, false, 0);
            Fired(u, castId);
            if (stage >= def->stages) s.chainSkill = 0;
            SendResult(u, req.seq, def->id, SkillResult::Ok, ReadyAt(u, *def) > now ? ReadyAt(u, *def) - now : 0);
            return;
        }
        // 이미 모으는 중 · 도는 중이면 누름은 무시(키 반복)
        if (const UserCast* c = Charging(u); c != nullptr && c->skillId == def->id)
            return;
        if (const UserCast* h = Holding(u); h != nullptr && h->skillId == def->id)
            return;

        UInt32& ready = ReadyAt(u, *def);
        if (now + kGraceMs < ready)
            return fail(SkillResult::Cooldown, ready - now);
        if (u->m_Mp < row->mp)
            return fail(SkillResult::NoMana);
        if (def->kind == SkillKind::Identity && u->m_Identity < 1000)
            return fail(SkillResult::NoGauge, u->m_Identity);
        if (row->target == SkillTarget::Enemy && def->kind != SkillKind::Basic && def->kind != SkillKind::Awaken)
        {
            if (!target) return fail(SkillResult::InvalidTarget);
            const int range = def->range > 0 ? def->range : row->range;
            const int slack = range >= 3 ? 1 : 0;
            if (Cheb(u->GetX(), u->GetY(), target->GetX(), target->GetY()) > range + slack)
                return fail(SkillResult::OutOfRange);
            if (!IsMonster(target) && ((req.flags & SKILLREQ_FORCE) == 0 || WorldData::IsSafeZone(target->GetX(), target->GetY())))
                return fail(SkillResult::SafeZone);
        }

        Aim aim = ResolveAim(u, *row, req, target);
        UInt32 castId = 0;

        // 이동기: 모으기 · 돌기 · 캐스팅을 끊고 곧바로 발사한다(움직일 칸이 없으면 Blocked — 아무것도 쓰지 않는다)
        if (def->kind == SkillKind::Move)
        {
            if (UserCast* c = Charging(u)) EndCharge(u, *c, EnumSkillStage::Terminate);
            if (const UserCast* h = Holding(u)) { if (const SkillDef* hd = SkillDefTable::Find(h->skillId)) EndHoldWithRelease(u, *hd, false); }
            s.castingUntil = 0;
            s.castingSeq = 0;
            SweepCasting(u, now);
            castId = Begin(u, *def, EnumSkillStage::Fire, now);
            if (!DoMove(u, castId, *def, *row, aim, target, req.targetId))
            {
                Discard(u, castId);
                return fail(SkillResult::Blocked);
            }
        }

        // 다른 기술을 누르면 모으던 · 돌던 기술은 끊긴다(새 기술이 받아들여질 때만 — 재사용 대기 · 자원 검사 뒤)
        if (def->kind != SkillKind::Move)
        {
            if (UserCast* c = Charging(u); c != nullptr && c->skillId != def->id)
                if (const SkillDef* cd = SkillDefTable::Find(c->skillId))
                {
                    EndCharge(u, *c, EnumSkillStage::Terminate);
                    Aim a; a.x = u->GetX(); a.y = u->GetY();
                    Notify(u, *cd, SkillCastPhase::Cancel, 0, 0, a, ObjID{}, 0);
                }
            if (const UserCast* h = Holding(u); h != nullptr && h->skillId != def->id)
                if (const SkillDef* hd = SkillDefTable::Find(h->skillId))
                    EndHoldWithRelease(u, *hd, false);
        }

        // ── 소모 + 재사용 대기(REIW StartCooldown — 누를 때부터 · 토글은 끌 때부터) ──
        ++s.castSeq;
        const UInt32 cooldown = CooldownOf(u, *def, *row);
        ready = now + cooldown;
        u->m_LastCombatAt = now;
        if (row->mp > 0)
        {
            u->m_Mp = static_cast<UInt16>(u->m_Mp - row->mp);
            SendResource(u);
        }
        if (def->kind == SkillKind::Identity) { u->m_Identity = 0; SendGauge(u, true); }
        SendResult(u, req.seq, row->id, SkillResult::Ok, cooldown);
        // 각인술사: 시전할수록 새김이 찬다
        if (u->m_eClassId == ClassId::Engraver && def->kind != SkillKind::Identity)
            AddIdentity(u, def->kind == SkillKind::Basic ? 12 : 55);
        ApplyWindows(u, *def);

        // ── 시전(알림 · 시전 창) → 발사 → 타격 일정 ──
        switch (def->kind)
        {
        case SkillKind::Basic:
        {
            // 콤보(REIW CSkillSequence): 입력 창 안이면 다음 타
            const UInt8 stage = (now - s.comboAt <= def->stageMs && s.comboStage < def->stages) ? static_cast<UInt8>(s.comboStage + 1) : 1;
            s.comboStage = stage;
            s.comboAt = now;
            castId = Begin(u, *def, EnumSkillStage::Fire, now);
            Notify(u, *def, SkillCastPhase::Start, stage, 0, aim, req.targetId, 0);
            ScheduleHits(u, castId, *def, aim, req.targetId, stage, 0, false, req.flags, false, false, 0);
            Fired(u, castId);
            break;
        }
        case SkillKind::Charge:
        {
            castId = Begin(u, *def, EnumSkillStage::Cast, now);   // 놓을 때(또는 최대 시간)까지 시전 창
            Notify(u, *def, SkillCastPhase::ChargeBegin, 0, 0, aim, req.targetId, static_cast<UInt16>(SkillDefTable::ChargeMaxMs(*def)));
            auto ev = MakeStep(u, castId, *def, EnumSkillStep::ChargeTimeout, aim, ObjID{}, 0, req.flags);
            Schedule(u, ev, SkillDefTable::ChargeMaxMs(*def) + 250);
            break;
        }
        case SkillKind::Hold:
        {
            castId = Begin(u, *def, EnumSkillStage::Cast, now);   // 놓을 때(또는 최대 시간)까지 시전 창 — 틱마다 타격
            Notify(u, *def, SkillCastPhase::HoldBegin, 0, 0, aim, req.targetId, def->holdMaxMs);
            auto tick = MakeStep(u, castId, *def, EnumSkillStep::HoldTick, aim, ObjID{}, 0, req.flags);
            Schedule(u, tick, SkillDefTable::kHoldFirstTickMs);   // 첫 0.12초 = 예고(Phase 2 4박자)
            auto timeout = MakeStep(u, castId, *def, EnumSkillStep::HoldTimeout, aim, ObjID{}, 0, req.flags);
            Schedule(u, timeout, def->holdMaxMs);
            break;
        }
        case SkillKind::Toggle:
        {
            castId = Begin(u, *def, EnumSkillStage::Cast, now);   // 끌 때까지 시전 창 — 틱마다 MP
            ApplyStatus(u, StatusId::Guard, def->holdMaxMs, 1, u->GetObjID());
            ready = now;   // 재사용 대기는 끌 때부터(REIW CooldownStartType::End)
            Notify(u, *def, SkillCastPhase::ToggleOn, 0, 0, aim, ObjID{}, def->holdMaxMs);
            auto ev = MakeStep(u, castId, *def, EnumSkillStep::ToggleTick, aim, ObjID{}, 0, 0);
            Schedule(u, ev, TickMsOf(*def));
            break;
        }
        case SkillKind::Chain:
            // 체인 1단(REIW CSkillSequence: 다음 단 입력 창을 연다)
            s.chainSkill = def->id;
            s.chainStage = 1;
            s.chainUntil = now + def->stageMs;
            castId = Begin(u, *def, EnumSkillStage::Fire, now);
            Notify(u, *def, SkillCastPhase::ChainStep, 1, 0, aim, req.targetId, 0);
            if (def->special == SkillSpecial::ShieldBounce)
            {
                s.marks.clear();
                if (target)
                {
                    auto ev = MakeStep(u, castId, *def, EnumSkillStep::Bounce, aim, target->GetObjID(), 1, req.flags);
                    ev.hit = 0;
                    ev.extra = 0;
                    Schedule(u, ev, def->hits[0].atMs);
                }
            }
            else
                ScheduleHits(u, castId, *def, aim, req.targetId, 1, 0, false, req.flags, false, false, 0);
            Fired(u, castId);
            break;
        case SkillKind::Move:
            // 발사(이동)는 위에서 끝났다
            Notify(u, *def, SkillCastPhase::Start, 0, 0, aim, req.targetId, 0);
            Fired(u, castId);
            break;
        case SkillKind::Identity:
            castId = Begin(u, *def, EnumSkillStage::Fire, now);
            Notify(u, *def, SkillCastPhase::Identity, 0, 0, aim, ObjID{}, 0);
            DoIdentity(u, castId, *def, *row, aim, req.flags);
            Fired(u, castId);
            break;
        case SkillKind::Cast:
            // 캐스팅: castMs 동안 시전 창(걸으면 끊긴다 — OnMoved). 타격은 표의 시각에(창 뒤 타격은 끊기면 버린다)
            s.castingUntil = now + def->castMs;
            s.castingSeq = s.castSeq;
            castId = Begin(u, *def, EnumSkillStage::Cast, now);
            Notify(u, *def, SkillCastPhase::Start, 0, 0, aim, req.targetId, def->castMs);
            DoSpecialStart(u, castId, *def, *row, aim, req.targetId, req.flags);
            break;
        case SkillKind::Normal:
        case SkillKind::Awaken:
            castId = Begin(u, *def, EnumSkillStage::Fire, now);
            Notify(u, *def, SkillCastPhase::Start, 0, 0, aim, req.targetId, def->castMs);
            DoSpecialStart(u, castId, *def, *row, aim, req.targetId, req.flags);
            Fired(u, castId);
            break;
        }
    }

    void HandleWake(const ObjID& casterId, UInt32 epoch)
    {
        auto u = ::GetGameObject<CUser>(casterId);
        if (!u)
            return;
        auto& s = Sched(u);
        if (!s.wakeArmed || s.wakeEpoch != epoch)
            return;   // 더 이른 깨우기가 걸렸다(그 깨우기가 처리한다)
        s.wakeArmed = false;
        const CArmWakeOnExit arm(u);
        Advance(u);
    }

    void Clear(const UserSharedPtr& u)
    {
        if (!u)
            return;
        auto& s = Sched(u);
        s.steps.clear();
        s.casts.clear();
        s.marks.clear();
        s.castingUntil = 0;
        s.castingSeq = 0;
        s.comboStage = 0;
        s.comboAt = 0;
        s.chainSkill = 0;
        s.chainStage = 0;
        s.chainUntil = 0;
        s.wakeArmed = false;
        ++s.wakeEpoch;   // 걸려 있던 깨우기는 낡는다
    }

    bool IsChanneling(const UserSharedPtr& u)
    {
        return u != nullptr && Sched(u).IsChanneling();
    }

    void OnHitDealt(const UserSharedPtr& u, const SkillRow& row, const SubjectSharedPtr& target, UInt32 dealt, UInt8 flags2, UInt32 castSeq)
    {
        if (!u || !target || dealt == 0)
            return;
        const UInt32 now = GetNowTime();
        // 고유 기술 게이지: 서약기사 = 때릴수록, 밤사냥꾼 = 뒤를 잡을수록(각인술사는 시전할 때)
        if (u->m_eClassId == ClassId::Knight && row.id != 24)
            AddIdentity(u, row.slot == 0 ? 10 : 22);
        else if (u->m_eClassId == ClassId::NightHunter && row.id != 36)
            AddIdentity(u, (flags2 & (DAMAGE2_BACKSTAB | DAMAGE2_BACK)) ? 70 : 9);

        if (!IsMonster(target) || target->GetStat()->IsDead())
            return;
        const auto monster = static_pointer_cast<CMonster>(target);
        // 서약 해방: 모든 타격에 금빛 잔향(30%)
        if (u->Status().Has(StatusId::Oath, now) && row.id != 24)
        {
            CCombatHelper::HitCtx ctx;
            ctx.skillId = 24;
            ctx.flags = DAMAGE_SKILL;
            ctx.flags2 = DAMAGE2_PROC;
            ctx.hitIndex = 0x80;
            CCombatHelper::HitMonster(u, monster, (std::max)(1u, dealt * 30u / 100u), ctx);
        }
        // 먹물 분신: 시전마다 한 번 줄이고, 그 시전의 타격을 분신 둘이 40%씩 따라 벤다
        if (row.id != 32 && !monster->GetStat()->IsDead())
            if (const UInt8 clones = u->Status().Stacks(StatusId::Clone, now); clones > 0)
            {
                if (Sched(u).lastCloneCast != castSeq)
                {
                    Sched(u).lastCloneCast = castSeq;
                    const UInt32 remain = u->Status().Remaining(StatusId::Clone, now);
                    RemoveStatus(u, StatusId::Clone);
                    if (clones > 1) ApplyStatus(u, StatusId::Clone, remain, static_cast<UInt8>(clones - 1), u->GetObjID());
                    CUserHelper::BroadcastEffect(u, u->GetObjID(), EffectId::CloneStrike, monster->GetX(), monster->GetY(), clones - 1);
                }
                // 갈래 「짙은 분신」 40% → 60%, 먹물 분신 레벨(+5%씩)
                const SkillGrowth::State cg = u->GrowState(32);
                const SkillGrowth::Variant* dense = SkillGrowth::FindMod(32, cg, SkillGrowth::Mod::Heavy);
                const UInt32 pct = (dense ? 40u * (100u + dense->value) / 100u : 40u) * SkillGrowth::DamagePercent(cg.level) / 100u;
                for (int k = 0; k < 2 && !monster->GetStat()->IsDead(); ++k)
                {
                    CCombatHelper::HitCtx ctx;
                    ctx.skillId = 32;
                    ctx.flags = DAMAGE_SKILL;
                    ctx.flags2 = DAMAGE2_PROC;
                    ctx.hitIndex = static_cast<UInt8>(0x80 | k);
                    CCombatHelper::HitMonster(u, monster, (std::max)(1u, dealt * pct / 100u), ctx);
                }
            }
    }

    void ApplyStagger(const UserSharedPtr& u, const MonsterSharedPtr& m, UInt8 amount)
    {
        if (!m || !m->IsBoss() || m->GetStat()->IsDead() || amount == 0)
            return;
        const UInt32 now = GetNowTime();
        if (m->m_StaggerMax == 0)
        {
            const int level = m->GetSpecies() ? m->CombatLevel() : 10;   // 정예면 + elite_level_bonus(무력화는 지금 보스만 — 정예는 보스가 아니다)
            m->m_StaggerMax = static_cast<UInt16>(300 + level * 12);
            m->m_Stagger = m->m_StaggerMax;
        }
        if (m->Status().Has(StatusId::Broken, now))
            return;
        if (m->m_Stagger == 0)
        {
            if (now < m->m_StaggerRefillAt) return;
            m->m_Stagger = m->m_StaggerMax;
        }
        const UInt16 before = m->m_Stagger;
        m->m_Stagger = static_cast<UInt16>(m->m_Stagger > amount ? m->m_Stagger - amount : 0);
        const bool broke = before > 0 && m->m_Stagger == 0;
        if (broke)
        {
            ApplyStatus(m, StatusId::Broken, 3000, 1, u->GetObjID());
            m->m_StaggerRefillAt = now + 3000;
            CEntityBehaviorHelper::OnCountered(m);   // 예고 중인 기술이 끊긴다
            CUserHelper::BroadcastEffect(m, m->GetObjID(), EffectId::StaggerBreak, m->GetX(), m->GetY(), 0);
        }
        if (broke || now - m->m_StaggerSentAt >= 100)
        {
            m->m_StaggerSentAt = now;
            MONSTER_GAUGE_NFY_PACKET p{};
            p.size = sizeof(p);
            p.type = static_cast<char>(PacketType::MONSTER_GAUGE_NFY);
            p.monsterId = m->GetObjID();
            p.stagger = static_cast<UInt16>(static_cast<UInt32>(m->m_Stagger) * 1000u / (std::max<UInt16>)(1, m->m_StaggerMax));
            p.state = broke ? 1 : 0;
            p.brokenMs = broke ? 3000 : 0;
            ToViewers(m, p, false);
        }
    }

    void AddIdentity(const UserSharedPtr& u, int amount)
    {
        if (!u || amount <= 0)
            return;
        const UInt16 before = u->m_Identity;
        u->m_Identity = static_cast<UInt16>((std::min)(1000, u->m_Identity + amount));
        // 가득 차는 순간 · 5% 단위로만 알린다(패킷을 아낀다)
        if (u->m_Identity != before && (u->m_Identity == 1000 || u->m_Identity / 50 != before / 50))
            SendGauge(u, u->m_Identity == 1000);
    }

    void SendGauge(const UserSharedPtr& u, bool force)
    {
        if (!u) return;
        if (!force && u->m_IdentitySent == u->m_Identity) return;
        auto session = u->GetGameSession();
        if (!session) return;
        u->m_IdentitySent = u->m_Identity;
        const UInt32 now = GetNowTime();
        COMBAT_GAUGE_INF_PACKET p{};
        p.size = sizeof(p);
        p.type = static_cast<char>(PacketType::COMBAT_GAUGE_INF);
        p.identity = u->m_Identity;
        UInt32 active = 0;
        if (u->Status().Has(StatusId::Oath, now)) active = u->Status().Remaining(StatusId::Oath, now);
        if (u->Status().Has(StatusId::Veil, now)) active = u->Status().Remaining(StatusId::Veil, now);
        p.flags = static_cast<UInt8>((u->m_Identity >= 1000 ? GAUGE_FULL : 0) | (active > 0 ? GAUGE_ACTIVE : 0));
        p.activeMs = active;
        session->PostSend(p);
    }

    void OnEvaded(const UserSharedPtr& u, UInt32 now)
    {
        if (!u || u->m_eClassId != ClassId::NightHunter || Sched(u).dodgeAt == 0 || now - Sched(u).dodgeAt > 350)
            return;
        Sched(u).dodgeAt = 0;
        ApplyStatus(u, StatusId::Ambush, 2500, 1, u->GetObjID());
        CUserHelper::BroadcastEffect(u, u->GetObjID(), EffectId::PerfectDodge, u->GetX(), u->GetY(), 0);
        AddIdentity(u, 120);
    }

    void OnMoved(const UserSharedPtr& u)
    {
        if (!u) return;
        const UInt32 now = GetNowTime();
        Aim a; a.x = u->GetX(); a.y = u->GetY();
        auto& s = Sched(u);
        if (s.castingUntil > now)
        {
            // 캐스팅 끊김 — 창 뒤 타격은 버린다(castingSeq). 알림은 예전 그대로(기술 id 없이 Cancel)
            s.castingUntil = 0;
            s.castingSeq = 0;
            SKILL_NFY_PACKET p{};
            p.size = sizeof(p);
            p.type = static_cast<char>(PacketType::SKILL_NFY);
            p.casterId = u->GetObjID();
            p.phase = static_cast<UInt8>(SkillCastPhase::Cancel);
            p.fromX = u->GetX(); p.fromY = u->GetY();
            p.aimX = a.x; p.aimY = a.y;
            p.serverTime = now;
            ToViewers(u, p, true);
            SweepCasting(u, now);
        }
        if (UserCast* c = Charging(u))
            if (const SkillDef* d = SkillDefTable::Find(c->skillId); d && !d->moveWhileCasting)
            {
                EndCharge(u, *c, EnumSkillStage::Terminate);
                Notify(u, *d, SkillCastPhase::Cancel, 0, 0, a, ObjID{}, 0);
            }
        if (const UserCast* h = Holding(u))
            if (const SkillDef* d = SkillDefTable::Find(h->skillId); d && !d->moveWhileCasting)
                EndHoldWithRelease(u, *d, false);
    }
}
