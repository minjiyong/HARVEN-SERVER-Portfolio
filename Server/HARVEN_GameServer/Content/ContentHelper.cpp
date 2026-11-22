#include "pch.h"
#include "Content/Transfer/TransferHelper.h"
#include "Content/ServiceCommon.h"
#include "Database/DBConnection.h"
#include "Network/GameSessionManager.h"
#include "Content/Npc/NpcHelper.h"
#include "Content/ContentHelper.h"
#include "Content/Combat/CombatHelper.h"

#include "User/User.h"
#include "Entity/Subject/Monster/Monster.h"
#include "User/UserHelper.h"
#include "Entity/Subject/SubjectHelper.h"
#include "Content/Field/SectorHelper.h"
#include "Entity/Subject/Monster/MonsterHelper.h"
#include "Content/Field/Sector.h"
#include "Worker/TimerThread.h"
#include "Database/DBThread.h"
#include "Base/Core/CoreTLS.h"
#include "Content/Navigator/Collision.h"
#include "Content/Inventory/ItemHelper.h"
#include "Database/GameData/Item/ItemTable.h"
#include "Database/GameData/ItemRules.h"
#include "Database/GameData/Item/ItemTableData.h"
#include "Content/Inventory/Item/EquipmentItem.h"
#include "Content/Skill/UserSkillHelper.h"
#include "Content/Field/ZoneLayout.h"
#include "Content/Field/ZoneManager.h"
#include "Database/GameData/LevelTable.h"
#include "Database/GameData/WorldData.h"
#include "Database/GameData/MonsterTable.h"
#include "Content/Party/PartyHelper.h"
#include "Content/Achieve/AchieveHelper.h"
#include "Content/Economy/EconomyLog.h"

namespace
{
    void ApplyPvpHit(const UserSharedPtr& attacker, const UserSharedPtr& victim, UInt32 rawDamage, UInt8 baseFlags,
        UInt16 skillId, UInt8 comboId, UInt8 flags2, UInt8 hitIndex, bool victimInnocent);

    void FinishRespawn(const UserSharedPtr& player)
    {
        ObjID playerId = player->GetObjID();
        CUserHelper::SendSUBJECT_RESPAWN_NFY(player, player);
        CSectorHelper::NotifyPlayerEnteredWorld(playerId, true);
        CUserHelper::BroadcastEffect(player, playerId, EffectId::Restart, player->GetX(), player->GetY(), 0);
        CUserHelper::SendUSER_INFO_INF(player);
        CCombatHelper::SendResource(player);
        CPartyHelper::OnRevived(player);   // 파티원에게 부활 알림 · 다시 선 자리
        CAchieveHelper::OnEnterRegion(player);   // 개인 업적: 마을에서 다시 섰다(다른 지역이면 들어가 본 지역)
        GTimerThread->ScheduleAfter(playerId, 5s, TIMER_EVENT_TYPE::EV_HEAL);
    }

    // 부활 · 귀환 자리 — TownSpawn은 R104(한 행 이상)가 막지만, 검증을 건너뛴 판에서도 0으로 나누지 않게
    // 비었으면 마을 지역(Region)의 길잡이 도착 지점으로 보낸다.
    WorldData::Point16 PickTownSpawn()
    {
        const auto spawns = WorldData::TownSpawnPoints();
        if (!spawns.empty())
            return spawns[LRng() % spawns.size()];
        if (const WorldData::RegionRow* region = WorldData::FindRegion(WorldData::kTownZoneId))
            return { region->landX, region->landY };
        return { static_cast<Int16>(WorldData::kTownX0 + WorldData::kTownW / 2), static_cast<Int16>(WorldData::kTownY0 + WorldData::kTownH / 2) };
    }

}

namespace CContentHelper
{
    void ApplyPvpDamage(const UserSharedPtr& attacker, const UserSharedPtr& victim, UInt32 damage, UInt8 flags,
        UInt16 skillId, UInt8 comboId, UInt8 flags2, UInt8 hitIndex)
    {
        if (!attacker || !victim || victim->GetStat()->IsDead() || victim->IsTransferring())
            return;
        if (WorldData::IsSafeZone(victim->GetX(), victim->GetY()))
        {
            CUserHelper::SendSystem(attacker, SystemMessageCode::SafeZoneNoCombat);
            return;
        }
        const UInt32 now = GetNowTime();
        const bool victimInnocent = !victim->IsPurple(now) && victim->GetAlignment() >= 0;
        if (victimInnocent)
        {
            const bool wasPurple = attacker->IsPurple(now);
            attacker->m_PurpleUntil = now + AlignmentRule::kPurpleDurationMs;
            GTimerThread->ScheduleAfter(attacker->GetObjID(), std::chrono::milliseconds(AlignmentRule::kPurpleDurationMs + 50), TIMER_EVENT_TYPE::EV_PURPLE_EXPIRE);
            if (!wasPurple)
            {
                CUserHelper::SendSystem(attacker, SystemMessageCode::BecamePurple, static_cast<Int32>(AlignmentRule::kPurpleDurationMs));
                CUserHelper::BroadcastInfo(attacker);
            }
        }
        // V2 방어 적용
        damage = StatRule::Mitigate(damage, victim->m_Derived.defense, attacker->m_Derived.penetration);
        if (victim->Status().Has(StatusId::Evade, now))
        {
            CUserHelper::BroadcastDamage(victim, attacker->GetObjID(), 0, static_cast<UInt8>(flags | DAMAGE_PVP), skillId, 0, static_cast<UInt8>(flags2 | DAMAGE2_EVADE), hitIndex);
            return;
        }
        if (victim->Status().Has(StatusId::Guard, now))
        {
            damage = std::max<UInt32>(1, damage / 2);
            flags2 |= DAMAGE2_GUARD;
        }
        ApplyPvpHit(attacker, victim, damage, flags, skillId, comboId, flags2, hitIndex, victimInnocent);
    }
}

namespace
{
    void ApplyPvpHit(const UserSharedPtr& attacker, const UserSharedPtr& victim, UInt32 rawDamage, UInt8 baseFlags,
        UInt16 skillId, UInt8 comboId, UInt8 flags2, UInt8 hitIndex, bool victimInnocent)
    {
        const UInt16 damage = static_cast<UInt16>(std::clamp<UInt32>(rawDamage, 1, 65535));
        victim->GetStat()->TakeDamage(damage);

        const UInt8 flags = static_cast<UInt8>(DAMAGE_PVP | baseFlags | (victim->GetStat()->IsDead() ? DAMAGE_KILLED : 0));

        const ObjID attackerId = attacker->GetObjID();
        CUserHelper::BroadcastDamage(victim, attackerId, damage, flags, skillId, comboId, flags2, hitIndex);

        if (!victim->GetStat()->IsDead())
            return;

        // 무고한 플레이어를 죽이면 PK — 성향치가 크게 떨어지고 악명(빨간 이름)이 된다.
        if (victimInnocent)
        {
            const Int16 before = attacker->GetAlignment();
            const Int16 after = AlignmentRule::Clamp(static_cast<Int32>(std::min<Int16>(before, 0)) - AlignmentRule::kPkPenalty);
            attacker->SetAlignment(after);
            attacker->SetPkCount(static_cast<UInt16>(std::min<UInt32>(attacker->GetPkCount() + 1u, 65535u)));
            CContentHelper::SaveAlignment(attacker, true);
            CUserHelper::SendSystem(attacker, SystemMessageCode::BecameChaotic, after);
            CUserHelper::BroadcastEffect(attacker, attackerId, EffectId::PlayerKill, attacker->GetX(), attacker->GetY(), 0);
            CUserHelper::BroadcastInfo(attacker);
            CUserHelper::SendUSER_INFO_INF(attacker);
            CUserHelper::BroadcastWorldNotice(WorldNoticeCode::PlayerKilled, attacker->GetZoneId(), 0, attacker->GetName());
        }

        CContentHelper::HandlePlayerDeath(victim, attackerId, true);
    }
}

namespace CContentHelper
{

    void HandlePlayerDeath(const UserSharedPtr& victim, const ObjID& killerId, bool killedByPlayer)
    {
        if (victim == nullptr || victim->GetSectorX() < 0)
            return;  // 이미 섹터에서 빠진 상태(중복 처리 방지)
        CTransferHelper::Cancel(victim);

        const ObjID victimId = victim->GetObjID();
        const short x = victim->GetX();
        const short y = victim->GetY();

        // 사망 알림(본인 포함 — 본인도 CanSee(victim, victim)이 참이다)
        GSector->ForEachNeighborObject(victim->GetSectorX(), victim->GetSectorY(), EnumCategory::eUser, [&](const SubjectSharedPtr& object)
        {
            ObjID id = object->GetObjID();
            if (id.GetCategory<EnumCategory>() != EnumCategory::eUser) return;
            if (!CSubjectHelper::CanSee(victim, object)) return;
            auto viewer = static_pointer_cast<CUser>(object);
            auto session = viewer->GetGameSession();
            if (!session || session->m_eState != SOCKET_STATE::ST_INGAME) return;
            CUserHelper::SendSUBJECT_DIE_NFY(viewer, victim);
        });

        // 경험치 페널티 — 몬스터에게 죽었을 때만(리니지처럼 PvP 사망은 경험치를 잃지 않는다)
        if (!killedByPlayer)
        {
            const UInt32 lost = victim->GetStat()->LoseExp(
                LevelTable::DeathExpPenalty(victim->GetStat()->GetLevel(), victim->GetStat()->GetExp()));
            if (lost > 0)
            {
                CUserHelper::SendUSER_STAT_CHANGE_INF(victim);
                CUserHelper::SendSystem(victim, SystemMessageCode::ExpLost, static_cast<Int32>(lost));
            }
        }

        // 악명(성향치 음수)은 죽을 때 소지품을 떨어뜨릴 수 있다 — 장착 중인 장비도 예외가 아니다.
        if (victim->GetAlignment() < 0 && (LRng() % 100) < AlignmentRule::kChaoticDropPercent)
        {
            const UInt32 roll = static_cast<UInt32>(LRng());
            if (CItem::SharedPtr dropped = victim->GetInventory()->TryTakeRandomItem(roll))
            {
                const CEquipmentItem* equipment = AsEquipment(dropped);
                const Int32 enchant = equipment != nullptr ? equipment->GetEnchant() : 0;
                const Int32 itemId = dropped->GetItemTableId();
                CItemHelper::SpawnFieldItem(dropped, x, y);
                CUserHelper::SendSystem(victim, SystemMessageCode::ItemDroppedOnDeath, itemId, enchant);
                CUserHelper::BroadcastEquipChange(victim);
            }
        }

        CPartyHelper::OnDied(victim);   // 파티원에게 사망 알림(쓰러진 자리) — 섹터에서 빠지면 Zone 틱이 더 보지 않는다
        CAchieveHelper::OnDeath(victim);   // 개인 업적(쓰러짐)

        ObjID mutableId = victimId;
        GSector->RemoveObject(mutableId, victim->RefSectorX(), victim->RefSectorY());

        // 부활 타이머 — 보통 먼저 USER_RESTART_REQ가 온다(타이머는 안전장치 — STRESS_TEST 봇은 이 타이머로 다시 선다).
        // 타이머에 사망 번호를 실어 보내, 그 사이 리스타트/재사망했으면 무시되게 한다.
        ++victim->m_DeathSeq;
        GTimerThread->ScheduleAfter(victimId, 30s, TIMER_EVENT_TYPE::EV_USER_RESPAWN,
            ObjID(EnumCategory::eNone, static_cast<UInt64>(victim->m_DeathSeq)));

        CUserHelper::SendUSER_INFO_INF(victim);
        (void)killerId;
    }

    void OnMonsterKilled(const UserSharedPtr& killer, const MonsterSharedPtr& monster)
    {
        if (killer == nullptr || monster == nullptr)
            return;

        const MonsterTableRow* species = monster->GetSpecies();
        const UInt8 level = species != nullptr ? species->level : 1;

        // 사냥으로 성향치가 오른다(명예 회복 — 악명도 사냥으로 속죄할 수 있다).
        const Int16 before = killer->GetAlignment();
        if (before < AlignmentRule::kMax)
        {
            const Int16 after = AlignmentRule::Clamp(static_cast<Int32>(before) + AlignmentRule::GainForMonster(level));
            killer->SetAlignment(after);
            if (before < 0 && after >= 0)
                CUserHelper::SendSystem(killer, SystemMessageCode::AlignmentRecovered, after);
            if (AlignmentRule::ColorOf(before, false) != AlignmentRule::ColorOf(after, false))
                CUserHelper::BroadcastInfo(killer);
            SaveAlignment(killer, false);
        }

        if (species != nullptr && species->boss)
            CUserHelper::BroadcastWorldNotice(WorldNoticeCode::BossKilled, species->id, monster->GetZoneId(), killer->GetName());
    }

    void HandleRestartRequest(const UserSharedPtr& player)
    {
        if (player == nullptr || !player->GetStat()->IsDead())
            return;
        RestartInTown(player);
    }

    void RestartInTown(const UserSharedPtr& player)
    {
        if (player == nullptr || !player->GetStat()->IsDead())
            return;
        if (player->GetInventory()->IsTransferCommitting()) return;
        CTransferHelper::Cancel(player);

        // 부하 시험 모드는 죽은 Zone 안에서 다시 선다 — 봇이 전부 마을로 몰리면 Zone 분산 측정이 깨진다.
        WorldData::Point16 spawn = PickTownSpawn();
        if (GLoadTestMode)
        {
            const auto [x, y] = CSectorHelper::PickRandomPositionInZone(true);
            spawn = { x, y };
        }
        ++player->m_DeathSeq;  // 대기 중인 부활 타이머 무효화
        player->GetStat()->SetDead(false);
        player->GetStat()->SetHp(player->GetStat()->GetMaxHp());
        player->Status().Clear();
        CUserSkillHelper::Clear(player);   // 진행 중이던 시전 · 일정(차지 · 홀딩 · 토글 포함)을 버린다
        player->m_Mp = player->m_Derived.maxMp;
        player->m_MoveClock = 0;

        const ObjID playerId = player->GetObjID();
        const ZoneId townZone = CZoneLayout::GetZoneIdByWorld(spawn.x, spawn.y);
        if (townZone == player->GetZoneId())
        {
            ObjID mutableId = playerId;
            CSectorHelper::UpdatePosition(mutableId, spawn.x, spawn.y);
            FinishRespawn(player);
            return;
        }

        // 다른 Zone(마을)으로 — 기존 Zone Transfer(CSectorHelper::HandleZoneTransfer)와 같은
        // 순서로 소유권을 넘긴다. 죽은 플레이어는 이미 섹터에 없으므로 A Zone 쪽 정리는 없다.
        // 1) Transferring 표시 → 2) 좌표/Zone 매핑 갱신 → 3) B Zone 큐에 입장 위임(FIFO) → 4) 라우팅을 B로(입장 위임 뒤 — 새 패킷이 그 뒤에 선다)
        player->SetTransferring(true);
        player->SetPosition(spawn.x, spawn.y);
        player->SetZoneId(townZone);
        GZoneManager->UpdateObjectZone(playerId, townZone);
        const SessionSharedPtr session = player->GetGameSession();

        GZoneManager->EnqueueByZone(townZone, [playerId, x = spawn.x, y = spawn.y]()
        {
            auto p = ::GetGameObject<CUser>(playerId);
            if (p == nullptr)
                return;  // 이동 중 접속 종료

            ObjID mutableId = playerId;
            const bool valid = GSector->UpdateObjectSectorAndPosition(p, x, y);
            if (!valid)
                CSectorHelper::GetRandomPosition(mutableId);

            FinishRespawn(p);
            p->SetTransferring(false);
        });
    }

    void TeleportPlayer(const UserSharedPtr& player, short x, short y)
    {
        if (player == nullptr || player->GetStat()->IsDead() || player->IsTransferring())
            return;
        if (player->GetInventory()->IsTransferCommitting()) return;
        CTransferHelper::Cancel(player);

        const ObjID playerId = player->GetObjID();
        CUserHelper::BroadcastEffect(player, playerId, EffectId::TeleportOut, player->GetX(), player->GetY(), 0);

        const ZoneId destZone = CZoneLayout::GetZoneIdByWorld(x, y);
        const bool crossZone = destZone != player->GetZoneId();

        // 같은 Zone이면 시야 갱신(REMOVE/ADD)까지, 다른 Zone이면 Zone Transfer까지
        // 기존 이동 처리 함수가 그대로 해 준다. 걸음 시간 0 = 보간 없이 바로 옮김.
        player->SetLastStepMs(0);
        player->m_MoveClock = 0;
        CSectorHelper::HandlePlayerMove(player, x, y);

        if (crossZone)
        {
            // B Zone 큐는 FIFO라 HandleEnterZone보다 반드시 뒤에 실행된다.
            GZoneManager->EnqueueByZone(destZone, [playerId]()
            {
                auto p = ::GetGameObject<CUser>(playerId);
                if (p != nullptr && !p->IsTransferring())
                    CUserHelper::BroadcastEffect(p, playerId, EffectId::TeleportIn, p->GetX(), p->GetY(), 0);
            });
        }
        else
        {
            CUserHelper::BroadcastEffect(player, playerId, EffectId::TeleportIn, player->GetX(), player->GetY(), 0);
        }
    }

    void UseEffectItem(const UserSharedPtr& player, UInt16 slotIndex)
    {
        auto inventory = player->GetInventory();
        const auto& slots = inventory->GetSlots();
        const auto it = slots.find(slotIndex);
        if (it == slots.end())
        {
            CUserHelper::SendITEM_USE_ACK(player, slotIndex, false);
            return;
        }

        const ItemTableRow* row = ItemTable::Find(it->second->GetItemTableId());
        ASSERT_CRASH(row != nullptr);

        bool success = false;
        switch (row->effect)
        {
        case ItemEffect::eReturnTown:
        {
            if (!inventory->TryConsumeOne(slotIndex, ItemEffect::eReturnTown))
                break;
            success = true;
            CUserHelper::SendITEM_USE_ACK(player, slotIndex, true);
            const auto& spawn = PickTownSpawn();
            TeleportPlayer(player, spawn.x, spawn.y);
            CUserHelper::SendSystem(player, SystemMessageCode::ReturnedToTown);
            return;
        }
        case ItemEffect::eTeleportRandom:
        {
            // 지금 서 있는 지역(Zone) 안의 무작위 칸. 막힌 칸/마을은 피한다.
            const ZoneCoord zone = CZoneLayout::GetZoneCoordById(player->GetZoneId());
            const int minX = zone.x * WorldData::kRegionSize + ZONE_BOUNDARY_MARGIN + 1;
            const int minY = zone.y * WorldData::kRegionSize + ZONE_BOUNDARY_MARGIN + 1;
            const int span = WorldData::kRegionSize - (ZONE_BOUNDARY_MARGIN + 1) * 2;
            short destX = -1, destY = -1;
            for (int attempt = 0; attempt < 64; ++attempt)
            {
                const int x = minX + static_cast<int>(LRng() % static_cast<UInt32>(span));
                const int y = minY + static_cast<int>(LRng() % static_cast<UInt32>(span));
                if (isCollision(static_cast<short>(x), static_cast<short>(y)) || WorldData::IsSafeZone(x, y))
                    continue;
                destX = static_cast<short>(x);
                destY = static_cast<short>(y);
                break;
            }
            if (destX < 0 || !inventory->TryConsumeOne(slotIndex, ItemEffect::eTeleportRandom))
                break;
            success = true;
            CUserHelper::SendITEM_USE_ACK(player, slotIndex, true);
            TeleportPlayer(player, destX, destY);
            CUserHelper::SendSystem(player, SystemMessageCode::Teleported, player->GetZoneId());
            return;
        }
        case ItemEffect::eHaste:
        {
            if (!inventory->TryConsumeOne(slotIndex, ItemEffect::eHaste))
                break;
            success = true;
            const UInt32 now = GetNowTime();
            const UInt32 durationMs = static_cast<UInt32>(row->effectValue) * 1000u;
            player->m_HasteUntil = now + durationMs;
            GTimerThread->ScheduleAfter(player->GetObjID(), std::chrono::milliseconds(durationMs + 50), TIMER_EVENT_TYPE::EV_HASTE_EXPIRE);
            CUserHelper::BroadcastEffect(player, player->GetObjID(), EffectId::HasteOn, player->GetX(), player->GetY(), static_cast<Int32>(row->effectValue));
            CUserHelper::BroadcastInfo(player);
            CUserHelper::SendSystem(player, SystemMessageCode::HasteStart, row->effectValue);
            CUserHelper::SendUSER_INFO_INF(player);
            break;
        }
        default:
            break;
        }

        CUserHelper::SendITEM_USE_ACK(player, slotIndex, success);
    }

    void HandleEnchant(const UserSharedPtr& player, const ITEM_ENCHANT_REQ_PACKET& request)
    {
        if (!player) return;
        auto reply = [&](EnchantResult result) {
            ENCHANT_ACK_PACKET p{};
            Service::Init(p, PacketType::ENCHANT_ACK);
            p.seq = request.seq; p.result = static_cast<UInt8>(result); p.targetSlot = request.targetSlot;
            Service::SendTo(player->GetGameSession(), p);
        };
        if (request.seq == 0 || request.seq <= player->m_LastEnchantSeq) return reply(EnchantResult::Duplicate);
        player->m_LastEnchantSeq = request.seq;
        if (player->GetStat()->IsDead() || player->IsTransferring()) return reply(EnchantResult::Invalid);
        if (request.npcId != EnchantRule::kBlacksmithNpcId || !CNpcHelper::IsNear(request.npcId, player->GetX(), player->GetY()))
            return reply(EnchantResult::NotAtBlacksmith);
        if (player->GetInventory()->IsBusy() || !player->m_PendingAuctionSeqs.empty()) return reply(EnchantResult::Busy);   // 강화 · 제작 · 분해 · 경매 진행 중
        const auto plan = player->GetInventory()->PrepareEnchant(request.ticketSlot, request.targetSlot, request.protectionSlot, EnchantRule::Roll(LRng));
        if (plan.outcome.result == EnchantResult::Invalid || plan.outcome.result == EnchantResult::Busy) return reply(plan.outcome.result);
        const auto first = std::make_shared<bool>(false);
        const auto ctx = Service::MakeCtx(player);
        GDBThread->RequestTx(ctx.playerId, "EnchantTransaction",
            [plan, first, pid = ctx.playerId](CDBConnection& c) { return c.SaveEnchant(pid, plan.save, *first); },
            [plan, first, ctx, request](bool ok) {
                // 증발(보호권 없는 실패)은 장비 소멸 — 경제 로그(커밋이 승부를 정한다)
                if (ok && plan.outcome.result == EnchantResult::Evaporated)
                    if (const ItemTableRow* row = ItemTable::Find(plan.outcome.itemId))
                        CEconomyLog::OnGearOut(GearSink::EnchantBreak, row->grade);
                // DB commit establishes the winner even if the character disconnected.
                if (ok && plan.outcome.result == EnchantResult::Success && plan.outcome.after >= EnchantRule::WorldNoticeLevel())
                {
                    ENCHANT_NOTICE_INF_PACKET p{};
                    Service::Init(p, PacketType::ENCHANT_NOTICE_INF);
                    p.itemId = plan.outcome.itemId; p.level = plan.outcome.after; p.first = *first ? 1 : 0;
                    strncpy_s(p.name, ctx.name.c_str(), _TRUNCATE);
                    for (const auto& s : GSessionManager->SnapshotSessions()) Service::SendTo(SessionWeakPtr(s), p);
                }
                Service::PostToOwner(ctx.obj, [plan, ok, ctx, request](const UserSharedPtr& u) {
                    if (!u) return;
                    if (ok) u->GetInventory()->CompleteEnchant(plan, true);
                    ENCHANT_ACK_PACKET p{};
                    Service::Init(p, PacketType::ENCHANT_ACK);
                    p.seq = request.seq; p.targetSlot = request.targetSlot; p.itemId = plan.outcome.itemId;
                    p.result = static_cast<UInt8>(ok ? plan.outcome.result : EnchantResult::StorageError);
                    p.before = plan.outcome.before; p.after = ok ? plan.outcome.after : plan.outcome.before;
                    Service::SendTo(ctx.session, p);
                    if (!ok)
                    {
                        // Keep the reserved slots locked until disconnect. A lost COMMIT reply is ambiguous.
                        // 방금 보낸 StorageError가 닿은 뒤에 닫는다(제작과 같은 CloseAfterFlush — 예전 CloseSession은 응답을 버려 클라가 끊김만 봤다).
                        if (const auto s = u->GetGameSession()) s->CloseAfterFlush();
                        return;
                    }
                    if (plan.outcome.wasEquipped) { CUserHelper::BroadcastEquipChange(u); CUserHelper::BroadcastInfo(u); }
                    if (plan.outcome.result == EnchantResult::Success)
                        CAchieveHelper::OnEnchant(u, plan.outcome.after);   // 개인 업적(강화 성공으로 도달한 단계)
                    CUserHelper::SendUSER_INFO_INF(u);
                    const bool success = plan.outcome.result == EnchantResult::Success;
                    if (plan.outcome.result != EnchantResult::Protected)
                        CUserHelper::BroadcastEffect(u, u->GetObjID(), success ? EffectId::EnchantSuccess : EffectId::EnchantFail,
                            u->GetX(), u->GetY(), static_cast<Int32>(std::min<UInt32>(success ? plan.outcome.after : plan.outcome.before, INT32_MAX)));
                });
            });
    }

    void GiveStarterKit(const UserSharedPtr& player)
    {
        using namespace ItemTableData::Id;
        auto inventory = player->GetInventory();

        // 새로 만드는 장비 — 획득 귀속 행이면 bit 8(경제 1차 명세 §1. 지금 시작 장비는 귀속이 아니다)
        const auto acquire = [](ItemTableId id) -> UInt64
        {
            const ItemTableRow* row = ItemTable::Find(id);
            return row != nullptr ? ItemRules::OnAcquire(*row, 0) : 0;
        };

        // 직업별 초보자 무기(서약기사 목검 / 각인술사 지팡이 / 사냥꾼 단검)
        std::vector<UInt16> touched;
        const ItemTableId weapon = ClassTable::Get(player->m_eClassId).starterWeapon;
        if (inventory->TryAddItem(weapon, 1, &touched, 0, acquire(weapon)) && !touched.empty())
            (void)inventory->TryEquip(touched.front());

        // V2: 누빔 갑옷 + 가죽 장화를 입혀서 시작한다(장비가 몸에 보이는 게임 — 첫 장비 교체의 기쁨은 곧 온다).
        for (const ItemTableId armor : { PaddedArmor, LeatherBoots })
        {
            touched.clear();
            if (inventory->TryAddItem(armor, 1, &touched, 0, acquire(armor)) && !touched.empty())
                (void)inventory->TryEquip(touched.front());
        }

        (void)inventory->TryAddItem(HerbPotion, 20);
        (void)inventory->TryAddItem(ReturnScroll, 3);
        (void)inventory->TryAddItem(OldwayScroll, 2);
        (void)inventory->TryAddItem(WindstepPotion, 2);
        (void)inventory->AddAdena(1000, AdenaReason::Init);
    }

    namespace
    {
        // 멱등성 검사: 이미 처리한 seq면 true(중복 — 무시해야 함). 아니면 기록하고 false.
        bool IsDuplicateShopRequest(const UserSharedPtr& player, UInt32 seq)
        {
            if (seq <= player->m_LastShopSeq)
                return true;
            player->m_LastShopSeq = seq;
            return false;
        }
    }

    void HandleShopBuy(const UserSharedPtr& player, UInt16 npcId, UInt16 itemId, UInt16 count, UInt32 seq)
    {
        auto reply = [&](ShopResult result, Int32 adenaDelta)
        {
            CUserHelper::SendSHOP_RESULT_ACK(player, ShopKind::Buy, result, seq, itemId, count, adenaDelta);
        };

        if (IsDuplicateShopRequest(player, seq))
            return reply(ShopResult::Duplicate, 0);
        if (player->GetStat()->IsDead() || player->IsTransferring())
            return reply(ShopResult::InvalidState, 0);

        const WorldData::NpcRow* npc = WorldData::FindNpc(npcId);
        if (npc == nullptr)
            return reply(ShopResult::InvalidNpc, 0);
        if (!CNpcHelper::IsNear(npc->id, player->GetX(), player->GetY()))
            return reply(ShopResult::TooFar, 0);
        if (!WorldData::ShopSells(npcId, itemId))
            return reply(ShopResult::InvalidItem, 0);

        const ItemTableRow* row = ItemTable::Find(itemId);
        if (row == nullptr || row->buyPrice == 0)
            return reply(ShopResult::InvalidItem, 0);
        if (count == 0 || count > row->maxStack)
            return reply(ShopResult::InvalidCount, 0);

        const UInt64 total64 = static_cast<UInt64>(row->buyPrice) * count;
        if (total64 > 2'000'000'000ull)
            return reply(ShopResult::InvalidCount, 0);
        const UInt32 total = static_cast<UInt32>(total64);

        UInt16 slot = MAX_INVENTORY_SLOTS;
        const ShopResult result = player->GetInventory()->TryBuy(itemId, count, total, &slot);
        if (result == ShopResult::NotEnoughAdena)
            CUserHelper::SendSystem(player, SystemMessageCode::NotEnoughAdena, static_cast<Int32>(total), static_cast<Int32>(player->GetInventory()->GetAdena()));
        else if (result == ShopResult::InventoryFull)
            CUserHelper::SendSYSTEM_MESSAGE_INF(player, SystemMessageCode::InventoryFull, itemId, count);

        reply(result, result == ShopResult::Success ? -static_cast<Int32>(total) : 0);
    }

    void HandleShopSell(const UserSharedPtr& player, UInt16 npcId, UInt16 slotIndex, UInt16 count, UInt32 seq)
    {
        UInt16 itemId = 0;
        {
            const auto& slots = player->GetInventory()->GetSlots();
            const auto it = slots.find(slotIndex);
            if (it != slots.end())
                itemId = it->second->GetItemTableId();
        }

        auto reply = [&](ShopResult result, Int32 adenaDelta)
        {
            CUserHelper::SendSHOP_RESULT_ACK(player, ShopKind::Sell, result, seq, itemId, count, adenaDelta);
        };

        if (IsDuplicateShopRequest(player, seq))
            return reply(ShopResult::Duplicate, 0);
        if (player->GetStat()->IsDead() || player->IsTransferring())
            return reply(ShopResult::InvalidState, 0);

        const WorldData::NpcRow* npc = WorldData::FindNpc(npcId);
        if (npc == nullptr || !WorldData::NpcBuysItems(*npc))
            return reply(ShopResult::InvalidNpc, 0);
        if (!CNpcHelper::IsNear(npc->id, player->GetX(), player->GetY()))
            return reply(ShopResult::TooFar, 0);

        UInt32 gain = 0;
        const ShopResult result = player->GetInventory()->TrySell(slotIndex, count, &gain);
        reply(result, result == ShopResult::Success ? static_cast<Int32>(gain) : 0);
    }

    void HandleNpcTeleport(const UserSharedPtr& player, UInt16 npcId, UInt16 destZone, UInt32 seq)
    {
        auto reply = [&](ShopResult result, Int32 adenaDelta)
        {
            CUserHelper::SendSHOP_RESULT_ACK(player, ShopKind::Teleport, result, seq, destZone, 0, adenaDelta);
        };

        if (IsDuplicateShopRequest(player, seq))
            return reply(ShopResult::Duplicate, 0);
        if (player->GetStat()->IsDead() || player->IsTransferring())
            return reply(ShopResult::InvalidState, 0);

        const WorldData::NpcRow* npc = WorldData::FindNpc(npcId);
        if (npc == nullptr || npc->kind != WorldData::NpcKind::eTeleporter)
            return reply(ShopResult::InvalidNpc, 0);
        if (!CNpcHelper::IsNear(npc->id, player->GetX(), player->GetY()))
            return reply(ShopResult::TooFar, 0);

        const WorldData::RegionRow* region = WorldData::FindRegion(destZone);
        if (region == nullptr || region->teleportFee == 0)
            return reply(ShopResult::InvalidItem, 0);

        if (!player->GetInventory()->TrySpendAdena(region->teleportFee, AdenaReason::Teleport))
        {
            CUserHelper::SendSystem(player, SystemMessageCode::NotEnoughAdena, static_cast<Int32>(region->teleportFee), static_cast<Int32>(player->GetInventory()->GetAdena()));
            return reply(ShopResult::NotEnoughAdena, 0);
        }

        reply(ShopResult::Success, -static_cast<Int32>(region->teleportFee));
        TeleportPlayer(player, region->landX, region->landY);
        CUserHelper::SendSystem(player, SystemMessageCode::Teleported, destZone);
    }

    void HandlePurpleExpire(const ObjID& playerId)
    {
        auto player = ::GetGameObject<CUser>(playerId);
        if (player == nullptr || player->IsPurple(GetNowTime()))
            return;  // 그 사이 다시 공격해서 연장됐다면 다음 타이머가 처리한다
        CUserHelper::BroadcastInfo(player);
        CUserHelper::SendUSER_INFO_INF(player);
    }

    void HandleHasteExpire(const ObjID& playerId)
    {
        auto player = ::GetGameObject<CUser>(playerId);
        if (player == nullptr || player->m_HasteUntil == 0 || player->IsHasted(GetNowTime()))
            return;
        player->m_HasteUntil = 0;
        CUserHelper::SendSystem(player, SystemMessageCode::HasteEnd);
        CUserHelper::BroadcastInfo(player);
        CUserHelper::SendUSER_INFO_INF(player);
    }

    void SaveAlignment(const UserSharedPtr& player, bool force)
    {
        if (player == nullptr)
            return;
        const Int16 current = player->GetAlignment();
        const int diff = abs(static_cast<int>(current) - static_cast<int>(player->m_SavedAlignment));
        const bool signChanged = (current < 0) != (player->m_SavedAlignment < 0);
        if (!force && diff < 50 && !signChanged)
            return;
        GDBThread->RequestSaveAlignment(player->GetPlayerId(), current, player->GetPkCount());
        player->m_SavedAlignment = current;
    }
}
