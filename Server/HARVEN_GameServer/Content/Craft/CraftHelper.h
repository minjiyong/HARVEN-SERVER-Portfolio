#pragma once

#include <array>
#include <bitset>
#include <functional>
#include <utility>
#include <vector>

#include "Database/GameData/CombatData.h"          // ClassId
#include "Database/GameData/Item/ItemTableRow.h"   // ItemGrade · ItemTableId

class CInventory;

// ─────────────────────────────────────────────────────────────────────────────
// 경제 1차 제작 · 분해(CRAFT_REQ → CRAFT_ACK, 명세 §7 · Shared/Document/ECONOMY.md)
//
// 흐름은 강화(CContentHelper::HandleEnchant)와 같다:
//   1) 소유 Zone: seq 멱등(m_LastCraftSeq) · 사망/이동 중 · 대장장이 곁 · Busy(강화 · 제작 · 경매) 확인
//   2) 계획(CraftPlan — 메모리를 바꾸지 않는다): 가방 사본에서 칸마다 before/after · 그로시 before/after를 만든다
//   3) CInventory::BeginBatch — 계획의 모든 칸을 잠그고 수수료를 메모리에서 예약
//   4) GDBThread->RequestTx(그 사람 샤드) — CDBConnection::SaveItemBatch → P_ITEM_BATCH_TRADE 한 트랜잭션(칸마다 낙관 검사 + 그로시)
//   5) 커밋 확인 → 경제 로그 → 소유 Zone에서 CompleteBatch(칸 반영 · 알림 · ADENA_INF Craft) → CRAFT_ACK
//      실패면 StorageError + 연결 닫기(칸은 잠근 채 — 커밋 응답을 잃었을 수 있다. 다시 들어오면 DB가 원본)
//
// 계획 규칙
//   Craft     itemId의 route에 Craft · (등급, 단, 부위 묶음) 레시피(EconRule::RecipeFor) → 재료를 그 id의 모든 묶음에서
//             작은 묶음부터(같으면 칸 번호 순) 덜어 내고 → 수수료(그로시) → 결과는 비는 칸(재료로 비워진 칸 포함) 중 가장 앞.
//             인스턴스 = OnAcquire(RollInstance(희귀도 FloorRarity(등급) 고정, 그 사람 직업)) — 아이템 찾기 · 천장 없음.
//             검사 순서: 없는 id Invalid → 레시피 NoRecipe → 재료 LackMaterial → 그로시 LackAdena → 자리 BagFull.
//   Dismantle 그 칸의 장비(장착 중 Equipped · 잠김 Busy · 장비 아님 Invalid · 분해 행 없음(신화) NoRecipe) → 칸을 비우고
//             [파편 shard_count, 재료 item_count](EconRule::DismantleFor(등급), 강화 수치는 안 본다; 거래 이력 bit 9면 둘 다
//             EconRule::TradedAmount)를 같은 id 묶음(max_stack 미만, 칸 번호 순)에 먼저 합치고 남으면 빈 칸(비운 칸 포함) — 못 넣으면 BagFull.
// ─────────────────────────────────────────────────────────────────────────────
namespace CraftPlan
{
    // 계획이 보는 가방 사본(칸 MAX_INVENTORY_SLOTS개 + 그로시 + 잠긴 칸). slots[i].slotIndex == i, 빈 칸은 hasItem=false.
    struct Bag
    {
        std::array<DB_ITEM_SLOT_SAVE, MAX_INVENTORY_SLOTS> slots{};
        std::bitset<MAX_INVENTORY_SLOTS> locked;
        UInt32 adena = 0;
    };
    [[nodiscard]] Bag SnapshotOf(const CInventory& inventory);

    struct Plan
    {
        CraftOp     op = CraftOp::Craft;
        CraftResult result = CraftResult::Invalid;
        ItemTableId itemId = ITEM_TABLE_ID_NONE;   // 만든 · 분해한 장비
        UInt16      slotIndex = 0;                 // 결과가 들어간 칸 · 비운 칸
        ItemGrade   grade = ItemGrade::eNone;      // 그 장비의 등급(경제 로그)
        UInt32      fee = 0;                       // 그로시(Dismantle 0)
        std::array<UInt16, 2> outItem{}, outCount{};               // Dismantle: [파편, 재료]
        std::vector<std::pair<ItemTableId, UInt32>> consumed;      // Craft: 쓴 재료(경제 로그)
        DB_ITEM_BATCH_SAVE save;                                   // 건드린 칸(처음 건드린 순서) · 그로시
    };

    // rng: 옵션 굴림(ItemRules::RollInstance). 실패면 result만 의미가 있다(save는 비어 있다).
    [[nodiscard]] Plan Craft(const Bag& bag, ItemTableId itemId, ClassId cls, const std::function<UInt32()>& rng);
    [[nodiscard]] Plan Dismantle(const Bag& bag, UInt16 slotIndex, ItemTableId expectItem = ITEM_TABLE_ID_NONE);   // expectItem: 클라가 고른 물건(다르면 Invalid, 0 = 보지 않음)
}

namespace CCraftHelper
{
    // CRAFT_REQ — 소유 Zone 스레드(Route::InGame)
    void HandleCraft(const UserSharedPtr& player, const CRAFT_REQ_PACKET& request);
}
