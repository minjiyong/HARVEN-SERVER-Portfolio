"""D2 — NPC대사 묶음(R066~R071) + NpcLine 구조 검사(talk ≤ kMaxTalk · bark ≤ kMaxBarks · seq 연속).

GameData/Npc.json 행 순서 = id 순서(행 i = id i+1, 시트 행 = i + 3). NpcLine은 (npc_id, talk → bark, seq) 순.
NPC 집 자리(x, y)는 마을 원본 TownNpcHome — W4 트리에서는 아직 constexpr TownMap::kNpcHomes라 GameData로 바꿀 수 없다.
그래서 R066은 "집이 없는 id로 바꾼 NPC"(자리 -1, -1 → 마을 밖)로 시험하고, TownNpcHome을 바꾸는 사례는 마을(W5)이 GameData로 온 뒤에 돈다.
"""
from .common import custom, set_cell


def _drop_npc(npc_id: int):
    def f(data):
        data["Npc"] = [r for r in data["Npc"] if r["id"] != npc_id]
    return custom(f"Npc에서 id {npc_id} 행을 지운다", f)


def _more_npcs(total: int):
    def f(data):
        rows = data["Npc"]
        base = dict(rows[0])
        nid = max(r["id"] for r in rows)
        while len(rows) < total:
            nid += 1
            r = dict(base)
            r["id"] = nid
            r["code_name"] = None
            rows.append(r)
    return custom(f"Npc를 {total}행으로 늘린다(새 id)", f)


CASES = [
    # R066: 집(TownNpcHome)은 마을 안 걸을 수 있는 칸 — 집이 없는 id(40)로 바꾸면 자리가 -1, -1(마을 밖)
    {"name": "R066_npc_without_home", "rules": ["R066", "R071"], "table": "Npc",
     "mutate": set_cell("Npc", 32, "id", 40),
     "expect": {"rule": "R066", "where": "Npc!35 id", "what": "마을 원본에 없다"}},
    # R066: 마을 원본의 집을 마을 밖으로(마을 W5가 GameData/TownNpcHome.json으로 온 뒤에 돈다 — 그 전에는 표가 없어 건너뛴다)
    {"name": "R066_home_outside_town", "rules": ["R066"], "table": "TownNpcHome",
     "mutate": set_cell("TownNpcHome", 0, "x", 0),
     "expect": {"rule": "R066", "where": "Npc!3 id"}},
    {"name": "R067_id_zero", "rules": ["R067"], "table": "Npc",
     "mutate": set_cell("Npc", 0, "id", 0),
     "expect": {"rule": "R067", "where": "Npc!3 id"}},
    {"name": "R067_id_64", "rules": ["R067"], "table": "Npc",
     "mutate": set_cell("Npc", 32, "id", 64),
     "expect": {"rule": "R067", "where": "Npc!35 id"}},
    {"name": "R068_id_duplicate", "rules": ["R068", "R071"], "table": "Npc",
     "mutate": set_cell("Npc", 1, "id", 1),
     "expect": {"rule": "R068", "where": "Npc!4 id", "what": "겹친다"}},
    # R069: 마을 원본에 집이 있는 NPC가 Npc 탭에서 빠지면(오타 · 삭제) 잡는다 — 위치는 마을 표 TownNpcHome의 행
    {"name": "R069_home_npc_missing", "rules": ["R069", "R071"], "table": "Npc",
     "mutate": _drop_npc(33),
     "expect": {"rule": "R069", "where": "TownNpcHome", "what": "NPC 33"}},
    {"name": "R070_more_than_48", "rules": ["R070", "R071"], "table": "Npc",
     "mutate": _more_npcs(49),
     "expect": {"rule": "R070", "where": "Npc.json#49 id", "what": "48"}},
    # 구조 상수 · 형 검사(규칙 번호 없음 — 로더가 위치와 함께 거부)
    {"name": "npcline_bark_over_max", "rules": [], "table": "NpcLine",
     "mutate": set_cell("NpcLine", 5, "seq", 5),
     "expect": {"where": "NpcLine!8 seq", "what": "kMaxBarks"}},
    {"name": "npcline_talk_over_max", "rules": [], "table": "NpcLine",
     "mutate": set_cell("NpcLine", 2, "seq", 6),
     "expect": {"where": "NpcLine!5 seq", "what": "kMaxTalk"}},
    {"name": "npcline_kind_unknown", "rules": [], "table": "NpcLine",
     "mutate": set_cell("NpcLine", 0, "kind", "shout"),
     "expect": {"where": "NpcLine!3 kind", "what": "talk"}},
    {"name": "npcline_seq_duplicate", "rules": [], "table": "NpcLine",
     "mutate": set_cell("NpcLine", 1, "seq", 1),
     "expect": {"where": "NpcLine!4 seq", "what": "겹친다"}},
    {"name": "npcline_seq_gap", "rules": [], "table": "NpcLine",
     "mutate": set_cell("NpcLine", 1, "seq", 4),
     "expect": {"where": "NpcLine npc_id 1", "what": "연속"}},
    {"name": "npc_kind_unknown", "rules": [], "table": "Npc",
     "mutate": set_cell("Npc", 3, "kind", "eDragon"),
     "expect": {"where": "Npc!6 kind", "what": "NpcKind"}},
    {"name": "npc_color_lowercase", "rules": [], "table": "Npc",
     "mutate": set_cell("Npc", 9, "color", "#3a7ad8"),
     "expect": {"where": "Npc!12 color", "what": "#RRGGBB"}},
]
