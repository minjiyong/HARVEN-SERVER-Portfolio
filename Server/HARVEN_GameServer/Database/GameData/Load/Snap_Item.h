#pragma once
// 스냅샷 — 아이템 묶음. Bind_Item.cpp가 채우고, 파사드(ItemTableData · ItemTable · ItemRules · EnchantRule ·
// WorldData::ShopSells · DropTable)가 읽는다. 탭: Item · ItemOption · ItemProc · ItemSet · Rarity · RarityStep · Enchant · EnchantConst ·
// DropTable · DropEntry · Shop.
#include <array>
#include <cstdint>
#include <random>
#include <vector>

#include "Database/GameData/ItemRules.h"
#include "Database/GameData/World/WorldShop.h"
#include "Database/GameData/Item/ItemTableRow.h"

struct SnapItem
{
    // ── Item ──
    std::vector<ItemTableRow> rows;        // id 순서(키 순 — export)
    std::vector<const char*> codeName;     // 행과 같은 순서 — ItemTableData::Id 이름(없으면 nullptr)
    std::vector<uint16_t> slotById;        // id → 행 번호 + 1(0 = 없음). 같은 id가 둘이면 앞 행(이관 전 순차 검색과 같다)

    // ── ItemOption · ItemProc(행 i = 열거 값 i — R025 · R026) ──
    std::vector<ItemOption> optionId;      // 행과 같은 순서 — 검증용
    std::vector<ItemRules::OptionRow> options;
    std::vector<ItemProc> procId;
    std::vector<ItemRules::ProcRow> procs;

    // ── ItemSet(id 순서, id 1~3 — GearExtras::setCount[4] 구조 상한) ──
    std::vector<ItemRules::SetRow> sets;

    // ── Rarity(행 i = Rarity i — 구조: 행 수 = Rarity::Count) · RarityStep(id 0~6) ──
    struct RarityRow
    {
        Rarity      id = Rarity::Common;
        const char* name = "";
        uint32_t    color = 0;
        uint8_t     optionCount = 0;   // ≤ 4(ItemInstance::opts[4])
        uint16_t    sellPercent = 0;
        bool        lootNotice = false;   // 몬스터에게서 얻으면 시스템 채팅으로 모두에게 알린다
    };
    std::vector<RarityRow> rarities;
    std::array<std::array<uint16_t, 5>, 7> stepWeights{};   // [0~5] 몬스터 단계 · [6] 보스(ItemRules::kStepRowBoss)

    // ── Enchant(행 i = +i, 0부터 연속 — 마지막 행이 그 위 단계 모두) · EnchantConst(한 행) ──
    struct EnchantRow
    {
        uint8_t  level = 0;
        uint64_t successTickets = 0;   // EnchantRule::kRollScale(1조) 중 성공 몫
        uint16_t graceAttempts = 0;    // 0 = 가호 없음
        uint16_t statGain = 0;         // +level → +level+1에 더하는 성능
    };
    std::vector<EnchantRow> enchant;
    std::vector<uint16_t> enchantBonus;   // [k] = +k의 누적 성능(k = 0 ~ 행 수, 65535에서 멈춤) — Bind가 미리 더해 둔다
    struct EnchantConst
    {
        uint32_t worldNoticeLevel = 0;
        uint32_t firstNoticeLevel = 0;
        uint16_t gainPerFurtherLevel = 0;
        uint16_t accessoryHpPerPoint = 0;
        uint16_t accessoryMpPerPoint = 0;
    };
    EnchantConst enchantConst;

    // ── DropTable · DropEntry(행 i = 표 번호 i + 1) ──
    struct DropEntry
    {
        ItemTableId itemId = ITEM_TABLE_ID_NONE;   // 종류 · 등급은 Item 행의 값을 쓴다(따로 두지 않는다 — R094 · R095 구조로 보장)
        uint32_t    weight = 0;
        uint16_t    minCount = 0;
        uint16_t    maxCount = 0;
        uint32_t    src = 0;                       // DropEntry 탭 행 번호(검증 위치)
    };
    struct Drop
    {
        uint8_t  id = 0;
        uint32_t noDropWeight = 0;
        std::vector<DropEntry> entries;            // seq 순서
        // 굴림 분포(index 0 = 드롭 없음, 1.. = entries) — 로드 때 미리 계산(DropTable::Roll이 넘긴다)
        std::discrete_distribution<size_t>::param_type dist;
    };
    std::vector<Drop> drops;

    // ── Shop(NPC마다 한 줄 — 빈 칸 0은 뒤에만) ──
    std::vector<WorldData::ShopRow> shops;
    struct ShopLine
    {
        uint16_t    npcId = 0;
        uint8_t     seq = 0;
        ItemTableId itemId = ITEM_TABLE_ID_NONE;
    };
    std::vector<ShopLine> shopLines;               // Shop 탭 행 그대로(검증 위치)
};
