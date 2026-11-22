"""D2 — 아이템 묶음(R023~R026 · R082~R099) + 구조 검사(R번호 없음). R030(+10 공격력 고정값)은 폐지 — 강화 성능이 시트 값.
R100 · R101(옛 장비 드롭 GearDrop)은 폐지 — 경제 1차 로직 2부에서 GearDrop · GearChance · ClassWeapon 표를 지웠다(장비는 econ 묶음 — d2/econ.py).

GameData 행 순서 = 키 순서(Item 행 i = id i+1, DropEntry는 (table_id, seq) 순). _src.json의 시트 행 = i + 3.
R023 · R024(열거형 비트 폭)는 코드에 남은 static_assert — engine "code"로 그 줄이 있는지 본다.
R094 · R095(드롭 엔트리의 type · grade = Item 값)는 구조로 보장 — 그 열을 없앴다:
  ① 로더가 DropEntry의 type · grade 열을 "모르는 열"로 거부한다(cpp) ② 런타임 엔트리 구조체에 그 필드가 없다(code).
"""
from .common import add_key, custom, multi, set_cell


def _drop_last(table: str):
    return custom(f"{table}의 마지막 행을 지운다", lambda data: data[table].pop())


def _zero_table_weights(table_id: int):
    def f(data):
        for r in data["DropEntry"]:
            if r["table_id"] == table_id:
                r["weight"] = 0
    return custom(f"DropEntry: 표 {table_id}의 가중치를 모두 0(no_drop_weight도 0인 표)", f)


def _shop_overflow(data):
    """NPC 2의 진열을 17칸까지 채운다(ShopRow::items[16] — 구조 상한)"""
    at = max(i for i, r in enumerate(data["Shop"]) if r["npc_id"] == 2)
    seq = data["Shop"][at]["seq"]
    for k in range(seq + 1, 18):
        at += 1
        data["Shop"].insert(at, {"npc_id": 2, "seq": k, "item_id": 1})


CASES = [
    # ── 코드에 남은 규칙(열거형 비트 폭 — 구조) ──
    {"name": "R023_option_bits_code", "rules": ["R023"], "table": "ItemOption", "engine": "code", "glob": "ItemRules.h",
     "pattern": r"static_assert\(static_cast<int>\(ItemOption::Count\) <= 32", "expect": {}},
    {"name": "R024_proc_bits_code", "rules": ["R024"], "table": "ItemProc", "engine": "code", "glob": "ItemRules.h",
     "pattern": r"static_assert\(static_cast<int>\(ItemProc::Count\) <= 16", "expect": {}},
    # ── ItemOption · ItemProc 행 수 · 순서 ──
    {"name": "R025_option_row_order", "rules": ["R025"], "table": "ItemOption",
     "mutate": set_cell("ItemOption", 3, "id", "Str"),
     "expect": {"rule": "R025", "where": "ItemOption!6 id"}},
    {"name": "R025_option_row_count", "rules": ["R025"], "table": "ItemOption",
     "mutate": _drop_last("ItemOption"),
     "expect": {"rule": "R025", "where": "ItemOption", "what": "ItemOption::Count"}},
    {"name": "R026_proc_row_order", "rules": ["R026"], "table": "ItemProc",
     "mutate": set_cell("ItemProc", 2, "id", "Thunder"),
     "expect": {"rule": "R026", "where": "ItemProc!5 id"}},
    {"name": "R026_proc_row_count", "rules": ["R026"], "table": "ItemProc",
     "mutate": _drop_last("ItemProc"),
     "expect": {"rule": "R026", "where": "ItemProc", "what": "ItemProc::Count"}},
    # ── Enchant(성공 몫 ≤ 1조) ──
    {"name": "R099_enchant_tickets_over_scale", "rules": ["R099"], "table": "Enchant",
     "mutate": set_cell("Enchant", 3, "success_tickets", 1000000000001),
     "expect": {"rule": "R099", "where": "Enchant!6 success_tickets"}},
    # ── Item V2 장비 필드(R082~R085 — R086은 묶음 호출) ──
    {"name": "R082_consumable_armor", "rules": ["R082", "R086"], "table": "Item",
     "mutate": set_cell("Item", 0, "armor_bonus", 3),
     "expect": {"rule": "R082", "where": "Item!3 armor_bonus"}},
    {"name": "R082_consumable_slot", "rules": ["R082"], "table": "Item",
     "mutate": set_cell("Item", 7, "equip_slot", "Ring"),
     "expect": {"rule": "R082", "where": "Item!10 equip_slot"}},
    {"name": "R083_equipment_tier_zero", "rules": ["R083"], "table": "Item",
     "mutate": set_cell("Item", 1, "tier", 0),
     "expect": {"rule": "R083", "where": "Item!4 tier"}},
    {"name": "R084_weapon_without_kind", "rules": ["R084"], "table": "Item",
     "mutate": set_cell("Item", 1, "weapon_kind", "None"),
     "expect": {"rule": "R084", "where": "Item!4 weapon_kind"}},
    {"name": "R084_helmet_with_kind", "rules": ["R084"], "table": "Item",
     "mutate": set_cell("Item", 28, "weapon_kind", "Sword"),
     "expect": {"rule": "R084", "where": "Item!31 weapon_kind"}},
    {"name": "R085_helmet_attack_bonus", "rules": ["R085"], "table": "Item",
     "mutate": set_cell("Item", 28, "attack_bonus", 5),
     "expect": {"rule": "R085", "where": "Item!31 attack_bonus"}},
    # ── Item 기본 검사(R087~R091 — R092는 묶음 호출) ──
    {"name": "R087_id_zero", "rules": ["R087", "R092"], "table": "Item",
     "mutate": set_cell("Item", 2, "id", 0),
     "expect": {"rule": "R087", "where": "Item!5 id"}},
    {"name": "R088_display_name_empty", "rules": ["R088"], "table": "Item",
     "mutate": set_cell("Item", 4, "display_name", ""),
     "expect": {"rule": "R088", "where": "Item!7 display_name"}},
    {"name": "R089_equipment_stack", "rules": ["R089"], "table": "Item",
     "mutate": set_cell("Item", 1, "max_stack", 5),
     "expect": {"rule": "R089", "where": "Item!4 max_stack"}},
    {"name": "R090_sell_over_buy", "rules": ["R090"], "table": "Item",
     "mutate": set_cell("Item", 0, "sell_price", 30),
     "expect": {"rule": "R090", "where": "Item!3 sell_price"}},
    # 상점이 파는 장비는 상점에서 산 희귀도(등급 바닥 — 철검 eRare → 고급 130%)로 되판다: 1600 ≤ 2000이지만 1600 × 130% = 2080 > 2000
    {"name": "R090_shop_resale_rarity", "rules": ["R090"], "table": "Item",
     "mutate": set_cell("Item", 2, "sell_price", 1600),
     "expect": {"rule": "R090", "where": "Item!5 sell_price", "what": "2080"}},
    {"name": "R091_duplicate_id", "rules": ["R091"], "table": "Item",
     "mutate": set_cell("Item", 5, "id", 3),
     "expect": {"rule": "R091", "where": "Item!8 id", "what": "Item!5"}},
    # ── 드롭(R093~R098) ──
    {"name": "R093_drop_unknown_item", "rules": ["R093"], "table": "DropEntry",
     "mutate": set_cell("DropEntry", 0, "item_id", 999),
     "expect": {"rule": "R093", "where": "DropEntry!3 item_id"}},
    {"name": "R094_drop_type_column", "rules": ["R094"], "table": "DropEntry",
     "mutate": add_key("DropEntry", 0, "type", "eConsumable"),
     "expect": {"where": "DropEntry!3 type", "what": "모르는 열"}},
    {"name": "R095_drop_grade_column", "rules": ["R095"], "table": "DropEntry",
     "mutate": add_key("DropEntry", 4, "grade", "eRare"),
     "expect": {"where": "DropEntry!7 grade", "what": "모르는 열"}},
    {"name": "R094_R095_entry_has_no_type_code", "rules": ["R094", "R095"], "table": "DropEntry", "engine": "code", "glob": "Snap_Item.h",
     "pattern": r"struct DropEntry\s*\{(?:(?!type|grade|Type|Grade)[^{}])*\}", "expect": {}},
    {"name": "R096_min_over_max", "rules": ["R096"], "table": "DropEntry",
     "mutate": set_cell("DropEntry", 0, "min_count", 3),
     "expect": {"rule": "R096", "where": "DropEntry!3 min_count"}},
    {"name": "R096_min_zero", "rules": ["R096"], "table": "DropEntry",
     "mutate": set_cell("DropEntry", 1, "min_count", 0),
     "expect": {"rule": "R096", "where": "DropEntry!4 min_count"}},
    # 드롭 표에는 장비가 없다(R126) — 겹치기 99인 각인 두루마리(표 1 seq 5)를 100개로
    {"name": "R097_over_max_stack", "rules": ["R097"], "table": "DropEntry",
     "mutate": set_cell("DropEntry", 4, "max_count", 100),
     "expect": {"rule": "R097", "where": "DropEntry!7 max_count"}},
    {"name": "R098_table_weight_zero", "rules": ["R098"], "table": "DropTable",
     "mutate": _zero_table_weights(6),
     "expect": {"rule": "R098", "where": "DropTable!8 no_drop_weight"}},
    # ── 구조(R번호 없음 — 배열 크기 · 바로 찾는 색인) ──
    {"name": "struct_item_set_id_over", "rules": [], "table": "Item",
     "mutate": set_cell("Item", 46, "set_id", 4),
     "expect": {"where": "Item!49 set_id", "what": "setCount"}},
    {"name": "struct_set_id_over", "rules": [], "table": "ItemSet",
     "mutate": set_cell("ItemSet", 2, "id", 4),
     "expect": {"where": "ItemSet!5 id", "what": "setCount"}},
    {"name": "struct_rarity_option_count", "rules": [], "table": "Rarity",
     "mutate": set_cell("Rarity", 5, "option_count", 5),
     "expect": {"where": "Rarity!8 option_count", "what": "opts"}},
    {"name": "struct_rarity_row_order", "rules": [], "table": "Rarity",
     "mutate": set_cell("Rarity", 1, "id", "Common"),
     "expect": {"where": "Rarity!4 id", "what": "행 순서"}},
    {"name": "struct_rarity_step_count", "rules": [], "table": "RarityStep",
     "mutate": _drop_last("RarityStep"),
     "expect": {"where": "RarityStep", "what": "행 수"}},
    {"name": "struct_enchant_no_rows", "rules": [], "table": "Enchant",
     "mutate": custom("Enchant 행을 모두 지운다", lambda data: data["Enchant"].clear()),
     "expect": {"where": "Enchant", "what": "행이 없다"}},
    {"name": "struct_enchant_level_gap", "rules": [], "table": "Enchant",
     "mutate": set_cell("Enchant", 4, "level", 7),
     "expect": {"where": "Enchant!7 level", "what": "+0부터 연속"}},
    {"name": "struct_enchant_const_two_rows", "rules": [], "table": "EnchantConst",
     "mutate": custom("EnchantConst 행을 하나 더", lambda data: data["EnchantConst"].append(dict(data["EnchantConst"][0]))),
     "expect": {"where": "EnchantConst", "what": "한 행"}},
    {"name": "struct_drop_table_gap", "rules": [], "table": "DropTable",
     "mutate": set_cell("DropTable", 3, "id", 12),
     "expect": {"where": "DropTable!6 id", "what": "1부터 연속"}},
    {"name": "struct_drop_entry_seq", "rules": [], "table": "DropEntry",
     "mutate": set_cell("DropEntry", 2, "seq", 9),
     "expect": {"where": "DropEntry!5 seq", "what": "1부터 연속"}},
    {"name": "struct_shop_17_slots", "rules": [], "table": "Shop",
     "mutate": custom("Shop: NPC 2를 17번째 칸까지", _shop_overflow),
     "expect": {"where": "Shop", "what": "최대 16칸"}},
    {"name": "struct_shop_item_zero", "rules": [], "table": "Shop",
     "mutate": set_cell("Shop", 0, "item_id", 0),
     "expect": {"where": "Shop!3 item_id", "what": "빈 칸"}},
    {"name": "ref_shop_unknown_npc", "rules": [], "table": "Shop",
     "mutate": multi(set_cell("Shop", 0, "npc_id", 1000), set_cell("Shop", 1, "npc_id", 1000), set_cell("Shop", 2, "npc_id", 1000),
                     set_cell("Shop", 3, "npc_id", 1000), set_cell("Shop", 4, "npc_id", 1000), set_cell("Shop", 5, "npc_id", 1000),
                     set_cell("Shop", 6, "npc_id", 1000)),
     "expect": {"where": "Shop!3 npc_id", "what": "없는 NPC"}},
    {"name": "type_item_name_20_bytes", "rules": [], "table": "Item",
     "mutate": set_cell("Item", 0, "name", "Herb Potion Extra Big"),
     "expect": {"where": "Item!3 name", "what": "> 19"}},
]
