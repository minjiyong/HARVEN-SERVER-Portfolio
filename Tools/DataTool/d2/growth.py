"""D2 — 기술성장 묶음(R011~R022) + 구조 검사(갈래 id 참조 · GrowthConst 한 행 · 갈래 id 중복).

GameData 행 순서 = 정본(key 오름차순). _src.json의 시트 행 = i + 3.
R021(SkillGrowth::Detail::Validate 묶음 호출)은 그 안의 규칙 사례가 행 위치와 함께 걸리는 것으로 확인한다.
R017 · R020 · R022는 구조로 보장된다 — engine "code"로 그 보장(코드)이 있는지 시험한다.
"""
from .common import custom, set_cell

CASES = [
    # R011 — skill_id < kIdSpan(40)
    {"name": "R011_tree_skill_id_over_span", "rules": ["R011", "R021"], "table": "GrowthTree",
     "mutate": set_cell("GrowthTree", 23, "skill_id", 40),
     "expect": {"rule": "R011", "where": "GrowthTree!26 skill_id"}},
    # R012 — 기술 표(SkillAction · Skill)에 있다
    {"name": "R012_tree_skill_not_in_tables", "rules": ["R012", "R021"], "table": "GrowthTree",
     "mutate": set_cell("GrowthTree", 23, "skill_id", 37),
     "expect": {"rule": "R012", "where": "GrowthTree!26 skill_id"}},
    # R013 — 칸이 C · V · Space · Z가 아니다
    {"name": "R013_tree_on_basic_attack", "rules": ["R013", "R021"], "table": "GrowthTree",
     "mutate": set_cell("GrowthTree", 0, "skill_id", 1),          # 검격(Knight C)
     "expect": {"rule": "R013", "where": "GrowthTree!3 skill_id"}},
    # R014 — 쓰인 갈래: mod ≠ None · 이름 · 설명
    {"name": "R014_variant_mod_none", "rules": ["R014", "R021"], "table": "GrowthVariant",
     "mutate": set_cell("GrowthVariant", 0, "mod", "None"),
     "expect": {"rule": "R014", "where": "GrowthVariant!3 mod"}},
    {"name": "R014_variant_name_empty", "rules": ["R014", "R021"], "table": "GrowthVariant",
     "mutate": set_cell("GrowthVariant", 1, "name", ""),
     "expect": {"rule": "R014", "where": "GrowthVariant!4 name"}},
    {"name": "R014_variant_desc_empty", "rules": ["R014", "R021"], "table": "GrowthVariant",
     "mutate": set_cell("GrowthVariant", 2, "desc", ""),
     "expect": {"rule": "R014", "where": "GrowthVariant!5 desc"}},
    # R015 — Element면 상태이상
    {"name": "R015_element_without_status", "rules": ["R015", "R021"], "table": "GrowthVariant",
     "mutate": set_cell("GrowthVariant", 6, "status", "None"),
     "expect": {"rule": "R015", "where": "GrowthVariant!9 status"}},
    # R016 — 같은 단의 두 갈래는 다르다
    {"name": "R016_same_tier_same_mod", "rules": ["R016", "R021"], "table": "GrowthTree",
     "mutate": set_cell("GrowthTree", 0, "a2", 1),
     "expect": {"rule": "R016", "where": "GrowthTree!3 a2"}},
    # R017 — 모든 조합에서 ApplyDef 뒤 타격 ≤ 8: ApplyDef가 8칸을 넘기지 않게 막는다(구조) — 막는 코드가 있는지
    {"name": "R017_applydef_echo_guard", "rules": ["R017"], "engine": "code", "glob": "SkillGrowth.h",
     "pattern": r"d\.special != SkillSpecial::Legacy && d\.hitCount > 0 && d\.hitCount < 8\)"},
    {"name": "R017_applydef_twin_guard", "rules": ["R017"], "engine": "code", "glob": "SkillGrowth.h",
     "pattern": r"h\.shape == HitShape::Line && d\.hitCount \+ 2 <= 8\)"},
    # R018 — skill_id 중복 없음
    {"name": "R018_tree_dup_skill", "rules": ["R018", "R021"], "table": "GrowthTree",
     "mutate": set_cell("GrowthTree", 1, "skill_id", 2),
     "expect": {"rule": "R018", "where": "GrowthTree!4 skill_id"}},
    # R019 — 세 직업 × QWERASDF 기본 기술이 모두 갈래를 가진다(위치 = 그 칸의 SkillKeyMap 행)
    {"name": "R019_keymap_skill_without_tree", "rules": ["R019", "R021"], "table": "GrowthTree",
     "mutate": custom("GrowthTree 마지막 행(달 가르기 35)을 뺀다", lambda d: d["GrowthTree"].pop()),
     "expect": {"rule": "R019", "where": "SkillKeyMap!35 skill_id"}},
    # R020 · R022 — 코드에 남는 규칙(static_assert)
    {"name": "R020_costto_static_assert", "rules": ["R020"], "engine": "code", "glob": "SkillGrowth.h",
     "pattern": r"static_assert\(CostTo\(kMaxLevel\) == 18"},
    {"name": "R022_state_size_static_assert", "rules": ["R022"], "engine": "code", "glob": "SkillGrowth.h",
     "pattern": r"static_assert\(sizeof\(State\) == 3"},
    # 구조(규칙 번호 없음)
    {"name": "struct_tree_unknown_variant", "rules": [], "table": "GrowthTree",
     "mutate": set_cell("GrowthTree", 0, "a1", 999),
     "expect": {"where": "GrowthTree!3 a1", "what": "없는 갈래"}},
    {"name": "struct_variant_dup_id", "rules": [], "table": "GrowthVariant",
     "mutate": set_cell("GrowthVariant", 1, "id", 1),
     "expect": {"where": "GrowthVariant!4 id", "what": "겹친다"}},
    {"name": "struct_growth_const_two_rows", "rules": [], "table": "GrowthConst",
     "mutate": custom("GrowthConst에 행을 하나 더", lambda d: d["GrowthConst"].append(dict(d["GrowthConst"][0]))),
     "expect": {"where": "GrowthConst", "what": "한 행"}},
]
