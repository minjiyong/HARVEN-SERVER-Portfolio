"""스키마(Tools/DataTool/schema/*.yaml) 읽기 · 정본 스키마(로더 메모리 덤프의 _schema.json = Canon_*.h)와 대조."""
from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path

import yaml

from .paths import SCHEMA_DIR

COL_ATTRS = {"key", "type", "desc", "ref", "ref_zero", "max_bytes", "min", "max", "default", "nonempty", "pattern", "values", "ascii"}
TAB_ATTRS = {"source", "desc", "key", "columns", "id_lock", "code_names", "rows_exact", "child_of"}
BASE_TYPES = {"u8", "u16", "u32", "u64", "i8", "i16", "i32", "bool", "str", "str?", "color", "f32"}
# u64: 시트의 수는 배정밀도라 2^53 - 1까지만 정확하다(강화 성공 몫 — 1조 = 100%)
INT_RANGE = {"u8": (0, 255), "u16": (0, 65535), "u32": (0, 4294967295), "u64": (0, 2**53 - 1), "i8": (-128, 127), "i16": (-32768, 32767), "i32": (-2147483648, 2147483647)}


@dataclass
class Column:
    key: str
    type: str
    desc: str = ""
    attrs: dict = field(default_factory=dict)

    @property
    def base(self) -> str:
        return self.type.split(":")[0] if ":" in self.type else self.type

    @property
    def enum(self) -> str | None:
        return self.type.split(":", 1)[1] if ":" in self.type else None

    def has_default(self) -> bool:
        return "default" in self.attrs


@dataclass
class Tab:
    name: str
    bundle: str
    source: str
    desc: str
    key: list[str]
    columns: list[Column]
    attrs: dict

    def col(self, key: str) -> Column:
        for c in self.columns:
            if c.key == key:
                return c
        raise KeyError(f"{self.name}.{key}")


def load(schema_dir: Path = SCHEMA_DIR) -> dict[str, Tab]:
    tabs: dict[str, Tab] = {}
    for f in sorted(schema_dir.glob("*.yaml")):
        doc = yaml.safe_load(f.read_text(encoding="utf-8"))
        bundle = doc["bundle"]
        for name, t in doc["tabs"].items():
            bad = set(t) - TAB_ATTRS
            if bad:
                raise SystemExit(f"[schema] {f.name} {name}: 모르는 탭 속성 {sorted(bad)}")
            cols = []
            for c in t["columns"]:
                badc = set(c) - COL_ATTRS
                if badc:
                    raise SystemExit(f"[schema] {f.name} {name}.{c.get('key')}: 모르는 열 속성 {sorted(badc)}")
                attrs = {k: v for k, v in c.items() if k not in ("key", "type", "desc")}
                cols.append(Column(c["key"], str(c["type"]), c.get("desc", ""), attrs))
            if name in tabs:
                raise SystemExit(f"[schema] 탭 {name}이 두 번 정의됐다")
            tabs[name] = Tab(name, bundle, t.get("source", "sheet"), t.get("desc", ""), list(t.get("key") or []), cols,
                             {k: v for k, v in t.items() if k not in ("source", "desc", "key", "columns")})
    return tabs


def check_against_canon(tabs: dict[str, Tab], canon_schema_path: Path | None = None) -> list[str]:
    """YAML 열(이름 · 순서 · 형)이 정본(서버 · datacheck가 쓰는 Canon_*.h)과 같은지. 정본 스키마는 datacheck 메모리 덤프에서."""
    if canon_schema_path is None:
        from . import checker
        canon = checker.canon_schema()
    else:
        canon = json.loads(canon_schema_path.read_text(encoding="utf-8"))
    errs = []
    for name in sorted(set(canon) | set(tabs)):
        if name not in tabs:
            errs.append(f"{name}: 정본에는 있는데 스키마 YAML에 없다")
            continue
        if name not in canon:
            errs.append(f"{name}: 스키마 YAML에는 있는데 정본에 없다")
            continue
        want = [tuple(x) for x in canon[name]]
        got = [(c.key, c.type) for c in tabs[name].columns]
        if want != got:
            for i in range(max(len(want), len(got))):
                w = want[i] if i < len(want) else None
                g = got[i] if i < len(got) else None
                if w != g:
                    errs.append(f"{name}: {i + 1}번째 열 — 정본 {w}, YAML {g}")
                    break
        for k in tabs[name].key:
            if k not in [c.key for c in tabs[name].columns]:
                errs.append(f"{name}: key 열 {k}가 없다")
        for c in tabs[name].columns:
            if c.base not in BASE_TYPES and c.base not in ("enum", "flags", "mask"):
                errs.append(f"{name}.{c.key}: 모르는 형 {c.type}")
            ref = c.attrs.get("ref")
            if ref and ref not in tabs:
                errs.append(f"{name}.{c.key}: ref {ref} 탭이 없다")
    return errs
