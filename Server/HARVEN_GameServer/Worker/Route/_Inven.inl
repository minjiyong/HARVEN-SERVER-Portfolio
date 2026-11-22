// 소지품 — 장착 · 해제 · 자리 바꿈 · 버림 · 줍기 · 사용 · 정리 · 대장간 강화 · 제작 · 분해
namespace RouteItem
{
    void OnRecv_ITEM_EQUIP_REQ(const UserSharedPtr& pUser, const ITEM_EQUIP_REQ_PACKET& req)
    {
        const bool bSuccess = pUser->GetInventory()->TryEquip(req.slotIndex);
        CUserHelper::SendITEM_EQUIP_ACK(pUser, req.slotIndex, bSuccess);
        if (bSuccess)
            CUserHelper::BroadcastEquipChange(pUser);
    }

    void OnRecv_ITEM_UNEQUIP_REQ(const UserSharedPtr& pUser, const ITEM_UNEQUIP_REQ_PACKET& req)
    {
        const bool bSuccess = pUser->GetInventory()->TryUnequip(req.slotIndex);
        CUserHelper::SendITEM_UNEQUIP_ACK(pUser, req.slotIndex, bSuccess);
        if (bSuccess)
            CUserHelper::BroadcastEquipChange(pUser);
    }

    void OnRecv_ITEM_SWAP_REQ(const UserSharedPtr& pUser, const ITEM_SWAP_REQ_PACKET& req)
    {
        const bool bSuccess = pUser->GetInventory()->TrySwapSlots(req.slotIndexA, req.slotIndexB);
        CUserHelper::SendITEM_SWAP_ACK(pUser, req.slotIndexA, req.slotIndexB, bSuccess);
    }

    void OnRecv_ITEM_DISCARD_REQ(const UserSharedPtr& pUser, const ITEM_DISCARD_REQ_PACKET& req)
    {
        // 귀속된 물건(획득 귀속 행 — 장비 파편 같은 재료 포함 · 인스턴스 bit 8)은 땅에 버릴 수 없다(경제 1차 명세 §1) — 상점 매입 · 분해는 된다
        {
            const auto& slots = pUser->GetInventory()->GetSlots();
            if (const auto it = slots.find(req.slotIndex); it != slots.end())
                if (const ItemTableRow* row = ItemTable::Find(it->second->GetItemTableId());
                    row != nullptr && ItemRules::IsBound(*row, pUser->GetInventory()->GetSlotExtra(req.slotIndex)))
                {
                    CUserHelper::SendSystem(pUser, SystemMessageCode::ItemBound, static_cast<Int32>(row->id), req.slotIndex);
                    CUserHelper::SendITEM_DISCARD_ACK(pUser, req.slotIndex, false);
                    return;
                }
        }
        CItem::SharedPtr pExtracted = pUser->GetInventory()->TryExtractItem(req.slotIndex, req.count);
        if (pExtracted != nullptr)
        {
            CItemHelper::SpawnFieldItem(pExtracted, pUser->GetX(), pUser->GetY());
            // 방금 버린 게 장착 중이던 아이템이었을 수 있다(TryExtractItem이 이미 탈착까지 해뒀다).
            CUserHelper::BroadcastEquipChange(pUser);
        }
        CUserHelper::SendITEM_DISCARD_ACK(pUser, req.slotIndex, pExtracted != nullptr);
    }

    void OnRecv_ITEM_PICKUP_REQ(const UserSharedPtr& pUser, const ITEM_PICKUP_REQ_PACKET& req)
    {
        CItemHelper::TryPickupAt(pUser);
    }

    void OnRecv_ITEM_USE_REQ(const UserSharedPtr& pUser, const ITEM_USE_REQ_PACKET& req)
    {
        if (pUser->GetStat()->IsDead())
            return;

        const UInt32 now = GetNowTime();

    // 회복 물약이 아닌 소비 아이템(두루마리/바람걸음 물약)은 효과가 인벤토리 밖에서
    // 일어나므로 별도 경로로 보낸다. 쿨타임도 물약(5초)과 따로 잰다.
    ItemEffect effect = ItemEffect::eNone;
    {
        const auto& slots = pUser->GetInventory()->GetSlots();
        if (const auto it = slots.find(req.slotIndex); it != slots.end())
            if (const ItemTableRow* row = ItemTable::Find(it->second->GetItemTableId()))
                effect = row->effect;
    }
    if (effect == ItemEffect::eReturnTown || effect == ItemEffect::eTeleportRandom || effect == ItemEffect::eHaste)
    {
        if (now > pUser->m_LastScrollUseTime + CombatRule::kScrollCooldownMs)
        {
            pUser->m_LastScrollUseTime = now;
            CContentHelper::UseEffectItem(pUser, req.slotIndex);
        }
        else
        {
            CUserHelper::SendSystem(pUser, SystemMessageCode::ItemCooldown,
                static_cast<Int32>(pUser->m_LastScrollUseTime + CombatRule::kScrollCooldownMs - now));
        }
        return;
    }

    // 회복 물약이 아니면(빈 슬롯/장비/각인 두루마리 등) 쿨타임을 소모하지 않고 바로 실패
    if (effect != ItemEffect::eHeal)
    {
        CUserHelper::SendITEM_USE_ACK(pUser, req.slotIndex, false);
        return;
    }

    if (now > pUser->m_LastPotionUseTime + CombatRule::kPotionCooldownMs)
    {
        pUser->m_LastPotionUseTime = now;

        const UInt16 before = pUser->GetStat()->GetHp();
        const bool success = pUser->GetInventory()->TryUseItem(req.slotIndex);
        if (success)
        {
            CUserHelper::SendUSER_HEAL_INF(pUser);
            CUserHelper::BroadcastEffect(pUser, pUser->GetObjID(), EffectId::PotionHeal, pUser->GetX(), pUser->GetY(),
                static_cast<Int32>(pUser->GetStat()->GetHp() - before));
        }

        CUserHelper::SendITEM_USE_ACK(pUser, req.slotIndex, success);
    }
    else
    {
        CUserHelper::SendSystem(pUser, SystemMessageCode::ItemCooldown,
            static_cast<Int32>(pUser->m_LastPotionUseTime + CombatRule::kPotionCooldownMs - now));
    }
    }

    void OnRecv_ITEM_SORT_REQ(const UserSharedPtr& pUser, const ITEM_SORT_REQ_PACKET& req)
    {
        CUserHelper::HandleItemSort(pUser);
    }

    void OnRecv_ITEM_ENCHANT_REQ(const UserSharedPtr& pUser, const ITEM_ENCHANT_REQ_PACKET& req)
    {
        CContentHelper::HandleEnchant(pUser, req);
    }

    // 경제 1차 제작 · 분해(대장장이 곁 — Content/Craft/CraftHelper)
    void OnRecv_CRAFT_REQ(const UserSharedPtr& pUser, const CRAFT_REQ_PACKET& req)
    {
        CCraftHelper::HandleCraft(pUser, req);
    }
}
