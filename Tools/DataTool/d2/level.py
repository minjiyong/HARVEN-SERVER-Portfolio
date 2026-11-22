"""D2 — 레벨 묶음(R002 · R027~R029) + 구조 검사(R번호 없음).

GameData/Level.json 행 i = 레벨 i+1, Class.json 행 i = ClassId i. _src.json의 시트 행 = i + 3.
"""
from .common import custom, set_cell


def _drop_last(table: str):
    return custom(f"{table}의 마지막 행을 지운다", lambda data: data[table].pop())


CASES = [
    {"name": "R002_class_row_order", "rules": ["R002"], "table": "Class",
     "mutate": set_cell("Class", 1, "id", "Knight"),
     "expect": {"rule": "R002", "where": "Class!4 id"}},
    {"name": "R002_class_row_count", "rules": ["R002"], "table": "Class",
     "mutate": _drop_last("Class"),
     "expect": {"rule": "R002", "where": "Class", "what": "ClassId::Count"}},
    {"name": "R027_level1_exp_to_next", "rules": ["R027"], "table": "Level",
     "mutate": set_cell("Level", 0, "exp_to_next", 31),
     "expect": {"rule": "R027", "where": "Level!3 exp_to_next"}},
    {"name": "R028_level1_max_hp", "rules": ["R028"], "table": "Level",
     "mutate": set_cell("Level", 0, "max_hp", 101),
     "expect": {"rule": "R028", "where": "Level!3 max_hp"}},
    {"name": "R029_level1_base_attack", "rules": ["R029"], "table": "Level",
     "mutate": set_cell("Level", 0, "base_attack", 11),
     "expect": {"rule": "R029", "where": "Level!3 base_attack"}},
    # 구조(R번호 없음 — 파사드가 레벨 - 1로 바로 찾는다 · 행 수 = LevelTable::kMaxLevel)
    {"name": "struct_level_row_count", "rules": [], "table": "Level",
     "mutate": _drop_last("Level"),
     "expect": {"where": "Level", "what": "kMaxLevel"}},
    {"name": "struct_level_not_contiguous", "rules": [], "table": "Level",
     "mutate": set_cell("Level", 9, "level", 12),
     "expect": {"where": "Level!12 level", "what": "1부터 연속"}},
    {"name": "type_class_unknown_enum", "rules": [], "table": "Class",
     "mutate": set_cell("Class", 2, "id", "Paladin"),
     "expect": {"where": "Class!5 id", "what": "ClassId"}},
]
