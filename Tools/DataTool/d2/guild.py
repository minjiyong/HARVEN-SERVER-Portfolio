"""D2 — 결사 성장 묶음(R117~R123) + 구조 검사(GuildConst 한 행).

시트 행 = 3부터(행 i → !(3 + i)). 시트 수준 범위(min · max · ref)는 스키마(schema/guild.yaml)가 먼저 막고(engine "py"),
C++ 로더(ValidateGuild)는 같은 규칙을 R번호로 한 번 더 막는다(검증을 건너뛴 판 · 손으로 고친 GameData).
"""
from .common import custom, set_cell

CASES = [
    # R117 — 레벨: 1부터 연속 · 1레벨 0 · 경험치는 늘고 인원은 줄지 않는다 · 인원 2 ~ 60
    {"name": "R117_level_gap", "rules": ["R117"], "table": "GuildLevel",
     "mutate": set_cell("GuildLevel", 2, "level", 4),
     "expect": {"rule": "R117", "where": "GuildLevel!5 level"}},
    {"name": "R117_level1_exp_nonzero", "rules": ["R117"], "table": "GuildLevel",
     "mutate": set_cell("GuildLevel", 0, "exp", 10),
     "expect": {"rule": "R117", "where": "GuildLevel!3 exp"}},
    {"name": "R117_exp_not_increasing", "rules": ["R117"], "table": "GuildLevel",
     "mutate": set_cell("GuildLevel", 3, "exp", 6000),
     "expect": {"rule": "R117", "where": "GuildLevel!6 exp", "what": "커야 한다"}},
    {"name": "R117_members_shrink", "rules": ["R117"], "table": "GuildLevel",
     "mutate": set_cell("GuildLevel", 4, "max_members", 31),
     "expect": {"rule": "R117", "where": "GuildLevel!7 max_members", "what": "줄었다"}},
    {"name": "R117_members_over_cap", "rules": ["R117"], "table": "GuildLevel",
     "mutate": set_cell("GuildLevel", 9, "max_members", 61),
     "expect": {"rule": "R117", "where": "GuildLevel!12 max_members"}},
    # R118 — 한 주의 임무 수 1 ~ 3, 1레벨 결사가 뽑을 임무가 그만큼
    {"name": "R118_slots_4", "rules": ["R118"], "table": "GuildConst",
     "mutate": set_cell("GuildConst", 0, "mission_slots", 4),
     "expect": {"rule": "R118", "where": "GuildConst!3 mission_slots"}},
    {"name": "R118_not_enough_level1", "rules": ["R118"], "table": "GuildMission",
     "mutate": custom("1레벨 임무를 모두 레벨 2로", lambda d: [m.__setitem__("min_level", 2) for m in d["GuildMission"]]),
     "expect": {"rule": "R118", "where": "GuildConst!3 mission_slots", "what": "1레벨"}},
    # R119 — 칭호 id 1 ~ 31 · 참조
    {"name": "R119_title_id_32", "rules": ["R119"], "table": "GuildTitle",
     "mutate": set_cell("GuildTitle", 10, "id", 32),
     "expect": {"rule": "R119", "where": "GuildTitle!13 id"}},
    {"name": "R119_level_title_missing", "rules": ["R119"], "table": "GuildLevel",
     "mutate": set_cell("GuildLevel", 2, "title_id", 30),
     "expect": {"rule": "R119", "where": "GuildLevel!5 title_id", "what": "없는 칭호"}},
    # R120 — 출석 단계: 1부터 연속 · 필요한 수가 는다
    {"name": "R120_attend_not_increasing", "rules": ["R120"], "table": "GuildAttend",
     "mutate": set_cell("GuildAttend", 1, "members", 3),
     "expect": {"rule": "R120", "where": "GuildAttend!4 members"}},
    {"name": "R120_attend_tier_gap", "rules": ["R120"], "table": "GuildAttend",
     "mutate": set_cell("GuildAttend", 2, "tier", 4),
     "expect": {"rule": "R120", "where": "GuildAttend!5 tier"}},
    # R121 — 임무 조건
    {"name": "R121_collect_unknown_item", "rules": ["R121"], "table": "GuildMission",
     "mutate": set_cell("GuildMission", 5, "sub", 999),
     "expect": {"rule": "R121", "where": "GuildMission!8 sub", "what": "없는 아이템"}},
    {"name": "R121_gear_rarity_6", "rules": ["R121"], "table": "GuildMission",
     "mutate": set_cell("GuildMission", 7, "sub", 6),
     "expect": {"rule": "R121", "where": "GuildMission!10 sub"}},
    {"name": "R121_active_zero_minutes", "rules": ["R121"], "table": "GuildMission",
     "mutate": set_cell("GuildMission", 9, "sub", 0),
     "expect": {"rule": "R121", "where": "GuildMission!12 sub"}},
    # R122 — 상한
    {"name": "R122_apply_max_9", "rules": ["R122"], "table": "GuildConst",
     "mutate": set_cell("GuildConst", 0, "apply_max", 9),
     "expect": {"rule": "R122", "where": "GuildConst!3 apply_max"}},
    {"name": "R122_log_keep_0", "rules": ["R122"], "table": "GuildConst",
     "mutate": set_cell("GuildConst", 0, "log_keep", 0),
     "expect": {"rule": "R122", "where": "GuildConst!3 log_keep"}},
    {"name": "R122_py_apply_max_9", "rules": ["R122"], "table": "GuildConst", "engine": "py",
     "mutate": set_cell("GuildConst", 0, "apply_max", 9),
     "expect": {"where": "GuildConst#1 apply_max", "what": "최대 8"}},
    # R123 — 상점 · 업적
    {"name": "R123_shop_unknown_item", "rules": ["R123"], "table": "GuildShop",
     "mutate": set_cell("GuildShop", 0, "item_id", 999),
     "expect": {"rule": "R123", "where": "GuildShop!3 item_id"}},
    {"name": "R123_shop_level_unreachable", "rules": ["R123"], "table": "GuildShop",
     "mutate": set_cell("GuildShop", 0, "min_level", 15),
     "expect": {"rule": "R123", "where": "GuildShop!3 min_level"}},
    {"name": "R123_level_ach_too_high", "rules": ["R123"], "table": "GuildAchievement",
     "mutate": set_cell("GuildAchievement", 12, "target", 11),
     "expect": {"rule": "R123", "where": "GuildAchievement!15 target"}},
    # 구조(규칙 번호 없음)
    {"name": "struct_guild_const_two_rows", "rules": [], "table": "GuildConst",
     "mutate": custom("GuildConst에 행을 하나 더", lambda d: d["GuildConst"].append(dict(d["GuildConst"][0]))),
     "expect": {"where": "GuildConst", "what": "한 행"}},
]
