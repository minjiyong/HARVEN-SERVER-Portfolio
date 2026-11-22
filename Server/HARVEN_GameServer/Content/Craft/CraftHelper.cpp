#include "pch.h"
#include "Content/Craft/CraftHelper.h"

#include <algorithm>

#include "Base/Core/CoreTLS.h"
#include "Content/Economy/EconomyLog.h"
#include "Content/Inventory/Inventory.h"
#include "Content/Npc/NpcHelper.h"
#include "Content/ServiceCommon.h"
#include "Database/DBThread.h"
#include "Database/GameData/EconRules.h"
#include "Database/GameData/EnchantRules.h"
#include "Database/GameData/Item/ItemTable.h"
#include "Database/GameData/ItemRules.h"
#include "User/User.h"
#include "Content/Achieve/AchieveHelper.h"

// ─────────────────────────────────────────────────────────────────────────────
// 계획(메모리를 바꾸지 않는다 — 가방 사본만 본다)
// ─────────────────────────────────────────────────────────────────────────────
namespace
{
    // 계획 중인 가방(사본) — 바꾼 칸을 처음 건드린 순서로 기억한다(DB 반영 · 알림 순서)
    struct Work
    {
        const CraftPlan::Bag& bag;
        std::array<DB_ITEM_SLOT_SAVE, MAX_INVENTORY_SLOTS> slots;
        std::bitset<MAX_INVENTORY_SLOTS> touched;
        std::vector<UInt16> order;

        explicit Work(const CraftPlan::Bag& b) : bag(b), slots(b.slots) {}

        DB_ITEM_SLOT_SAVE& Touch(UInt16 slot)
        {
            if (!touched.test(slot))
            {
                touched.set(slot);
                order.push_back(slot);
            }
            return slots[slot];
        }
        [[nodiscard]] bool Free(UInt16 slot) const { return !bag.locked.test(slot) && !slots[slot].hasItem; }
        void Clear(UInt16 slot) { Touch(slot) = DB_ITEM_SLOT_SAVE{ slot, false, 0, 0, false, 0, 0 }; }
    };

    // 겹치는 물건 count개를 넣는다 — 같은 id 묶음(max_stack 미만, 칸 번호 순)에 먼저, 남으면 빈 칸(칸 번호 순). 다 못 넣으면 false.
    bool Put(Work& w, const ItemTableRow& row, UInt32 count)
    {
        const UInt16 maxStack = row.maxStack == 0 ? 1 : row.maxStack;
        if (maxStack > 1)
        {
            for (UInt16 s = 0; s < MAX_INVENTORY_SLOTS && count > 0; ++s)
            {
                const DB_ITEM_SLOT_SAVE& cur = w.slots[s];
                if (w.bag.locked.test(s) || !cur.hasItem || cur.itemId != row.id || cur.count >= maxStack)
                    continue;
                const UInt16 add = static_cast<UInt16>(std::min<UInt32>(maxStack - cur.count, count));
                w.Touch(s).count = static_cast<UInt16>(cur.count + add);
                count -= add;
            }
        }
        for (UInt16 s = 0; s < MAX_INVENTORY_SLOTS && count > 0; ++s)
        {
            if (!w.Free(s))
                continue;
            const UInt16 add = static_cast<UInt16>(std::min<UInt32>(maxStack, count));
            w.Touch(s) = DB_ITEM_SLOT_SAVE{ s, true, row.id, add, false, 0, 0 };
            count -= add;
        }
        return count == 0;
    }

    void Finish(const Work& w, CraftPlan::Plan& plan)
    {
        plan.save.before.reserve(w.order.size());
        plan.save.after.reserve(w.order.size());
        for (const UInt16 s : w.order)
        {
            plan.save.before.push_back(w.bag.slots[s]);
            plan.save.after.push_back(w.slots[s]);
        }
        plan.result = CraftResult::Ok;
    }
}

CraftPlan::Bag CraftPlan::SnapshotOf(const CInventory& inventory)
{
    Bag bag;
    for (UInt16 s = 0; s < MAX_INVENTORY_SLOTS; ++s)
    {
        bag.slots[s] = inventory.DescribeSlot(s);
        if (inventory.IsSlotLocked(s))
            bag.locked.set(s);
    }
    bag.adena = inventory.GetAdena();
    return bag;
}

CraftPlan::Plan CraftPlan::Craft(const Bag& bag, ItemTableId itemId, ClassId cls, const std::function<UInt32()>& rng)
{
    Plan plan;
    plan.op = CraftOp::Craft;
    plan.itemId = itemId;
    const ItemTableRow* row = ItemTable::Find(itemId);
    if (row == nullptr)
        return plan;   // Invalid
    plan.grade = row->grade;
    const EconRule::CraftRecipeRow* recipe = EconRule::RecipeFor(*row);
    if (recipe == nullptr)
    {
        plan.result = CraftResult::NoRecipe;
        return plan;
    }

    // 재료(같은 id는 합친다)
    for (const EconRule::CraftInput& in : recipe->in)
    {
        if (in.item == ITEM_TABLE_ID_NONE || in.count == 0)
            continue;
        const auto it = std::find_if(plan.consumed.begin(), plan.consumed.end(), [&](const auto& c) { return c.first == in.item; });
        if (it != plan.consumed.end()) it->second += in.count;
        else plan.consumed.emplace_back(in.item, in.count);
    }

    Work w(bag);
    for (const auto& [id, need] : plan.consumed)
    {
        // 그 id의 묶음(잠기지 않은 것) — 작은 묶음부터, 같으면 칸 번호 순
        std::vector<UInt16> stacks;
        UInt32 have = 0;
        for (UInt16 s = 0; s < MAX_INVENTORY_SLOTS; ++s)
        {
            const DB_ITEM_SLOT_SAVE& cur = w.slots[s];
            if (bag.locked.test(s) || !cur.hasItem || cur.itemId != id)
                continue;
            stacks.push_back(s);
            have += cur.count;
        }
        if (have < need)
        {
            plan.result = CraftResult::LackMaterial;
            return plan;
        }
        std::stable_sort(stacks.begin(), stacks.end(), [&](UInt16 a, UInt16 b) { return w.slots[a].count < w.slots[b].count; });
        UInt32 left = need;
        for (const UInt16 s : stacks)
        {
            if (left == 0)
                break;
            const UInt16 take = static_cast<UInt16>(std::min<UInt32>(w.slots[s].count, left));
            left -= take;
            if (take == w.slots[s].count) w.Clear(s);
            else w.Touch(s).count = static_cast<UInt16>(w.slots[s].count - take);
        }
    }

    if (bag.adena < recipe->fee)
    {
        plan.result = CraftResult::LackAdena;
        return plan;
    }

    // 결과 칸 — 비는 칸(재료로 비워진 칸 포함) 중 가장 앞
    UInt16 target = MAX_INVENTORY_SLOTS;
    for (UInt16 s = 0; s < MAX_INVENTORY_SLOTS; ++s)
        if (w.Free(s)) { target = s; break; }
    if (target >= MAX_INVENTORY_SLOTS)
    {
        plan.result = CraftResult::BagFull;
        return plan;
    }

    // 제작 결과의 희귀도는 등급 바닥으로 고정(아이템 찾기 · 천장 없음) — 옵션만 굴린다. 획득 귀속 장비면 bit 8
    auto roll = [&rng]() { return rng(); };
    const UInt64 extra = ItemRules::OnAcquire(*row, ItemRules::RollInstance(*row, ItemRules::FloorRarity(*row), cls, roll).Pack());
    w.Touch(target) = DB_ITEM_SLOT_SAVE{ target, true, itemId, 1, false, 0, extra };

    plan.slotIndex = target;
    plan.fee = recipe->fee;
    plan.save.adenaBefore = bag.adena;
    plan.save.adenaAfter = bag.adena - recipe->fee;
    Finish(w, plan);
    return plan;
}

CraftPlan::Plan CraftPlan::Dismantle(const Bag& bag, UInt16 slotIndex, ItemTableId expectItem)
{
    Plan plan;
    plan.op = CraftOp::Dismantle;
    plan.slotIndex = slotIndex;
    if (slotIndex >= MAX_INVENTORY_SLOTS || !bag.slots[slotIndex].hasItem)
        return plan;   // Invalid
    const DB_ITEM_SLOT_SAVE& src = bag.slots[slotIndex];
    // 클라가 고른 물건(itemId)과 지금 그 칸의 물건이 다르면 거절 — 정리 · 이동 사이에 다른 장비가 들어온 칸을 부수지 않는다(0 = 옛 클라, 보지 않음)
    if (expectItem != ITEM_TABLE_ID_NONE && src.itemId != expectItem)
        return plan;   // Invalid
    plan.itemId = src.itemId;
    if (bag.locked.test(slotIndex))
    {
        plan.result = CraftResult::Busy;
        return plan;
    }
    const ItemTableRow* row = ItemTable::Find(src.itemId);
    if (row == nullptr || row->type != ItemType::eEquipment)
        return plan;   // Invalid — 장비만 분해한다
    plan.grade = row->grade;
    if (src.equipped)
    {
        plan.result = CraftResult::Equipped;
        return plan;
    }
    const EconRule::DismantleRow* d = EconRule::DismantleFor(row->grade);
    if (d == nullptr)
    {
        plan.result = CraftResult::NoRecipe;   // 분해할 수 없는 등급(신화)
        return plan;
    }

    // 나오는 것: [파편, 재료] — 강화 수치는 안 본다. 거래 이력(bit 9)이면 둘 다 traded_dismantle_percent(올림)
    const bool traded = ItemRules::IsTraded(src.extra);
    const std::array<std::pair<ItemTableId, UInt16>, 2> outs = { {
        { EconRule::ShardItem(), traded ? EconRule::TradedAmount(d->shardCount) : d->shardCount },
        { d->itemId, traded ? EconRule::TradedAmount(d->itemCount) : d->itemCount } } };

    Work w(bag);
    w.Clear(slotIndex);   // 비운 칸도 나온 것이 들어갈 자리다
    for (size_t i = 0; i < outs.size(); ++i)
    {
        const auto [id, count] = outs[i];
        if (id == ITEM_TABLE_ID_NONE || count == 0)
            continue;
        const ItemTableRow* outRow = ItemTable::Find(id);
        if (outRow == nullptr)
            return plan;   // Invalid — 표가 틀렸다(R133이 막는다)
        if (!Put(w, *outRow, count))
        {
            plan.result = CraftResult::BagFull;
            return plan;
        }
        plan.outItem[i] = id;
        plan.outCount[i] = count;
    }
    plan.save.adenaBefore = plan.save.adenaAfter = bag.adena;
    Finish(w, plan);
    return plan;
}

// ─────────────────────────────────────────────────────────────────────────────
// 요청 처리(소유 Zone) — 강화와 같은 「준비 → 한 트랜잭션 → 소유 Zone에서 완료」
// ─────────────────────────────────────────────────────────────────────────────
namespace
{
    void SendAck(const SessionWeakPtr& session, const CRAFT_REQ_PACKET& request, CraftResult result, const CraftPlan::Plan* plan)
    {
        CRAFT_ACK_PACKET p{};
        Service::Init(p, PacketType::CRAFT_ACK);
        p.op = request.op;
        p.result = static_cast<UInt8>(result);
        p.seq = request.seq;
        p.itemId = request.itemId;
        p.slotIndex = request.slotIndex;
        if (plan != nullptr && result == CraftResult::Ok)
        {
            p.itemId = plan->itemId;
            p.slotIndex = plan->slotIndex;
            p.fee = plan->fee;
            for (size_t i = 0; i < 2; ++i)
            {
                p.outItem[i] = plan->outItem[i];
                p.outCount[i] = plan->outCount[i];
            }
        }
        Service::SendTo(session, p);
    }

    // 커밋된 계획을 경제 로그에(DB 스레드 — 원자 카운터)
    void LogCommitted(const CraftPlan::Plan& plan)
    {
        if (plan.op == CraftOp::Craft)
        {
            CEconomyLog::OnGearIn(GearSource::Craft, plan.grade);
            for (const auto& [id, count] : plan.consumed)
                CEconomyLog::OnMaterialOut(MaterialSink::Craft, id, count);
            CEconomyLog::OnCraftFee(plan.fee);
            CEconomyLog::OnAdena(AdenaReason::Craft, -static_cast<int64_t>(plan.fee));
        }
        else
        {
            CEconomyLog::OnGearOut(GearSink::Dismantle, plan.grade);
            for (size_t i = 0; i < 2; ++i)
                CEconomyLog::OnMaterialIn(MaterialSource::Dismantle, plan.outItem[i], plan.outCount[i]);
        }
    }
}

void CCraftHelper::HandleCraft(const UserSharedPtr& player, const CRAFT_REQ_PACKET& request)
{
    if (player == nullptr)
        return;
    const SessionWeakPtr session = player->GetGameSession();
    auto reply = [&](CraftResult result) { SendAck(session, request, result, nullptr); };

    if (request.seq == 0 || request.seq <= player->m_LastCraftSeq)
        return reply(CraftResult::Duplicate);
    player->m_LastCraftSeq = request.seq;
    if (player->GetStat()->IsDead() || player->IsTransferring())
        return reply(CraftResult::Invalid);
    if (request.op != static_cast<UInt8>(CraftOp::Craft) && request.op != static_cast<UInt8>(CraftOp::Dismantle))
        return reply(CraftResult::Invalid);
    // 제작 · 분해는 어디서나(소지품 「분해하기」 · 메뉴 「제작」 — 2026-10-13 사용자 요청). npcId는 보지 않는다(옛 클라는 2를 보낸다).
    // NotAtBlacksmith(2)는 열거형 자리만 남는다 — 강화는 지금도 대장장이 곁(EnchantRule::kBlacksmithNpcId).
    const auto inventory = player->GetInventory();
    if (inventory->IsBusy() || !player->m_PendingAuctionSeqs.empty())
        return reply(CraftResult::Busy);

    const CraftPlan::Bag bag = CraftPlan::SnapshotOf(*inventory);
    auto plan = std::make_shared<CraftPlan::Plan>(static_cast<CraftOp>(request.op) == CraftOp::Craft
        ? CraftPlan::Craft(bag, request.itemId, player->m_eClassId, []() { return static_cast<UInt32>(LRng()); })
        : CraftPlan::Dismantle(bag, request.slotIndex, request.itemId));
    if (plan->result != CraftResult::Ok)
        return reply(plan->result);
    if (!inventory->BeginBatch(plan->save))
        return reply(CraftResult::Busy);   // 계획과 칸이 어긋났다(같은 스레드라 일어나지 않는다)

    const auto ctx = Service::MakeCtx(player);
    const std::shared_ptr<const CraftPlan::Plan> committed = plan;
    GDBThread->RequestTx(ctx.playerId, plan->op == CraftOp::Craft ? "CraftTransaction" : "DismantleTransaction",
        [committed, pid = ctx.playerId](CDBConnection& c) { return c.SaveItemBatch(pid, committed->save); },
        [committed, ctx, request](bool ok)
        {
            // 커밋이 승부를 정한다(사람이 나갔어도) — 경제 로그는 여기서
            if (ok)
                LogCommitted(*committed);
            Service::PostToOwner(ctx.obj, [committed, ok, ctx, request](const UserSharedPtr& u)
            {
                if (u == nullptr)
                    return;   // 나갔다 — 커밋됐으면 DB가 원본(다시 들어오면 그대로), 아니면 아무것도 바뀌지 않았다
                u->GetInventory()->CompleteBatch(committed->save, ok, AdenaReason::Craft);
                SendAck(ctx.session, request, ok ? CraftResult::Ok : CraftResult::StorageError, committed.get());
                // 개인 업적(제작 — 만든 장비도 장비 획득 · 분해) — 커밋되고 그 사람이 남아 있을 때(나간 사이 커밋된 것은 세지 못한다)
                if (ok && committed->op == CraftOp::Craft)
                    CAchieveHelper::OnCraft(u, committed->grade);
                else if (ok)
                    CAchieveHelper::OnDismantle(u);
                if (!ok)
                {
                    // 칸은 잠근 채 연결을 닫는다(강화와 같음 — 커밋 응답을 잃었을 수 있다). 방금 보낸 StorageError가 닿도록 보낸 뒤에 닫는다
                    std::cout << "[Craft] DB 반영 실패 pid=" << ctx.playerId << " op=" << static_cast<int>(request.op)
                              << " seq=" << request.seq << " — 연결을 닫는다\n" << std::flush;
                    if (const auto s = u->GetGameSession()) s->CloseAfterFlush();
                }
            });
        });
}
