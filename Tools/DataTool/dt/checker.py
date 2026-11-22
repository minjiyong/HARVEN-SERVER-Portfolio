"""C++ 검사기(datacheck) — 서버와 같은 로더 · 규칙 코드를 리눅스 g++로 빌드해 부른다.

묶음 규칙(Shared/Document/DATA_RULES.md의 R번호)은 C++ 한 곳(Bind_<묶음>.cpp Validate<묶음>)에만 있다 — datatool check는 여기로 위임한다.
HARVEN_DATACHECK=<서버 실행 파일>이면 따로 빌드하지 않고 `<GameServer> --check-data <폴더>`로 같은 일을 한다.
"""
from __future__ import annotations

import hashlib
import json
import os
import subprocess
import tempfile
from pathlib import Path

from .paths import BUILD, DATA_SRC, GAMEDATA, LIBS, SERVER, SHARED, TOOL_DIR

STUB = TOOL_DIR / "checker" / "stub"
WARN_FLAGS = ["-Wall", "-Wextra", "-Wno-unused-parameter"]


def _sources() -> list[Path]:
    return sorted((DATA_SRC / "Load").glob("*.cpp")) + [TOOL_DIR / "checker" / "datacheck.cpp"]


def _fingerprint() -> str:
    h = hashlib.sha256()
    for root in (DATA_SRC, LIBS / "Base" / "ObjID", SHARED / "Protocol", TOOL_DIR / "checker"):
        for p in sorted(root.rglob("*")):
            if p.is_file() and p.suffix in (".h", ".cpp", ".hpp"):
                h.update(p.as_posix().encode())
                h.update(p.read_bytes())
    return h.hexdigest()[:16]


def build(force: bool = False) -> tuple[Path, list[str]]:
    """(실행 파일, 새 로더 파일의 경고 줄) — 바뀐 것이 없으면 다시 빌드하지 않는다."""
    exe = BUILD / "datacheck"
    stamp = BUILD / "datacheck.stamp"
    fp = _fingerprint()
    if not force and exe.exists() and stamp.exists() and stamp.read_text() == fp:
        warn = (BUILD / "datacheck.warnings").read_text().splitlines() if (BUILD / "datacheck.warnings").exists() else []
        return exe, warn
    BUILD.mkdir(parents=True, exist_ok=True)
    objs = []
    warnings: list[str] = []
    for src in _sources():
        obj = BUILD / "obj" / (src.stem + ".o")
        obj.parent.mkdir(parents=True, exist_ok=True)
        cmd = [os.environ.get("CXX_DATACHECK", "g++"), "-std=c++20", "-O1", "-g0", *WARN_FLAGS,
               "-finput-charset=UTF-8", "-fexec-charset=UTF-8",
               "-include", str(STUB / "prelude.h"),
               "-I", str(STUB), "-I", str(SERVER), "-I", str(LIBS), "-I", str(SHARED),
               "-c", str(src), "-o", str(obj)]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            errs = [l for l in r.stderr.splitlines() if "error" in l][:25]
            raise SystemExit(f"[datacheck] 빌드 실패: {src.name}\n" + "\n".join(errs))
        warnings += [l for l in r.stderr.splitlines() if "warning:" in l and "/GameData/Load/" in l]
        objs.append(str(obj))
    r = subprocess.run([os.environ.get("CXX_DATACHECK", "g++"), *objs, "-o", str(exe)], capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit("[datacheck] 링크 실패\n" + r.stderr[-3000:])
    stamp.write_text(fp)
    (BUILD / "datacheck.warnings").write_text("\n".join(warnings))
    return exe, warnings


def run(data_dir: Path, dump_dir: Path | None = None) -> tuple[bool, list[dict], str]:
    """(통과, 오류[{where, what, rule}], 요약 줄)"""
    env_exe = os.environ.get("HARVEN_DATACHECK")
    if env_exe:
        cmd = [env_exe, "--check-data" if dump_dir is None else "--dump-data", str(data_dir)]
        if dump_dir is not None:
            cmd = [env_exe, "--data-dir", str(data_dir), "--dump-data", str(dump_dir)]
    else:
        exe, _ = build()
        cmd = [str(exe), str(data_dir)] + (["--dump", str(dump_dir)] if dump_dir else [])
    r = subprocess.run(cmd, capture_output=True, text=True)
    errors = []
    summary = ""
    for line in r.stdout.splitlines():
        if line.startswith("ERROR\t"):
            parts = line.split("\t")
            errors.append({"where": parts[1], "what": parts[2] if len(parts) > 2 else "", "rule": parts[3] if len(parts) > 3 else ""})
        elif line.startswith("[GameData]"):
            summary = line
    if r.returncode != 0 and not errors:
        errors.append({"where": "datacheck", "what": (r.stderr or r.stdout)[-500:], "rule": ""})
    return r.returncode == 0, errors, summary


def canon_schema() -> dict:
    """정본 스키마(표 → [(열, 형)]) — 로더의 Canon_*.h가 메모리 덤프에 쓰는 _schema.json. 로더 코드가 같으면 캐시를 쓴다."""
    cache, stamp = BUILD / "canon_schema.json", BUILD / "canon_schema.stamp"
    fp = _fingerprint()
    if cache.exists() and stamp.exists() and stamp.read_text() == fp:
        return json.loads(cache.read_text(encoding="utf-8"))
    with tempfile.TemporaryDirectory() as td:
        ok, errs, _ = run(GAMEDATA, Path(td) / "dump")
        if not ok:
            raise SystemExit("[schema] 정본 스키마를 못 만들었다(GameData 로드 실패): " + "; ".join(f"{e['where']}: {e['what']}" for e in errs[:5]))
        text = (Path(td) / "dump" / "_schema.json").read_text(encoding="utf-8")
    BUILD.mkdir(parents=True, exist_ok=True)
    cache.write_text(text, encoding="utf-8")
    stamp.write_text(fp)
    return json.loads(text)
