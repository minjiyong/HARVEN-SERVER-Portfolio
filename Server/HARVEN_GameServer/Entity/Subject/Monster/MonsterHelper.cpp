#include "pch.h"
#include "Entity/Subject/Monster/MonsterHelper.h"
#include "User/User.h"
#include "User/UserHelper.h"
#include "Entity/Subject/SubjectHelper.h"
#include "Entity/Component/Behavior/EntityBehaviorHelper.h"
#include "Content/Field/SectorHelper.h"
#include "Content/Field/Sector.h"
#include "Content/Field/ZoneLayout.h"
#include "Content/Navigator/AStar.h"
#include "Content/Navigator/Collision.h"
#include "Base/Core/CoreTLS.h"
#include "Database/GameData/WorldData.h"
#include "Database/GameData/MonsterTable.h"
#include "Database/GameData/EconRules.h"
#include "Content/Reward/LootRoll.h"

namespace
{
    // 길을 못 찾은 같은 목표는 이만큼 지나야 다시 A*를 돌린다(목표가 움직이면 바로 다시)
    constexpr UInt32 kPathRetryMs = 1000;

    int ManhattanDistance(short x1, short y1, short x2, short y2)
    {
        return abs(x1 - x2) + abs(y1 - y2);
    }

    // 몬스터가 밟을 수 없는 칸: 장애물 + 마을(안전지대) + 다른 Zone(몬스터는 Zone을 넘지 않는다). 마을 규칙은 서버가 판정한다.
    bool IsMonsterBlocked(const MonsterSharedPtr& monster, short x, short y)
    {
        return !CZoneLayout::IsValidWorldPosition(x, y) || isCollision(x, y) || WorldData::IsSafeZone(x, y)
            || CZoneLayout::GetZoneIdByWorld(x, y) != monster->GetZoneId();
    }

    bool IsWalkableStep(const MonsterSharedPtr& monster, short currentX, short currentY, short nextX, short nextY)
    {
        if (abs(nextX - currentX) + abs(nextY - currentY) != 1)
            return false;

        return IsMonsterBlocked(monster, nextX, nextY) == false;
    }

    void MoveMonsterStep(const MonsterSharedPtr& monster, short nextX, short nextY, UInt32 stepMs)
    {
        monster->UpdateFacing(nextX - monster->GetX());
        // 보간 시간 = 실제 걸음 간격(둔화 · 질풍 반영) — 예전에는 AStarMove가 늘 표의 이동 간격으로 덮어써서
        // 돌진(70ms) · 넉백 걸음까지 평소 걷는 속도로 보였다(SUBJECT_MOVE_NFY.move_time)
        monster->SetLastStepMs(stepMs);
        CSectorHelper::MoveMonster(monster, nextX, nextY);
    }

    bool TryMoveCachedPathStep(const MonsterSharedPtr& monster, short goalX, short goalY, UInt32 stepMs)
    {
        if (monster == nullptr || monster->HasCachedPathTo(goalX, goalY) == false)
            return false;

        auto& path = monster->GetPath();
        const NODE next = path.back();
        if (IsWalkableStep(monster, monster->GetX(), monster->GetY(), next.m_X, next.m_Y) == false)
        {
            monster->ClearPath();
            return false;
        }

        path.pop_back();
        MoveMonsterStep(monster, next.m_X, next.m_Y, stepMs);
        return true;
    }

    // 막힌 칸이 없으면 목표 쪽으로 한 칸(4방향 중 맨해튼 거리가 가장 줄어드는 칸) — 대부분의 추격은 A* 없이 끝난다
    bool TryMoveOneStepTowardGoal(const MonsterSharedPtr& monster, short goalX, short goalY, UInt32 stepMs)
    {
        if (monster == nullptr)
            return false;

        const short currentX = monster->GetX();
        const short currentY = monster->GetY();
        const int currentDistance = ManhattanDistance(currentX, currentY, goalX, goalY);
        if (currentDistance <= 1)
            return false;

        short bestX = currentX;
        short bestY = currentY;
        int bestDistance = currentDistance;

        const array<pair<short, short>, 4> directions{ {
            { 0, -1 },
            { 0, 1 },
            { -1, 0 },
            { 1, 0 }
        } };

        for (const auto [dx, dy] : directions)
        {
            const short nextX = static_cast<short>(currentX + dx);
            const short nextY = static_cast<short>(currentY + dy);
            if (IsMonsterBlocked(monster, nextX, nextY))
                continue;

            const int nextDistance = ManhattanDistance(nextX, nextY, goalX, goalY);
            if (nextDistance >= bestDistance)
                continue;

            bestDistance = nextDistance;
            bestX = nextX;
            bestY = nextY;
        }

        if (bestDistance >= currentDistance)
            return false;

        monster->ClearPath();
        MoveMonsterStep(monster, bestX, bestY, stepMs);
        return true;
    }

    bool TryMoveUsingFreshPath(const MonsterSharedPtr& monster, short goalX, short goalY, UInt32 stepMs)
    {
        if (monster == nullptr)
            return false;

        const UInt32 now = GetNowTime();
        if (monster->IsPathSearchBackedOff(goalX, goalY, now))
            return false;

        auto& path = monster->GetPath();
        path = FindPath(monster->GetX(), monster->GetY(), goalX, goalY);
        monster->CachePathTarget(goalX, goalY);

        if (path.empty())
        {
            monster->ClearPath();
            monster->BackOffPathSearch(goalX, goalY, now + kPathRetryMs);
            return false;
        }

        const NODE next = path.back();
        if (IsWalkableStep(monster, monster->GetX(), monster->GetY(), next.m_X, next.m_Y) == false)
        {
            monster->ClearPath();
            return false;
        }

        path.pop_back();
        MoveMonsterStep(monster, next.m_X, next.m_Y, stepMs);
        return true;
    }

    // 지역 스폰 가중치로 종족을 하나 고른다.
    UInt16 PickSpecies(const WorldData::RegionRow& region)
    {
        int total = 0;
        for (const auto& spawn : region.spawns)
            total += spawn.weight;
        if (total <= 0)
            return 0;

        int roll = static_cast<int>(LRng() % static_cast<UInt32>(total));
        for (const auto& spawn : region.spawns)
        {
            if (roll < spawn.weight)
                return spawn.species;
            roll -= spawn.weight;
        }
        return region.spawns[0].species;
    }
}

namespace CMonsterHelper
{
    bool StepToward(const MonsterSharedPtr& monster, short goalX, short goalY, UInt32 stepMs)
    {
        if (TryMoveCachedPathStep(monster, goalX, goalY, stepMs))
            return true;

        if (TryMoveOneStepTowardGoal(monster, goalX, goalY, stepMs))
            return true;

        return TryMoveUsingFreshPath(monster, goalX, goalY, stepMs);
    }

    bool RandomStep(const MonsterSharedPtr& monster, UInt32 stepMs)
    {
        if (monster == nullptr)
            return false;

        short x = monster->GetX();
        short y = monster->GetY();
        CSubjectHelper::MovePositionByDirection(x, y, static_cast<char>(LRng() % 4));
        if (x == monster->GetX() && y == monster->GetY())
            return false;   // 벽 — 이번엔 서 있는다(같은 칸 이동 알림을 보내지 않는다)

        // 새 칸은 이 Zone · 월드 안이어야 한다(y=499에서 y=500으로 걸으면 다른 Zone이라 섹터 갱신이 실패한다).
        // 마을(안전지대)에는 몬스터가 들어가지 않는다.
        if (IsMonsterBlocked(monster, x, y))
            return false;

        monster->ClearPath();
        MoveMonsterStep(monster, x, y, stepMs);
        return true;
    }

    // 존 경계는 축 정렬(axis-aligned)이므로 4방향 극단 좌표만 검사한다.
    // 어느 한 방향이라도 ZONE_BOUNDARY_MARGIN 이내에 다른 존이 있으면 경계 근처로 판정.
    bool IsNearZoneBoundary(short x, short y)
    {
        const ZoneId curZone = CZoneLayout::GetZoneIdByWorld(x, y);
        if (curZone == InvalidZoneId)
            return false;

        const short x1 = static_cast<short>(std::max<int>(0, x - ZONE_BOUNDARY_MARGIN));
        const short x2 = static_cast<short>(std::min<int>(W_WIDTH  - 1, x + ZONE_BOUNDARY_MARGIN));
        const short y1 = static_cast<short>(std::max<int>(0, y - ZONE_BOUNDARY_MARGIN));
        const short y2 = static_cast<short>(std::min<int>(W_HEIGHT - 1, y + ZONE_BOUNDARY_MARGIN));

        return CZoneLayout::GetZoneIdByWorld(x1, y) != curZone
            || CZoneLayout::GetZoneIdByWorld(x2, y) != curZone
            || CZoneLayout::GetZoneIdByWorld(x,  y1) != curZone
            || CZoneLayout::GetZoneIdByWorld(x,  y2) != curZone;
    }

    UInt32 MoveDelayMs(const MonsterSharedPtr& monster, UInt32 now)
    {
        // 둔화면 느려지고(%) 질풍이면 빨라진다(÷ (1 + 세기%))
        const UInt32 slow = monster->Status().SlowPercent(now);
        const UInt32 swift = monster->Status().Power(StatusId::Swift, now);
        const UInt32 ms = monster->GetMoveIntervalMs() * (100u + slow) / 100u * 100u / (100u + swift);
        return std::max<UInt32>(ms, MonsterTable::kMinActIntervalMs / 2);
    }

    UInt32 AttackDelayMs(const MonsterSharedPtr& monster, UInt32 now)
    {
        // 빙결된 몬스터는 공격이 느리다
        const UInt32 frozen = monster->Status().Has(StatusId::Freeze, now) ? 2u : 1u;
        const UInt32 swift = monster->Status().Power(StatusId::Swift, now);
        const UInt32 ms = monster->GetAttackIntervalMs() * frozen * 100u / (100u + swift);
        return std::max<UInt32>(ms, MonsterTable::kMinActIntervalMs);
    }

    UInt32 DisabledUntil(const MonsterSharedPtr& monster, UInt32 now, bool forMove)
    {
        const bool blocked = forMove ? monster->Status().BlocksMove(now) : monster->Status().BlocksAttack(now);
        if (!blocked)
            return 0;
        UInt32 until = now + 60;
        for (auto& slot : monster->Status().Slots())
            if (slot.stacks > 0 && static_cast<Int32>(slot.until - now) > 0)
            {
                const StatusRow& row = StatusTable::Get(slot.id);
                if ((forMove && row.blocksMove) || (!forMove && row.blocksAttack))
                    if (static_cast<Int32>(slot.until - until) > 0)
                        until = slot.until;
            }
        // 풀린 직후 바로 움직이게 조금 뒤에 — 너무 길게 자지는 않는다(기절이 지워지거나 줄면 그때 다시 본다)
        return now + std::clamp<UInt32>(until - now + 30, 60, 1500);
    }

    void HandleRespawn(const ObjID& monsterId)
    {
        // Reuse the existing monster object so the ID stays the same.
        // The stress-test client tracks dead monsters by ID: if we send
        // RESPAWN_NFY with the original ID it can decrement g_monster_dead.
        // Creating a brand-new ID (old behaviour) left the dead entry in
        // g_monsterMap forever and g_monster_dead would only go up.
        auto monster = ::GetGameObject<CMonster>(monsterId);
        if (monster == nullptr) return;

        CEntityBehaviorHelper::Sleep(monster);   // 죽을 때 이미 잠들었다 — 혹시 남았으면 돌려준다
        // 정예는 재등장마다 새로 굴린다(몬스터는 Zone을 넘지 않는다 — 지역 단계는 죽은 Zone 그대로). 최대 HP가 바뀌니 HP는 그 뒤에 채운다.
        const bool wasElite = monster->IsElite();
        const bool elite = RollElite(monster);
        monster->GetStat()->SetHp(monster->GetStat()->GetMaxHp());
        monster->m_DamageLedger.Clear();         // 새 생애 — 지난 생애의 피해 기록(파티 경험치 몫)은 버린다
        monster->GetStat()->SetDead(false);
        monster->ClearPath();

        ObjID mutableId = monsterId;
        if (monster->IsBoss())
        {
            // 보스는 정해진 자리에 다시 나타난다(스폰 지점 = 레이드 장소).
            CSectorHelper::UpdatePosition(mutableId, monster->GetSpawnX(), monster->GetSpawnY());
        }
        else
        {
            CSectorHelper::GetRandomPosition(mutableId);
            monster->SetSpawn(monster->GetX(), monster->GetY());
        }

        GSector->ForEachObjectWithin(monster->GetSectorX(), monster->GetSectorY(), monster->GetX(), monster->GetY(), GViewRange, EnumCategory::eUser, [&](const SubjectSharedPtr& object)
        {
            ObjID id = object->GetObjID();
            if (id.GetCategory<EnumCategory>() != EnumCategory::eUser) return;

            auto user = static_pointer_cast<CUser>(object);
            auto session = user->GetGameSession();
            if (!session || session->m_eState != SOCKET_STATE::ST_INGAME) return;
            if (!CSubjectHelper::CanSee(object, monster)) return;

            CUserHelper::SendSUBJECT_RESPAWN_NFY(user, monster);
            // 클라는 최대 HP를 종족에서 짐작한다 — 정예(배율 · 표시)이거나 직전 생애가 정예였으면(되돌림) 바로 알려 준다
            if (elite || wasElite)
                CUserHelper::SendSUBJECT_INFO_NFY(user, monster);
        });

        if (monster->IsBoss())
        {
            CUserHelper::BroadcastEffect(monster, monsterId, EffectId::BossSpawn, monster->GetX(), monster->GetY(), 0);
            CUserHelper::BroadcastWorldNotice(WorldNoticeCode::BossSpawned, monster->GetSpecies()->id, monster->GetZoneId(), "");
        }
    }

    bool RollElite(const MonsterSharedPtr& monster)
    {
        if (monster == nullptr)
            return false;
        const WorldData::RegionRow* region = WorldData::FindRegion(monster->GetZoneId());
        const bool candidate = region != nullptr
            && LootRoll::EliteCandidate(monster->IsBoss(), monster->IsDummy(), region->tier, EconRule::EliteMinTier());
        const bool elite = LootRoll::RollElite(candidate, EconRule::ElitePermille(), [] { return static_cast<UInt32>(LRng()); });
        monster->SetElite(elite);
        return elite;
    }

    void InitForZone(ZoneId zoneId)
    {
        const WorldData::RegionRow* region = WorldData::FindRegion(zoneId);

        // stride 분배: zoneId, zoneId+ZoneCount, zoneId+2*ZoneCount, ...
        // 각 존이 전체 ID 범위에서 균등한 간격으로 몬스터를 가져간다. 종족은 지역의 스폰
        // 가중치로 정하고, ObjID의 KIND에 실어 둔다 — 클라이언트는 KIND만 보고도 무엇을
        // 그릴지 안다(기존 패킷 구조 변경 없음).
        const int stride = static_cast<int>(CZoneLayout::ZoneCount);
        int elites = 0;
        for (Int32 i = zoneId; i < MAX_MONSTER; i += stride)
        {
            ObjID monsterId(EnumCategory::eMonster, MONSTER_ID_START + static_cast<UInt64>(i));
            if (region != nullptr)
                monsterId.SetKIND(PickSpecies(*region));
            auto newMonster = MakeNewSubject(monsterId, ObjID::npos);
            ASSERT_CRASH(GGameObjectManager->Insert(monsterId, newMonster));
            CSectorHelper::GetRandomPosition(monsterId);  // GSector는 호출 전에 CZone::Run()이 설정

            auto monster = ::GetGameObject<CMonster>(monsterId);
            if (monster)
            {
                monster->SetSpawn(monster->GetX(), monster->GetY());
                if (RollElite(monster))   // 첫 스폰도 정예일 수 있다(자리를 정한 뒤 — 지역 단계)
                {
                    monster->GetStat()->SetHp(monster->GetStat()->GetMaxHp());
                    ++elites;
                }
            }
        }

        // 필드 보스 — 일반 몬스터 ID 구간 뒤쪽(MONSTER_ID_START + MAX_MONSTER + zoneId)을 쓴다.
        if (region != nullptr && region->bossSpecies != 0)
        {
            ObjID bossId(EnumCategory::eMonster, MONSTER_ID_START + static_cast<UInt64>(MAX_MONSTER) + zoneId);
            bossId.SetKIND(region->bossSpecies);
            auto newBoss = MakeNewSubject(bossId, ObjID::npos);
            ASSERT_CRASH(GGameObjectManager->Insert(bossId, newBoss));
            CSectorHelper::UpdatePosition(bossId, region->bossX, region->bossY);
            if (auto boss = ::GetGameObject<CMonster>(bossId))
                boss->SetSpawn(region->bossX, region->bossY);
        }

        // Phase 2-E: 마을 훈련장 허수아비 — 보스 구간 뒤(MAX_MONSTER + 10000 + k)의 id.
        // 여느 몬스터처럼 보이지만(SUBJECT_ADD_NFY) 움직이지 않는다.
        if (zoneId == static_cast<ZoneId>(WorldData::kTownZoneId))
            for (int k = 0; k < static_cast<int>(WorldData::DummySpots().size()); ++k)
            {
                ObjID dummyId(EnumCategory::eMonster, MONSTER_ID_START + static_cast<UInt64>(MAX_MONSTER) + 10000u + static_cast<UInt64>(k));
                dummyId.SetKIND(MonsterTable::Id::TrainingDummy);
                auto dummy = MakeNewSubject(dummyId, ObjID::npos);
                ASSERT_CRASH(GGameObjectManager->Insert(dummyId, dummy));
                CSectorHelper::UpdatePosition(dummyId, WorldData::DummySpots()[k].x, WorldData::DummySpots()[k].y);
                if (auto m = ::GetGameObject<CMonster>(dummyId))
                {
                    m->SetSpawn(WorldData::DummySpots()[k].x, WorldData::DummySpots()[k].y);
                    m->UpdateFacing(-1);   // 광장 쪽(서쪽)을 본다
                }
            }

        cout << "[Zone " << zoneId << "] Monster Init Success ("
             << (MAX_MONSTER / stride) << " monsters, " << elites << " elite"
             << ((region != nullptr && region->bossSpecies != 0) ? " + boss" : "") << ")\n";
    }
}
