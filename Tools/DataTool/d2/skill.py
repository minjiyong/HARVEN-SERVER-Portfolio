"""D2 — 스킬 묶음(R001 · R003~R010 · R103) + 구조 검사(자식 탭 SkillHit · 키 중복 · SkillKeyMap 칸 · Combo 순서).

GameData 행 순서 = 정본(key 오름차순). _src.json의 시트 행 = i + 3. SkillAction 행 i = 기술 id i(3 = Hold, 5 = Toggle).
R005(SkillTable::Detail::Validate 묶음 호출) · R010(SkillDefTable 묶음 호출)은 그 안의 규칙 사례가 행 위치와 함께 걸리는 것으로 확인한다.
"""
from .common import custom, set_cell


def _append_hits(skill_id: int, upto: int):
    def f(data):
        rows = data["SkillHit"]
        mine = [h for h in rows if h["skill_id"] == skill_id]
        last = dict(mine[-1])
        for seq in range(len(mine) + 1, upto + 1):
            h = dict(last)
            h["seq"] = seq
            rows.append(h)
    return custom(f"SkillHit에 기술 {skill_id}의 타격을 {upto}개까지 덧붙인다", f)


CASES = [
    # R001 — Status: 행 수 = StatusId::Count, 행 순서 = id
    {"name": "R001_status_id_not_row_order", "rules": ["R001"], "table": "Status",
     "mutate": set_cell("Status", 1, "id", "Freeze"),
     "expect": {"rule": "R001", "where": "Status!4 id"}},
    {"name": "R001_status_row_missing", "rules": ["R001"], "table": "Status",
     "mutate": custom("Status 마지막 행(Swift)을 뺀다", lambda d: d["Status"].pop()),
     "expect": {"rule": "R001", "where": "Status!20 id", "what": "StatusId::Count"}},
    # R003 — Skill id = 행 순서(0부터 연속). R005 = 과도기 부팅 검증(Detail::Validate) → 행 위치 규칙
    {"name": "R003_skill_id_not_row_order", "rules": ["R003", "R005"], "table": "Skill",
     "mutate": set_cell("Skill", 5, "id", 99),
     "expect": {"rule": "R003", "where": "Skill!8 id"}},
    # R004 — Class의 skill_0~5 ↔ Skill(직업 · slot)
    {"name": "R004_skill_slot_not_class_column", "rules": ["R004", "R005"], "table": "Skill",
     "mutate": set_cell("Skill", 3, "slot", 4),                 # 회전 베기(Knight skill_2)
     "expect": {"rule": "R004", "where": "Skill!6 slot"}},
    {"name": "R004_skill_cls_not_class", "rules": ["R004", "R005"], "table": "Skill",
     "mutate": set_cell("Skill", 7, "cls", "Knight"),           # 각인탄(Engraver skill_0)
     "expect": {"rule": "R004", "where": "Skill!10 cls"}},
    # R006 — SkillAction id마다 Skill 행. R010 = SkillDefTable 묶음 호출
    {"name": "R006_action_without_skill", "rules": ["R006", "R010"], "table": "SkillAction",
     "mutate": set_cell("SkillAction", 36, "id", 40),           # 밤 장막(타격 없음) → Skill에 없는 40
     "expect": {"rule": "R006", "where": "SkillAction!39 id"}},
    # R007 — SkillKeyMap(직업, 칸) = SkillAction id
    {"name": "R007_action_key_not_keymap", "rules": ["R007", "R010"], "table": "SkillAction",
     "mutate": set_cell("SkillAction", 2, "key", "W"),          # 방패 강타(Knight Q)
     "expect": {"rule": "R007", "where": "SkillAction!5 key"}},
    # R008 — 부채꼴 타격은 b > 0
    {"name": "R008_cone_hit_b_zero", "rules": ["R008", "R010"], "table": "SkillHit",
     "mutate": set_cell("SkillHit", 0, "b", 0),
     "expect": {"rule": "R008", "where": "SkillHit!3 b"}},
    # R103 — 누르고 있기(Hold) · 켜 두기(Toggle): tick_ms ≥ 50, hold_max_ms > 0
    {"name": "R103_hold_tick_zero", "rules": ["R103"], "table": "SkillAction",
     "mutate": set_cell("SkillAction", 3, "tick_ms", 0),
     "expect": {"rule": "R103", "where": "SkillAction!6 tick_ms", "what": "Hold"}},
    {"name": "R103_toggle_hold_max_zero", "rules": ["R103"], "table": "SkillAction",
     "mutate": set_cell("SkillAction", 5, "hold_max_ms", 0),
     "expect": {"rule": "R103", "where": "SkillAction!8 hold_max_ms", "what": "Toggle"}},
    # R009 — 세 직업 × 12칸이 모두 SkillAction에 있다
    {"name": "R009_keymap_skill_not_in_action", "rules": ["R009", "R010"], "table": "SkillKeyMap",
     "mutate": set_cell("SkillKeyMap", 0, "skill_id", 37),
     "expect": {"rule": "R009", "where": "SkillKeyMap!3 skill_id"}},
    # 구조(규칙 번호 없음 — 로더가 위치와 함께 거부)
    {"name": "struct_skill_hits_over_8", "rules": [], "table": "SkillHit",
     "mutate": _append_hits(19, 9),
     "expect": {"where": "SkillHit", "what": "8개를 넘는다"}},
    {"name": "struct_skill_hit_seq_gap", "rules": [], "table": "SkillHit",
     "mutate": set_cell("SkillHit", 1, "seq", 3),
     "expect": {"where": "SkillHit!4 seq", "what": "1부터 연속"}},
    {"name": "struct_skill_hit_unknown_skill", "rules": [], "table": "SkillHit",
     "mutate": set_cell("SkillHit", 0, "skill_id", 300),
     "expect": {"where": "SkillHit!3 skill_id", "what": "SkillAction에 없는"}},
    {"name": "struct_skill_action_dup_id", "rules": [], "table": "SkillAction",
     "mutate": set_cell("SkillAction", 1, "id", 2),
     "expect": {"where": "SkillAction!5 id", "what": "겹친다"}},
    {"name": "struct_keymap_dup_cell", "rules": [], "table": "SkillKeyMap",
     "mutate": set_cell("SkillKeyMap", 1, "key", "C"),
     "expect": {"where": "SkillKeyMap!4 key", "what": "두 번"}},
    {"name": "struct_keymap_missing_cell", "rules": [], "table": "SkillKeyMap",
     "mutate": set_cell("SkillKeyMap", 1, "key", "C"),
     "expect": {"where": "SkillKeyMap", "what": "(Knight, Q) 행이 없다"}},
    {"name": "struct_combo_id_not_row_order", "rules": [], "table": "Combo",
     "mutate": set_cell("Combo", 3, "id", 9),
     "expect": {"where": "Combo!6 id", "what": "0부터 연속"}},
    {"name": "type_hit_flag_unknown", "rules": [], "table": "SkillHit",
     "mutate": set_cell("SkillHit", 0, "flags", "HF_Heavy|HF_Nope"),
     "expect": {"where": "SkillHit!3 flags", "what": "HitFlag"}},
]
