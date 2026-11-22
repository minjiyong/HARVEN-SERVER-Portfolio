"""D2 — 개인 업적 묶음(R136~R140).

시트 행 = 3부터(행 i → !(3 + i)). Achievement.json 행 i = 업적 id i + 1(1차 49행). 시트 수준 범위(min · max · max_bytes · ref)는
스키마(schema/achieve.yaml)가 먼저 막고(engine "py"), C++ 로더(ValidateAchieve)는 같은 규칙을 R번호로 한 번 더 막는다.
"""
from .common import custom, multi, set_cell


def _many_keys(d):
    # 서로 다른 (eKill, 종) 130개 — 세는 종류 상한 128(진행 기록 크기)을 넘긴다
    for k in range(130):
        r = dict(d["Achievement"][0])
        r.update({"id": 300 + k, "group": 300 + k, "step": 1, "param": 1000 + k, "target": 1})
        d["Achievement"].append(r)


CASES = [
    # R136 — 표 구조
    {"name": "R136_id_1024", "rules": ["R136"], "table": "Achievement",
     "mutate": set_cell("Achievement", 48, "id", 1024),
     "expect": {"rule": "R136", "where": "Achievement!51 id"}},
    {"name": "R136_id_duplicate", "rules": ["R136"], "table": "Achievement",
     "mutate": set_cell("Achievement", 1, "id", 1),
     "expect": {"rule": "R136", "where": "Achievement!4 id", "what": "겹친다"}},
    {"name": "R136_name_31_bytes", "rules": ["R136"], "table": "Achievement",
     "mutate": set_cell("Achievement", 0, "name", "가나다라마바사아자차카"),
     "expect": {"rule": "R136", "where": "Achievement!3 name"}},
    {"name": "R136_desc_93_bytes", "rules": ["R136"], "table": "Achievement",
     "mutate": set_cell("Achievement", 5, "desc", "가" * 31),
     "expect": {"rule": "R136", "where": "Achievement!8 desc"}},
    {"name": "R136_target_zero", "rules": ["R136"], "table": "Achievement",
     "mutate": set_cell("Achievement", 23, "target", 0),
     "expect": {"rule": "R136", "where": "Achievement!26 target"}},
    {"name": "R136_too_many_keys", "rules": ["R136"], "table": "Achievement",
     "mutate": custom("서로 다른 (eKill, 종) 130개를 더한다", _many_keys),
     "expect": {"rule": "R136", "where": "Achievement", "what": "kMaxStatKeys"}},
    {"name": "R136_py_id_1024", "rules": ["R136"], "table": "Achievement", "engine": "py",
     "mutate": set_cell("Achievement", 48, "id", 1024),
     "expect": {"where": "Achievement#49 id", "what": "최대 1023"}},
    {"name": "R136_py_name_bytes", "rules": ["R136"], "table": "Achievement", "engine": "py",
     "mutate": set_cell("Achievement", 0, "name", "가나다라마바사아자차카"),
     "expect": {"where": "Achievement#1 name", "what": "33바이트 > 30"}},
    # R137 — kind · param
    {"name": "R137_kill_unknown_species", "rules": ["R137"], "table": "Achievement",
     "mutate": multi(set_cell("Achievement", 0, "param", 999), set_cell("Achievement", 1, "param", 999),
                     set_cell("Achievement", 2, "param", 999), set_cell("Achievement", 3, "param", 999)),
     "expect": {"rule": "R137", "where": "Achievement!3 param", "what": "없는 종"}},
    {"name": "R137_kill_dummy", "rules": ["R137"], "table": "Achievement",
     "mutate": multi(set_cell("Achievement", 0, "param", 35), set_cell("Achievement", 1, "param", 35),
                     set_cell("Achievement", 2, "param", 35), set_cell("Achievement", 3, "param", 35)),
     "expect": {"rule": "R137", "where": "Achievement!3 param", "what": "허수아비"}},
    {"name": "R137_boss_not_boss", "rules": ["R137"], "table": "Achievement",
     "mutate": set_cell("Achievement", 9, "param", 1),
     "expect": {"rule": "R137", "where": "Achievement!12 param", "what": "보스가 아니다"}},
    {"name": "R137_gear_grade_6", "rules": ["R137"], "table": "Achievement",
     "mutate": set_cell("Achievement", 29, "param", 6),
     "expect": {"rule": "R137", "where": "Achievement!32 param"}},
    {"name": "R137_quest_unknown", "rules": ["R137"], "table": "Achievement",
     "mutate": multi(set_cell("Achievement", 40, "param", 999), set_cell("Achievement", 41, "param", 999)),
     "expect": {"rule": "R137", "where": "Achievement!43 param", "what": "없는 퀘스트"}},
    {"name": "R137_param_unused", "rules": ["R137"], "table": "Achievement",
     "mutate": set_cell("Achievement", 23, "param", 3),
     "expect": {"rule": "R137", "where": "Achievement!26 param", "what": "쓰지 않는다"}},
    # R138 — 목표가 닿는다
    {"name": "R138_level_51", "rules": ["R138"], "table": "Achievement",
     "mutate": set_cell("Achievement", 22, "target", 51),
     "expect": {"rule": "R138", "where": "Achievement!25 target", "what": "최고 레벨"}},
    {"name": "R138_zone_17", "rules": ["R138"], "table": "Achievement",
     "mutate": set_cell("Achievement", 48, "target", 17),
     "expect": {"rule": "R138", "where": "Achievement!51 target", "what": "들어갈 수 있는 지역"}},
    {"name": "R138_zone_unreachable_region", "rules": ["R138"], "table": "Region",
     "mutate": set_cell("Region", 15, "teleport_fee", 0),
     "expect": {"rule": "R138", "where": "Achievement!51 target", "what": "(15"}},
    {"name": "R138_enchant_stops", "rules": ["R138"], "table": "Enchant",
     "mutate": multi(set_cell("Enchant", 7, "success_tickets", 0), set_cell("Enchant", 7, "grace_attempts", 0)),
     "expect": {"rule": "R138", "where": "Achievement!36 target", "what": "+7에서 멈춘다"}},
    # R139 — 묶음 · 단계
    {"name": "R139_step_gap", "rules": ["R139"], "table": "Achievement",
     "mutate": set_cell("Achievement", 2, "step", 4),
     "expect": {"rule": "R139", "where": "Achievement!5 step"}},
    {"name": "R139_target_not_increasing", "rules": ["R139"], "table": "Achievement",
     "mutate": set_cell("Achievement", 1, "target", 1),
     "expect": {"rule": "R139", "where": "Achievement!4 target", "what": "커야 한다"}},
    {"name": "R139_same_param_not_increasing", "rules": ["R139"], "table": "Achievement",
     "mutate": set_cell("Achievement", 27, "param", 1),
     "expect": {"rule": "R139", "where": "Achievement!30 target", "what": "같은 것을 세는 앞 단계"}},
    {"name": "R139_category_differs", "rules": ["R139"], "table": "Achievement",
     "mutate": set_cell("Achievement", 19, "category", "eHunt"),
     "expect": {"rule": "R139", "where": "Achievement!22 category"}},
    # R140 — 보상
    {"name": "R140_unknown_item", "rules": ["R140"], "table": "Achievement",
     "mutate": set_cell("Achievement", 0, "reward_item", 999),
     "expect": {"rule": "R140", "where": "Achievement!3 reward_item", "what": "없는 아이템"}},
    {"name": "R140_currency_item", "rules": ["R140"], "table": "Achievement",
     "mutate": set_cell("Achievement", 0, "reward_item", 7),
     "expect": {"rule": "R140", "where": "Achievement!3 reward_item", "what": "그로시"}},
    {"name": "R140_count_over_stack", "rules": ["R140"], "table": "Achievement",
     "mutate": set_cell("Achievement", 3, "reward_count", 100),
     "expect": {"rule": "R140", "where": "Achievement!6 reward_count", "what": "max_stack(99)"}},
    {"name": "R140_count_zero", "rules": ["R140"], "table": "Achievement",
     "mutate": set_cell("Achievement", 0, "reward_count", 0),
     "expect": {"rule": "R140", "where": "Achievement!3 reward_count"}},
    {"name": "R140_count_without_item", "rules": ["R140"], "table": "Achievement",
     "mutate": set_cell("Achievement", 43, "reward_count", 5),
     "expect": {"rule": "R140", "where": "Achievement!46 reward_count", "what": "개수는 0"}},
    {"name": "R140_adena_over_wallet", "rules": ["R140"], "table": "Achievement",
     "mutate": set_cell("Achievement", 43, "reward_adena", 2000000001),
     "expect": {"rule": "R140", "where": "Achievement!46 reward_adena", "what": "지갑 상한"}},
    {"name": "R140_py_adena_over_wallet", "rules": ["R140"], "table": "Achievement", "engine": "py",
     "mutate": set_cell("Achievement", 43, "reward_adena", 2000000001),
     "expect": {"where": "Achievement#44 reward_adena", "what": "최대 2000000000"}},
    {"name": "R140_py_unknown_item", "rules": ["R140"], "table": "Achievement", "engine": "py",
     "mutate": set_cell("Achievement", 0, "reward_item", 999),
     "expect": {"where": "Achievement#1 reward_item", "what": "Item에 없는 id 999"}},
]
