"""D2 — 파티 묶음(R114~R116) + 구조 검사(PartyConst 한 행).

PartyConst는 한 행 — 시트 행 = 3. 시트 수준 범위(min · max)는 스키마(schema/party.yaml)가 먼저 막고(engine "py"),
C++ 로더(ValidateParty)는 같은 규칙을 R번호로 한 번 더 막는다(검증을 건너뛴 판 · 손으로 고친 GameData).
"""
from .common import custom, set_cell

CASES = [
    # R114 — 인원 2 ~ PartyRule::kMaxMembers(8)
    {"name": "R114_max_members_9", "rules": ["R114"], "table": "PartyConst",
     "mutate": set_cell("PartyConst", 0, "max_members", 9),
     "expect": {"rule": "R114", "where": "PartyConst!3 max_members"}},
    {"name": "R114_max_members_1", "rules": ["R114"], "table": "PartyConst",
     "mutate": set_cell("PartyConst", 0, "max_members", 1),
     "expect": {"rule": "R114", "where": "PartyConst!3 max_members"}},
    {"name": "R114_py_max_members_9", "rules": ["R114"], "table": "PartyConst", "engine": "py",
     "mutate": set_cell("PartyConst", 0, "max_members", 9),
     "expect": {"where": "PartyConst#1 max_members", "what": "최대 8"}},
    # R115 — 줍기 우선권: 주인 ≤ 주인 + 파티 < 30초(물건이 사라지는 시간)
    {"name": "R115_party_shorter_than_owner", "rules": ["R115"], "table": "PartyConst",
     "mutate": set_cell("PartyConst", 0, "loot_party_ms", 5000),
     "expect": {"rule": "R115", "where": "PartyConst!3 loot_party_ms", "what": "보다 짧다"}},
    {"name": "R115_party_reaches_despawn", "rules": ["R115"], "table": "PartyConst",
     "mutate": set_cell("PartyConst", 0, "loot_party_ms", 30000),
     "expect": {"rule": "R115", "where": "PartyConst!3 loot_party_ms", "what": "사라지는 시간"}},
    # R116 — 경험치 나눔 거리 1 ~ 30칸
    {"name": "R116_share_range_zero", "rules": ["R116"], "table": "PartyConst",
     "mutate": set_cell("PartyConst", 0, "exp_share_range", 0),
     "expect": {"rule": "R116", "where": "PartyConst!3 exp_share_range"}},
    {"name": "R116_share_range_31", "rules": ["R116"], "table": "PartyConst",
     "mutate": set_cell("PartyConst", 0, "exp_share_range", 31),
     "expect": {"rule": "R116", "where": "PartyConst!3 exp_share_range"}},
    # 구조(규칙 번호 없음)
    {"name": "struct_party_const_two_rows", "rules": [], "table": "PartyConst",
     "mutate": custom("PartyConst에 행을 하나 더", lambda d: d["PartyConst"].append(dict(d["PartyConst"][0]))),
     "expect": {"where": "PartyConst", "what": "한 행"}},
]
