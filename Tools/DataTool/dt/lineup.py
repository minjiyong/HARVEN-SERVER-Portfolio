"""라인업 생성기 — Tools/DataTool/lineup/<판>.yaml → Bin/GameData/Item.json의 장비 · 재료 행 + CraftRecipe.json(오프라인).

`datatool lineup [--yaml …] [--dry-run] [--seed-econ]`. 계약: Shared/Document/ECONOMY.md(경제 1차) · ECON_SPEC §3 · §9.

  · 기존 장비는 code_name으로 찾아 id · 이름 · 표시 이름 · 아이콘 · 판매가(buy_price) · 세트 · 부위 · 무기 종류를 그대로 둔다.
  · 등급 · 단 · 경로 · 귀속 · 단계(tier) · 착용 레벨 · 직업 · 공격력 · 방어 · 매입가는 YAML 곡선으로 다시 계산한다.
  · 새 장비 id = 지금 가장 큰 id + 1부터(재료 61~66은 YAML의 id). 같은 YAML로 다시 돌리면 같은 결과(멱등).
  · 소비품 · 화폐 행은 그대로(새 열 rank · route · bind만 기본값으로 채운다).
  · Item.json의 장비가 YAML에 없으면 멈춘다(id는 영구 — 지우지 않는다).
쓰기는 dt.export.write — 표 JSON · _src.json(시트 행 = 키 순 + 3) · ids.lock · Gen/ItemIds.h · manifest.json.
"""
from __future__ import annotations

import copy
import json
import math
from pathlib import Path

import yaml

from . import export, schema
from .paths import GAMEDATA, TOOL_DIR

DEFAULT_YAML = TOOL_DIR / "lineup" / "phase1.yaml"
PARTS = ["Helmet", "Armor", "Gloves", "Boots", "Cloak", "Necklace", "Ring"]
CRAFT_PARTS = ["eWeapon", "eArmor", "eAccessory"]
ROUTE_BITS = ["Shop", "Field", "Elite", "Boss", "Craft", "Quest"]   # ItemRoute 값 1 << i


class LineupError(SystemExit):
    pass


def rhu(x: float) -> int:
    """반올림(0.5는 올림) — 파이썬 round()의 짝수 반올림을 쓰지 않는다"""
    return int(math.floor(x + 0.5 + 1e-9))


def ceil_ratio(n: int, ratio: float) -> int:
    return int(math.ceil(round(n * ratio, 6)))


def part_kind(slot: str) -> str:
    if slot in ("None", "Weapon"):
        return "weapon"
    return "accessory" if slot in ("Necklace", "Ring") else "armor"


def route_bits(text: str) -> int:
    bits = 0
    for name in [x.strip() for x in (text or "").split("|") if x.strip()]:
        if name not in ROUTE_BITS:
            raise LineupError(f"[lineup] 모르는 경로 {name!r}(ItemRoute: {', '.join(ROUTE_BITS)})")
        bits |= 1 << ROUTE_BITS.index(name)
    return bits


def route_text(bits: int) -> str:
    return "|".join(n for i, n in enumerate(ROUTE_BITS) if bits & (1 << i))


def interp(base: list, gi: int, rank: int) -> float:
    """하 × r^(단-1), r = sqrt(다음 등급 하 / 이 등급 하) — 상 = 다음 등급 하"""
    lo = base[gi]
    if rank == 1:
        return float(lo)
    if gi + 1 >= len(base):
        raise LineupError(f"[lineup] 등급 {gi}의 다음 등급 기준값이 없다(곡선 배열을 늘린다)")
    hi = base[gi + 1]
    return lo * (hi / lo) ** ((rank - 1) / 2)


def load_rows(gamedata: Path, name: str) -> list[dict]:
    p = gamedata / f"{name}.json"
    return json.loads(p.read_text(encoding="utf-8"))["rows"] if p.exists() else []


def craft_recipes(cfg: dict) -> list[dict]:
    """무기 레시피 → (무기 · 방어구 · 장신구) 세 행. 방어구 = 개수 · 수수료 × armor_craft_ratio(올림), 장신구 = 방어구 + 결정"""
    out = []
    ratio = cfg.get("armor_craft_ratio", 0.5)
    crystal = cfg.get("accessory_crystal", {})
    extra = cfg.get("accessory_crystal_count", 0)
    for idx, c in enumerate(cfg["craft"]):
        weapon = [[int(i), int(n)] for i, n in c["in"]]
        armor = [[i, ceil_ratio(n, ratio)] for i, n in weapon]
        acc = copy.deepcopy(armor)
        if c["grade"] in crystal and extra:
            cid = crystal[c["grade"]]
            for x in acc:
                if x[0] == cid:
                    x[1] += extra
                    break
            else:
                acc.append([cid, extra])
        fees = [int(c["fee"]), ceil_ratio(int(c["fee"]), ratio), ceil_ratio(int(c["fee"]), ratio)]
        for p, (part, ins) in enumerate(zip(CRAFT_PARTS, (weapon, armor, acc))):
            if len(ins) > 4:
                raise LineupError(f"[lineup] 레시피 {c['grade']}-{c['rank']} {part}: 재료가 4칸을 넘는다")
            row = {"id": idx * 3 + p + 1, "grade": c["grade"], "rank": int(c["rank"]), "part": part, "fee": fees[p]}
            for k in range(4):
                item, count = ins[k] if k < len(ins) else (0, 0)
                row[f"in{k + 1}_item"] = item
                row[f"in{k + 1}_count"] = count
            out.append(row)
    return out


def build(cfg: dict, gamedata: Path = GAMEDATA) -> tuple[list[dict], list[dict], list[str]]:
    """(Item 행, CraftRecipe 행, 요약 줄)"""
    items = load_rows(gamedata, "Item")
    if not items:
        raise LineupError(f"[lineup] {gamedata / 'Item.json'}이 없다")
    grades = list(cfg["grades"])
    gidx = {g: i for i, g in enumerate(grades)}
    rarity_pct = [r["sell_percent"] for r in load_rows(gamedata, "Rarity")]
    if len(rarity_pct) < len(grades):
        raise LineupError("[lineup] Rarity.json 행이 등급 수보다 적다(등급 i의 바닥 희귀도 = Rarity 행 i)")
    quest_items = {r[k] for r in load_rows(gamedata, "Quest") for k in ("reward_item_knight", "reward_item_engraver", "reward_item_hunter") if r[k]}
    shop_items = {r["item_id"] for r in load_rows(gamedata, "Shop")}
    by_code = {r["code_name"]: r for r in items if r.get("code_name")}
    by_id = {r["id"]: r for r in items}

    recipes = craft_recipes(cfg)
    fee_at = {(r["grade"], r["rank"], r["part"]): r["fee"] for r in recipes}
    wclass = cfg["weapon_class"]
    tendency = cfg["tendency"]
    part_names = cfg["part_names"]
    sell_cfg = cfg["sell"]
    bind_cells = {(g, int(r)) for g, r in cfg.get("bind_acquire", [])}

    # ── 라인업 항목(새 id 순서 = 등급 → 무기(직업 · 단) → 방어구(계열 · 부위)) ──
    entries: list[dict] = []
    for g in grades:
        for cls, per_grade in cfg["weapons"].items():
            for rank, e in enumerate(per_grade.get(g, []) or [], 1):
                entries.append(dict(e, cat="weapon", cls=cls, grade=g, rank=rank))
        for fam in cfg["armor"].get(g, []) or []:
            parts = fam.get("only") or PARTS
            for part in parts:
                piece = dict((fam.get("pieces") or {}).get(part) or {})
                if "code" not in piece:
                    disp, en, tail = part_names[part]
                    piece = dict({"code": f"{fam['code']}{tail}", "name": f"{fam['en']} {en}", "display": f"{fam['family']} {disp}"}, **piece)
                entries.append(dict(piece, cat="armor", part=part, grade=g, rank=int(fam["rank"]), tendency=fam["tendency"], family=fam["family"]))
    for e in cfg.get("existing_high", []) or []:
        entries.append(dict(e, cat="existing"))

    seen_codes: set[str] = set()
    for e in entries:
        if e["code"] in seen_codes:
            raise LineupError(f"[lineup] code {e['code']}가 두 번 있다")
        seen_codes.add(e["code"])
    mat_codes = {m["code"] for m in cfg["materials"]}
    if seen_codes & mat_codes:
        raise LineupError(f"[lineup] 재료와 장비 code가 겹친다: {sorted(seen_codes & mat_codes)}")

    # 다루지 않은 장비가 있으면 멈춘다
    missing = [f"{r['id']}({r.get('code_name')} {r['display_name']})" for r in items
               if r["type"] == "eEquipment" and r.get("code_name") not in seen_codes]
    if missing:
        raise LineupError("[lineup] YAML에 없는 장비(id는 영구 — 라인업에 넣거나 existing_high에 적는다): " + ", ".join(missing))

    out: dict[int, dict] = {}
    # ── 재료(id 고정) ──
    for m in cfg["materials"]:
        old = by_code.get(m["code"])
        if old is not None and old["id"] != m["id"]:
            raise LineupError(f"[lineup] 재료 {m['code']}: Item.json id {old['id']} ≠ YAML id {m['id']}")
        if old is None and m["id"] in by_id:
            raise LineupError(f"[lineup] 재료 {m['code']}: id {m['id']}를 이미 {by_id[m['id']].get('code_name')}가 쓴다")
        out[m["id"]] = {
            "id": m["id"], "code_name": m["code"], "type": "eMaterial", "max_stack": int(cfg.get("material_stack", 999)), "name": m["name"],
            "grade": m["grade"], "attack_bonus": 0, "heal_amount": 0, "effect": "eNone", "effect_value": 0,
            "buy_price": old["buy_price"] if old else 0, "sell_price": int(m["sell"]), "display_name": m["display"], "icon_id": int(m["icon"]),
            "equip_slot": "None", "weapon_kind": "None", "armor_bonus": 0, "magic_armor_bonus": 0, "tier": 1, "set_id": 0,
            "class_mask": "", "req_level": 0, "rank": 0, "route": "", "bind": m.get("bind", "eNone")}

    # ── 소비품 · 화폐(그대로 + 새 열 기본값) ──
    for r in items:
        if r["type"] == "eEquipment" or r.get("code_name") in mat_codes:
            continue
        if r["id"] in out:
            raise LineupError(f"[lineup] id {r['id']}({r.get('code_name')})가 재료 id와 겹친다")
        row = dict(r)
        row.setdefault("rank", 0)
        row.setdefault("route", "")
        row.setdefault("bind", "eNone")
        out[r["id"]] = row

    next_id = max([r["id"] for r in items] + list(out)) + 1
    new_ids: list[int] = []
    summary: list[str] = []
    for e in entries:
        old = by_code.get(e["code"])
        if e["cat"] == "existing" and old is None:
            raise LineupError(f"[lineup] existing_high {e['code']}가 Item.json에 없다")
        if old is not None:
            if old["type"] != "eEquipment":
                raise LineupError(f"[lineup] {e['code']}는 장비가 아니다")
            if e.get("display") and e["display"] != old["display_name"]:
                raise LineupError(f"[lineup] {e['code']}: YAML 표시 이름 {e['display']!r} ≠ Item.json {old['display_name']!r}")
            row = dict(old)
        else:
            for k in ("name", "display"):
                if not e.get(k):
                    raise LineupError(f"[lineup] 새 장비 {e['code']}에 {k}가 없다")
            if e["cat"] == "weapon" and not e.get("kind"):
                raise LineupError(f"[lineup] 새 무기 {e['code']}에 kind가 없다")
            gi0 = gidx[e["grade"]]
            if e["cat"] == "weapon":
                slot, kind = "Weapon", e["kind"]
                icon = cfg["icons"]["weapon"][kind][gi0]
            else:
                slot, kind = e["part"], "None"
                icon = cfg["icons"]["armor"][slot][gi0]
            row = {"id": next_id, "code_name": e["code"], "type": "eEquipment", "max_stack": 1, "name": e["name"], "grade": e["grade"],
                   "attack_bonus": 0, "heal_amount": 0, "effect": "eNone", "effect_value": 0, "buy_price": 0, "sell_price": 0,
                   "display_name": e["display"], "icon_id": icon, "equip_slot": slot, "weapon_kind": kind, "armor_bonus": 0,
                   "magic_armor_bonus": 0, "tier": 1, "set_id": 0, "class_mask": "", "req_level": 0}
            new_ids.append(next_id)
            next_id += 1
        if len(row["name"].encode("ascii", errors="replace")) > 19 or any(ord(ch) > 127 for ch in row["name"]):
            raise LineupError(f"[lineup] {e['code']}: 영문 이름 {row['name']!r}은 ASCII 19바이트 이하")

        g, rank = e["grade"], int(e["rank"])
        gi = gidx[g]
        slot = row["equip_slot"]
        kind = part_kind(slot)
        if kind == "weapon" and slot == "None":
            row["equip_slot"] = slot = "Weapon"
        row["grade"] = g
        row["rank"] = rank
        row["tier"] = int(cfg["grades"][g]["tier"])
        row["req_level"] = int(cfg["grades"][g]["req_level"])
        if kind == "weapon":
            cls = wclass.get(row["weapon_kind"])
            if cls is None:
                raise LineupError(f"[lineup] {e['code']}: 무기 종류 {row['weapon_kind']}의 직업이 weapon_class에 없다")
            if e["cat"] == "weapon" and cls != e["cls"]:
                raise LineupError(f"[lineup] {e['code']}: {e['cls']} 줄에 있는데 무기 종류 {row['weapon_kind']}는 {cls} 무기")
            row["class_mask"] = cls
            row["attack_bonus"] = rhu(interp(cfg["weapon_base"][cls], gi, rank))
            row["armor_bonus"] = row["magic_armor_bonus"] = 0
        else:
            ten = e.get("tendency")
            if ten not in tendency:
                raise LineupError(f"[lineup] {e['code']}: 계열 성향 {ten!r}가 tendency에 없다")
            value = interp(cfg["armor_base"], gi, rank) * cfg["part_mult"][slot]
            row["class_mask"] = ""
            row["attack_bonus"] = 0
            # 합을 먼저 반올림하고(최소 1) 성향대로 나눈다 — 칸마다 최소 1로 올리면 작은 부위가 1/1에 묶여 단이 올라도 그대로였다
            total = max(1, rhu(value))
            share = tendency[ten]
            armor = min(total, rhu(total * share[0]))
            if armor == 0 and share[0] >= share[1]:
                armor = 1
            if armor == total and share[1] > share[0] and total > 1:
                armor = total - 1
            row["armor_bonus"] = armor
            row["magic_armor_bonus"] = total - armor
        routes = cfg["routes"][g]
        bits = route_bits(routes[rank - 1] if rank - 1 < len(routes) else "")
        if row["id"] in quest_items:
            bits |= 1 << ROUTE_BITS.index("Quest")
        if row["id"] in shop_items:
            bits |= 1 << ROUTE_BITS.index("Shop")
        row["route"] = route_text(bits)
        row["bind"] = "eAcquire" if (g, rank) in bind_cells or (g, 0) in bind_cells else "eNone"
        # 매입가
        craft_part = {"weapon": "eWeapon", "armor": "eArmor", "accessory": "eAccessory"}[kind]
        pct = rarity_pct[gi]
        fee = fee_at.get((g, rank, craft_part))
        if bits & (1 << ROUTE_BITS.index("Craft")):
            if fee is None:
                raise LineupError(f"[lineup] {e['code']}: 제작 가능(Craft)인데 {g}-{rank} {craft_part} 레시피가 없다")
            row["sell_price"] = fee * int(sell_cfg["craft_percent"]) // pct
        else:
            row["sell_price"] = rhu(sell_cfg["curve"][g][rank - 1] * sell_cfg["part_factor"][kind])
        # 상점에서 사서 되팔기(R090) — 상점품의 판매가는 그대로 두므로 매입가가 맞춘다
        if row["buy_price"]:
            resale = row["sell_price"] * pct // 100
            if row["sell_price"] > row["buy_price"] or resale > row["buy_price"]:
                raise LineupError(f"[lineup] {e['code']}: 매입가 {row['sell_price']}(되팔기 {resale}) > 판매가 {row['buy_price']} — sell 곡선을 낮춘다")
        if row["id"] in out:
            raise LineupError(f"[lineup] id {row['id']}({e['code']})가 두 번 나온다")
        out[row["id"]] = row

    rows = [out[k] for k in sorted(out)]
    # 요약(등급-단 칸마다)
    cells: dict = {}
    for r in rows:
        if r["type"] == "eEquipment":
            cells.setdefault((gidx[r["grade"]], r["rank"]), []).append(r)
    for (gi, rank), lst in sorted(cells.items()):
        g = grades[gi]
        summary.append(f"{cfg['grades'][g]['label']}-{'하중상'[rank - 1]} {len(lst):3}종  " + " · ".join(f"{r['id']} {r['display_name']}" for r in lst))
    n_equip = sum(1 for r in rows if r["type"] == "eEquipment")
    summary.append(f"장비 {n_equip} · 재료 {len(cfg['materials'])} · 전체 {len(rows)}행 · 새 장비 id {new_ids[0] if new_ids else '-'}~{new_ids[-1] if new_ids else '-'}({len(new_ids)}개)")
    return rows, recipes, summary


def seed_tables(cfg: dict) -> dict[str, list[dict]]:
    """--seed-econ: LootConst · TierLoot · EliteGear · BossLoot · Dismantle 첫 값"""
    s = cfg["seed"]
    tiers = []
    keys = ["tier", "common_bp", "uncommon_bp", "rare_bp", "essence_item", "essence_permille", "crystal_item", "crystal_permille",
            "elite_crystal_item", "elite_crystal_count", "elite_essence_count", "elite_gear_percent"]
    for t in s["TierLoot"]:
        if len(t) != len(keys):
            raise LineupError(f"[lineup] seed.TierLoot 행 {t}: {len(keys)}칸이어야 한다")
        tiers.append(dict(zip(keys, t)))
    elite = [{"tier": int(t), "seq": i + 1, "grade": g, "rank": int(r), "weight": int(w)}
             for t, lst in s["EliteGear"].items() for i, (g, r, w) in enumerate(lst)]
    boss = [{"species": int(sp), "seq": i + 1, "item_id": int(item), "weight": int(w)}
            for sp, lst in s["BossLoot"].items() for i, (item, w) in enumerate(lst)]
    dis = [{"grade": g, "shard_count": int(a), "item_id": int(b), "item_count": int(c)} for g, a, b, c in s["Dismantle"]]
    return {"LootConst": [dict(s["LootConst"])], "TierLoot": tiers, "EliteGear": elite, "BossLoot": boss, "Dismantle": dis}


def write_tables(tables: dict[str, list[dict]], enums: dict, gamedata: Path = GAMEDATA) -> list[str]:
    """표를 키 순으로 정렬해 시트 행(키 순 + 3)과 함께 export.write로"""
    tabs = schema.load()
    payload = {}
    for name, rows in tables.items():
        tab = tabs[name]
        want = [c.key for c in tab.columns]
        for r in rows:
            if set(r) != set(want):
                raise LineupError(f"[lineup] {name} 행 {r.get(want[0])}: 열이 스키마와 다르다(빠짐 {sorted(set(want) - set(r))} · 남음 {sorted(set(r) - set(want))})")
        if tab.key:
            rows = sorted(rows, key=export.sort_key(tab, enums))
        payload[name] = (tab, [{k: r[k] for k in want} for r in rows], list(range(3, 3 + len(rows))))
    return export.write(payload, enums, gamedata=gamedata)


def cmd_lineup(args) -> int:
    from .pipeline import enums_dict
    path = Path(args.yaml) if getattr(args, "yaml", None) else DEFAULT_YAML
    cfg = yaml.safe_load(path.read_text(encoding="utf-8"))
    rows, recipes, summary = build(cfg)
    for line in summary:
        print("[lineup] " + line)
    tables = {"Item": rows, "CraftRecipe": recipes}
    if getattr(args, "seed_econ", False):
        tables.update(seed_tables(cfg))
    if getattr(args, "dry_run", False):
        print(f"[lineup] --dry-run — 쓰지 않는다(표 {', '.join(tables)})")
        return 0
    changed = write_tables(tables, enums_dict())
    print(f"[lineup] 표 {', '.join(tables)} · 바뀐 파일 {len(changed)}")
    for c in changed:
        print("  " + c)
    print("[lineup] 다음: datatool check --gamedata · verify(--update) · d2 — 시트에는 push --tabs Item,CraftRecipe --force-seed(ECONOMY.md)")
    return 0
