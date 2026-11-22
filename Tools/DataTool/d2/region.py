"""D2 — 지형 묶음(R076~R079 · R081) + RegionSpawn 구조(spawns[4] · seq) · 종족 참조 · 형 검사.

GameData/Region.json 행 i = zone_id i, 시트 행 = i + 3. RegionSpawn.json은 (zone_id, seq) 순 — 시트 행 = i + 3.
RegionSpawn 행 3~6 = zone 1(seq 1~4), 46~47 = zone 14(seq 1~2).
걷기 판정은 마을 파사드 WorldData::IsBlocked: (750, 246)은 지역 1의 막힌 칸(옛 패턴), (752, 753)은 데니차 마을의 막힌 칸,
(1760, 760)은 지역 7의 막힌 칸.
R081(예전 static_assert(Detail::ValidateRegions()) 묶음 호출)은 R076~R079 사례가 행 위치와 함께 걸리는 것으로 덮는다.
"""
from .common import custom, drop_key, multi, set_cell


def _swap_regions(a: int, b: int):
    def f(data):
        rows = data["Region"]
        rows[a], rows[b] = rows[b], rows[a]
    return custom(f"Region[{a}] ↔ Region[{b}] 행 순서를 바꾼다", f)


def _drop_last_region():
    def f(data):
        last = data["Region"].pop()
        data["RegionSpawn"] = [r for r in data["RegionSpawn"] if r["zone_id"] != last["zone_id"]]
    return custom("Region 마지막 행(zone 15)과 그 RegionSpawn 행을 지운다 — 15행", f)


def _append_region():
    def f(data):
        extra = dict(data["Region"][-1])
        extra["zone_id"] = len(data["Region"])
        data["Region"].append(extra)
        data["RegionSpawn"].append({"zone_id": extra["zone_id"], "seq": 1, "species": 1, "weight": 10})
    return custom("Region에 17번째 행(zone_id 16)을 더한다", f)


def _drop_spawns(zone: int):
    def f(data):
        data["RegionSpawn"] = [r for r in data["RegionSpawn"] if r["zone_id"] != zone]
    return custom(f"zone {zone}의 RegionSpawn 행을 모두 지운다", f)


CASES = [
    # R076: zone_id = 행 순서 · 행 수 = kRegionCount(16)
    {"name": "R076_zone_id_not_row_order", "rules": ["R076", "R081"], "table": "Region",
     "mutate": _swap_regions(3, 4),
     "expect": {"rule": "R076", "where": "Region!6 zone_id", "what": "행 순서"}},
    {"name": "R076_rows_15", "rules": ["R076", "R081"], "table": "Region",
     "mutate": _drop_last_region(),
     "expect": {"rule": "R076", "where": "Region", "what": "행 수 15 < 16"}},
    {"name": "R076_rows_17", "rules": ["R076"], "table": "Region",
     "mutate": _append_region(),
     "expect": {"rule": "R076", "where": "Region.json#17 zone_id", "what": "행 수 17 > 16"}},
    {"name": "R076_py_rows_exact", "rules": ["R076"], "table": "Region", "engine": "py",
     "mutate": _drop_last_region(),
     "expect": {"where": "Region", "what": "행 수 15 ≠ 16"}},
    # R077: 길잡이 도착 지점 — 그 지역 안 · 걸을 수 있음
    {"name": "R077_land_x_other_region", "rules": ["R077", "R081"], "table": "Region",
     "mutate": set_cell("Region", 1, "land_x", 249),
     "expect": {"rule": "R077", "where": "Region!4 land_x", "what": "밖이다"}},
    {"name": "R077_land_y_other_region", "rules": ["R077"], "table": "Region",
     "mutate": set_cell("Region", 1, "land_y", 500),
     "expect": {"rule": "R077", "where": "Region!4 land_y", "what": "밖이다"}},
    {"name": "R077_land_outside_world", "rules": ["R077"], "table": "Region",
     "mutate": set_cell("Region", 0, "land_x", -1),
     "expect": {"rule": "R077", "where": "Region!3 land_x", "what": "밖이다"}},
    {"name": "R077_land_blocked_field", "rules": ["R077"], "table": "Region",
     "mutate": multi(set_cell("Region", 1, "land_x", 756), set_cell("Region", 1, "land_y", 240)),   # (750, 246)은 길(IsRoad)이 됐다
     "expect": {"rule": "R077", "where": "Region!4 land_x", "what": "막힌 칸"}},
    {"name": "R077_land_blocked_town", "rules": ["R077"], "table": "Region",
     "mutate": multi(set_cell("Region", 5, "land_x", 752), set_cell("Region", 5, "land_y", 753)),
     "expect": {"rule": "R077", "where": "Region!8 land_x", "what": "막힌 칸"}},
    # R078: 필드 보스가 있으면 보스 자리 — 그 지역 안 · 걸을 수 있음
    {"name": "R078_boss_other_region", "rules": ["R078", "R081"], "table": "Region",
     "mutate": set_cell("Region", 7, "boss_x", 1499),
     "expect": {"rule": "R078", "where": "Region!10 boss_x", "what": "밖이다"}},
    {"name": "R078_boss_y_other_region", "rules": ["R078"], "table": "Region",
     "mutate": set_cell("Region", 0, "boss_y", 500),
     "expect": {"rule": "R078", "where": "Region!3 boss_y", "what": "밖이다"}},
    {"name": "R078_boss_blocked", "rules": ["R078"], "table": "Region",
     "mutate": multi(set_cell("Region", 7, "boss_x", 1760), set_cell("Region", 7, "boss_y", 760)),
     "expect": {"rule": "R078", "where": "Region!10 boss_x", "what": "막힌 칸"}},
    # R079: 지역마다 스폰 가중치 합 > 0
    {"name": "R079_weights_zero", "rules": ["R079", "R081"], "table": "RegionSpawn",
     "mutate": multi(set_cell("RegionSpawn", 46, "weight", 0), set_cell("RegionSpawn", 47, "weight", 0)),
     "expect": {"rule": "R079", "where": "RegionSpawn!49 weight", "what": "가중치 합이 0"}},
    {"name": "R079_no_spawn_rows", "rules": ["R079"], "table": "RegionSpawn",
     "mutate": _drop_spawns(14),
     "expect": {"rule": "R079", "where": "Region!17 zone_id", "what": "RegionSpawn 행이 없다"}},
    # RegionSpawn 구조(spawns[4] — 구조 상수) · seq · 부모(규칙 번호 없음 — 로더가 위치와 함께 거부)
    {"name": "spawn_seq_over_slots", "rules": [], "table": "RegionSpawn",
     "mutate": set_cell("RegionSpawn", 6, "seq", 5),
     "expect": {"where": "RegionSpawn!9 seq", "what": "구조 상수"}},
    {"name": "spawn_seq_gap", "rules": [], "table": "RegionSpawn",
     "mutate": set_cell("RegionSpawn", 47, "seq", 3),
     "expect": {"where": "RegionSpawn!50 seq", "what": "1부터 연속"}},
    {"name": "spawn_seq_duplicate", "rules": [], "table": "RegionSpawn",
     "mutate": set_cell("RegionSpawn", 47, "seq", 1),
     "expect": {"where": "RegionSpawn!50 seq", "what": "중복"}},
    {"name": "spawn_zone_unknown", "rules": [], "table": "RegionSpawn",
     "mutate": set_cell("RegionSpawn", 0, "zone_id", 16),
     "expect": {"where": "RegionSpawn!3 zone_id", "what": "Region에 없는"}},
    # 종족 참조 — MonsterTable 파사드로
    {"name": "spawn_species_unknown", "rules": [], "table": "RegionSpawn",
     "mutate": set_cell("RegionSpawn", 3, "species", 99),
     "expect": {"where": "RegionSpawn!6 species", "what": "Monster에 없는"}},
    {"name": "spawn_species_zero", "rules": [], "table": "RegionSpawn",
     "mutate": set_cell("RegionSpawn", 3, "species", 0),
     "expect": {"where": "RegionSpawn!6 species", "what": "Monster에 없는"}},
    {"name": "boss_species_unknown", "rules": [], "table": "Region",
     "mutate": set_cell("Region", 0, "boss_species", 99),
     "expect": {"where": "Region!3 boss_species", "what": "Monster에 없는"}},
    # 형 · 열 검사
    {"name": "region_type_enum_unknown", "rules": [], "table": "Region",
     "mutate": set_cell("Region", 2, "biome", "eJungle"),
     "expect": {"where": "Region!5 biome", "what": "Biome"}},
    {"name": "region_type_i16_overflow", "rules": [], "table": "Region",
     "mutate": set_cell("Region", 2, "land_x", 40000),
     "expect": {"where": "Region!5 land_x", "what": "범위"}},
    {"name": "region_type_missing_column", "rules": [], "table": "RegionSpawn",
     "mutate": drop_key("RegionSpawn", 10, "weight"),
     "expect": {"where": "RegionSpawn!13 weight", "what": "열이 없다"}},
]
