"""D2 실행기 — `datatool d2 [--random 10] [--only 이름,…]`.

사례마다 GameData를 임시 폴더로 복사 → 한 곳을 틀리게 → 검사기(cpp: datacheck · py: datatool 검사 · code: grep)
→ 기대한 R번호 · 위치 · 이유가 오류 목록에 있으면 통과. 틀리지 않은 GameData가 통과하는지도 먼저 본다.
결과: build/datatool/d2_result.json + 화면 줄(PASS/FAIL).
"""
from __future__ import annotations

import copy
import importlib
import json
import pkgutil
import random
import re
import shutil
import tempfile
from pathlib import Path

from . import checker, schema
from .paths import BUILD, GAMEDATA, SERVER, TOOL_DIR

RESULT = BUILD / "d2_result.json"


def all_cases() -> list[dict]:
    import d2  # Tools/DataTool/d2
    cases = []
    for m in pkgutil.iter_modules(d2.__path__):
        if m.name in ("common",):
            continue
        mod = importlib.import_module(f"d2.{m.name}")
        for c in getattr(mod, "CASES", []):
            c = dict(c)
            c["bundle"] = m.name
            cases.append(c)
    return cases


def load_gamedata() -> dict[str, list[dict]]:
    data = {}
    for f in GAMEDATA.glob("*.json"):
        if f.name.startswith("_") or f.name == "manifest.json":
            continue
        data[f.stem] = json.loads(f.read_text(encoding="utf-8"))["rows"]
    return data


def write_dir(tmp: Path, data: dict[str, list[dict]]) -> None:
    for name, rows in data.items():
        body = '{"table": ' + json.dumps(name) + ', "rows": [\n' + ",\n".join(json.dumps(r, ensure_ascii=False) for r in rows) + "\n]}\n"
        (tmp / f"{name}.json").write_text(body, encoding="utf-8")
    if (GAMEDATA / "_src.json").exists():
        shutil.copy(GAMEDATA / "_src.json", tmp / "_src.json")


def matches(err: dict, expect: dict) -> bool:
    if "rule" in expect and err.get("rule", "") != expect["rule"]:
        return False
    if "where" in expect and expect["where"] not in err.get("where", ""):
        return False
    if "what" in expect and expect["what"] not in err.get("what", ""):
        return False
    return True


def run_case(case: dict, base: dict) -> tuple[bool, str]:
    engine = case.get("engine", "cpp")
    if engine == "code":
        pat = re.compile(case["pattern"])
        hit = [p for p in SERVER.rglob(case.get("glob", "*.h")) if pat.search(p.read_text(encoding="utf-8-sig", errors="replace"))]
        return (bool(hit), f"{case['pattern']} → {len(hit)}곳")
    data = copy.deepcopy(base)
    case["mutate"](data)
    if engine == "py":
        from . import pipeline
        tabs = schema.load()
        errs, _ = pipeline.check_rows(tabs, pipeline.enums_dict(), pipeline.full_data(tabs, data), {}, [case["table"]])
        errs = [{"where": e.split(": ", 1)[0], "what": e.split(": ", 1)[-1], "rule": ""} for e in errs]
    else:
        with tempfile.TemporaryDirectory() as td:
            tmp = Path(td)
            write_dir(tmp, data)
            ok, errs, _ = checker.run(tmp)
    hit = [e for e in errs if matches(e, case["expect"])]
    if hit:
        e = hit[0]
        return True, f"{e['where']}: {e['what']}" + (f" [{e['rule']}]" if e.get("rule") else "")
    got = "; ".join(f"{e['where']}: {e['what']} [{e.get('rule', '')}]" for e in errs[:3]) or "오류 없음"
    return False, f"기대 {case['expect']} — 받은 것: {got}"


def run(only: list[str] | None = None, sample: int | None = None, seed: int | None = None) -> int:
    base = load_gamedata()
    cases = all_cases()
    # 지금 GameData에 있는 표만(이관 중 — 아직 코드에 있는 묶음의 사례는 건너뛴다)
    runnable = [c for c in cases if c.get("engine") == "code" or c["table"] in base]
    if only:
        runnable = [c for c in runnable if c["name"] in only]
    if sample:
        rnd = random.Random(seed)
        rules = sorted({r for c in runnable for r in c["rules"]})
        pick = set(rnd.sample(rules, min(sample, len(rules))))
        runnable = [c for c in runnable if set(c["rules"]) & pick]
        print(f"[d2] 무작위 규칙 {len(pick)}개(seed={seed}): {' '.join(sorted(pick))}")
    with tempfile.TemporaryDirectory() as td:
        tmp = Path(td)
        write_dir(tmp, base)
        ok, errs, summary = checker.run(tmp)
    results = [{"name": "_clean", "pass": ok, "detail": summary if ok else str(errs[:3])}]
    print(("PASS" if ok else "FAIL") + "  _clean(틀리지 않은 GameData가 통과)  " + (summary if ok else str(errs[:3])))
    for c in runnable:
        passed, detail = run_case(c, base)
        results.append({"name": c["name"], "rules": c["rules"], "bundle": c["bundle"], "pass": passed, "detail": detail})
        print(f"{'PASS' if passed else 'FAIL'}  {c['name']:<34} {' '.join(c['rules']):<12} {detail[:150]}")
    RESULT.parent.mkdir(parents=True, exist_ok=True)
    RESULT.write_text(json.dumps(results, ensure_ascii=False, indent=1), encoding="utf-8")
    failed = [r for r in results if not r["pass"]]
    covered = sorted({r for c in runnable for r in c["rules"]})
    print(f"[d2] 사례 {len(results)} · 통과 {len(results) - len(failed)} · 실패 {len(failed)} · 덮은 규칙 {len(covered)}: {' '.join(covered)}")
    return 1 if failed else 0
