#include "pch.h"
#include "Content/Quest/QuestHelper.h"
#include "User/User.h"
#include "User/UserHelper.h"
#include "Network/GameSession.h"
#include "Base/GameObjectManager.h"
#include "Content/Field/Sector.h"
#include "Base/Core/CoreTLS.h"
#include "Content/Npc/NpcHelper.h"
#include "Content/Guild/GuildService.h"
#include "Database/GameData/Item/ItemTable.h"
#include "Database/GameData/QuestData.h"
#include "Database/GameData/ItemRules.h"
#include "Database/GameData/MonsterTable.h"
#include "Database/GameData/WorldData.h"
#include "Content/Economy/EconomyLog.h"
#include "Content/Achieve/AchieveHelper.h"

#include <ctime>

namespace
{
    using QuestData::QuestRow;
    using QuestData::ObjKind;

    template<typename Packet>
    void InitPacket(Packet& packet, PacketType type)
    {
        packet = {};
        packet.size = sizeof(Packet);
        packet.type = static_cast<char>(type);
    }

    // 반복 퀘스트 대기 시간은 벽시계(재접속 · 서버 재시작을 넘어 유지된다)
    UInt32 NowUnix() { return static_cast<UInt32>(std::time(nullptr)); }

    bool IsDone(const CUser& u, UInt16 id) { return QuestData::TestBit(u.m_QuestDone.data(), id); }

    void SetDone(CUser& u, UInt16 id)
    {
        if (id > 0 && id < 256)
            u.m_QuestDone[id >> 3] = static_cast<UInt8>(u.m_QuestDone[id >> 3] | (1u << (id & 7)));
    }

    int SlotIndexOf(const CUser& u, UInt16 id)
    {
        if (id == 0) return -1;
        for (size_t i = 0; i < u.m_Quests.size(); ++i)
            if (u.m_Quests[i].id == id) return static_cast<int>(i);
        return -1;
    }

    int FreeSlot(const CUser& u)
    {
        for (size_t i = 0; i < u.m_Quests.size(); ++i)
            if (u.m_Quests[i].id == 0) return static_cast<int>(i);
        return -1;
    }

    // 남은 대기(초). 지난 기록은 이참에 지운다.
    UInt32 CooldownRemain(CUser& u, UInt16 id, UInt32 now)
    {
        UInt32 remain = 0;
        auto& cds = u.m_QuestCooldowns;
        for (auto it = cds.begin(); it != cds.end();)
        {
            if (it->readyAtUnix <= now) { it = cds.erase(it); continue; }
            if (it->id == id) remain = (std::max)(remain, it->readyAtUnix - now);
            ++it;
        }
        return remain;
    }

    int CountEquipped(CUser& u)
    {
        auto inventory = u.GetInventory();
        if (inventory == nullptr) return 0;
        const CInventory::EquippedParts parts = inventory->GetEquippedParts();
        int n = 0;
        for (int s = 1; s < static_cast<int>(EquipSlot::Count); ++s)
            if (parts[s] < MAX_INVENTORY_SLOTS) ++n;
        return n;
    }

    template <typename Packet>
    void Post(const UserSharedPtr& u, const Packet& packet)
    {
        if (u == nullptr) return;
        if (auto session = u->GetGameSession())
            session->PostSend(packet);
    }

    void SendResult(const UserSharedPtr& u, UInt16 questId, QuestAction action, QuestResult result, UInt32 param = 0)
    {
        QUEST_RESULT_ACK_PACKET p;
        InitPacket(p, PacketType::QUEST_RESULT_ACK);
        p.questId = questId;
        p.action = static_cast<UInt8>(action);
        p.result = static_cast<UInt8>(result);
        p.param = param;
        Post(u, p);
    }

    QUEST_UPDATE_INF_PACKET MakeUpdate(UInt16 questId, const UInt16* progress, QuestEvent ev)
    {
        QUEST_UPDATE_INF_PACKET p;
        InitPacket(p, PacketType::QUEST_UPDATE_INF);
        p.questId = questId;
        p.event = static_cast<UInt8>(ev);
        p.objIndex = 0xFF;
        p.x = p.y = -1;
        if (progress)
            for (int i = 0; i < QuestData::kMaxObjs; ++i) p.progress[i] = progress[i];
        return p;
    }

    QuestResult CanAccept(CUser& u, const QuestRow& q, UInt32& param)
    {
        if (SlotIndexOf(u, q.id) >= 0) return QuestResult::NotAvailable;
        if (QuestData::IsRepeatable(q))
        {
            if (const UInt32 remain = CooldownRemain(u, q.id, NowUnix()); remain > 0)
            {
                param = remain;
                return QuestResult::Cooldown;
            }
        }
        else if (IsDone(u, q.id))
            return QuestResult::NotAvailable;
        if (q.prereq != 0 && !IsDone(u, q.prereq)) return QuestResult::NotAvailable;
        if (!QuestData::ClassCanTake(q, u.m_eClassId)) return QuestResult::WrongClass;
        if (u.GetStat()->GetLevel() < q.minLevel)
        {
            param = q.minLevel;
            return QuestResult::LevelTooLow;
        }
        if (FreeSlot(u) < 0) return QuestResult::LogFull;
        return QuestResult::Ok;
    }

    // 보상 희귀도: 적어도 표의 값(그리고 물건의 바닥), 가끔 한두 단계 위(제한된 무작위).
    Rarity RollRewardRarity(const ItemTableRow& row, Rarity base)
    {
        int r = (std::max)(static_cast<int>(ItemRules::FloorRarity(row)), static_cast<int>(base));
        const UInt32 roll = static_cast<UInt32>(LRng() % 100u);
        if (roll < 4) r += 2;
        else if (roll < 24) r += 1;
        return static_cast<Rarity>((std::min)(r, static_cast<int>(Rarity::Mythic)));
    }

    // 보상을 주고 끝낸다. 가방이 가득 차면 아무것도 바꾸지 않고 false.
    bool Finish(const UserSharedPtr& u, int slotIndex, const QuestRow& q)
    {
        auto& slot = u->m_Quests[static_cast<size_t>(slotIndex)];
        const UInt8 levelBefore = u->GetStat()->GetLevel();

        // 1) 물건(실패할 수 있는 단계를 맨 앞에 — 실패하면 아무것도 바뀌지 않는다)
        const UInt16 itemId = QuestData::RewardItem(q, u->m_eClassId);
        Rarity rarity = Rarity::Common;
        const ItemTableRow* gearReward = nullptr;   // 개인 업적(장비 획득) — 끝에서 퀘스트 완료와 함께 센다
        if (itemId != 0)
        {
            const ItemTableRow* row = ItemTable::Find(itemId);
            if (row == nullptr)
                return false;
            UInt64 extra = 0;
            if (row->type == ItemType::eEquipment)
            {
                rarity = RollRewardRarity(*row, q.reward.rarity);
                auto rng = []() { return static_cast<UInt32>(LRng()); };
                extra = ItemRules::OnAcquire(*row, ItemRules::RollInstance(*row, rarity, u->m_eClassId, rng).Pack());   // 획득 귀속이면 bit 8
            }
            std::vector<UInt16> touched;
            if (!u->GetInventory()->TryAddItem(itemId, q.reward.count, &touched, 0, extra))
                return false;
            if (row->type == ItemType::eEquipment)
            {
                CEconomyLog::OnGearIn(GearSource::Quest, row->grade, q.reward.count);
                gearReward = row;
            }
        }

        // 2) 기록
        UInt32 cooldownSec = 0;
        if (QuestData::IsRepeatable(q))
        {
            cooldownSec = static_cast<UInt32>(q.cooldownMin) * 60u;
            (void)CooldownRemain(*u, q.id, NowUnix());   // 지난 기록 정리
            u->m_QuestCooldowns.push_back({ q.id, NowUnix() + cooldownSec });
        }
        else
            SetDone(*u, q.id);
        const UInt16 lastProgress[QuestData::kMaxObjs] = { slot.progress[0], slot.progress[1], slot.progress[2] };
        slot = CUser::QuestSlot{};

        // 3) 알림(보상 내역) → 그로시 → 경험치(레벨업 연출은 완료 연출 뒤에 온다)
        const UInt32 exp = QuestData::RewardExp(q, levelBefore);
        auto p = MakeUpdate(q.id, lastProgress, QuestEvent::Completed);
        p.exp = exp;
        p.adena = q.reward.adena;
        p.itemId = itemId;
        p.itemCount = itemId != 0 ? q.reward.count : 0;
        p.rarity = static_cast<UInt8>(rarity);
        p.cooldownSec = cooldownSec;
        Post(u, p);

        if (q.reward.adena > 0)
            (void)u->GetInventory()->AddAdena(q.reward.adena, AdenaReason::Quest);
        if (exp > 0)
            CUserHelper::GainExp(u, exp);
        CUserHelper::SaveProgress(u);
        // 개인 업적 — 퀘스트 완료(0 · 이 퀘스트) · 보상 장비(그 등급 이상)
        if (gearReward != nullptr)
            CAchieveHelper::OnGear(u, gearReward->grade, q.reward.count);
        CAchieveHelper::OnQuest(u, q.id);

        if (q.id == QuestData::kFinalQuest)
            CUserHelper::BroadcastWorldNotice(WorldNoticeCode::StoryComplete, q.id, 0, u->GetName());
        return true;
    }

    // 목표 하나의 진행도를 value로(목표 수를 넘지 않게). 다 이루면 — 보고할 NPC가 없는
    // 퀘스트는 — 그 자리에서 끝낸다.
    void Advance(const UserSharedPtr& u, int slotIndex, int objIndex, int value, short x, short y)
    {
        auto& slot = u->m_Quests[static_cast<size_t>(slotIndex)];
        const QuestRow* q = QuestData::Find(slot.id);
        if (q == nullptr)
            return;
        const auto& obj = q->objs[objIndex];
        const UInt16 v = static_cast<UInt16>(std::clamp(value, 0, static_cast<int>(obj.count)));
        if (v == slot.progress[static_cast<size_t>(objIndex)])
            return;
        const bool reachedNow = v >= obj.count;
        slot.progress[static_cast<size_t>(objIndex)] = v;

        auto p = MakeUpdate(slot.id, slot.progress.data(), QuestEvent::Progress);
        p.objIndex = static_cast<UInt8>(objIndex);
        p.x = x;
        p.y = y;
        Post(u, p);

        if (!QuestData::ObjectivesDone(*q, slot.progress.data()))
        {
            if (reachedNow)
                CUserHelper::SaveProgress(u);
            return;
        }
        if (q->turnIn == 0)
        {
            if (!Finish(u, slotIndex, *q))
                SendResult(u, q->id, QuestAction::Complete, QuestResult::InventoryFull);
            return;
        }
        CUserHelper::SaveProgress(u);
    }

    bool ZoneMatches(const QuestData::Objective& o, int zoneId)
    {
        return o.zone == QuestData::kAnyZone || o.zone == zoneId;
    }
}

namespace CQuestHelper
{
    void SendList(const UserSharedPtr& user)
    {
        if (user == nullptr)
            return;
        QUEST_LIST_INF_PACKET p;
        InitPacket(p, PacketType::QUEST_LIST_INF);
        for (const auto& s : user->m_Quests)
        {
            if (s.id == 0 || p.activeCount >= kQuestListActiveMax) continue;
            auto& e = p.active[p.activeCount++];
            e.questId = s.id;
            for (int i = 0; i < QuestData::kMaxObjs; ++i) e.progress[i] = s.progress[static_cast<size_t>(i)];
        }
        std::memcpy(p.done, user->m_QuestDone.data(), sizeof(p.done));
        const UInt32 now = NowUnix();
        (void)CooldownRemain(*user, 0, now);   // 지난 기록 정리
        for (const auto& c : user->m_QuestCooldowns)
        {
            if (p.cooldownCount >= kQuestListCooldownMax) break;
            p.cooldowns[p.cooldownCount++] = QUEST_COOLDOWN{ c.id, c.readyAtUnix > now ? c.readyAtUnix - now : 0 };
        }
        Post(user, p);
    }

    void HandleAccept(const UserSharedPtr& user, UInt16 questId)
    {
        if (user == nullptr)
            return;
        auto reply = [&](QuestResult r, UInt32 param = 0) { SendResult(user, questId, QuestAction::Accept, r, param); };
        const QuestRow* q = QuestData::Find(questId);
        if (q == nullptr)
            return reply(QuestResult::Unknown);
        if (user->GetStat()->IsDead() || user->IsTransferring())
            return reply(QuestResult::Busy);
        UInt32 param = 0;
        if (const QuestResult r = CanAccept(*user, *q, param); r != QuestResult::Ok)
            return reply(r, param);
        if (!CNpcHelper::IsNear(q->giver, user->GetX(), user->GetY()))
            return reply(QuestResult::TooFar);

        const int si = FreeSlot(*user);
        auto& slot = user->m_Quests[static_cast<size_t>(si)];
        slot = CUser::QuestSlot{};
        slot.id = q->id;
        // 지금 상태로 정해지는 목표(입고 있는 장비)는 받는 순간의 값에서 시작한다.
        for (int i = 0; i < QuestData::kMaxObjs; ++i)
            if (q->objs[i].kind == ObjKind::Equip)
                slot.progress[static_cast<size_t>(i)] = static_cast<UInt16>((std::min)(CountEquipped(*user), static_cast<int>(q->objs[i].count)));

        Post(user, MakeUpdate(q->id, slot.progress.data(), QuestEvent::Accepted));
        reply(QuestResult::Ok);
        CUserHelper::SaveProgress(user);
        // 이미 그 자리에 서 있는 탐험 목표 등
        OnMoved(user);
    }

    void HandleComplete(const UserSharedPtr& user, UInt16 questId)
    {
        if (user == nullptr)
            return;
        auto reply = [&](QuestResult r, UInt32 param = 0) { SendResult(user, questId, QuestAction::Complete, r, param); };
        const QuestRow* q = QuestData::Find(questId);
        const int si = q != nullptr ? SlotIndexOf(*user, questId) : -1;
        if (si < 0)
            return reply(QuestResult::Unknown);
        if (user->GetStat()->IsDead() || user->IsTransferring())
            return reply(QuestResult::Busy);
        if (!QuestData::ObjectivesDone(*q, user->m_Quests[static_cast<size_t>(si)].progress.data()))
            return reply(QuestResult::NotReady);
        if (q->turnIn != 0 && !CNpcHelper::IsNear(q->turnIn, user->GetX(), user->GetY()))
            return reply(QuestResult::TooFar);
        if (!Finish(user, si, *q))
            return reply(QuestResult::InventoryFull);
        reply(QuestResult::Ok);
        CGuildHelper::OnQuestDone(user);   // 결사 임무(의뢰 해결)
    }

    void HandleAbandon(const UserSharedPtr& user, UInt16 questId)
    {
        if (user == nullptr)
            return;
        const int si = SlotIndexOf(*user, questId);
        if (si < 0)
            return SendResult(user, questId, QuestAction::Abandon, QuestResult::Unknown);
        user->m_Quests[static_cast<size_t>(si)] = CUser::QuestSlot{};
        Post(user, MakeUpdate(questId, nullptr, QuestEvent::Abandoned));
        SendResult(user, questId, QuestAction::Abandon, QuestResult::Ok);
        CUserHelper::SaveProgress(user);
    }

    void OnKill(const UserSharedPtr& killer, const MonsterTableRow* species, short x, short y)
    {
        if (killer == nullptr || species == nullptr)
            return;
        const int zoneId = WorldData::RegionIdAt(x, y);
        for (size_t si = 0; si < killer->m_Quests.size(); ++si)
        {
            auto& slot = killer->m_Quests[si];
            if (slot.id == 0) continue;
            const QuestRow* q = QuestData::Find(slot.id);
            if (q == nullptr) continue;
            for (int oi = 0; oi < QuestData::kMaxObjs; ++oi)
            {
                const auto& o = q->objs[oi];
                auto& progress = slot.progress[static_cast<size_t>(oi)];
                if (o.kind == ObjKind::None || progress >= o.count || !ZoneMatches(o, zoneId))
                    continue;
                switch (o.kind)
                {
                case ObjKind::Kill:
                    if (species->id == o.target)
                        Advance(killer, static_cast<int>(si), oi, progress + 1, x, y);
                    break;
                case ObjKind::KillIn:
                    Advance(killer, static_cast<int>(si), oi, progress + 1, x, y);
                    break;
                case ObjKind::Collect:
                {
                    if (species->id != o.target) break;
                    // 운 보정: 실패할 때마다 확률이 조금씩 오른다(끝없이 헤매지 않게)
                    auto& miss = slot.misses[static_cast<size_t>(oi)];
                    const UInt32 step = (std::max)(5u, static_cast<UInt32>(o.chance) / 3u);
                    const UInt32 chance = (std::min)(100u, o.chance + miss * step);
                    if (static_cast<UInt32>(LRng() % 100u) < chance)
                    {
                        miss = 0;
                        Advance(killer, static_cast<int>(si), oi, progress + 1, x, y);
                    }
                    else if (miss < 250)
                        ++miss;
                    break;
                }
                default:
                    break;
                }
                // Advance가 퀘스트를 끝냈으면(보고할 NPC 없음) 이 칸은 비었다
                if (slot.id == 0) break;
            }
        }
    }

    void OnBossKilled(UInt16 bossSpecies, short x, short y)
    {
        const int zoneId = WorldData::RegionIdAt(x, y);
        const int range = QuestData::kBossShareRange;
        std::vector<UserSharedPtr> nearby;
        const CSector& sector = *GSector;
        for (int sy = (y - range) / SECTOR_RANGE; sy <= (y + range) / SECTOR_RANGE; ++sy)
            for (int sx = (x - range) / SECTOR_RANGE; sx <= (x + range) / SECTOR_RANGE; ++sx)
            {
                if (!sector.IsValidSector(static_cast<short>(sx), static_cast<short>(sy)))
                    continue;
                sector.ForEachObjectInCell(static_cast<short>(sx), static_cast<short>(sy), EnumCategory::eUser, [&](const SubjectSharedPtr& object)
                {
                    auto user = static_pointer_cast<CUser>(object);
                    if (user->IsTransferring())
                        return;
                    if ((std::max)(std::abs(user->GetX() - x), std::abs(user->GetY() - y)) > range)
                        return;
                    nearby.push_back(std::move(user));
                });
            }

        for (const auto& user : nearby)
        {
            for (size_t si = 0; si < user->m_Quests.size(); ++si)
            {
                const auto& slot = user->m_Quests[si];
                if (slot.id == 0) continue;
                const QuestRow* q = QuestData::Find(slot.id);
                if (q == nullptr) continue;
                for (int oi = 0; oi < QuestData::kMaxObjs; ++oi)
                {
                    const auto& o = q->objs[oi];
                    if (o.kind == ObjKind::Boss && o.target == bossSpecies && ZoneMatches(o, zoneId) && slot.progress[static_cast<size_t>(oi)] < o.count)
                    {
                        Advance(user, static_cast<int>(si), oi, o.count, x, y);
                        break;
                    }
                }
            }
        }
    }

    void OnTalk(const UserSharedPtr& user, UInt16 npcId)
    {
        if (user == nullptr)
            return;
        for (size_t si = 0; si < user->m_Quests.size(); ++si)
        {
            const auto& slot = user->m_Quests[si];
            if (slot.id == 0) continue;
            const QuestRow* q = QuestData::Find(slot.id);
            if (q == nullptr) continue;
            for (int oi = 0; oi < QuestData::kMaxObjs && slot.id != 0; ++oi)
            {
                const auto& o = q->objs[oi];
                if (o.kind == ObjKind::Talk && o.target == npcId && slot.progress[static_cast<size_t>(oi)] < o.count)
                    Advance(user, static_cast<int>(si), oi, o.count, user->GetX(), user->GetY());
            }
        }
    }

    void OnMoved(const UserSharedPtr& user)
    {
        if (user == nullptr)
            return;
        const int x = user->GetX(), y = user->GetY();
        for (size_t si = 0; si < user->m_Quests.size(); ++si)
        {
            const auto& slot = user->m_Quests[si];
            if (slot.id == 0) continue;
            const QuestRow* q = QuestData::Find(slot.id);
            if (q == nullptr) continue;
            for (int oi = 0; oi < QuestData::kMaxObjs && slot.id != 0; ++oi)
            {
                const auto& o = q->objs[oi];
                if (o.kind != ObjKind::Explore || slot.progress[static_cast<size_t>(oi)] >= o.count)
                    continue;
                const QuestData::Poi* poi = QuestData::FindPoi(o.target);
                if (poi != nullptr && QuestData::InPoi(*poi, x, y))
                    Advance(user, static_cast<int>(si), oi, o.count, static_cast<short>(poi->x), static_cast<short>(poi->y));
            }
        }
    }

    void OnEquipChanged(const UserSharedPtr& user)
    {
        if (user == nullptr)
            return;
        int equipped = -1;
        for (size_t si = 0; si < user->m_Quests.size(); ++si)
        {
            const auto& slot = user->m_Quests[si];
            if (slot.id == 0) continue;
            const QuestRow* q = QuestData::Find(slot.id);
            if (q == nullptr) continue;
            for (int oi = 0; oi < QuestData::kMaxObjs && slot.id != 0; ++oi)
            {
                if (q->objs[oi].kind != ObjKind::Equip)
                    continue;
                if (equipped < 0)
                    equipped = CountEquipped(*user);
                Advance(user, static_cast<int>(si), oi, equipped, -1, -1);
            }
        }
    }
}
