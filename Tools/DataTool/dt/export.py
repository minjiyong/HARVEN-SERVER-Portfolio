"""export — 검증을 통과한 정본 행 → Bin/GameData/(표 JSON · _src.json · manifest.json · ids.lock) + Server/HARVEN_GameServer/Database/GameData/Gen/<탭>Ids.h.

같은 입력이면 같은 바이트(키 순서 · 들여쓰기 · 행 순서 고정 — 행은 스키마 key 오름차순).
mapgen이 주인인 Town* 표는 건드리지 않고 manifest에만 넣는다.
"""
from __future__ import annotations

import hashlib
import json
import os
import time
from pathlib import Path

from . import canon
from .paths import GAMEDATA, GEN_DIR
from .schema import Tab

FORMAT = 1


def sort_key(tab: Tab, enums: dict):
    order = {}
    for k in tab.key:
        c = tab.col(k)
        if c.base == "enum":
            order[k] = {n: v for n, v in enums[c.enum]["items"]}
        elif c.attrs.get("values"):
            order[k] = {v: i for i, v in enumerate(c.attrs["values"])}

    def kf(r):
        out = []
        for k in tab.key:
            v = r[k]
            out.append(order[k][v] if k in order else v)
        return tuple(out)
    return kf


def table_text(tab: Tab, rows: list[dict]) -> str:
    return canon.table_text(tab.name, [(c.key, c.type) for c in tab.columns], rows)


def manifest_text(gamedata: Path) -> str:
    tables = {}
    for f in sorted(gamedata.glob("*.json")):
        if f.name.startswith("_") or f.name == "manifest.json":
            continue
        body = f.read_bytes()
        rows = len(json.loads(body)["rows"])
        tables[f.stem] = {"rows": rows, "sha256": hashlib.sha256(body).hexdigest()}
    all_h = hashlib.sha256()
    for name in sorted(tables):   # nlohmann::json 객체 순회 순서(키 정렬)와 같게
        all_h.update((name + "\n" + tables[name]["sha256"] + "\n").encode())
    version = all_h.hexdigest()[:16]
    lines = ['{"format": %d, "version": %s, "tables": {' % (FORMAT, canon.json_string(version))]
    names = sorted(tables)
    for i, n in enumerate(names):
        t = tables[n]
        lines.append('%s: {"rows": %d, "sha256": %s}%s' % (canon.json_string(n), t["rows"], canon.json_string(t["sha256"]), "," if i + 1 < len(names) else ""))
    lines.append("}}")
    return "\n".join(lines) + "\n"


def lock_text(lock: dict) -> str:
    out = ["{"]
    names = sorted(lock)
    for i, n in enumerate(names):
        entries = ", ".join("[%d, %s]" % (e[0], canon.json_string(e[1] or "")) for e in lock[n])
        out.append("%s: [%s]%s" % (canon.json_string(n), entries, "," if i + 1 < len(names) else ""))
    out.append("}")
    return "\n".join(out) + "\n"


def gen_ids_header(tab: Tab, rows: list[dict]) -> tuple[str, str] | None:
    cn = tab.attrs.get("code_names")
    if not cn:
        return None
    idk = tab.key[0]
    name_key = next((k for k in ("name", "display_name") if k in [c.key for c in tab.columns]), None)
    lines = [
        "#pragma once",
        f"// 생성물 — datatool export(시트 {tab.name} 탭의 code_name 열). 손으로 고치지 않는다.",
        "// 코드가 이름으로 부르는 id만 여기 온다. id는 영구(Bin/GameData/ids.lock) — 이름을 바꾸면 이 헤더의 이름도 바뀐다.",
        "#include <cstdint>",
    ]
    if cn.get("include"):
        lines.append(f'#include "{cn["include"]}"')
    lines += ["", f"namespace {cn['ns']}", "{"]
    for r in rows:
        if r.get("code_name"):
            label = r.get(name_key, "") if name_key else ""
            epi = r.get("epithet")
            comment = f"   // {label}" + (f" — {epi}" if epi else "") if label else ""
            lines.append(f"\tinline constexpr {cn['type']} {r['code_name']} = {r[idk]};{comment}")
    lines += ["}", ""]
    return cn["header"], "\n".join(lines)


def write(tables: dict[str, tuple[Tab, list[dict], list[int] | None]], enums: dict, gamedata: Path = GAMEDATA,
          gen_dir: Path = GEN_DIR) -> list[str]:
    """tables: 탭 → (스키마, 행, 시트 행 번호|None). 바뀐 파일 목록을 돌려준다."""
    gamedata.mkdir(parents=True, exist_ok=True)
    gen_dir.mkdir(parents=True, exist_ok=True)
    changed = []

    def put(path: Path, text: str):
        old = path.read_text(encoding="utf-8") if path.exists() else None
        if old != text:
            # 임시 파일 → 바꿔 넣기: --dev 서버(감시 · 핫 리로드)가 반쯤 쓴 파일을 읽지 않게. manifest는 맨 마지막.
            tmp = path.with_name(path.name + ".tmp")
            tmp.write_text(text, encoding="utf-8", newline="\n")
            for attempt in range(20):
                try:
                    os.replace(tmp, path)
                    break
                except PermissionError:   # Windows — 서버가 그 순간 파일을 읽고 있으면 잠깐 기다린다
                    time.sleep(0.05)
            else:
                raise SystemExit(f"[export] {path} 바꿔 넣기 실패(다른 프로그램이 잡고 있다)")
            changed.append(str(path))

    src_path = gamedata / "_src.json"
    src = json.loads(src_path.read_text(encoding="utf-8")) if src_path.exists() else {}
    lock_path = gamedata / "ids.lock"
    lock = json.loads(lock_path.read_text(encoding="utf-8")) if lock_path.exists() else {}
    for name, (tab, rows, rows_src) in sorted(tables.items()):
        kf = sort_key(tab, enums)
        order = sorted(range(len(rows)), key=lambda i: kf(rows[i]))
        rows_sorted = [rows[i] for i in order]
        put(gamedata / f"{name}.json", table_text(tab, rows_sorted))
        if rows_src is not None:
            src[name] = [rows_src[i] for i in order]
        else:
            src.pop(name, None)
        lock = ids_lock_update_obj(lock, tab, rows_sorted)
        g = gen_ids_header(tab, rows_sorted)
        if g:
            put(gen_dir / g[0], g[1])
    put(src_path, "{\n" + ",\n".join(f"{canon.json_string(k)}: [{', '.join(str(x) for x in src[k])}]" for k in sorted(src)) + "\n}\n")
    put(lock_path, lock_text(lock))
    put(gamedata / "manifest.json", manifest_text(gamedata))
    return changed


def ids_lock_update_obj(lock: dict, tab: Tab, rows: list[dict]) -> dict:
    if not tab.attrs.get("id_lock"):
        return lock
    idk = tab.key[0]
    name_key = next((k for k in ("name", "display_name") if k in [c.key for c in tab.columns]), None)
    have = {x[0]: x for x in lock.get(tab.name, [])}
    for r in rows:
        if r[idk] not in have:
            have[r[idk]] = [r[idk], (r.get(name_key) or "") if name_key else ""]
    lock[tab.name] = sorted(have.values(), key=lambda x: x[0])
    return lock
