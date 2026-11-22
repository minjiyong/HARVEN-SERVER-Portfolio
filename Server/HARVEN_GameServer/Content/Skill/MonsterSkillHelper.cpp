#include "pch.h"
#include "Content/Skill/MonsterSkillHelper.h"

#include <cmath>

#include "Base/Core/CoreTLS.h"
#include "Content/Combat/CombatHelper.h"
#include "Content/ContentHelper.h"
#include "Content/Field/Sector.h"
#include "Content/Field/SectorHelper.h"
#include "Content/Field/ZoneLayout.h"
#include "Content/Navigator/Collision.h"
#include "Database/GameData/LevelTable.h"
#include "Database/GameData/MonsterAITable.h"
#include "Database/GameData/MoveRule.h"
#include "Database/GameData/WorldData.h"
#include "Entity/Component/Behavior/EntityBehavior.h"
#include "Entity/Component/Behavior/Task/Monster/MonsterAgent.h"
#include "Entity/Subject/Monster/Monster.h"
#include "Entity/Subject/Skill/AreaOfEffect.h"
#include "Entity/Subject/SubjectHelper.h"
#include "User/User.h"
#include "User/UserHelper.h"

namespace
{
    [[nodiscard]] int Cheb(int ax, int ay, int bx, int by) noexcept { return std::max(std::abs(ax - bx), std::abs(ay - by)); }
    [[nodiscard]] int Sign(int v) noexcept { return (v > 0) - (v < 0); }

    [[nodiscard]] bool InGame(const UserSharedPtr& u)
    {
        if (u == nullptr)
            return false;
        const auto session = u->GetGameSession();
        return session && session->m_eState == SOCKET_STATE::ST_INGAME;
    }

    // 몬스터가 맞힐 수 있는 플레이어(살아 있음 · 게임 중 · 같은 Zone · 마을 밖)
    [[nodiscard]] bool HittablePlayer(const MonsterSharedPtr& m, const UserSharedPtr& u)
    {
        return InGame(u) && !u->GetStat()->IsDead() && u->GetZoneId() == m->GetZoneId() && !u->IsTransferring()
            && !WorldData::IsSafeZone(u->GetX(), u->GetY());
    }

    // 몬스터가 (x, y)에서 (dx, dy)로 한 칸 갈 수 있는가 — 지형(모서리 규칙) · 마을 · Zone 경계
    [[nodiscard]] bool MonsterStepOk(const MonsterSharedPtr& m, int x, int y, int dx, int dy)
    {
        const int nx = x + dx, ny = y + dy;
        return CZoneLayout::IsValidWorldPosition(static_cast<short>(nx), static_cast<short>(ny))
            && MoveRule::CleanStep(x, y, dx, dy, &CollisionAt)
            && !WorldData::IsSafeZone(nx, ny)
            && CZoneLayout::GetZoneIdByWorld(static_cast<short>(nx), static_cast<short>(ny)) == m->GetZoneId();
    }

    [[nodiscard]] bool MonsterStandOk(const MonsterSharedPtr& m, int x, int y)
    {
        return CZoneLayout::IsValidWorldPosition(static_cast<short>(x), static_cast<short>(y)) && !CollisionAt(x, y)
            && !WorldData::IsSafeZone(x, y) && CZoneLayout::GetZoneIdByWorld(static_cast<short>(x), static_cast<short>(y)) == m->GetZoneId();
    }

    // (cx, cy) 기준 섹터(이 Zone 밖이면 기준 객체의 섹터)
    void SectorOf(const SubjectSharedPtr& me, int cx, int cy, short& sx, short& sy)
    {
        sx = static_cast<short>(cx / SECTOR_RANGE);
        sy = static_cast<short>(cy / SECTOR_RANGE);
        if (!GSector->IsValidSector(sx, sy))
        {
            sx = me->GetSectorX();
            sy = me->GetSectorY();
        }
    }

    // 보는 사람(그 개체를 볼 수 있는 게임 중 플레이어)에게
    template <class Packet>
    void ToViewers(const SubjectSharedPtr& subject, const Packet& packet)
    {
        if (subject == nullptr || subject->GetSectorX() < 0)
            return;
        GSector->ForEachObjectWithin(subject->GetSectorX(), subject->GetSectorY(), subject->GetX(), subject->GetY(), GViewRange, EnumCategory::eUser,
            [&](const SubjectSharedPtr& object)
            {
                const auto viewer = static_pointer_cast<CUser>(object);
                const auto session = viewer->GetGameSession();
                if (!session || session->m_eState != SOCKET_STATE::ST_INGAME)
                    return;
                if (!CSubjectHelper::CanSee(object, subject))
                    return;
                session->PostSend(packet);
            });
    }

    [[nodiscard]] UserSharedPtr TargetOf(const CMonsterAgent& a)
    {
        if (!a.brain.m_TargetId)
            return nullptr;
        return ::GetGameObject<CUser>(a.brain.m_TargetId);
    }

    // 체력 비율이 가장 낮은 동료(자신 · 허수아비 빼고, radius 안)
    [[nodiscard]] MonsterSharedPtr WeakestAlly(const MonsterSharedPtr& m, int radius)
    {
        MonsterSharedPtr best;
        UInt32 bestRatio = 1001;
        short sx = 0, sy = 0;
        SectorOf(m, m->GetX(), m->GetY(), sx, sy);
        GSector->ForEachObjectWithin(sx, sy, m->GetX(), m->GetY(), radius, EnumCategory::eMonster, [&](const SubjectSharedPtr& object)
        {
            if (object.get() == m.get() || object->GetStat()->IsDead())
                return;
            if (Cheb(object->GetX(), object->GetY(), m->GetX(), m->GetY()) > radius)
                return;
            const auto ally = static_pointer_cast<CMonster>(object);
            if (ally->IsDummy())
                return;
            const UInt32 maxHp = std::max<UInt32>(1, ally->GetStat()->GetMaxHp());
            const UInt32 ratio = static_cast<UInt32>(ally->GetStat()->GetHp()) * 1000u / maxHp;
            if (ratio < bestRatio)
            {
                bestRatio = ratio;
                best = ally;
            }
        });
        return best;
    }

    // 시전의 기준점 → 칸(Caster = 그 순간 자기 자리 · Aim = 시전 때 조준 칸 · Target = 그 순간 기술 대상 자리 — 적 기술은 쫓는 플레이어,
    // 동료 기술(Ally)은 고른 동료). along만큼 조준 방향으로.
    void ResolveCenter(const CMonsterAgent& a, const MonsterCast& c, HitAnchor anchor, int along, short& outX, short& outY)
    {
        int x = a.monster->GetX(), y = a.monster->GetY();
        if (anchor == HitAnchor::Aim)
        {
            x = c.aimX;
            y = c.aimY;
        }
        else if (anchor == HitAnchor::Target)
        {
            x = c.aimX;
            y = c.aimY;
            if (c.targetId)
                if (const auto t = ::GetGameObject<CSubject>(c.targetId); t && t->GetZoneId() == a.monster->GetZoneId() && !t->GetStat()->IsDead()
                    && t->GetSectorX() >= 0)
                {
                    x = t->GetX();
                    y = t->GetY();
                }
        }
        if (along != 0)
        {
            x += static_cast<int>(std::lround(c.dirX * static_cast<float>(along)));
            y += static_cast<int>(std::lround(c.dirY * static_cast<float>(along)));
        }
        outX = static_cast<short>(std::clamp(x, 0, W_WIDTH - 1));
        outY = static_cast<short>(std::clamp(y, 0, W_HEIGHT - 1));
    }

    // 돌진 경로의 i번째 칸(1부터) — 조준 방향의 직선을 칸으로(긴 축으로 한 칸씩, 짧은 축은 비율대로 반올림).
    // 8방향으로 자르면 대각선에서 조금 벗어난 대상(2칸 옆 · 3칸 위)을 비껴 지나간다 — 서 있는 대상에게 닿는 선으로 긋는다.
    // 이웃한 두 칸의 차는 축마다 −1 ~ 1(한 걸음)이다.
    void DashTile(int fromX, int fromY, float dirX, float dirY, int i, int& outX, int& outY)
    {
        const float major = std::max(std::abs(dirX), std::abs(dirY));
        const float ux = major > 0.f ? dirX / major : 1.f;
        const float uy = major > 0.f ? dirY / major : 0.f;
        outX = fromX + static_cast<int>(std::lround(ux * static_cast<float>(i)));
        outY = fromY + static_cast<int>(std::lround(uy * static_cast<float>(i)));
    }

    // 돌진 경로 — 막히거나(지형 · 마을 · Zone 경계) 플레이어가 선 칸 앞에서 멈춘다. 막은 플레이어의 칸도 경로에 넣는다(직선 a = 0 판정).
    // blockByPlayers = false면 지형만 본다(시전 때 예고 선의 끝).
    void BuildDashPath(const CMonsterAgent& a, MonsterCast& c, int tiles, bool blockByPlayers)
    {
        c.pathCount = 0;
        std::array<std::pair<short, short>, 24> players{};
        int playerCount = 0;
        if (blockByPlayers)
        {
            const MonsterSharedPtr& m = a.monster;
            GSector->ForEachObjectInRange(m->GetSectorX(), m->GetSectorY(), m->GetX(), m->GetY(), tiles + 1, EnumCategory::eUser, [&](const SubjectSharedPtr& object)
            {
                const auto u = static_pointer_cast<CUser>(object);
                if (playerCount < static_cast<int>(players.size()) && HittablePlayer(m, u))
                    players[static_cast<size_t>(playerCount++)] = { u->GetX(), u->GetY() };
            });
        }
        const int fromX = a.monster->GetX(), fromY = a.monster->GetY();
        int x = fromX, y = fromY;
        for (int i = 1; i <= tiles && c.pathCount < MonsterCast::kMaxPath; ++i)
        {
            int nx = 0, ny = 0;
            DashTile(fromX, fromY, c.dirX, c.dirY, i, nx, ny);
            if (!MonsterStepOk(a.monster, x, y, nx - x, ny - y))
                break;
            bool occupied = false;
            for (int p = 0; p < playerCount; ++p)
                if (players[static_cast<size_t>(p)].first == nx && players[static_cast<size_t>(p)].second == ny)
                    occupied = true;
            c.pathX[c.pathCount] = static_cast<short>(nx);
            c.pathY[c.pathCount] = static_cast<short>(ny);
            ++c.pathCount;
            if (occupied)
                break;   // 대상에 부딪힌다 — 그 앞 칸에서 멈추고, 이 칸은 맞는 칸으로만 남는다
            x = nx;
            y = ny;
        }
    }

    // 경로의 마지막 "설 수 있는" 칸(막은 플레이어 칸은 빼고)
    void DashEnd(const CMonsterAgent& a, const MonsterCast& c, bool lastIsBlocker, short& outX, short& outY)
    {
        outX = a.monster->GetX();
        outY = a.monster->GetY();
        const int stand = lastIsBlocker ? c.pathCount - 1 : c.pathCount;
        if (stand > 0)
        {
            outX = c.pathX[static_cast<size_t>(stand - 1)];
            outY = c.pathY[static_cast<size_t>(stand - 1)];
        }
    }

    [[nodiscard]] bool PlayerOn(const MonsterSharedPtr& m, int x, int y)
    {
        bool found = false;
        GSector->ForEachObjectWithin(m->GetSectorX(), m->GetSectorY(), x, y, 0, EnumCategory::eUser, [&](const SubjectSharedPtr& object)
        {
            if (object->GetX() == x && object->GetY() == y && !object->GetStat()->IsDead())
                found = true;
        });
        return found;
    }

    // (x, y) 옆 빈 칸 중 from에 가장 가까운 칸(도약 · 순간이동 착지 — 대상 위에 겹쳐 서지 않게)
    [[nodiscard]] bool LandingNear(const MonsterSharedPtr& m, int x, int y, int fromX, int fromY, short& outX, short& outY)
    {
        if (MonsterStandOk(m, x, y) && !PlayerOn(m, x, y))
        {
            outX = static_cast<short>(x);
            outY = static_cast<short>(y);
            return true;
        }
        int best = INT32_MAX;
        bool found = false;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
            {
                if (dx == 0 && dy == 0)
                    continue;
                const int nx = x + dx, ny = y + dy;
                if (!MonsterStandOk(m, nx, ny) || PlayerOn(m, nx, ny))
                    continue;
                const int d = Cheb(nx, ny, fromX, fromY) * 10 + std::abs(nx - fromX) + std::abs(ny - fromY);
                if (d < best)
                {
                    best = d;
                    outX = static_cast<short>(nx);
                    outY = static_cast<short>(ny);
                    found = true;
                }
            }
        return found;
    }

    [[nodiscard]] TelegraphShape TeleShapeOf(HitShape shape) noexcept
    {
        return shape == HitShape::Line ? TelegraphShape::Line : TelegraphShape::Circle;
    }

    // 타격 하나의 예고 — 직선은 (기준점 → 끝), 나머지는 (중심 · 반경)
    void SendHitTelegraph(const CMonsterAgent& a, const MonsterSkillRow& row, const MonsterCast& c, int i, UInt32 hitAt)
    {
        const MonsterSkillHit& h = row.hits[i];
        const short cx = c.hitX[static_cast<size_t>(i)], cy = c.hitY[static_cast<size_t>(i)];
        const UInt32 left = static_cast<Int32>(hitAt - a.now) > 0 ? hitAt - a.now : 1;
        const UInt16 duration = static_cast<UInt16>(std::min<UInt32>(left, 65535));
        if (h.shape == HitShape::Line)
        {
            short ex = cx, ey = cy;
            if (h.a == 0 && c.pathCount > 0)
            {
                ex = c.pathX[static_cast<size_t>(c.pathCount - 1)];
                ey = c.pathY[static_cast<size_t>(c.pathCount - 1)];
            }
            else
            {
                ex = static_cast<short>(std::clamp(cx + static_cast<int>(std::lround(c.dirX * h.a)), 0, W_WIDTH - 1));
                ey = static_cast<short>(std::clamp(cy + static_cast<int>(std::lround(c.dirY * h.a)), 0, W_HEIGHT - 1));
            }
            CMonsterSkillHelper::SendTelegraph(a.monster, TelegraphShape::Line, ex, ey, cx, cy, static_cast<UInt8>(std::max<int>(1, h.b)), duration, row.pattern);
            return;
        }
        const UInt8 radius = h.shape == HitShape::Target ? 0 : h.a;
        CMonsterSkillHelper::SendTelegraph(a.monster, TeleShapeOf(h.shape), cx, cy, c.fromX, c.fromY, radius, duration, row.pattern);
    }

    // 지금 보낼 예고를 보낸다(그때 타격 중심을 정한다 — 예고한 자리에 맞으므로 피할 수 있다). 다음 예고 시각을 WakeAt으로.
    void ProcessWarnings(CMonsterAgent& a, const MonsterSkillRow& row, MonsterCast& c)
    {
        for (int i = 0; i < row.hitCount; ++i)
        {
            const UInt8 bit = static_cast<UInt8>(1u << i);
            if ((c.hitWarned & bit) != 0 || (c.hitDone & bit) != 0)
                continue;
            const MonsterSkillHit& h = row.hits[i];
            if (h.warnMs == 0)
                continue;
            const UInt32 hitAt = c.fireAt + h.atMs;
            const UInt32 lead = hitAt - c.startAt;
            const UInt32 sendAt = h.warnMs >= lead ? c.startAt : hitAt - h.warnMs;
            if (!TimeReached(a.now, sendAt))
            {
                a.WakeAt(sendAt);
                continue;
            }
            ResolveCenter(a, c, h.anchor, h.along, c.hitX[static_cast<size_t>(i)], c.hitY[static_cast<size_t>(i)]);
            c.hitWarned = static_cast<UInt8>(c.hitWarned | bit);
            // 보스 광역기 연출은 시전 효과(BossSlamCast)가 예고를 그린다 — 첫 타는 예고를 따로 보내지 않는다
            if (!(row.fx == MonsterFx::BossSlam && i == 0))
                SendHitTelegraph(a, row, c, i, hitAt);
        }
    }

    // 플레이어 밀기 · 당기기(몬스터 기술 반응) — 끝 칸을 정해 알림을 먼저, 그다음 옮긴다
    void PushPlayer(const MonsterSharedPtr& m, const UserSharedPtr& u, int cx, int cy, HitReact react, int tiles)
    {
        if (tiles <= 0 || (react != HitReact::Knockback && react != HitReact::Pull) || u->IsTransferring())
            return;
        int dx = Sign(u->GetX() - cx), dy = Sign(u->GetY() - cy);
        if (react == HitReact::Pull)
        {
            dx = -Sign(u->GetX() - m->GetX());
            dy = -Sign(u->GetY() - m->GetY());
        }
        if (dx == 0 && dy == 0)
            return;
        int x = u->GetX(), y = u->GetY();
        for (int i = 0; i < tiles; ++i)
        {
            const int nx = x + dx, ny = y + dy;
            if (react == HitReact::Pull && nx == m->GetX() && ny == m->GetY())
                break;   // 몬스터 칸 바로 앞에서 멈춘다
            if (!CZoneLayout::IsValidWorldPosition(static_cast<short>(nx), static_cast<short>(ny)) || !MoveRule::CleanStep(x, y, dx, dy, &CollisionAt)
                || WorldData::IsSafeZone(nx, ny) || CZoneLayout::GetZoneIdByWorld(static_cast<short>(nx), static_cast<short>(ny)) != u->GetZoneId())
                break;
            x = nx;
            y = ny;
        }
        if (x == u->GetX() && y == u->GetY())
            return;
        const UInt32 ms = static_cast<UInt32>(Cheb(x, y, u->GetX(), u->GetY())) * 90u;
        SUBJECT_FORCED_MOVE_NFY_PACKET p{};
        p.size = sizeof(p);
        p.type = static_cast<char>(PacketType::SUBJECT_FORCED_MOVE_NFY);
        p.id = u->GetObjID();
        p.fromX = u->GetX();
        p.fromY = u->GetY();
        p.toX = static_cast<short>(x);
        p.toY = static_cast<short>(y);
        p.durationMs = static_cast<UInt16>(ms);
        p.kind = static_cast<UInt8>(react == HitReact::Pull ? ForcedMoveKind::Pull : ForcedMoveKind::Knockback);
        ToViewers(u, p);
        CCombatHelper::MovePlayerTo(u, static_cast<short>(x), static_cast<short>(y), ms);
    }

    // 타격 하나 — 판정 · 피해 · 상태이상 · 밀기 · 효과. 몬스터가 반격에 죽으면 false(더 하지 않는다).
    bool ApplyHit(CMonsterAgent& a, const MonsterSkillRow& row, MonsterCast& c, int i)
    {
        const MonsterSharedPtr& m = a.monster;
        const MonsterSkillHit& h = row.hits[i];
        const UInt8 bit = static_cast<UInt8>(1u << i);
        if ((c.hitWarned & bit) == 0)
            ResolveCenter(a, c, h.anchor, h.along, c.hitX[static_cast<size_t>(i)], c.hitY[static_cast<size_t>(i)]);
        const short cx = c.hitX[static_cast<size_t>(i)], cy = c.hitY[static_cast<size_t>(i)];

        if (row.fx == MonsterFx::BossSlam)
            CUserHelper::BroadcastEffect(m, m->GetObjID(), EffectId::BossSlamImpact, cx, cy, std::max<int>(1, h.a));
        if (h.coeff == 0 && h.status == StatusId::None && h.react == HitReact::None)
            return true;   // 예고만 하는 타격(장판 자리 알리기)

        // 대상 모으기 — (cx, cy) 둘레에서 모양 판정
        SkillHit shape{};
        shape.shape = h.shape;
        shape.a = h.a;
        shape.b = h.b;
        const bool pathLine = h.shape == HitShape::Line && h.a == 0;
        const int reach = pathLine ? MonsterCast::kMaxPath + 1 : std::max<int>(1, h.a) + 1;
        std::array<UserSharedPtr, 16> victims{};
        int count = 0;
        short sx = 0, sy = 0;
        SectorOf(m, cx, cy, sx, sy);
        const int centerX = pathLine ? m->GetX() : cx, centerY = pathLine ? m->GetY() : cy;
        GSector->ForEachObjectInRange(sx, sy, centerX, centerY, std::min(reach, SECTOR_RANGE), EnumCategory::eUser, [&](const SubjectSharedPtr& object)
        {
            if (count >= static_cast<int>(victims.size()))
                return;
            const auto u = static_pointer_cast<CUser>(object);
            if (!HittablePlayer(m, u))
                return;
            bool inside = false;
            if (pathLine)
            {
                for (int p = 0; p < c.pathCount && !inside; ++p)
                    inside = c.pathX[static_cast<size_t>(p)] == u->GetX() && c.pathY[static_cast<size_t>(p)] == u->GetY();
            }
            else
            {
                inside = SkillDefTable::InsideHit(shape, static_cast<float>(cx), static_cast<float>(cy), c.dirX, c.dirY,
                    static_cast<float>(u->GetX()), static_cast<float>(u->GetY()));
            }
            if (inside)
                victims[static_cast<size_t>(count++)] = u;
        });
        if (h.maxTargets > 0 && count > h.maxTargets)
        {
            std::sort(victims.begin(), victims.begin() + count, [&](const UserSharedPtr& l, const UserSharedPtr& r)
            {
                return Cheb(l->GetX(), l->GetY(), cx, cy) < Cheb(r->GetX(), r->GetY(), cx, cy);
            });
            count = h.maxTargets;
        }

        const ObjID monsterId = m->GetObjID();
        const StatusRow& st = StatusTable::Get(h.status);
        for (int v = 0; v < count; ++v)
        {
            const UserSharedPtr& u = victims[static_cast<size_t>(v)];
            if (h.coeff > 0)
            {
                bool landed = false;
                const bool died = CCombatHelper::HitPlayerFromMonster(m, u, CMonsterSkillHelper::RollDamage(m, h.coeff), DAMAGE_BOSS_SKILL, &landed);
                if (died)
                {
                    a.brain.m_Aggro.Remove(u->GetObjID());
                    CContentHelper::HandlePlayerDeath(u, monsterId, false);
                }
                if (m->GetStat()->IsDead())
                    return false;   // 철벽 태세 반격에 쓰러졌다
                if (died || !landed)
                    continue;   // 피했다 — 상태이상 · 밀기도 없다
            }
            else if (CCombatHelper::IsUntouchable(u, a.now))
                continue;
            if (h.status != StatusId::None && h.statusMs > 0)
            {
                const UInt16 tick = st.tickMs > 0 ? static_cast<UInt16>(std::max<UInt32>(1, m->GetStat()->GetOffensive() * st.tickPercent / 100u)) : 0;
                CCombatHelper::ApplyStatus(u, h.status, h.statusMs, std::max<UInt8>(1, h.statusStacks), monsterId, tick);
            }
            PushPlayer(m, u, cx, cy, h.react, h.push);
            if (row.fx != MonsterFx::None && row.fx != MonsterFx::BossSlam && v < 4)
                CMonsterSkillHelper::SendFx(m, u->GetObjID(), row.fx, u->GetX(), u->GetY(), 0);
        }
        if (count == 0 && row.fx != MonsterFx::None && row.fx != MonsterFx::BossSlam)
            CMonsterSkillHelper::SendFx(m, monsterId, row.fx, cx, cy, 0);
        return true;
    }

    // 발사 — 이동 · 자기 강화 · 장판 · 효과(타격 없는 기술)
    void Fire(CMonsterAgent& a, const MonsterSkillRow& row, MonsterCast& c)
    {
        const MonsterSharedPtr& m = a.monster;
        const int mx = m->GetX(), my = m->GetY();
        switch (row.move)
        {
        case MonsterMove::Dash:
        {
            BuildDashPath(a, c, row.moveTiles, true);
            short ex = 0, ey = 0;
            const bool blocked = c.pathCount > 0 && PlayerOn(m, c.pathX[static_cast<size_t>(c.pathCount - 1)], c.pathY[static_cast<size_t>(c.pathCount - 1)]);
            DashEnd(a, c, blocked, ex, ey);
            if (ex != mx || ey != my)
                CMonsterSkillHelper::ForcedMove(m, ex, ey, static_cast<UInt32>(row.moveMs) * static_cast<UInt32>(Cheb(mx, my, ex, ey)), ForcedMoveKind::Dash);
            break;
        }
        case MonsterMove::Leap:
        {
            short lx = 0, ly = 0;
            if (LandingNear(m, c.aimX, c.aimY, mx, my, lx, ly) && (lx != mx || ly != my))
                CMonsterSkillHelper::ForcedMove(m, lx, ly, row.moveMs, ForcedMoveKind::Launch);
            break;
        }
        case MonsterMove::Blink:
        {
            short lx = 0, ly = 0;
            if (LandingNear(m, c.aimX, c.aimY, mx, my, lx, ly) && (lx != mx || ly != my))
                CMonsterSkillHelper::ForcedMove(m, lx, ly, 0, ForcedMoveKind::Blink);
            break;
        }
        case MonsterMove::Retreat:
        {
            int dx = -static_cast<int>(std::lround(c.dirX)), dy = -static_cast<int>(std::lround(c.dirY));
            if (dx == 0 && dy == 0)
                dx = m->IsFacingLeft() ? 1 : -1;
            int x = mx, y = my;
            for (int i = 0; i < row.moveTiles; ++i)
            {
                if (!MonsterStepOk(m, x, y, dx, dy))
                    break;
                x += dx;
                y += dy;
            }
            if (x != mx || y != my)
            {
                CMonsterSkillHelper::ForcedMove(m, static_cast<short>(x), static_cast<short>(y),
                    static_cast<UInt32>(row.moveMs) * static_cast<UInt32>(Cheb(mx, my, x, y)), ForcedMoveKind::Roll);
                m->UpdateFacing(-dx);   // 물러서도 대상을 본다
            }
            break;
        }
        default:
            break;
        }

        if (row.selfStatus != StatusId::None && row.selfStatusMs > 0)
            CCombatHelper::ApplyStatus(m, row.selfStatus, row.selfStatusMs, 1, m->GetObjID(), 0, row.selfStatusPower);

        if (row.aoe != 0)
        {
            short ax = 0, ay = 0;
            ResolveCenter(a, c, row.aoeAnchor, 0, ax, ay);
            CAreaOfEffectHelper::Spawn(m, row.aoe, ax, ay);
        }

        if (row.hitCount == 0 && row.fx != MonsterFx::None && row.fx != MonsterFx::BossSlam)
            CMonsterSkillHelper::SendFx(m, m->GetObjID(), row.fx, m->GetX(), m->GetY(), 0);

        UInt32 lastHit = c.fireAt;
        for (int i = 0; i < row.hitCount; ++i)
            if (static_cast<Int32>(c.fireAt + row.hits[i].atMs - lastHit) > 0)
                lastHit = c.fireAt + row.hits[i].atMs;
        c.endAt = lastHit + row.lockMs;
    }

    [[nodiscard]] UInt16 SkillGapMs(const CMonsterAgent& a) noexcept { return a.ai != nullptr ? a.ai->skillGapMs : 1500; }

    // 이 칸의 기술을 지금 쓸 수 있는가(대상 종류 · 거리 · 동료 수). 돌진은 첫 칸이 열려 있어야.
    [[nodiscard]] bool CanUse(const CMonsterAgent& a, const MonsterSkillRow& row, const MonsterAiSkill& slot, const UserSharedPtr& target)
    {
        const MonsterSharedPtr& m = a.monster;
        const int mx = m->GetX(), my = m->GetY();
        switch (row.target)
        {
        case MonsterSkillTarget::Enemy:
        {
            if (!target)
                return false;
            const int d = Cheb(mx, my, target->GetX(), target->GetY());
            if (d < row.rangeMin || d > row.rangeMax)
                return false;
            if (row.move == MonsterMove::Dash)
            {
                // 첫 칸이 열려 있어야(벽에 대고 돌진하지 않는다) — 경로와 같은 직선
                const float dx = static_cast<float>(target->GetX() - mx), dy = static_cast<float>(target->GetY() - my);
                const float len = std::sqrt(dx * dx + dy * dy);
                int fx = 0, fy = 0;
                DashTile(mx, my, len > 0.f ? dx / len : 1.f, len > 0.f ? dy / len : 0.f, 1, fx, fy);
                if (!MonsterStepOk(m, mx, my, fx - mx, fy - my))
                    return false;
            }
            break;
        }
        case MonsterSkillTarget::Self:
            if (!target || Cheb(mx, my, target->GetX(), target->GetY()) > row.rangeMax)
                return false;
            break;
        case MonsterSkillTarget::Ally:
        {
            // 체력 비율이 가장 낮은 동료 — 회복 장판이면 다친 동료가 있어야 한다
            const MonsterSharedPtr ally = WeakestAlly(m, row.rangeMax);
            if (ally == nullptr)
                return false;
            if (const MonsterAoeRow* aoe = row.aoe != 0 ? MonsterAoeTable::Find(row.aoe) : nullptr;
                aoe != nullptr && aoe->healPercent > 0 && ally->GetStat()->GetHp() >= ally->GetStat()->GetMaxHp())
                return false;
            break;
        }
        case MonsterSkillTarget::AllyGroup:
            if (CMonsterSkillHelper::CountAllies(m, mx, my, row.rangeMax) < std::max<int>(1, slot.alliesMin))
                return false;
            break;
        }
        if (slot.alliesMin > 0 && row.target != MonsterSkillTarget::AllyGroup
            && CMonsterSkillHelper::CountAllies(m, mx, my, std::max<int>(row.rangeMax, 3)) < slot.alliesMin)
            return false;
        return true;
    }
}

namespace CMonsterSkillHelper
{
    UInt32 RollDamage(const MonsterSharedPtr& m, UInt32 coeffPercent)
    {
        bool crit = false;
        const UInt32 base = CombatRule::RollDamage(m->GetStat()->GetOffensive(), static_cast<UInt32>(LRng() % 1001), 100, crit);
        const UInt32 empower = m->Status().Power(StatusId::Empower, GetNowTime());
        const UInt64 dmg = static_cast<UInt64>(base) * coeffPercent / 100u * (100u + empower) / 100u;
        return static_cast<UInt32>(std::clamp<UInt64>(dmg, 1, 65535));
    }

    void ForcedMove(const MonsterSharedPtr& m, short x, short y, UInt32 durationMs, ForcedMoveKind kind)
    {
        if (m == nullptr || m->GetSectorX() < 0)
            return;
        const short fx = m->GetX(), fy = m->GetY();
        if (fx == x && fy == y)
            return;
        SUBJECT_FORCED_MOVE_NFY_PACKET p{};
        p.size = sizeof(p);
        p.type = static_cast<char>(PacketType::SUBJECT_FORCED_MOVE_NFY);
        p.id = m->GetObjID();
        p.fromX = fx;
        p.fromY = fy;
        p.toX = x;
        p.toY = y;
        p.durationMs = static_cast<UInt16>(std::min<UInt32>(durationMs, 65535));
        p.kind = static_cast<UInt8>(kind);
        ToViewers(m, p);   // 먼저 — 클라가 한 동작으로 보간한다(뒤따르는 SUBJECT_MOVE_NFY는 이미 그 칸이라 무시)
        m->UpdateFacing(x - fx);
        m->SetLastStepMs(durationMs);
        CSectorHelper::MoveMonster(m, x, y);
        m->ClearPath();
    }

    void SendFx(const SubjectSharedPtr& center, const ObjID& subjectId, MonsterFx fx, short x, short y, Int32 param)
    {
        EffectId effect = EffectId::None;
        switch (fx)
        {
        case MonsterFx::Fire:    effect = EffectId::ProcEmber; break;
        case MonsterFx::Poison:  effect = EffectId::ProcVenom; break;
        case MonsterFx::Frost:   effect = EffectId::ProcFrost; break;
        case MonsterFx::Thunder: effect = EffectId::ProcThunder; break;
        case MonsterFx::Rune:    effect = EffectId::RuneExplosion; break;
        case MonsterFx::Steam:   effect = EffectId::SteamBurst; break;
        case MonsterFx::Heal:    effect = EffectId::PriestHeal; break;
        case MonsterFx::Haste:   effect = EffectId::HasteOn; break;
        case MonsterFx::Guard:   effect = EffectId::ProcGuardian; break;
        case MonsterFx::BossSlam: effect = EffectId::BossSlamImpact; break;
        case MonsterFx::None:    return;
        }
        CUserHelper::BroadcastEffect(center, subjectId, effect, x, y, param);
    }

    void SendTelegraph(const MonsterSharedPtr& m, TelegraphShape shape, short x, short y, short fromX, short fromY,
        UInt8 radius, UInt16 durationMs, MonsterPattern pattern)
    {
        if (m == nullptr || m->GetSectorX() < 0)
            return;
        MONSTER_TELEGRAPH_NFY_PACKET packet{};
        packet.size = sizeof(packet);
        packet.type = static_cast<char>(PacketType::MONSTER_TELEGRAPH_NFY);
        packet.monsterId = m->GetObjID();
        packet.shape = static_cast<UInt8>(shape);
        packet.x = x;
        packet.y = y;
        packet.fromX = fromX;
        packet.fromY = fromY;
        packet.radius = radius;
        packet.durationMs = durationMs;
        packet.pattern = static_cast<UInt8>(pattern);
        ToViewers(m, packet);
    }

    int CountAllies(const MonsterSharedPtr& m, int cx, int cy, int radius)
    {
        int n = 0;
        short sx = 0, sy = 0;
        SectorOf(m, cx, cy, sx, sy);
        GSector->ForEachObjectWithin(sx, sy, cx, cy, radius, EnumCategory::eMonster, [&](const SubjectSharedPtr& object)
        {
            if (object.get() == m.get() || object->GetStat()->IsDead())
                return;
            if (Cheb(object->GetX(), object->GetY(), cx, cy) > radius)
                return;
            if (static_pointer_cast<CMonster>(object)->IsDummy())
                return;
            ++n;
        });
        return n;
    }

    bool TryBegin(CMonsterAgent& a)
    {
        const MonsterAiRow* ai = a.ai;
        if (ai == nullptr || ai->skillCount == 0)
            return false;
        CSkillScheduler& sk = a.brain.m_Skills;
        if (sk.IsCasting())
            return false;
        const MonsterSharedPtr& m = a.monster;
        if (m->Status().BlocksAttack(a.now))
            return false;   // 기절 · 무력화 — 풀리면 생각이 다시 돈다
        if (!TimeReached(a.now, sk.nextSkillAt))
        {
            a.WakeAt(sk.nextSkillAt);
            return false;
        }
        sk.nextSkillAt = a.now;
        if (ai->firstSkillMs > 0 && !TimeReached(a.now, a.brain.m_CombatSince + ai->firstSkillMs))
        {
            a.WakeAt(a.brain.m_CombatSince + ai->firstSkillMs);
            return false;
        }

        const UserSharedPtr target = TargetOf(a);
        const UInt32 maxHp = std::max<UInt32>(1, m->GetStat()->GetMaxHp());
        const UInt32 hpPercent = static_cast<UInt32>(m->GetStat()->GetHp()) * 100u / maxHp;

        struct Candidate { UInt8 slot; UInt32 weight; };
        std::array<Candidate, kMonsterAiSkills> cand{};
        int count = 0;
        int best = -1;
        UInt32 total = 0;
        for (int i = 0; i < ai->skillCount; ++i)
        {
            const MonsterAiSkill& s = ai->skills[i];
            if (a.brain.m_Phase < s.phaseMin || a.brain.m_Phase > s.phaseMax)
                continue;
            if (!TimeReached(a.now, sk.slotReadyAt[static_cast<size_t>(i)]))
            {
                a.WakeAt(sk.slotReadyAt[static_cast<size_t>(i)]);   // 이 칸이 풀리는 시각에 다시 본다
                continue;
            }
            sk.slotReadyAt[static_cast<size_t>(i)] = a.now;   // 풀린 칸은 지금으로 당겨 둔다(오래 안 쓴 시각이 시계가 돌아 미래로 보이지 않게)
            if (s.hpBelow > 0 && hpPercent > s.hpBelow)
                continue;
            if (s.priority < best)
                continue;
            const MonsterSkillRow* row = MonsterSkillTable::Find(s.skillId);
            if (row == nullptr || !CanUse(a, *row, s, target))
                continue;
            if (s.priority > best)
            {
                best = s.priority;
                count = 0;
                total = 0;
            }
            // 같은 칸을 잇달아 덜 고르게 — 보스 패턴이 한 기술로 굳지 않는다
            UInt32 w = s.weight;
            if (i == sk.lastSlot && w > 1)
                w /= 2;
            cand[static_cast<size_t>(count++)] = Candidate{ static_cast<UInt8>(i), w };
            total += w;
        }
        if (count == 0 || total == 0)
            return false;

        UInt32 roll = a.Random(total);
        UInt8 pick = cand[0].slot;
        for (int i = 0; i < count; ++i)
        {
            if (roll < cand[static_cast<size_t>(i)].weight)
            {
                pick = cand[static_cast<size_t>(i)].slot;
                break;
            }
            roll -= cand[static_cast<size_t>(i)].weight;
        }
        const MonsterAiSkill& chosen = ai->skills[pick];
        if (chosen.chance < 100 && a.Random(100) >= chosen.chance)
        {
            // 이번 기회는 넘긴다 — 같은 칸을 틱마다 다시 굴리지 않게 잠깐 쉰다(확률이 "기회마다"로 남는다)
            sk.slotReadyAt[pick] = a.now + 1500;
            a.WakeAt(a.now + 1500);
            return false;
        }
        return Begin(a, chosen.skillId, pick);
    }

    bool Begin(CMonsterAgent& a, UInt16 skillId, UInt8 slot)
    {
        const MonsterSkillRow* row = MonsterSkillTable::Find(skillId);
        if (row == nullptr)
            return false;
        const MonsterSharedPtr& m = a.monster;
        CSkillScheduler& sk = a.brain.m_Skills;
        MonsterCast& c = sk.cast;
        c = MonsterCast{};
        c.skillId = row->id;
        c.slot = slot;
        c.stage = 0;
        c.startAt = a.now;
        c.fireAt = a.now + row->castMs;
        c.fromX = m->GetX();
        c.fromY = m->GetY();
        c.targetId = a.brain.m_TargetId;

        // 조준 — 대상 · 자기 · 동료
        int aimX = m->GetX(), aimY = m->GetY();
        const UserSharedPtr target = TargetOf(a);
        switch (row->target)
        {
        case MonsterSkillTarget::Enemy:
            if (target)
            {
                aimX = target->GetX();
                aimY = target->GetY();
            }
            break;
        case MonsterSkillTarget::Ally:
            if (const MonsterSharedPtr ally = WeakestAlly(m, row->rangeMax))
            {
                aimX = ally->GetX();
                aimY = ally->GetY();
                c.targetId = ally->GetObjID();   // Target 기준점 = 이 동료(움직여도 따라간다)
            }
            break;
        default:
            break;
        }
        c.aimX = static_cast<short>(aimX);
        c.aimY = static_cast<short>(aimY);

        // 방향 — 조준 칸 쪽(자기 자리면 대상 쪽, 그것도 없으면 보는 쪽)
        float dx = static_cast<float>(aimX - m->GetX()), dy = static_cast<float>(aimY - m->GetY());
        if (dx == 0.f && dy == 0.f && target)
        {
            dx = static_cast<float>(target->GetX() - m->GetX());
            dy = static_cast<float>(target->GetY() - m->GetY());
        }
        if (dx == 0.f && dy == 0.f)
            dx = m->IsFacingLeft() ? -1.f : 1.f;
        const float len = std::sqrt(dx * dx + dy * dy);
        c.dirX = dx / len;
        c.dirY = dy / len;
        m->UpdateFacing(static_cast<int>(std::lround(c.dirX * 2.f)));

        // 재사용 대기는 시전 시작부터(끊겨도 다시 바로 쓰지 않는다 — 예전 m_PatternReadyAt과 같다)
        if (slot < kMonsterAiSkills)
        {
            sk.slotReadyAt[slot] = a.now + row->cooldownMs + (row->cooldownJitterMs > 0 ? a.Random(row->cooldownJitterMs + 1u) : 0u);
            sk.lastSlot = slot;
        }

        // 돌진 예고 선의 끝(지형만 — 플레이어에 부딪히는 자리는 발사 때 다시 본다)
        if (row->move == MonsterMove::Dash)
            BuildDashPath(a, c, row->moveTiles, false);

        ProcessWarnings(a, *row, c);
        if (row->fx == MonsterFx::BossSlam && row->hitCount > 0)
        {
            const MonsterSkillHit& h0 = row->hits[0];
            short cx = 0, cy = 0;
            if ((c.hitWarned & 1u) != 0)
            {
                cx = c.hitX[0];
                cy = c.hitY[0];
            }
            else
            {
                ResolveCenter(a, c, h0.anchor, h0.along, cx, cy);
                c.hitX[0] = cx;
                c.hitY[0] = cy;
                c.hitWarned = static_cast<UInt8>(c.hitWarned | 1u);
            }
            CUserHelper::BroadcastEffect(m, m->GetObjID(), EffectId::BossSlamCast, cx, cy, std::max<int>(1, h0.a));
        }
        a.WakeAt(c.fireAt);
        return true;
    }

    EnumTaskStatus Advance(CMonsterAgent& a)
    {
        CSkillScheduler& sk = a.brain.m_Skills;
        MonsterCast& c = sk.cast;
        if (!c.Active())
            return EnumTaskStatus::Failure;
        const MonsterSkillRow* row = MonsterSkillTable::Find(c.skillId);
        const MonsterSharedPtr& m = a.monster;
        if (row == nullptr || m->GetStat()->IsDead())
        {
            c = MonsterCast{};
            return EnumTaskStatus::Failure;
        }

        if (c.stage == 0)
        {
            // 예고 중 기절 · 빙결 · 반격 · 무력화 → 끊긴다(방패 강타 · 서리 각인 · 반격 타격으로 받아칠 수 있다)
            if (row->counterable && (c.countered || m->Status().BlocksAttack(a.now) || m->Status().BlocksMove(a.now)))
            {
                Cancel(a);
                return EnumTaskStatus::Failure;
            }
            if (!TimeReached(a.now, c.fireAt))
            {
                ProcessWarnings(a, *row, c);   // 발사 전에 낼 예고만(발사 순간 이후의 예고는 이동이 끝난 뒤에 자리를 정한다 — 아래)
                a.WakeAt(c.fireAt);
                return EnumTaskStatus::Running;
            }
            Fire(a, *row, c);
            c.stage = 1;
            if (m->GetStat()->IsDead())
                return EnumTaskStatus::Failure;
        }

        // 발사 뒤: 타격 시각표
        const UInt8 all = static_cast<UInt8>((1u << row->hitCount) - 1u);
        if (row->counterable && (c.hitDone & all) != all && m->Status().BlocksAttack(a.now))
        {
            Cancel(a);   // 연타 중 기절 — 남은 타격을 버린다
            return EnumTaskStatus::Failure;
        }
        ProcessWarnings(a, *row, c);
        for (int i = 0; i < row->hitCount; ++i)
        {
            const UInt8 bit = static_cast<UInt8>(1u << i);
            if ((c.hitDone & bit) != 0)
                continue;
            const UInt32 hitAt = c.fireAt + row->hits[i].atMs;
            if (!TimeReached(a.now, hitAt))
            {
                a.WakeAt(hitAt);
                continue;
            }
            c.hitDone = static_cast<UInt8>(c.hitDone | bit);
            if (!ApplyHit(a, *row, c, i))
            {
                c = MonsterCast{};
                return EnumTaskStatus::Failure;   // 반격에 쓰러졌다
            }
        }
        if ((c.hitDone & all) != all)
            return EnumTaskStatus::Running;
        if (!TimeReached(a.now, c.endAt))
        {
            a.WakeAt(c.endAt);
            return EnumTaskStatus::Running;
        }
        // 끝 — 다음 기술까지 간격
        sk.nextSkillAt = a.now + SkillGapMs(a);
        c = MonsterCast{};
        return EnumTaskStatus::Success;
    }

    void Cancel(CMonsterAgent& a)
    {
        CSkillScheduler& sk = a.brain.m_Skills;
        MonsterCast& c = sk.cast;
        if (!c.Active())
            return;
        // 아직 맞지 않은 예고가 화면에 있으면 끊김을 알린다(클라: "끊김" · 예고 지움)
        const bool pendingTelegraph = c.stage == 0 || (c.hitWarned & static_cast<UInt8>(~c.hitDone)) != 0;
        if (pendingTelegraph && !a.monster->GetStat()->IsDead())   // 죽어서 끊긴 것은 알리지 않는다(쓰러지는 몸에 "끊김"이 뜨지 않게)
        {
            const MonsterSkillRow* row = MonsterSkillTable::Find(c.skillId);
            SendTelegraph(a.monster, TelegraphShape::Circle, c.aimX, c.aimY, c.fromX, c.fromY, 0, 0,
                row != nullptr ? row->pattern : MonsterPattern::None);
        }
        sk.nextSkillAt = a.now + SkillGapMs(a) / 2u;
        c = MonsterCast{};
    }
}
