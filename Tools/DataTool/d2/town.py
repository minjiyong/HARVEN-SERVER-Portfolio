"""D2 — 마을 묶음(R064 · R065 · R072~R075 · R080 · R104) + 구조 검사(구조 상수 · seq · 한 글자 열 · 이름 겹침) · 형 · 열 검사.

mapgen이 주인인 표(Town*)는 시트 행이 없다 — 위치는 `TownDummy.json#2 x`(GameData 파일의 행 번호, 1부터).
시트 탭(Town · District · Ambient)은 _src.json의 시트 행(`Ambient!3 kind` — 행 i = 시트 i + 3).
마을 좌표: 원점 (708, 714), 84 × 72. 훈련장 (775, 748)~(789, 761). 허수아비 (778, 755) · (781, 755) · (784, 755).
"""
from .common import custom, drop_key, multi, set_cell


def _grid_cell(row: int, col: int, ch: str):
    def f(data):
        r = data["TownGrid"][row]
        r["ground"] = r["ground"][:col] + ch + r["ground"][col + 1:]
    f.desc = f"TownGrid[{row}].ground[{col}] = {ch!r}"
    return f


def _grid_text(row: int, key: str, fn, what: str):
    def f(data):
        data["TownGrid"][row][key] = fn(data["TownGrid"][row][key])
    f.desc = f"TownGrid[{row}].{key} {what}"
    return f


CASES = [
    # ── R064 · R065: 허수아비 자리는 훈련장 안 · 걸을 수 있음 · 안전지대 ──
    {"name": "R064_dummy_outside_yard_x", "rules": ["R064", "R065"], "table": "TownDummy",
     "mutate": set_cell("TownDummy", 0, "x", 774),
     "expect": {"rule": "R064", "where": "TownDummy.json#1 x", "what": "훈련장"}},
    {"name": "R064_dummy_outside_yard_y", "rules": ["R064", "R065"], "table": "TownDummy",
     "mutate": set_cell("TownDummy", 2, "y", 762),
     "expect": {"rule": "R064", "where": "TownDummy.json#3 y", "what": "훈련장"}},
    {"name": "R064_dummy_blocked", "rules": ["R064", "R065"], "table": "TownDummy",
     "mutate": multi(set_cell("TownDummy", 1, "x", 776), set_cell("TownDummy", 1, "y", 749)),   # 훈련장 안의 과녁(P 칸)
     "expect": {"rule": "R064", "where": "TownDummy.json#2 x", "what": "막힌 칸 'P'"}},
    {"name": "R064_dummy_outside_safezone", "rules": ["R064", "R065"], "table": "TownInfo",
     # 훈련장 사각형을 마을 밖으로 옮기고 허수아비도 따라가면 훈련장 안이지만 안전지대 밖
     "mutate": multi(set_cell("TownInfo", 0, "yard_x0", 795), set_cell("TownInfo", 0, "yard_x1", 800),
                     set_cell("TownDummy", 0, "x", 797)),
     "expect": {"rule": "R064", "where": "TownDummy.json#1 x", "what": "안전지대"}},
    # ── R072 · R080: 격자 kH줄 · 줄마다 kW글자 ──
    {"name": "R072_ground_row_short", "rules": ["R072", "R080"], "table": "TownGrid",
     "mutate": _grid_text(4, "ground", lambda s: s[:-1], "한 글자 줄임"),
     "expect": {"rule": "R072", "where": "TownGrid.json#5 ground", "what": "83"}},
    {"name": "R072_level_row_long", "rules": ["R072", "R080"], "table": "TownGrid",
     "mutate": _grid_text(10, "level", lambda s: s + "0", "한 글자 늘림"),
     "expect": {"rule": "R072", "where": "TownGrid.json#11 level", "what": "85"}},
    {"name": "R072_surface_row_short", "rules": ["R072", "R080"], "table": "TownGrid",
     "mutate": _grid_text(70, "surface", lambda s: s[:40], "절반으로"),
     "expect": {"rule": "R072", "where": "TownGrid.json#71 surface"}},
    {"name": "R072_row_missing", "rules": ["R072", "R080"], "table": "TownGrid",
     "mutate": custom("TownGrid 마지막 줄을 뺀다", lambda data: data["TownGrid"].pop()),
     "expect": {"rule": "R072", "where": "TownGrid.json#72 y", "what": "71"}},
    # ── R073 · R080: 건물 양 끝이 마을 안 ──
    {"name": "R073_building_x0_outside", "rules": ["R073", "R080"], "table": "TownBuilding",
     "mutate": set_cell("TownBuilding", 0, "x0", 700),
     "expect": {"rule": "R073", "where": "TownBuilding.json#1 x0", "what": "밖"}},
    {"name": "R073_building_x1_outside", "rules": ["R073", "R080"], "table": "TownBuilding",
     "mutate": set_cell("TownBuilding", 44, "x1", 800),
     "expect": {"rule": "R073", "where": "TownBuilding.json#45 x1", "what": "밖"}},
    # ── R074 · R080: 건물 발자국 칸은 모두 'B' ──
    {"name": "R074_building_wider_than_footprint", "rules": ["R074", "R080"], "table": "TownBuilding",
     "mutate": set_cell("TownBuilding", 0, "x1", 741),   # 경비 초소 동쪽 옆 광장 판석까지
     "expect": {"rule": "R074", "where": "TownBuilding.json#1 x0", "what": "(741, 777)이 ','"}},
    {"name": "R074_footprint_cell_open", "rules": ["R074", "R080"], "table": "TownGrid",
     "mutate": _grid_cell(63, 29, "."),                   # (737, 777) — 경비 초소 발자국 한 칸을 풀밭으로
     "expect": {"rule": "R074", "where": "TownBuilding.json#1 x0", "what": "(737, 777)이 '.'"}},
    # ── R075 · R080: 부활 자리는 걸을 수 있는 마을 칸 ──
    {"name": "R075_spawn_on_building", "rules": ["R075", "R080"], "table": "TownSpawn",
     "mutate": multi(set_cell("TownSpawn", 0, "x", 737), set_cell("TownSpawn", 0, "y", 777)),
     "expect": {"rule": "R075", "where": "TownSpawn.json#1 x", "what": "'B'"}},
    {"name": "R075_spawn_outside_town", "rules": ["R075", "R080"], "table": "TownSpawn",
     "mutate": set_cell("TownSpawn", 2, "x", 700),
     "expect": {"rule": "R075", "where": "TownSpawn.json#3 x", "what": "걸을 수 있는"}},
    # ── R104: 부활 · 귀환 자리가 하나 이상(PickTownSpawn이 행 수로 나눈다) ──
    {"name": "R104_no_town_spawn", "rules": ["R104"], "table": "TownSpawn",
     "mutate": custom("TownSpawn 행을 모두 지운다", lambda data: data["TownSpawn"].clear()),
     "expect": {"rule": "R104", "where": "TownSpawn", "what": "행이 없다"}},
    # ── 구조 검사(규칙 번호 없음 — 구조 상수 · 정본 행 순서 · 한 글자 열. 로더가 위치와 함께 거부) ──
    {"name": "struct_towninfo_w_not_structure_constant", "rules": [], "table": "TownInfo",
     "mutate": set_cell("TownInfo", 0, "w", 85),
     "expect": {"where": "TownInfo.json#1 w", "what": "구조 상수 TownMap::kW"}},
    {"name": "struct_towninfo_zone_not_structure_constant", "rules": [], "table": "TownInfo",
     "mutate": multi(set_cell("TownInfo", 0, "zone_id", 6), set_cell("Town", 0, "zone_id", 6)),
     "expect": {"where": "TownInfo.json#1 zone_id", "what": "구조 상수 TownMap::kZoneId"}},
    {"name": "struct_town_zone_differs_from_mapgen", "rules": [], "table": "Town",
     "mutate": set_cell("Town", 0, "zone_id", 4),
     "expect": {"where": "Town!3 zone_id", "what": "TownInfo.zone_id"}},
    {"name": "struct_town_two_rows", "rules": [], "table": "Town",
     "mutate": custom("Town 행을 하나 더", lambda data: data["Town"].append(dict(data["Town"][0]))),
     "expect": {"where": "Town.json", "what": "1개"}},
    {"name": "struct_grid_y_gap", "rules": [], "table": "TownGrid",
     "mutate": set_cell("TownGrid", 3, "y", 7),
     "expect": {"where": "TownGrid.json#4 y", "what": "연속"}},
    {"name": "struct_spawn_seq_gap", "rules": [], "table": "TownSpawn",
     "mutate": set_cell("TownSpawn", 3, "seq", 9),
     "expect": {"where": "TownSpawn.json#4 seq", "what": "연속"}},
    {"name": "struct_district_seq_gap", "rules": [], "table": "District",
     "mutate": set_cell("District", 5, "seq", 1),
     "expect": {"where": "District!8 seq", "what": "연속"}},
    {"name": "struct_door_two_chars", "rules": [], "table": "TownBuilding",
     "mutate": set_cell("TownBuilding", 2, "door", "EW"),
     "expect": {"where": "TownBuilding.json#3 door", "what": "바이트"}},
    {"name": "struct_door_empty", "rules": [], "table": "TownBuilding",
     "mutate": set_cell("TownBuilding", 2, "door", ""),
     "expect": {"where": "TownBuilding.json#3 door", "what": "비었다"}},
    {"name": "struct_spot_key_duplicate", "rules": [], "table": "TownSpot",
     "mutate": set_cell("TownSpot", 1, "key", "behind_store"),
     "expect": {"where": "TownSpot.json#2 key", "what": "겹치지"}},
    {"name": "struct_mapgen_table_missing", "rules": [], "table": "TownProp",
     "mutate": custom("TownProp 표(파일)를 뺀다", lambda data: data.pop("TownProp")),
     "expect": {"where": "TownProp.json", "what": "파일이 없다"}},
    # ── 형 · 열 검사(RowReader) ──
    {"name": "type_ambient_kind_unknown", "rules": [], "table": "Ambient",
     "mutate": set_cell("Ambient", 0, "kind", "eThunder"),
     "expect": {"where": "Ambient!3 kind", "what": "AmbientKind"}},
    {"name": "type_building_kind_unknown", "rules": [], "table": "TownBuilding",
     "mutate": set_cell("TownBuilding", 4, "kind", "eCastle"),
     "expect": {"where": "TownBuilding.json#5 kind", "what": "BuildingKind"}},
    {"name": "type_district_missing_zoom", "rules": [], "table": "District",
     "mutate": drop_key("District", 2, "zoom"),
     "expect": {"where": "District!5 zoom", "what": "열이 없다"}},
    {"name": "type_spawn_x_over_i16", "rules": [], "table": "TownSpawn",
     "mutate": set_cell("TownSpawn", 1, "x", 40000),
     "expect": {"where": "TownSpawn.json#2 x", "what": "범위"}},
    {"name": "type_spot_x_not_number", "rules": [], "table": "TownSpot",
     "mutate": set_cell("TownSpot", 0, "x", "755"),
     "expect": {"where": "TownSpot.json#1 x", "what": "수가 아니다"}},
]
