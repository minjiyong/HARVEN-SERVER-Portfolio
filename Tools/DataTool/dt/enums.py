"""C++ 열거형 → 이름 표. 손으로 쓰지 않는다 — 헤더를 읽어 만든다(SHEET_DATA_PROMPT [8]).

ENUMS의 (C++ 이름, 헤더, 시트 이름)마다 헤더에서 `enum [class] 짧은이름 : 형 { … }`를 찾아
열거자 이름과 값을 읽는다. 값 식은 정수 · `1 << n` · 앞 열거자 + 1만 허용한다.
생성물:
  Server/HARVEN_GameServer/Database/GameData/Gen/EnumNames.h — C++ 이름 표 + 열거자마다 static_assert(값이 컴파일러와 같은지)
  (dict)                           — datatool이 검증 · _meta/_Enums · 드롭다운 목록에 쓴다
"""
from __future__ import annotations

import re
from pathlib import Path

# (C++ 정규 이름, Database/GameData 기준 헤더, 짧은 이름 = 시트 · 스키마에서 부르는 이름, 비트 플래그인가)
ENUMS: list[tuple[str, str, str, str]] = [
    # 이름, 헤더, 시트 이름, 종류(enum | flags)
    ("ClassId", "CombatData.h", "ClassId", "enum"),
    ("StatusId", "CombatData.h", "StatusId", "enum"),
    ("SkillTarget", "CombatData.h", "SkillTarget", "enum"),
    ("SkillShape", "CombatData.h", "SkillShape", "enum"),
    ("DamageKind", "CombatData.h", "DamageKind", "enum"),
    ("SkillKey", "SkillData.h", "SkillKey", "enum"),
    ("SkillKind", "SkillData.h", "SkillKind", "enum"),
    ("HitShape", "SkillData.h", "HitShape", "enum"),
    ("HitAnchor", "SkillData.h", "HitAnchor", "enum"),
    ("HitReact", "SkillData.h", "HitReact", "enum"),
    ("HitFlag", "SkillData.h", "HitFlag", "flags"),
    ("SkillSpecial", "SkillData.h", "SkillSpecial", "enum"),
    ("SkillGrowth::Mod", "SkillGrowth.h", "GrowthMod", "enum"),
    ("MonsterArchetype", "MonsterTypes.h", "MonsterArchetype", "enum"),
    ("MonsterVariant", "MonsterTypes.h", "MonsterVariant", "enum"),
    ("MonsterPattern", "MonsterTypes.h", "MonsterPattern", "enum"),
    ("MonsterAiType", "MonsterAITypes.h", "MonsterAiType", "enum"),
    ("MonsterSkillTarget", "MonsterAITypes.h", "MonsterSkillTarget", "enum"),
    ("MonsterMove", "MonsterAITypes.h", "MonsterMove", "enum"),
    ("AoeAffect", "MonsterAITypes.h", "AoeAffect", "enum"),
    ("MonsterFx", "MonsterAITypes.h", "MonsterFx", "enum"),
    ("ItemType", "Item/ItemTableRow.h", "ItemType", "enum"),
    ("ItemEffect", "Item/ItemTableRow.h", "ItemEffect", "enum"),
    ("ItemGrade", "Item/ItemTableRow.h", "ItemGrade", "enum"),
    ("EquipSlot", "Item/ItemTableRow.h", "EquipSlot", "enum"),
    ("WeaponKind", "Item/ItemTableRow.h", "WeaponKind", "enum"),
    ("ItemRoute", "Item/ItemTableRow.h", "ItemRoute", "flags"),
    ("ItemBind", "Item/ItemTableRow.h", "ItemBind", "enum"),
    ("CraftPart", "Item/ItemTableRow.h", "CraftPart", "enum"),
    ("Rarity", "ItemRules.h", "Rarity", "enum"),
    ("ItemOption", "ItemRules.h", "ItemOption", "enum"),
    ("ItemProc", "ItemRules.h", "ItemProc", "enum"),
    ("WorldData::Biome", "WorldData.h", "Biome", "enum"),
    ("WorldData::NpcKind", "WorldData.h", "NpcKind", "enum"),
    ("QuestData::QuestKind", "QuestData.h", "QuestKind", "enum"),
    ("QuestData::ObjKind", "QuestData.h", "ObjKind", "enum"),
    ("QuestData::QuestIcon", "QuestData.h", "QuestIcon", "enum"),
    ("TownMap::BuildingKind", "TownTypes.h", "BuildingKind", "enum"),
    ("TownMap::AmbientKind", "TownTypes.h", "AmbientKind", "enum"),
    ("TownMap::PropKind", "TownTypes.h", "PropKind", "enum"),
    ("GuildMissionKind", "GuildGrowth.h", "GuildMissionKind", "enum"),
    ("GuildAchKind", "GuildGrowth.h", "GuildAchKind", "enum"),
    ("GuildPerm", "GuildGrowth.h", "GuildPerm", "flags"),
    ("AchCategory", "AchieveRules.h", "AchCategory", "enum"),
    ("AchKind", "AchieveRules.h", "AchKind", "enum"),
]


def _strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def _eval(expr: str, prev: int | None, known: dict[str, int]) -> int:
    expr = expr.strip()
    m = re.fullmatch(r"(\d+)\s*<<\s*(\d+)", expr)
    if m:
        return int(m.group(1)) << int(m.group(2))
    if re.fullmatch(r"0[xX][0-9a-fA-F]+|\d+", expr):
        return int(expr, 0)
    if expr in known:
        return known[expr]
    raise ValueError(f"열거형 값 식을 읽을 수 없다: {expr!r}")


def parse_enum(header_text: str, short: str) -> tuple[str, list[tuple[str, int]]]:
    """(밑바탕 형, [(이름, 값)]) — Count는 포함하지 않는다(따로 검사)."""
    text = _strip_comments(header_text)
    m = re.search(r"\benum\s+(?:class\s+)?" + re.escape(short) + r"\s*:\s*(\w+)\s*\{(.*?)\}", text, flags=re.S)
    if not m:
        raise ValueError(f"enum {short}를 찾지 못했다")
    underlying, body = m.group(1), m.group(2)
    items: list[tuple[str, int]] = []
    known: dict[str, int] = {}
    prev: int | None = None
    for part in body.split(","):
        part = part.strip()
        if not part:
            continue
        if "=" in part:
            name, expr = (s.strip() for s in part.split("=", 1))
            value = _eval(expr, prev, known)
        else:
            name, value = part, (0 if prev is None else prev + 1)
        if not re.fullmatch(r"[A-Za-z_]\w*", name):
            raise ValueError(f"enum {short}: 열거자 이름 {name!r}")
        known[name] = value
        prev = value
        items.append((name, value))
    return underlying, items


def load_all(data_dir: Path) -> list[dict]:
    """data_dir = Server/HARVEN_GameServer/Database/GameData → [{cpp, sheet, kind, underlying, items:[(name,value)], has_count, count}]"""
    out = []
    for cpp, header, sheet, kind in ENUMS:
        short = cpp.split("::")[-1]
        # 적힌 헤더 → GameData 아래 모든 헤더 순으로 찾는다(헤더를 옮겨도 열거형 이름으로 찾는다)
        candidates = [data_dir / header] + sorted(data_dir.rglob("*.h"))
        found = None
        for path in candidates:
            if not path.exists():
                continue
            try:
                found = parse_enum(path.read_text(encoding="utf-8-sig"), short)
                header = path.relative_to(data_dir).as_posix()
                break
            except ValueError:
                continue
        if found is None:
            raise ValueError(f"enum {short}를 찾지 못했다")
        underlying, items = found
        has_count = bool(items) and items[-1][0] == "Count"
        count = items[-1][1] if has_count else None
        if has_count:
            items = items[:-1]
        out.append({"cpp": cpp, "sheet": sheet, "kind": kind, "header": header, "underlying": underlying,
                    "items": items, "has_count": has_count, "count": count})
    return out


def render_cpp(enums: list[dict]) -> str:
    lines = [
        "#pragma once",
        "// 생성물 — `python3 Tools/DataTool/datatool.py gen` (dt/enums.py). 손으로 고치지 않는다.",
        "// C++ 열거형 ↔ 이름 글자. 시트 · GameData JSON은 이름으로 적는다(SHEET_DATA_PROMPT [5]).",
        "// 열거자마다 static_assert로 값이 헤더와 같은지 확인한다 — 헤더가 바뀌면 다시 생성해야 빌드된다.",
        "",
        "#include <cstddef>",
        "#include <cstdint>",
        "#include <string_view>",
        "",
        '#include "Database/GameData/CombatData.h"',
        '#include "Database/GameData/SkillData.h"',
        '#include "Database/GameData/SkillGrowth.h"',
        '#include "Database/GameData/MonsterTable.h"',
        '#include "Database/GameData/MonsterAITypes.h"',
        '#include "Database/GameData/ItemRules.h"',
        '#include "Database/GameData/WorldData.h"',
        '#include "Database/GameData/QuestData.h"',
        '#include "Database/GameData/TownTypes.h"',
        '#include "Database/GameData/Item/ItemTableRow.h"',
        '#include "Database/GameData/GuildGrowth.h"',
        '#include "Database/GameData/AchieveRules.h"',
        "",
        "namespace GameDataEnums",
        "{",
        "\tstruct Item { std::string_view name; int value; };",
        "",
        "\ttemplate <class E> struct Info;   // 정의는 아래 특수화만",
        "",
    ]
    for e in enums:
        cpp, items = e["cpp"], e["items"]
        lines.append(f"\ttemplate <> struct Info<{cpp}>")
        lines.append("\t{")
        lines.append(f'\t\tstatic constexpr std::string_view kName = "{e["sheet"]}";')
        lines.append(f"\t\tstatic constexpr bool kFlags = {'true' if e['kind'] == 'flags' else 'false'};")
        lines.append(f"\t\tstatic constexpr Item kItems[] = {{")
        for name, value in items:
            lines.append(f'\t\t\t{{ "{name}", {value} }},')
        lines.append("\t\t};")
        lines.append(f"\t\tstatic constexpr size_t kCount = {len(items)};")
        lines.append("\t};")
        for name, value in items:
            lines.append(f"\tstatic_assert(static_cast<int>({cpp}::{name}) == {value});")
        if e["has_count"]:
            lines.append(f"\tstatic_assert(static_cast<int>({cpp}::Count) == {e['count']}, \"{cpp}: 열거자 수가 바뀌었다 — gen을 다시\");")
        lines.append("")
    lines.append("}")
    lines.append("")
    return "\n".join(lines)


def as_json(enums: list[dict]) -> dict:
    return {e["sheet"]: {"cpp": e["cpp"], "kind": e["kind"], "items": [[n, v] for n, v in e["items"]]} for e in enums}
