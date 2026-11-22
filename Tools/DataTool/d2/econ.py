"""D2 — 경제 묶음(R124~R135) + 구조 검사(R번호 없음). 설계: Shared/Document/ECONOMY.md.

GameData 행 순서 = 키 순서, 시트 행(_src.json) = i + 3.
  Item 행 i = id i + 1(1~130 연속 — 시트 Item!(id + 2)). 61~66 재료, 67~ 새 장비.
  TierLoot 행 t - 1 = 단계 t. EliteGear (tier, seq) 순: 0~1 = 단계 2, 2~4 = 단계 3, 5~6 = 단계 4, 7~8 = 단계 5.
  BossLoot (species, seq) 순: 0~4 = 모로즈(29), 5~9 = 우르간(30), 10~23 = 세베린(31), 24~28 = 굴개미 여왕(32), 29~41 = 즈마이(33), 42~45 = 그라목(34).
  CraftRecipe 행 i = id i + 1(등급-단 칸마다 무기 · 방어구 · 장신구). Dismantle 행 i = 등급 i.
"""
from .common import custom, multi, set_cell


def _item(id_: int, key: str, value):
    return set_cell("Item", id_ - 1, key, value)


def _strip_route(grade: str, bit: str):
    def f(data):
        for r in data["Item"]:
            if r["type"] == "eEquipment" and r["grade"] == grade:
                r["route"] = "|".join(x for x in r["route"].split("|") if x and x != bit)
    return custom(f"Item: {grade} 장비의 route에서 {bit}를 뺀다", f)


def _drop_species(species: int):
    def f(data):
        data["BossLoot"] = [r for r in data["BossLoot"] if r["species"] != species]
    return custom(f"BossLoot: 몬스터 {species}의 행을 모두 지운다", f)


def _species_weights_zero(species: int):
    def f(data):
        for r in data["BossLoot"]:
            if r["species"] == species:
                r["weight"] = 0
    return custom(f"BossLoot: 몬스터 {species}의 가중치를 모두 0", f)


def _species_to(old: int, new: int):
    def f(data):
        for r in data["BossLoot"]:
            if r["species"] == old:
                r["species"] = new
    return custom(f"BossLoot: 몬스터 {old} 행을 {new}(보스 아님)로", f)


def _elite_tier_weights_zero(tier: int):
    def f(data):
        for r in data["EliteGear"]:
            if r["tier"] == tier:
                r["weight"] = 0
    return custom(f"EliteGear: 단계 {tier}의 가중치를 모두 0", f)


def _drop_last(table: str):
    return custom(f"{table}의 마지막 행을 지운다", lambda data: data[table].pop())


CASES = [
    # ── R124 — 장비는 단 1~3, 장비가 아니면 단 0 · 경로 없음 ──
    {"name": "R124_equipment_rank_zero", "rules": ["R124"], "table": "Item",
     "mutate": _item(2, "rank", 0),
     "expect": {"rule": "R124", "where": "Item!4 rank", "what": "장비는 단 1~3"}},
    {"name": "R124_consumable_rank", "rules": ["R124"], "table": "Item",
     "mutate": _item(1, "rank", 1),
     "expect": {"rule": "R124", "where": "Item!3 rank", "what": "장비가 아니면 단 0"}},
    {"name": "R124_material_route", "rules": ["R124"], "table": "Item",
     "mutate": _item(61, "route", "Field"),
     "expect": {"rule": "R124", "where": "Item!63 route"}},
    {"name": "R124_py_rank_4", "rules": ["R124"], "table": "Item", "engine": "py",
     "mutate": _item(3, "rank", 4),
     "expect": {"where": "Item#3 rank", "what": "최대 3"}},
    # ── R125 — Craft면 (등급, 단, 부위) 레시피 · 레시피의 칸에 Craft 장비 ──
    {"name": "R125_craft_without_recipe", "rules": ["R125"], "table": "Item",
     "mutate": _item(2, "route", "Shop|Field|Craft"),
     "expect": {"rule": "R125", "where": "Item!4 route", "what": "레시피"}},
    {"name": "R125_recipe_cell_without_craft", "rules": ["R125"], "table": "CraftRecipe",
     "mutate": set_cell("CraftRecipe", 0, "rank", 1),
     "expect": {"rule": "R125", "where": "CraftRecipe!3 grade", "what": "일반-하"}},
    # ── R126 — 드롭 표에 장비 없음 ──
    {"name": "R126_drop_entry_equipment", "rules": ["R126"], "table": "DropEntry",
     "mutate": set_cell("DropEntry", 0, "item_id", 2),
     "expect": {"rule": "R126", "where": "DropEntry!3 item_id", "what": "연습용 목검"}},
    # ── R127 — BossLoot ↔ route Boss · 보스 몬스터 · 가중치 ──
    {"name": "R127_loot_item_without_boss_route", "rules": ["R127"], "table": "BossLoot",
     "mutate": set_cell("BossLoot", 0, "item_id", 4),
     "expect": {"rule": "R127", "where": "BossLoot!3 item_id", "what": "Boss가 없다"}},
    {"name": "R127_boss_route_not_in_loot", "rules": ["R127"], "table": "BossLoot",
     "mutate": _drop_species(34),
     "expect": {"rule": "R127", "where": "Item!20 route", "what": "칼둔의 처형검"}},
    {"name": "R127_not_boss_species", "rules": ["R127"], "table": "BossLoot",
     "mutate": _species_to(29, 28),
     "expect": {"rule": "R127", "where": "BossLoot!3 species", "what": "보스가 아니다"}},
    {"name": "R127_weights_zero", "rules": ["R127"], "table": "BossLoot",
     "mutate": _species_weights_zero(34),
     "expect": {"rule": "R127", "where": "BossLoot!45 weight"}},
    # ── R128 — 상점 ↔ route Shop ──
    {"name": "R128_shop_equipment_without_route", "rules": ["R128"], "table": "Shop",
     "mutate": _item(2, "route", "Field"),
     "expect": {"rule": "R128", "where": "Shop!10 item_id", "what": "연습용 목검"}},
    {"name": "R128_shop_route_not_sold", "rules": ["R128"], "table": "Item",
     "mutate": _item(67, "route", "Shop|Field|Craft"),
     "expect": {"rule": "R128", "where": "Item!69 route", "what": "구리 장검"}},
    # ── R129 — 퀘스트 보상 ↔ route Quest ──
    {"name": "R129_reward_without_route", "rules": ["R129"], "table": "Quest",
     "mutate": _item(4, "route", "Field|Elite|Craft"),
     "expect": {"rule": "R129", "where": "reward_item_knight", "what": "강철 장검"}},
    {"name": "R129_quest_route_unused", "rules": ["R129"], "table": "Item",
     "mutate": _item(67, "route", "Field|Craft|Quest"),
     "expect": {"rule": "R129", "where": "Item!69 route", "what": "주는 퀘스트가 없다"}},
    # ── R130 — TierLoot ──
    {"name": "R130_bp_over_10000", "rules": ["R130"], "table": "TierLoot",
     "mutate": multi(set_cell("TierLoot", 1, "common_bp", 9000), set_cell("TierLoot", 1, "uncommon_bp", 1000)),
     "expect": {"rule": "R130", "where": "TierLoot!4 common_bp", "what": "10012"}},
    {"name": "R130_rare_without_field_gear", "rules": ["R130"], "table": "TierLoot",
     "mutate": _strip_route("eEpic", "Field"),
     "expect": {"rule": "R130", "where": "TierLoot!4 rare_bp", "what": "Field"}},
    {"name": "R130_essence_not_material", "rules": ["R130"], "table": "TierLoot",
     "mutate": set_cell("TierLoot", 0, "essence_item", 1),
     "expect": {"rule": "R130", "where": "TierLoot!3 essence_item"}},
    {"name": "R130_crystal_chance_without_item", "rules": ["R130"], "table": "TierLoot",
     "mutate": set_cell("TierLoot", 1, "crystal_item", 0),
     "expect": {"rule": "R130", "where": "TierLoot!4 crystal_item", "what": "결정 아이템이 0"}},
    {"name": "R130_py_bp_over", "rules": ["R130"], "table": "TierLoot", "engine": "py",
     "mutate": set_cell("TierLoot", 0, "common_bp", 10001),
     "expect": {"where": "TierLoot#1 common_bp", "what": "최대 10000"}},
    # ── R131 — EliteGear ──
    {"name": "R131_cell_without_elite_gear", "rules": ["R131"], "table": "EliteGear",
     "mutate": set_cell("EliteGear", 0, "rank", 2),
     "expect": {"rule": "R131", "where": "EliteGear!3 grade", "what": "고급-중"}},
    {"name": "R131_tier_weights_zero", "rules": ["R131"], "table": "EliteGear",
     "mutate": _elite_tier_weights_zero(2),
     "expect": {"rule": "R131", "where": "TierLoot!4 elite_gear_percent"}},
    # ── R132 — CraftRecipe 재료 · 유일 ──
    {"name": "R132_input_not_material", "rules": ["R132"], "table": "CraftRecipe",
     "mutate": set_cell("CraftRecipe", 0, "in1_item", 1),
     "expect": {"rule": "R132", "where": "CraftRecipe!3 in1_item"}},
    {"name": "R132_input_count_zero", "rules": ["R132"], "table": "CraftRecipe",
     "mutate": set_cell("CraftRecipe", 0, "in2_count", 0),
     "expect": {"rule": "R132", "where": "CraftRecipe!3 in2_count", "what": "개수가 0"}},
    {"name": "R132_duplicate_cell", "rules": ["R132"], "table": "CraftRecipe",
     "mutate": set_cell("CraftRecipe", 1, "part", "eWeapon"),
     "expect": {"rule": "R132", "where": "CraftRecipe!4 part", "what": "겹친다"}},
    # ── R133 — Dismantle · 파편 ──
    {"name": "R133_dismantle_not_material", "rules": ["R133"], "table": "Dismantle",
     "mutate": set_cell("Dismantle", 0, "item_id", 1),
     "expect": {"rule": "R133", "where": "Dismantle!3 item_id"}},
    {"name": "R133_shard_not_material", "rules": ["R133"], "table": "LootConst",
     "mutate": set_cell("LootConst", 0, "shard_item", 2),
     "expect": {"rule": "R133", "where": "LootConst!3 shard_item"}},
    {"name": "R133_dismantle_row_count", "rules": ["R133"], "table": "Dismantle",
     "mutate": _drop_last("Dismantle"),
     "expect": {"rule": "R133", "where": "Dismantle", "what": "등급 수"}},
    # ── R134 — 제작 장비 매입가 ≤ 수수료 50% ──
    {"name": "R134_craft_sell_over_half_fee", "rules": ["R134"], "table": "Item",
     "mutate": _item(67, "sell_price", 51),
     "expect": {"rule": "R134", "where": "Item!69 sell_price", "what": "수수료 100"}},
    # ── R135 — 겹치는 아이템 귀속은 재료만 ──
    {"name": "R135_consumable_bind", "rules": ["R135"], "table": "Item",
     "mutate": _item(1, "bind", "eAcquire"),
     "expect": {"rule": "R135", "where": "Item!3 bind"}},
    # ── 구조(R번호 없음 — 바로 찾는 색인 · 열 형) ──
    {"name": "struct_loot_const_two_rows", "rules": [], "table": "LootConst",
     "mutate": custom("LootConst에 행을 하나 더", lambda d: d["LootConst"].append(dict(d["LootConst"][0]))),
     "expect": {"where": "LootConst", "what": "한 행"}},
    {"name": "struct_tier_loot_gap", "rules": [], "table": "TierLoot",
     "mutate": set_cell("TierLoot", 2, "tier", 4),
     "expect": {"where": "TierLoot!5 tier", "what": "1부터 연속"}},
    {"name": "struct_elite_seq_gap", "rules": [], "table": "EliteGear",
     "mutate": set_cell("EliteGear", 1, "seq", 3),
     "expect": {"where": "EliteGear!4 seq", "what": "1부터 연속"}},
    {"name": "struct_boss_seq_gap", "rules": [], "table": "BossLoot",
     "mutate": set_cell("BossLoot", 1, "seq", 9),
     "expect": {"where": "BossLoot!4 seq", "what": "1부터 연속"}},
    {"name": "struct_recipe_rank_zero", "rules": [], "table": "CraftRecipe",
     "mutate": set_cell("CraftRecipe", 0, "rank", 0),
     "expect": {"where": "CraftRecipe!3 rank", "what": "단은 1~3"}},
    {"name": "struct_dismantle_order", "rules": [], "table": "Dismantle",
     "mutate": set_cell("Dismantle", 1, "grade", "eNone"),
     "expect": {"where": "Dismantle!4 grade", "what": "행 순서"}},
    {"name": "type_item_route_unknown", "rules": [], "table": "Item",
     "mutate": _item(2, "route", "Field|Teleport"),
     "expect": {"where": "Item!4 route", "what": "Teleport"}},
    {"name": "code_instance_bound_bits", "rules": [], "table": "Item", "engine": "code", "glob": "ItemRules.h",
     "pattern": r"static_assert\(ItemInstance::Unpack\(kBoundBit \| kTradedBit\)\.bound", "expect": {}},
]
