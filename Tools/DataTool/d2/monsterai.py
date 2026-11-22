"""D2 — 몬스터 AI 묶음(R105 ~ R113) + 자식 탭 구조 검사(MonsterSkillHit · MonsterAISkill).

행 위치는 지금 GameData에서 이름 · 키로 찾는다(행을 끝에 더해도 사례가 따라온다). 시트 행 = 인덱스 + 3.
"""
from __future__ import annotations

import json

from dt.paths import GAMEDATA

from .common import custom, multi, set_cell


def _rows(table: str) -> list[dict]:
    return json.loads((GAMEDATA / f"{table}.json").read_text(encoding="utf-8"))["rows"]


def _idx(table: str, **match) -> int:
    for i, r in enumerate(_rows(table)):
        if all(r.get(k) == v for k, v in match.items()):
            return i
    raise SystemExit(f"[d2 monsterai] {table}에 {match} 행이 없다 — 사례를 고친다")


def _skill(name: str) -> int:
    return _idx("MonsterSkill", name=name)


def _hit(name: str, seq: int) -> int:
    return _idx("MonsterSkillHit", skill_id=_rows("MonsterSkill")[_skill(name)]["id"], seq=seq)


def _aoe(name: str) -> int:
    return _idx("MonsterAoE", name=name)


def _ai(monster_id: int) -> int:
    return _idx("MonsterAI", monster_id=monster_id)


def _slot(monster_id: int, seq: int) -> int:
    return _idx("MonsterAISkill", monster_id=monster_id, seq=seq)


def _w(table: str, i: int, key: str) -> str:
    return f"{table}!{i + 3} {key}"


DASH = _skill("늑대 돌진")
LEAP = _skill("덮치기")
SPEAR = _skill("창 던지기")
DEW = _skill("치유의 이슬")
SHELL = _skill("껍질 웅크리기")
SWIPE_HIT = _hit("앞발 후려치기", 1)          # 부채꼴
SPLIT_HIT = _hit("땅 가르기", 1)              # 직선 a = 4(돌진 아님)
RING_HIT = _hit("죽음의 고리", 1)             # 고리 a 4 · b 2
GRASP_HIT2 = _hit("망자의 손아귀", 2)         # 두 번째 타격
FROST = _aoe("서리 지대")
LAVA = _aoe("용암 웅덩이")
WARD = _aoe("뼈 결계(오라)")
LAST_AOE = len(_rows("MonsterAoE")) - 1
SLIME = _ai(1)                                # 수액덩이 — 기술 없음
RAIDER = _ai(2)                               # 그락 약탈꾼 — 기술 없음
BONE = _ai(13)                                # 백골 수비병 — 뼈 결계 오라
MOROZ = _ai(29)                               # 보스 모로즈
WOLF_SLOT = _slot(5, 1)                       # 회색늑대 칸 1
BEAR_SLOT2 = _slot(6, 2)                      # 불곰 칸 2
LEAF_SLOT = _slot(11, 1)                      # 잎새 정령 칸 1

CASES = [
    # R105 — id 1부터 연속(파사드가 인덱스로 찾는다)
    {"name": "R105_aoe_id_gap", "rules": ["R105"], "table": "MonsterAoE",
     "mutate": set_cell("MonsterAoE", LAST_AOE, "id", LAST_AOE + 5),
     "expect": {"rule": "R105", "where": _w("MonsterAoE", LAST_AOE, "id")}},
    # 타격(자식 행)이 없는 기술로 — 타격이 있으면 자식 행의 skill_id가 먼저 묶기에서 걸린다
    {"name": "R105_skill_id_gap", "rules": ["R105"], "table": "MonsterSkill",
     "mutate": set_cell("MonsterSkill", DEW, "id", 99),
     "expect": {"rule": "R105", "where": _w("MonsterSkill", DEW, "id")}},
    # R106 — 거리 · 이동
    {"name": "R106_range_min_gt_max", "rules": ["R106"], "table": "MonsterSkill",
     "mutate": set_cell("MonsterSkill", SPEAR, "range_min", 6),
     "expect": {"rule": "R106", "where": _w("MonsterSkill", SPEAR, "range_min")}},
    {"name": "R106_dash_zero_tiles", "rules": ["R106"], "table": "MonsterSkill",
     "mutate": set_cell("MonsterSkill", DASH, "move_tiles", 0),
     "expect": {"rule": "R106", "where": _w("MonsterSkill", DASH, "move_tiles")}},
    {"name": "R106_dash_move_ms_20", "rules": ["R106"], "table": "MonsterSkill",
     "mutate": set_cell("MonsterSkill", DASH, "move_ms", 20),
     "expect": {"rule": "R106", "where": _w("MonsterSkill", DASH, "move_ms")}},
    {"name": "R106_leap_move_ms_50", "rules": ["R106"], "table": "MonsterSkill",
     "mutate": set_cell("MonsterSkill", LEAP, "move_ms", 50),
     "expect": {"rule": "R106", "where": _w("MonsterSkill", LEAP, "move_ms"), "what": "도약"}},
    # R107 — 장판 참조
    {"name": "R107_aoe_missing", "rules": ["R107"], "table": "MonsterSkill",
     "mutate": set_cell("MonsterSkill", DEW, "aoe", 200),
     "expect": {"rule": "R107", "where": _w("MonsterSkill", DEW, "aoe")}},
    # R108 — 모양
    {"name": "R108_cone_half_angle_zero", "rules": ["R108"], "table": "MonsterSkillHit",
     "mutate": set_cell("MonsterSkillHit", SWIPE_HIT, "b", 0),
     "expect": {"rule": "R108", "where": _w("MonsterSkillHit", SWIPE_HIT, "b")}},
    {"name": "R108_path_line_without_dash", "rules": ["R108"], "table": "MonsterSkillHit",
     "mutate": set_cell("MonsterSkillHit", SPLIT_HIT, "a", 0),
     "expect": {"rule": "R108", "where": _w("MonsterSkillHit", SPLIT_HIT, "a"), "what": "돌진"}},
    {"name": "R108_ring_inner_gt_outer", "rules": ["R108"], "table": "MonsterSkillHit",
     "mutate": set_cell("MonsterSkillHit", RING_HIT, "b", 5),
     "expect": {"rule": "R108", "where": _w("MonsterSkillHit", RING_HIT, "b")}},
    # R109 — 틱 바닥 · 지속 시간
    {"name": "R109_interval_100", "rules": ["R109"], "table": "MonsterAoE",
     "mutate": set_cell("MonsterAoE", FROST, "interval_ms", 100),
     "expect": {"rule": "R109", "where": _w("MonsterAoE", FROST, "interval_ms")}},
    {"name": "R109_duration_zero", "rules": ["R109"], "table": "MonsterAoE",
     "mutate": set_cell("MonsterAoE", LAVA, "duration_ms", 0),
     "expect": {"rule": "R109", "where": _w("MonsterAoE", LAVA, "duration_ms")}},
    {"name": "R109_py_min_interval", "rules": ["R109"], "table": "MonsterAoE", "engine": "py",
     "mutate": set_cell("MonsterAoE", FROST, "interval_ms", 100),
     "expect": {"where": f"MonsterAoE#{FROST + 1} interval_ms", "what": "최소 200"}},
    # R110 — 종족이 있다 · 한 종족 한 행
    {"name": "R110_unknown_monster", "rules": ["R110"], "table": "MonsterAI",
     "mutate": set_cell("MonsterAI", SLIME, "monster_id", 999),
     "expect": {"rule": "R110", "where": _w("MonsterAI", SLIME, "monster_id"), "what": "999"}},
    {"name": "R110_duplicate_monster", "rules": ["R110"], "table": "MonsterAI",
     "mutate": set_cell("MonsterAI", RAIDER, "monster_id", 1),
     "expect": {"rule": "R110", "where": _w("MonsterAI", RAIDER, "monster_id"), "what": "둘"}},
    # R111 — 기술 칸
    {"name": "R111_slot_skill_missing", "rules": ["R111"], "table": "MonsterAISkill",
     "mutate": set_cell("MonsterAISkill", WOLF_SLOT, "skill_id", 900),
     "expect": {"rule": "R111", "where": _w("MonsterAISkill", WOLF_SLOT, "skill_id")}},
    {"name": "R111_phase_min_gt_max", "rules": ["R111"], "table": "MonsterAISkill",
     "mutate": multi(set_cell("MonsterAISkill", BEAR_SLOT2, "phase_min", 3), set_cell("MonsterAISkill", BEAR_SLOT2, "phase_max", 2)),
     "expect": {"rule": "R111", "where": _w("MonsterAISkill", BEAR_SLOT2, "phase_min")}},
    {"name": "R111_weight_zero", "rules": ["R111"], "table": "MonsterAISkill",
     "mutate": set_cell("MonsterAISkill", LEAF_SLOT, "weight", 0),
     "expect": {"rule": "R111", "where": _w("MonsterAISkill", LEAF_SLOT, "weight")}},
    {"name": "R111_chance_zero", "rules": ["R111"], "table": "MonsterAISkill",
     "mutate": set_cell("MonsterAISkill", LEAF_SLOT, "chance", 0),
     "expect": {"rule": "R111", "where": _w("MonsterAISkill", LEAF_SLOT, "chance")}},
    # R112 — 보스 단계 · 단계 기술 · 오라
    {"name": "R112_phase3_above_phase2", "rules": ["R112"], "table": "MonsterAI",
     "mutate": set_cell("MonsterAI", MOROZ, "phase3_hp", 70),
     "expect": {"rule": "R112", "where": _w("MonsterAI", MOROZ, "phase2_hp")}},
    {"name": "R112_phase_skill_missing", "rules": ["R112"], "table": "MonsterAI",
     "mutate": set_cell("MonsterAI", MOROZ, "phase2_skill", 999),
     "expect": {"rule": "R112", "where": _w("MonsterAI", MOROZ, "phase2_skill")}},
    {"name": "R112_phase_skill_without_threshold", "rules": ["R112"], "table": "MonsterAI",
     "mutate": set_cell("MonsterAI", SLIME, "phase3_skill", 1),
     "expect": {"rule": "R112", "where": _w("MonsterAI", SLIME, "phase3_skill"), "what": "문턱"}},
    {"name": "R112_aura_not_follow", "rules": ["R112"], "table": "MonsterAI",
     "mutate": set_cell("MonsterAI", BONE, "aura", FROST + 1),
     "expect": {"rule": "R112", "where": _w("MonsterAI", BONE, "aura"), "what": "follow"}},
    # R113 — 굳건 세기 상한(받는 피해 −90%까지)
    {"name": "R113_skill_fortify_95", "rules": ["R113"], "table": "MonsterSkill",
     "mutate": set_cell("MonsterSkill", SHELL, "self_status_power", 95),
     "expect": {"rule": "R113", "where": _w("MonsterSkill", SHELL, "self_status_power")}},
    {"name": "R113_aoe_fortify_95", "rules": ["R113"], "table": "MonsterAoE",
     "mutate": set_cell("MonsterAoE", WARD, "status_power", 95),
     "expect": {"rule": "R113", "where": _w("MonsterAoE", WARD, "status_power")}},
    # 자식 탭 구조(규칙 번호 없음 — 로더가 위치와 함께 거부)
    {"name": "struct_hit_seq_gap", "rules": [], "table": "MonsterSkillHit",
     "mutate": set_cell("MonsterSkillHit", GRASP_HIT2, "seq", 3),
     "expect": {"where": _w("MonsterSkillHit", GRASP_HIT2, "seq"), "what": "1부터 연속"}},
    {"name": "struct_hit_unknown_skill", "rules": [], "table": "MonsterSkillHit",
     "mutate": set_cell("MonsterSkillHit", SWIPE_HIT, "skill_id", 900),
     "expect": {"where": _w("MonsterSkillHit", SWIPE_HIT, "skill_id"), "what": "없는 기술"}},
    {"name": "struct_slot_unknown_monster", "rules": [], "table": "MonsterAISkill",
     "mutate": set_cell("MonsterAISkill", WOLF_SLOT, "monster_id", 999),
     "expect": {"where": _w("MonsterAISkill", WOLF_SLOT, "monster_id")}},
    {"name": "type_enum_unknown_ai", "rules": [], "table": "MonsterAI",
     "mutate": set_cell("MonsterAI", SLIME, "ai", "Sniper"),
     "expect": {"where": _w("MonsterAI", SLIME, "ai"), "what": "MonsterAiType"}},
    {"name": "type_enum_unknown_move", "rules": [], "table": "MonsterSkill",
     "mutate": set_cell("MonsterSkill", DASH, "move", "Teleport"),
     "expect": {"where": _w("MonsterSkill", DASH, "move"), "what": "MonsterMove"}},
]
