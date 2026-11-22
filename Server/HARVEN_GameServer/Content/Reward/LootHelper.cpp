#include "pch.h"
#include "Content/Reward/LootHelper.h"

#include <array>

#include "Base/Core/CoreTLS.h"
#include "Content/Economy/EconomyLog.h"
#include "Content/Guild/GuildService.h"
#include "Content/Inventory/Inventory.h"
#include "Content/Inventory/Item/EquipmentItem.h"
#include "Content/Inventory/ItemHelper.h"
#include "Content/Reward/DropTable.h"
#include "Content/Reward/LootRoll.h"
#include "Content/Reward/RewardHelper.h"
#include "Content/Achieve/AchieveHelper.h"
#include "Database/GameData/EconRules.h"
#include "Database/GameData/Item/ItemTable.h"
#include "Database/GameData/Item/ItemTableData.h"
#include "Database/GameData/ItemRules.h"
#include "Database/GameData/MonsterTable.h"
#include "User/User.h"
#include "User/UserHelper.h"

namespace CLootHelper
{
    namespace
    {
        auto Rng() { return [] { return static_cast<UInt32>(LRng()); }; }

        // 경제 로그 — 장비는 출처 × 등급, 재료는 출처 × 아이템. 소비품은 세지 않는다(명세 §8).
        void LogIn(const ItemTableRow& row, UInt16 count, Source source)
        {
            if (row.type == ItemType::eEquipment)
            {
                const GearSource g = source == Source::Elite ? GearSource::Elite : (source == Source::Boss ? GearSource::Boss : GearSource::Field);
                CEconomyLog::OnGearIn(g, row.grade, count);
            }
            else if (row.type == ItemType::eMaterial)
            {
                const MaterialSource m = source == Source::Elite ? MaterialSource::Elite : (source == Source::Boss ? MaterialSource::Boss : MaterialSource::Kill);
                CEconomyLog::OnMaterialIn(m, row.id, count);
            }
        }

        // 장비 인스턴스 — 희귀도 = 등급 바닥(FloorRarity) 고정, 옵션은 그 희귀도로(그 사람 직업 가중치) + 획득 귀속 비트
        UInt64 NewInstance(const ItemTableRow& row, ClassId cls)
        {
            if (row.type != ItemType::eEquipment)
                return 0;
            return ItemRules::OnAcquire(row, ItemRules::RollInstance(row, ItemRules::FloorRarity(row), cls, Rng()).Pack());
        }

        // 남의 직업 무기인가(방어구 · 장신구는 직업 제한이 없다)
        bool OtherClassWeapon(const ItemTableRow& row, ClassId cls) noexcept
        {
            return ItemTableData::SlotOf(row) == EquipSlot::Weapon && !ItemTableData::ClassCanUse(row, static_cast<UInt8>(cls));
        }

        // pool에서 균등 하나 → 남의 직업 무기면 class_swap_percent로 같은 (등급, 단, 경로)의 자기 직업 무기
        ItemTableId PickInPool(std::span<const ItemTableId> pool, ClassId cls, ItemRoute route)
        {
            if (pool.empty())
                return ITEM_TABLE_ID_NONE;
            const ItemTableId picked = pool[LootRoll::Below(Rng(), static_cast<UInt32>(pool.size()))];
            const ItemTableRow* row = ItemTable::Find(picked);
            if (row == nullptr)
                return ITEM_TABLE_ID_NONE;
            return LootRoll::ClassSwap(picked, OtherClassWeapon(*row, cls), EconRule::ClassWeapons(cls, row->grade, row->rank, route),
                EconRule::ClassSwapPercent(), Rng());
        }

        Rolled Finish(const UserSharedPtr& finder, ItemTableId id)
        {
            const ItemTableRow* row = ItemTable::Find(id);
            if (row == nullptr)
                return {};
            return Rolled{ id, NewInstance(*row, finder->m_eClassId) };
        }
    }

    void Grant(const UserSharedPtr& user, ItemTableId id, UInt16 count, UInt64 extra, Source source, DropSpot& spot)
    {
        if (user == nullptr || count == 0)
            return;
        const ItemTableRow* row = ItemTable::Find(id);
        if (row == nullptr || row->type == ItemType::eCurrency)
            return;
        LogIn(*row, count, source);
        if (user->GetInventory()->TryAddItem(id, count, nullptr, 0, extra))
        {
            CUserHelper::SendSystem(user, SystemMessageCode::LootAcquired, static_cast<Int32>(id), count);
            CRewardHelper::AnnounceLoot(user, id, extra);
            CGuildHelper::OnLoot(user, id, count, extra);   // 결사 임무(전리품 · 장비)
            CAchieveHelper::OnLoot(user, id, count);        // 개인 업적(장비 등급 · 사냥 정수 · 결정) — 가방에 들어온 것만(넘쳐 떨어진 것은 세지 않는다)
            return;
        }
        // 가방이 찼다 — 죽은 자리 둘레에, 받을 사람의 줍기 우선권(그 사람 > 그 사람의 파티 > 누구나)으로
        CItem::SharedPtr item = MakeNewItem(id, count);
        if (item == nullptr)
            return;
        if (CEquipmentItem* equipment = AsEquipment(item))
            equipment->SetExtra(extra);
        short x = spot.x, y = spot.y;
        if (!CItemHelper::PickDropTile(spot.x, spot.y, spot.tile++, x, y))
        {
            x = spot.x;
            y = spot.y;
        }
        CItemHelper::SpawnFieldItem(item, x, y, user->GetObjID(), user->m_PartyId.load(std::memory_order_relaxed), user->GetPlayerId());
        CUserHelper::SendSystem(user, SystemMessageCode::LootOverflow, static_cast<Int32>(id), count);
    }

    void GiveDropTable(const UserSharedPtr& looter, const MonsterTableRow* species, DropSpot& spot)
    {
        // drop_table 0 = 드롭 없음(시트 "0 = 없음" — 허수아비). 종족이 없는 옛 몬스터는 1번 표 한 번.
        const UInt8 tableId = species != nullptr ? species->dropTable : 1;
        const UInt8 rolls = species != nullptr ? species->dropRolls : 1;
        if (looter == nullptr || tableId == 0)
            return;
        for (UInt8 i = 0; i < rolls; ++i)
        {
            const DropTable::RollResult roll = DropTable::Roll(tableId);
            if (roll.hasItem)
                Grant(looter, roll.itemId, roll.count, 0, Source::Drop, spot);   // 드롭 표에는 장비가 없다(R126) — 인스턴스 없음
        }
    }

    Rolled RollFieldGear(const UserSharedPtr& finder, int tier)
    {
        if (finder == nullptr)
            return {};
        const EconRule::TierLootRow& t = EconRule::Tier(tier);
        if (t.commonBp == 0 && t.uncommonBp == 0 && t.rareBp == 0)
            return {};
        LootRoll::FieldPick pick = LootRoll::PickFieldGrade(t.commonBp, t.uncommonBp, t.rareBp, finder->m_GearExtras.itemFind,
            LootRoll::Below(Rng(), LootRoll::kBpScale));
        if (pick == LootRoll::FieldPick::None)
            return {};
        const UInt16 before = finder->m_PityRare;
        pick = LootRoll::ApplyRarePity(pick, t.rareBp > 0, EconRule::RarePity(), finder->m_PityRare);
        const ItemTableId id = PickInPool(EconRule::Items(LootRoll::GradeOf(pick), 0, ItemRoute::Field), finder->m_eClassId, ItemRoute::Field);
        // 천장 카운터가 0으로 돌아간 순간을 남긴다(그 사이 값은 로그아웃 때 저장 — 예전과 같다)
        if (finder->m_PityRare == 0 && before != 0)
            CUserHelper::SaveProgress(finder);
        return Finish(finder, id);
    }

    Rolled RollEliteGear(const UserSharedPtr& finder, int tier)
    {
        if (finder == nullptr)
            return {};
        const std::span<const EconRule::EliteGearRow> rows = EconRule::EliteGear(tier);
        std::array<LootRoll::GradeCell, 32> cells{};
        const size_t n = std::min(rows.size(), cells.size());
        for (size_t i = 0; i < n; ++i)
            cells[i] = LootRoll::GradeCell{ rows[i].grade, rows[i].rank, rows[i].weight };
        const UInt16 before = finder->m_PityHeroic;
        const size_t k = LootRoll::PickEliteCell(std::span<const LootRoll::GradeCell>(cells.data(), n), EconRule::HeroicPity(), finder->m_PityHeroic, Rng());
        if (k == LootRoll::kNone)
            return {};
        const ItemTableId id = PickInPool(EconRule::Items(cells[k].grade, cells[k].rank, ItemRoute::Elite), finder->m_eClassId, ItemRoute::Elite);
        if (finder->m_PityHeroic == 0 && before != 0)
            CUserHelper::SaveProgress(finder);
        return Finish(finder, id);
    }

    Rolled RollBossLoot(const UserSharedPtr& finder, const MonsterTableRow& species)
    {
        if (finder == nullptr)
            return {};
        const std::span<const EconRule::BossLootRow> rows = EconRule::BossLoot(species.id);
        if (rows.empty())
            return {};
        std::vector<LootRoll::BossRow> cand;
        cand.reserve(rows.size());
        for (const EconRule::BossLootRow& r : rows)
        {
            LootRoll::BossRow b;
            b.item = r.itemId;
            b.weight = r.weight;
            if (const ItemTableRow* row = r.itemId != ITEM_TABLE_ID_NONE ? ItemTable::Find(r.itemId) : nullptr)
            {
                b.gear = row->type == ItemType::eEquipment;
                b.grade = row->grade;
                b.usable = !OtherClassWeapon(*row, finder->m_eClassId);   // 무기 행은 그 사람 직업이 못 쓰면 뺀다
            }
            else if (r.itemId != ITEM_TABLE_ID_NONE)
                b.usable = false;   // 없는 아이템(검증을 건너뛴 판) — 굴리지 않는다
            cand.push_back(b);
        }
        const UInt16 before = finder->m_PityHeroic;
        const ItemTableId id = LootRoll::RollBoss(std::span<const LootRoll::BossRow>(cand), EconRule::HeroicPity(), finder->m_PityHeroic, Rng());
        if (finder->m_PityHeroic == 0 && before != 0)
            CUserHelper::SaveProgress(finder);
        return id != ITEM_TABLE_ID_NONE ? Finish(finder, id) : Rolled{};
    }
}
