"""datatool 명령 — push · pull · check · export · sync · watch · verify.

시트 = 드라이브 HARVEN_DATA 폴더, 표 하나 = 스프레드시트 파일 하나(파일 안 탭 이름 = 표 이름).
표 → 파일 id는 Shared/ToolsConfig/DataTool.json의 "tables"(순서 = 처리 순서), 안내 · 열거형 · 오류는 "meta"의 파일.
"""
from __future__ import annotations

import json
import shutil
import sys
import tempfile
import unicodedata
from pathlib import Path

from . import canon, checker, export, schema, sheetio
from . import enums as enums_mod
from .paths import CACHE, CONFIG, DATA_SRC, GAMEDATA, GEN_DIR, REPO, ROWCOUNTS

README_TAB = "_README"
ERRORS_TAB = "_Errors"
PULL_THREADS = 6          # 표 파일을 나눠 읽는 동시 요청 수(읽기 할당량 분당 60 안에서)


def load_config() -> dict:
    return json.loads(CONFIG.read_text(encoding="utf-8")) if CONFIG.exists() else {}


def save_config(cfg: dict) -> None:
    CONFIG.write_text(json.dumps(cfg, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def enums_dict() -> dict:
    return {e["sheet"]: {"cpp": e["cpp"], "kind": e["kind"], "items": e["items"]} for e in enums_mod.load_all(DATA_SRC)}


def sheet_tabs(tabs: dict, cfg: dict) -> list[str]:
    """시트가 원본인 표(설정 순서) — 표마다 파일 하나"""
    names = list(cfg.get("tables", {}))
    for n in names:
        if n not in tabs:
            raise SystemExit(f"[config] 모르는 표 {n}")
        if tabs[n].source != "sheet":
            raise SystemExit(f"[config] {n}은 mapgen이 주인 — 시트 표가 아니다")
        if not cfg["tables"][n]:
            raise SystemExit(f"[config] {n}의 파일 id가 비었다 — 드라이브 HARVEN_DATA 폴더에 {file_title(n)} 스프레드시트를 만들고 id를 적는다")
    return names


def file_title(name: str) -> str:
    """표 → 드라이브 파일 이름(<표>_Table, 표 이름이 Table로 끝나면 그대로)"""
    return name if name.endswith("Table") else f"{name}_Table"


def table_file(client, cfg: dict, name: str):
    return client.file(cfg["tables"][name])


def meta_file(client, cfg: dict, key: str):
    fid = cfg.get("meta", {}).get(key)
    if not fid:
        raise SystemExit(f"[config] meta.{key} 파일 id가 없다(Shared/ToolsConfig/DataTool.json)")
    return client.file(fid)


def gamedata_rows(name: str) -> list[dict] | None:
    p = GAMEDATA / f"{name}.json"
    return json.loads(p.read_text(encoding="utf-8"))["rows"] if p.exists() else None


# ── 검사(스키마 수준) ──────────────────────────────────────────────────────
def check_rows(tabs: dict, enums: dict, data: dict[str, list[dict]], src: dict[str, list[int]], scope: list[str],
               deprecated: dict[str, dict] | None = None) -> tuple[list[str], list[str]]:
    """(오류, 경고). data = 탭 → 정본 행(범위 밖 탭은 골든/GameData로 채워 참조 검사에 쓴다)."""
    errs: list[str] = []
    warns: list[str] = []
    deprecated = deprecated or {}
    dep_ids = {t: {k[0] for k in d} for t, d in deprecated.items()}
    lock_path = GAMEDATA / "ids.lock"
    lock = json.loads(lock_path.read_text(encoding="utf-8")) if lock_path.exists() else {}

    def where(t, i, k=None):
        rows = src.get(t)
        loc = f"{t}!{rows[i]}" if rows and i < len(rows) else f"{t}#{i + 1}"
        return loc + (f" {k}" if k else "")

    keysets: dict[str, set] = {}
    for name, rows in data.items():
        t = tabs[name]
        if t.key:
            keysets[name] = {r[t.key[0]] for r in rows} if len(t.key) == 1 else {tuple(r[k] for k in t.key) for r in rows}

    for name in scope:
        t = tabs[name]
        rows = data[name]
        if "rows_exact" in t.attrs and len(rows) != t.attrs["rows_exact"]:
            errs.append(f"{name}: 행 수 {len(rows)} ≠ {t.attrs['rows_exact']}(구조 상수)")
        seen = {}
        for i, r in enumerate(rows):
            if t.key:
                k = tuple(r[x] for x in t.key)
                if k in seen:
                    errs.append(f"{where(name, i, t.key[0])}: 키 {k}가 겹친다({where(name, seen[k])})")
                seen[k] = i
            for c in t.columns:
                v = r[c.key]
                if v is None:
                    continue
                a = c.attrs
                if c.base in schema.INT_RANGE or c.base == "f32":
                    if "min" in a and v < a["min"]:
                        errs.append(f"{where(name, i, c.key)}: {v} < 최소 {a['min']}")
                    if "max" in a and v > a["max"]:
                        errs.append(f"{where(name, i, c.key)}: {v} > 최대 {a['max']}")
                if c.base in ("str", "str?"):
                    b = len(v.encode("utf-8"))
                    if "max_bytes" in a and b > a["max_bytes"]:
                        errs.append(f"{where(name, i, c.key)}: {b}바이트 > {a['max_bytes']}")
                    if a.get("nonempty") and v == "":
                        errs.append(f"{where(name, i, c.key)}: 비어 있으면 안 된다")
                    if a.get("ascii") and any(ord(ch) > 127 for ch in v):
                        errs.append(f"{where(name, i, c.key)}: 영문 ASCII만(패킷 이름)")
                    if "values" in a and v not in a["values"]:
                        errs.append(f"{where(name, i, c.key)}: {v!r}는 {a['values']} 중 하나여야 한다")
                    if "pattern" in a:
                        import re
                        if not re.fullmatch(a["pattern"], v):
                            errs.append(f"{where(name, i, c.key)}: 형식 {a['pattern']}에 맞지 않는다({v!r})")
                    if not unicodedata.is_normalized("NFC", v):
                        errs.append(f"{where(name, i, c.key)}: NFC가 아니다")
                if c.base in ("enum", "flags", "mask"):
                    names = {n for n, _ in enums[c.enum]["items"]}
                    if c.base == "enum":
                        if v not in names:
                            errs.append(f"{where(name, i, c.key)}: 열거형 {c.enum}에 없는 이름 {v!r}")
                    else:
                        for part in [x for x in v.split("|") if x] if v else []:
                            if part not in names:
                                errs.append(f"{where(name, i, c.key)}: {c.enum}에 없는 이름 {part!r}")
                ref = a.get("ref")
                if ref and c.base in schema.INT_RANGE:
                    if v == 0 and a.get("ref_zero"):
                        pass
                    elif ref in keysets and v not in keysets[ref]:
                        errs.append(f"{where(name, i, c.key)}: {ref}에 없는 id {v}")
                    elif ref in dep_ids and v in dep_ids[ref]:
                        warns.append(f"{where(name, i, c.key)}: 폐기(deprecated)된 {ref} {v}를 가리킨다")
        # 자식 탭: seq는 부모마다 1부터 연속, 최대 수
        co = t.attrs.get("child_of")
        if co:
            groups: dict = {}
            for i, r in enumerate(rows):
                gk = r[co["key"]] if "kind" not in t.key else (r[co["key"]], r["kind"])
                groups.setdefault(gk, []).append((r["seq"], i))
            for gk, lst in groups.items():
                seqs = sorted(s for s, _ in lst)
                if seqs != list(range(1, len(seqs) + 1)):
                    errs.append(f"{where(name, lst[0][1], 'seq')}: {co['key']}={gk}의 seq가 1부터 연속이 아니다({seqs})")
                mx = co.get("max")
                if mx and len(seqs) > mx:
                    errs.append(f"{where(name, lst[0][1], 'seq')}: {co['key']}={gk}의 행이 {len(seqs)} > {mx}(구조 상수)")
        # id 잠금: 나간 적 있는 id는 남아 있어야 한다
        if t.attrs.get("id_lock") and name in lock:
            have = {r[t.key[0]] for r in rows}
            for idv, nm in lock[name]:
                if idv not in have:
                    errs.append(f"{name}: id {idv}({nm})가 사라졌다 — id는 영구, 행을 지우지 말고 deprecated를 체크")
    return errs, warns


# ── 명령 ─────────────────────────────────────────────────────────────────
def cmd_push(args) -> int:
    """정본 JSON → 표 파일(새 표를 처음 올릴 때). --meta면 _README · _Enums 파일만 다시 쓴다."""
    from .sheets import Client, service_email
    cfg = load_config()
    tabs = schema.load()
    enums = enums_dict()
    client = Client()
    email = service_email()
    meta_only = getattr(args, "meta", False)
    names = [] if meta_only else (args.tabs.split(",") if args.tabs else sheet_tabs(tabs, cfg))
    for n in names:
        if n not in cfg.get("tables", {}) or not cfg["tables"][n]:
            raise SystemExit(f"[push] {n}: 파일이 없다 — 드라이브 HARVEN_DATA 폴더에 {file_title(n)} 스프레드시트를 만들고(서비스 계정은 폴더 공유로 편집자) "
                             f"DataTool.json tables에 id를 적는다(서비스 계정은 파일을 만들 수 없다 — 저장 공간 0)")
    # 안내 · 열거형 파일(datatool이 관리)
    write_meta(client, cfg, tabs, enums, email)
    src = Path(args.source) if args.source else GAMEDATA
    lookup = sheetio.name_lookup(tabs, {n: gamedata_rows(n) or [] for n in tabs})
    for n in names:
        sh = table_file(client, cfg, n)
        existing = sh.tabs()
        # 시트가 원본이다 — 이미 데이터가 있는 표는 --force-seed 없이 덮지 않는다
        if n in existing and not args.force_seed:
            got = sh.batch_get([f"'{n}'!A3:A4"]).get(n, [])
            if got and any(row for row in got):
                raise SystemExit(f"[push] {n} 파일에 이미 데이터가 있다 — 시트가 원본(기획자 수정을 덮지 않는다). 다시 심으려면 --force-seed")
        rows = json.loads((src / f"{n}.json").read_text(encoding="utf-8"))["rows"]
        sh.batch_update(sheetio.push_requests(tabs[n], tabs, rows, enums, existing.get(n), email, lookup))
        # 새 파일의 빈 기본 탭(시트1 · Sheet1)은 지운다 — 파일 하나 = 표 하나
        extra = [t for t, v in sh.tabs().items() if t != n]
        empty = [t for t in extra if not any(r for r in sh.batch_get([f"'{t}'!A1:Z5"]).get(t, []))]
        sh.batch_update([{"deleteSheet": {"sheetId": existing_tab["properties"]["sheetId"]}}
                         for t, existing_tab in sh.tabs().items() if t in empty])
        print(f"[push] {n}: {len(rows)}행 → {file_title(n)}")
    return 0


def write_meta(client, cfg: dict, tabs: dict, enums: dict, email: str) -> None:
    """_meta/_README · _meta/_Enums 파일을 다시 쓴다(각 파일의 탭 하나)."""
    for key, reqs_of in ((README_TAB, lambda ex: sheetio.text_tab_requests(README_TAB, readme_lines(tabs, cfg), ex, email, index=0)),
                         (sheetio.ENUM_TAB, lambda ex: sheetio.enums_requests(enums, ex, email))):
        sh = meta_file(client, cfg, key)
        existing = sh.tabs()
        sh.batch_update(reqs_of(existing.get(key)))
        sh.batch_update([{"deleteSheet": {"sheetId": v["properties"]["sheetId"]}} for t, v in sh.tabs().items() if t != key])


def readme_lines(tabs: dict, cfg: dict) -> list[str]:
    names = list(cfg.get("tables", {}))
    return [
        "■ HARVEN_DATA — 하르벤 기획 데이터 원본(이 폴더의 표를 고치면 datatool sync 한 줄로 서버에 반영된다)",
        "",
        "■ 파일 규칙",
        "· 표 하나 = 스프레드시트 파일 하나(<표>_Table). 파일 안에는 탭 하나(탭 이름 = 표 이름 — 바꾸지 않는다).",
        "· 1행 = 열 키(영문 — 고치지 않는다, 보호), 2행 = 한글 설명(보호), 3행부터 데이터.",
        "· 무시되는 것: 첫 칸이 비었거나 #로 시작하는 행(주석 · 빈 줄), 이름이 #로 시작하는 열(메모 · 이름 보조 열).",
        "· #<열> 이름 보조 열(예: #item_id)은 그 id가 가리키는 이름이다 — datatool sync가 채운다(고쳐도 다시 덮인다).",
        "· 행 순서는 상관없다 — 서버는 키(id 등)로만 찾는다. 정렬해도 된다.",
        "· _meta 폴더: _README(이 안내) · _Enums(드롭다운 이름 원본 — C++ 열거형에서 생성) · _Errors(마지막 검사 결과).",
        "",
        "■ 값",
        "· 숫자 열은 숫자만(천 단위 쉼표 · % 서식은 표시일 뿐 값은 숫자). 정수 열에 소수 · 글자를 넣으면 오류.",
        "· 체크박스 = 예/아니오. 색 = #RRGGBB.",
        "· 드롭다운 열(열거형)은 목록의 이름 중 하나(_Enums와 같다). 여러 개를 고르는 열(플래그 · 직업 마스크)은 이름을 | 로 잇는다 — 예: Knight|NightHunter, 비우면 없음/모두.",
        "· 글자 열: 앞뒤 공백 · 조합형(NFD) 한글 · 전각 기호(！～)는 오류. 줄바꿈은 셀 안 줄바꿈 그대로.",
        "· 빈 칸은 기본값이 있는 열만 된다(설명에 적힌 열). 0과 빈 칸은 다르다.",
        "",
        "■ id는 영구",
        "· 아이템 · 퀘스트 · 기술 · 몬스터 · NPC id는 DB와 패킷에 저장된다. id를 바꾸거나 지운 id를 다시 쓰지 않는다.",
        "· 더 쓰지 않을 행은 지우지 말고 deprecated 열을 체크한다(다른 표가 가리키면 경고).",
        "· 새 행은 아직 쓰지 않은 id로.",
        "",
        "■ 반영하는 법(개발자 PC)",
        "· python Tools/DataTool/datatool.py sync   (pull → check → export: 오류가 없을 때만 Bin/GameData/가 바뀐다)",
        "· 오류는 _meta/_Errors 파일(위치 · 이유 — 표!행 열)에 — 고치고 다시 sync. Bin/GameData/ 변경을 git으로 커밋하면 기획 변경 이력이 된다.",
        "· 개발 서버(--dev)는 GameData가 바뀌면 다시 읽는다(datatool watch를 켜 두면 시트를 고치고 몇 초 안에 반영).",
        "",
        f"■ 표 {len(names)}개: " + ", ".join(names),
        "  새 표는 이 폴더에 <표>_Table 스프레드시트를 만들고 Shared/ToolsConfig/DataTool.json에 id를 더한 뒤 datatool push --tabs <표>.",
        "  마을 격자 · 배치는 시트가 아니라 HARVEN-DESIGN tools/mapgen이 원본이다.",
    ]


def pull_raw(client, cfg: dict, names: list[str]) -> dict:
    """표 파일마다 values.batchGet 한 번(UNFORMATTED_VALUE, 여러 파일은 동시에) — 표 → 셀 행들"""
    from concurrent.futures import ThreadPoolExecutor

    def one(n):
        got = table_file(client, cfg, n).batch_get([f"'{n}'!A1:ZZ"])
        if n not in got:
            raise SystemExit(f"[pull] {file_title(n)}에 탭 {n}이 없다(파일 안 탭 이름 = 표 이름)")
        return n, got[n]

    with ThreadPoolExecutor(PULL_THREADS) as ex:
        return dict(ex.map(one, names))


def save_cache(raw: dict, meta: dict) -> None:
    CACHE.mkdir(parents=True, exist_ok=True)
    (CACHE / "sheet_raw.json").write_text(json.dumps({"meta": meta, "tabs": raw}, ensure_ascii=False), encoding="utf-8")


def cmd_pull(args) -> int:
    from .sheets import Client
    cfg = load_config()
    tabs = schema.load()
    names = sheet_tabs(tabs, cfg)
    raw = pull_raw(Client(), cfg, names)
    save_cache(raw, {"files": len(names)})
    print(f"[pull] 표 파일 {len(raw)}개 · {sum(len(v) for v in raw.values())}행(머리 포함) → {CACHE / 'sheet_raw.json'}")
    return 0


def refresh_helpers(client, cfg: dict, tabs: dict, raw: dict, data: dict) -> int:
    """이름 보조 열(#<키>)을 지금 데이터로 채운다 — 바뀐 열이 있는 파일만 쓴다. 쓴 파일 수."""
    full = full_data(tabs, data)
    lookup = sheetio.name_lookup(tabs, full)
    n_files = 0
    for n, values in raw.items():
        ups = sheetio.helper_updates(tabs[n], tabs, values, lookup)
        if ups:
            table_file(client, cfg, n).values_batch_update(ups)
            n_files += 1
    return n_files


def parse_cache(tabs: dict, names: list[str]):
    raw = json.loads((CACHE / "sheet_raw.json").read_text(encoding="utf-8"))["tabs"]
    data, src, errs, dep = {}, {}, [], {}
    for n in names:
        p = sheetio.parse_tab(tabs[n], raw.get(n, []))
        errs += p.errors
        data[n] = p.rows
        src[n] = p.src
        dep[n] = p.deprecated
    return data, src, errs, dep


def full_data(tabs: dict, data: dict) -> dict:
    """범위 밖 탭(mapgen이 주인인 마을 표 등)은 GameData로 채운다(참조 · 묶음 규칙 검사용)"""
    out = dict(data)
    for n in tabs:
        if n in out:
            continue
        g = gamedata_rows(n)
        if g is not None:
            out[n] = g
    return out


def refresh_gen(tabs: dict, enums: dict, names: list[str], data: dict, src: dict, dep: dict) -> None:
    """이름 헤더(Gen/<표>Ids.h)를 시트의 code_name에 맞춘다 — 검사기(C++) 빌드보다 먼저.
    코드가 시트에 새로 더한 code_name(NpcIds::Blacksmith 등)을 부르면 검사기 빌드가 그 이름을 알아야 한다. 이름 헤더는 시트의 생성물이라
    파이썬 검사(형 · 범위 · 패턴 · 유일)를 통과하면 묶음 검사 결과와 상관없이 시트를 따른다."""
    errs, _ = check_rows(tabs, enums, full_data(tabs, data), src, names, dep)
    if errs:
        return
    for n in names:
        rows = sorted(data[n], key=export.sort_key(tabs[n], enums))
        g = export.gen_ids_header(tabs[n], rows)
        if not g:
            continue
        dst = GEN_DIR / g[0]
        if not dst.exists() or dst.read_text(encoding="utf-8") != g[1]:
            dst.write_text(g[1], encoding="utf-8", newline="\n")
            print(f"[gen] {dst.relative_to(REPO).as_posix()} — 시트의 code_name을 따라 갱신")


def run_check(tabs: dict, enums: dict, names: list[str], data: dict, src: dict, dep: dict) -> tuple[list[str], list[str]]:
    errs, warns = check_rows(tabs, enums, full_data(tabs, data), src, names, dep)
    if errs:
        return errs, warns
    # 묶음 규칙(C++ — 서버와 같은 코드): 후보 GameData를 임시 폴더에 쓰고 검사기로
    with tempfile.TemporaryDirectory() as td:
        tmp = Path(td)
        for f in GAMEDATA.glob("*.json"):
            if f.name not in ("manifest.json",):
                shutil.copy(f, tmp / f.name)
        cand = {n: (tabs[n], data[n], src.get(n)) for n in names}
        export.write(cand, enums, gamedata=tmp, gen_dir=tmp / "_gen")
        (tmp / "manifest.json").unlink(missing_ok=True)
        ok, cerrs, _summary = checker.run(tmp)
        for e in cerrs:
            errs.append(f"{e['where']}: {e['what']}" + (f" [{e['rule']}]" if e["rule"] else ""))
    return errs, warns


def write_errors_tab(errs: list[str], warns: list[str], client=None) -> None:
    """_meta/_Errors 파일에 마지막 검사 결과(위치 = 표!행 열 — 표는 <표>_Table 파일)"""
    import datetime
    from .sheets import Client, service_email
    client = client or Client()
    sh = meta_file(client, load_config(), ERRORS_TAB)
    stamp = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    lines = [f"■ datatool check 결과({stamp}) — " + (f"오류 {len(errs)}개(이것을 고치기 전에는 GameData가 바뀌지 않는다)" if errs else "오류 없음"),
             "  위치는 표!행 열 — 표 이름의 파일(<표>_Table)에서 그 행을 찾는다."]
    lines += ["오류: " + e for e in errs] + ["경고: " + w for w in warns]
    try:   # 보통은 쓰기 두 번(읽기 할당량을 쓰지 않는다 — watch 중에도)
        sh.values_clear(f"'{ERRORS_TAB}'!A:A")
        sh.values_append(f"'{ERRORS_TAB}'!A1", [[l] for l in lines])
        return
    except SystemExit:
        pass   # 탭이 아직 없다 — 만든다
    existing = sh.tabs()
    sh.batch_update(sheetio.text_tab_requests(ERRORS_TAB, lines, existing.get(ERRORS_TAB), service_email(), index=0))
    stray = [v["properties"]["sheetId"] for t, v in existing.items() if t != ERRORS_TAB]
    if stray:
        sh.batch_update([{"deleteSheet": {"sheetId": i}} for i in stray])


def cmd_check(args) -> int:
    cfg = load_config()
    tabs = schema.load()
    enums = enums_dict()
    names = sheet_tabs(tabs, cfg)
    if args.gamedata:
        data = {n: gamedata_rows(n) or [] for n in names}
        src_all = json.loads((GAMEDATA / "_src.json").read_text(encoding="utf-8")) if (GAMEDATA / "_src.json").exists() else {}
        src = {n: src_all.get(n, []) for n in names}
        perr, dep = [], {}
    else:
        data, src, perr, dep = parse_cache(tabs, names)
        if not perr:
            refresh_gen(tabs, enums, names, data, src, dep)
    sch_errs = schema.check_against_canon(tabs)
    if sch_errs:
        print("\n".join("[schema] " + e for e in sch_errs))
        return 1
    errs, warns = (perr, []) if perr else run_check(tabs, enums, names, data, src, dep)
    for e in errs:
        print("오류 " + e)
    for w in warns:
        print("경고 " + w)
    if getattr(args, "to_sheet", False):
        write_errors_tab(errs, warns)
    print(f"[check] 오류 {len(errs)} · 경고 {len(warns)} · 표 {len(names)}")
    return 1 if errs else 0


def cmd_export(args) -> int:
    cfg = load_config()
    tabs = schema.load()
    enums = enums_dict()
    names = sheet_tabs(tabs, cfg)
    data, src, perr, dep = parse_cache(tabs, names)
    if not perr:
        refresh_gen(tabs, enums, names, data, src, dep)
    errs, warns = (perr, []) if perr else run_check(tabs, enums, names, data, src, dep)
    if errs:
        for e in errs:
            print("오류 " + e)
        print(f"[export] 오류 {len(errs)} — GameData를 바꾸지 않는다")
        return 1
    for w in warns:
        print("경고 " + w)
    changed = export.write({n: (tabs[n], data[n], src[n]) for n in names}, enums)
    print(f"[export] 표 {len(names)} · 바뀐 파일 {len(changed)}")
    for c in changed:
        print("  " + c)
    return 0


def cmd_sync(args) -> int:
    """pull → check → export(검사는 한 번 — 묶음 규칙 C++ 검사기도 한 번). 오류면 GameData를 바꾸지 않는다.
    시트에서 읽었으니 이름 보조 열(#…)도 채운다."""
    from .sheets import Client
    cfg = load_config()
    names = sheet_tabs(schema.load(), cfg)
    client = Client()
    raw = pull_raw(client, cfg, names)
    save_cache(raw, {"files": len(names)})
    print(f"[pull] 표 파일 {len(raw)}개 · {sum(len(v) for v in raw.values())}행(머리 포함)")
    return sync_from_cache(args, client)


def sync_from_cache(args, client=None) -> int:
    """캐시(.cache/sheet_raw.json) → check → export. client가 있으면 _Errors · 이름 보조 열을 시트에 쓴다."""
    cfg = load_config()
    tabs = schema.load()
    enums = enums_dict()
    names = sheet_tabs(tabs, cfg)
    data, src, perr, dep = parse_cache(tabs, names)
    if not perr:
        refresh_gen(tabs, enums, names, data, src, dep)
    sch_errs = schema.check_against_canon(tabs)
    if sch_errs:
        print("\n".join("[schema] " + e for e in sch_errs))
        return 1
    errs, warns = (perr, []) if perr else run_check(tabs, enums, names, data, src, dep)
    for e in errs:
        print("오류 " + e)
    for w in warns:
        print("경고 " + w)
    if getattr(args, "to_sheet", False):
        write_errors_tab(errs, warns, client)
    if client is not None:
        try:
            raw = json.loads((CACHE / "sheet_raw.json").read_text(encoding="utf-8"))["tabs"]
            n = refresh_helpers(client, cfg, tabs, raw, data)
            if n:
                print(f"[sync] 이름 보조 열(#…)을 고친 표 파일 {n}개")
        except SystemExit as e:   # 보조 열은 편의 — 실패해도 데이터 반영은 막지 않는다
            print(f"[sync] 이름 보조 열을 못 썼다: {e}")
    print(f"[check] 오류 {len(errs)} · 경고 {len(warns)} · 표 {len(names)}")
    if errs:
        print(f"[export] 오류 {len(errs)} — GameData를 바꾸지 않는다")
        return 1
    changed = export.write({n: (tabs[n], data[n], src[n]) for n in names}, enums)
    print(f"[export] 표 {len(names)} · 바뀐 파일 {len(changed)}")
    for c in changed:
        print("  " + c)
    return 0


WATCH_READS_PER_MIN = 45   # 시트 읽기 할당량(사용자당 분당 60) 안 — 나머지는 sync · 재시도 몫
WATCH_HOT_SECONDS = 600    # 바뀐 표는 이 시간 동안 매 차례 읽는다(기획자는 보통 한 표를 이어서 고친다)


def table_digest(values: list[list]) -> str:
    """표 내용 해시 — 이름 보조 열(#…)은 뺀다(datatool이 쓴 보조 열 때문에 다시 돌지 않게)"""
    import hashlib
    header = [str(h).strip() if h is not None else "" for h in (values[0] if values else [])]
    keep = [i for i, h in enumerate(header) if not h.startswith("#")]
    body = [[row[i] if i < len(row) else None for i in keep] for row in values]
    return hashlib.sha256(json.dumps(body, ensure_ascii=False).encode()).hexdigest()


def cmd_watch(args) -> int:
    """개발용 — 표 파일을 돌아가며 읽어 바뀌면 check → export(sync와 같은 길, --to-sheet).
    --dev 서버는 GameData/manifest.json이 바뀌면 다시 읽는다 → 시트를 고치고 몇 초 안에 게임에 반영된다.

    표 하나 = 파일 하나라 한 번에 다 읽으면 파일 수만큼 요청이다(읽기 할당량 사용자당 분당 60). 그래서
      · 매 차례(interval초) 읽기 몇 번만: 최근에 바뀐 표(뜨거운 표 — WATCH_HOT_SECONDS) 먼저, 남는 몫으로 나머지를 차례로.
      · --tables로 고칠 표를 알려 주면 그 표만 매 차례 읽는다(가장 빠르다).
    Drive 수정 시각 · version은 시트 API로 값을 고친 뒤 몇 분씩 늦게 바뀌어(직접 재 봤다) 내용 해시로 본다."""
    import datetime
    import time as _time
    from collections import deque
    from concurrent.futures import ThreadPoolExecutor
    from .sheets import Client
    cfg = load_config()
    tabs = schema.load()
    all_names = sheet_tabs(tabs, cfg)
    focus = [n.strip() for n in args.tables.split(",")] if getattr(args, "tables", None) else []
    for n in focus:
        if n not in all_names:
            raise SystemExit(f"[watch] 모르는 표 {n}")
    client = Client()
    interval = max(1.0, float(args.interval))
    budget = max(1, int(WATCH_READS_PER_MIN * interval / 60))
    stamp = lambda: datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]   # noqa: E731

    reads: deque = deque()   # 지난 60초의 읽기 시각(할당량을 넘지 않게 — 넘으면 차례를 건너뛴다)

    def spend(k: int) -> None:
        t = _time.time()
        reads.extend([t] * k)

    def allowed() -> int:
        t = _time.time()
        while reads and reads[0] < t - 60:
            reads.popleft()
        return WATCH_READS_PER_MIN - len(reads)

    raw = pull_raw(client, cfg, all_names)
    spend(len(all_names))
    digest = {n: table_digest(v) for n, v in raw.items()}
    save_cache(raw, {"watch": stamp()})
    hot: dict[str, float] = {n: float("inf") for n in focus}
    cold = deque(n for n in all_names if n not in focus)
    if focus:
        print(f"[watch] {stamp()} 표 {', '.join(focus)}를 {interval:g}초마다 읽는다 — 바뀌면 sync --to-sheet (Ctrl+C로 끝)", flush=True)
    else:
        cycle = len(all_names) / budget * interval
        print(f"[watch] {stamp()} 표 {len(all_names)}개를 {interval:g}초마다 {budget}개씩 돌아가며 읽는다(한 바퀴 약 {cycle:.0f}초, "
              f"바뀐 표는 {WATCH_HOT_SECONDS // 60}분 동안 매 차례) — 고칠 표를 알면 --tables (Ctrl+C로 끝)", flush=True)

    def read(n):
        return n, table_file(client, cfg, n).batch_get([f"'{n}'!A1:ZZ"]).get(n, [])

    syncs = 0
    with ThreadPoolExecutor(max(2, budget)) as ex:
        while args.count <= 0 or syncs < args.count:
            _time.sleep(interval)
            now = _time.time()
            room = min(allowed(), max(budget, len(focus)))
            if room <= 0:
                continue
            picks = [n for n, t in sorted(hot.items(), key=lambda kv: -kv[1]) if t > now][:room]
            while len(picks) < min(budget, room) and cold:
                n = cold[0]
                cold.rotate(-1)
                if n in picks:
                    break
                picks.append(n)
            spend(len(picks))
            try:
                got = dict(ex.map(read, picks))
            except SystemExit as e:
                print(f"[watch] {stamp()} 시트를 못 읽었다: {e}", flush=True)
                continue
            changed = [n for n, v in got.items() if table_digest(v) != digest[n]]
            for n, v in got.items():
                raw[n] = v
            if not changed:
                continue
            t0 = _time.time()
            for n in changed:
                digest[n] = table_digest(raw[n])
                if n not in focus:
                    hot[n] = now + WATCH_HOT_SECONDS
            print(f"[watch] {stamp()} 바뀐 표 {', '.join(changed)} → check · export", flush=True)
            save_cache(raw, {"watch": stamp()})
            try:
                rc = sync_from_cache(type("A", (), {"to_sheet": True})(), client)
            except SystemExit as e:
                print(f"[watch] sync 실패: {e}", flush=True)
                rc = 1
            syncs += 1
            print(f"[watch] {stamp()} sync {'완료' if rc == 0 else '오류 — GameData 그대로, _meta/_Errors 확인'} ({_time.time() - t0:.1f}s)", flush=True)
    return 0


def cmd_verify(args) -> int:
    """상시 시험 — GameData = 서버 메모리 덤프(로더 충실도, 바이트) + 표마다 행 수 = Tools/DataTool/rowcounts.json(스냅샷).
    행 수가 바뀌면(기획자가 행을 더함) 실패 — 의도한 변경이면 --update로 스냅샷을 고쳐 같은 커밋에 넣는다."""
    tabs = schema.load()
    names = [n for n in sorted(tabs) if (GAMEDATA / f"{n}.json").exists()]
    with tempfile.TemporaryDirectory() as td:
        dump = Path(td) / "dump"
        ok, errs, summary = checker.run(GAMEDATA, dump)
        if not ok:
            print("[verify] 로드 실패: " + "; ".join(f"{e['where']}: {e['what']}" for e in errs[:10]))
            return 1
        print(summary)
        diffs = []
        counts = {}
        for n in names:
            d = (GAMEDATA / f"{n}.json").read_bytes()
            counts[n] = len(json.loads(d)["rows"])
            mp = dump / f"{n}.json"
            if not mp.exists():
                diffs.append(f"{n}: 메모리 덤프 파일이 없다")
            elif mp.read_bytes() != d:
                a, b = d.decode().split("\n"), mp.read_bytes().decode().split("\n")
                i = next((k for k in range(max(len(a), len(b))) if (a[k] if k < len(a) else None) != (b[k] if k < len(b) else None)), 0)
                diffs.append(f"{n} {i + 1}줄: GameData {(a[i] if i < len(a) else '<끝>')[:120]!r} ≠ 메모리 덤프 {(b[i] if i < len(b) else '<끝>')[:120]!r}")
    if getattr(args, "update", False):
        ROWCOUNTS.write_text(json.dumps(counts, ensure_ascii=False, indent=1, sort_keys=True) + "\n", encoding="utf-8")
        print(f"[verify] 행 수 스냅샷 갱신 → {ROWCOUNTS.relative_to(REPO)} (표 {len(counts)})")
    else:
        snap = json.loads(ROWCOUNTS.read_text(encoding="utf-8")) if ROWCOUNTS.exists() else {}
        if not snap:
            diffs.append(f"{ROWCOUNTS.relative_to(REPO)} 없음 — datatool verify --update로 만든다")
        for n in sorted(set(snap) | set(counts)):
            if snap.get(n) != counts.get(n):
                diffs.append(f"{n}: 행 수 {snap.get(n)} → {counts.get(n)} (기획 변경이면 datatool verify --update)")
    if diffs:
        for d in diffs[:20]:
            print("[verify] 다름 " + d)
        return 1
    print(f"[verify] GameData = 메모리 덤프 · 행 수 스냅샷 일치 — 표 {len(names)} · 행 {sum(counts.values())}")
    return 0
