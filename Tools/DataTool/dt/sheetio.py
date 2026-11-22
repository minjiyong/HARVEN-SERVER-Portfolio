"""시트 ↔ 정본 행 — 탭 모양, push(첫 올리기), pull 결과 해석, 이름 보조 열 채우기.

파일 · 탭
  표 하나 = 스프레드시트 파일 하나(드라이브 HARVEN_DATA/<표>_Table), 파일 안 탭 하나(탭 이름 = 표 이름).
  1행 = 열 키(스키마와 같음 — 보호), 2행 = 한글 설명(보호), 3행부터 데이터. 1~2행 · 첫 열 고정.
  ref 열 바로 뒤에 보조 열 `#<키>` = 대상 표의 이름. 파일이 나뉘어 수식으로 다른 파일을 읽지 않고,
  datatool sync · watch가 값으로 채운다(name_lookup · helper_updates). datatool은 이 열을 읽지 않는다.
  id 잠금 표(id_lock) 끝에 시트 전용 `deprecated` 열(체크박스 — GameData로 가지 않는다).
  무시: 첫 칸이 비었거나 #로 시작하는 행, 키가 #로 시작하는 열.
값
  정수 · f32 → 숫자, bool → 체크박스, 색 · 문자열 · 열거형 → 글자(열거형은 그 파일 안의 목록 드롭다운 — 원본은 _meta/_Enums),
  str?의 null → 빈 칸. 읽기는 UNFORMATTED_VALUE. 숫자 열에 글자, 글자 열에 숫자는 오류(조용히 바꾸지 않는다).
"""
from __future__ import annotations

import unicodedata
import zlib
from dataclasses import dataclass, field

from .schema import INT_RANGE, Column, Tab

HEADER_BG = {"red": 0.91, "green": 0.92, "blue": 0.93}
DESC_FG = {"red": 0.40, "green": 0.40, "blue": 0.40}
HELPER_BG = {"red": 0.96, "green": 0.96, "blue": 0.96}
ENUM_TAB = "_Enums"


def sheet_id(name: str) -> int:
    return (zlib.crc32(name.encode("utf-8")) & 0x3FFFFFFF) or 1


def col_letter(i: int) -> str:
    s = ""
    i += 1
    while i:
        i, r = divmod(i - 1, 26)
        s = chr(65 + r) + s
    return s


@dataclass
class SheetCol:
    header: str
    desc: str
    kind: str                 # data | helper | deprecated
    col: Column | None = None
    ref: str | None = None    # helper: 가리키는 데이터 열 키


def name_column(tab: Tab) -> str | None:
    keys = [c.key for c in tab.columns]
    for k in ("name", "display_name", "title", "text"):
        if k in keys:
            return k
    return None


def layout(tab: Tab, tabs: dict[str, Tab]) -> list[SheetCol]:
    cols: list[SheetCol] = []
    for c in tab.columns:
        cols.append(SheetCol(c.key, c.desc, "data", c))
        ref = c.attrs.get("ref")
        if ref and ref in tabs and name_column(tabs[ref]):
            cols.append(SheetCol("#" + c.key, f"← {ref} 이름(datatool sync가 채운다 — 고치지 않는다)", "helper", None, c.key))
    if tab.attrs.get("id_lock"):
        cols.append(SheetCol("deprecated", "폐기 — id는 영구라 행을 지우지 말고 체크(다른 탭이 가리키면 경고)", "deprecated"))
    return cols


# ── push: 정본 행 → 셀 ──────────────────────────────────────────────────────
def _cell_value(c: Column, v) -> dict:
    b = c.base
    if v is None:
        return {}
    if b in INT_RANGE or b == "f32":
        return {"numberValue": v}
    if b == "bool":
        return {"boolValue": bool(v)}
    return {"stringValue": str(v)}


def name_lookup(tabs: dict[str, Tab], data: dict[str, list[dict]]) -> dict[str, dict]:
    """표 → {id: 이름} — 보조 열 `#<키>`에 쓴다(이름 열이 있는 표만)."""
    out: dict[str, dict] = {}
    for n, rows in data.items():
        t = tabs.get(n)
        nk = name_column(t) if t else None
        if t and nk and len(t.key) == 1:
            out[n] = {r[t.key[0]]: r.get(nk) or "" for r in rows}
    return out


def helper_value(tab: Tab, sc: SheetCol, ref_value, lookup: dict[str, dict]) -> str:
    target = tab.col(sc.ref).attrs["ref"]
    if ref_value is None or ref_value == "":
        return ""
    return lookup.get(target, {}).get(ref_value, "")


def validation_requests(tab: Tab, tabs: dict[str, Tab], enums: dict, sid: int, last_row: int,
                        header: list[str] | None = None) -> list[dict]:
    """드롭다운 · 체크박스(3행 ~ last_row). 열거형은 파일 안의 목록(ONE_OF_LIST) — 다른 파일(_Enums)을 가리키지 않는다.
    header = 지금 1행(기획자가 메모 열을 끼워 넣었어도 열 키로 자리를 찾는다). 없으면 layout 순서."""
    reqs: list[dict] = [{"setDataValidation": {"range": {"sheetId": sid, "startRowIndex": 2, "endRowIndex": last_row}}}]   # 지우고 다시
    cols = layout(tab, tabs)
    pos = {h: i for i, h in enumerate(header)} if header is not None else {c.header: i for i, c in enumerate(cols)}
    for c in cols:
        if c.header not in pos:
            continue
        i = pos[c.header]
        rng = {"sheetId": sid, "startRowIndex": 2, "endRowIndex": last_row, "startColumnIndex": i, "endColumnIndex": i + 1}
        if c.kind == "deprecated" or (c.kind == "data" and c.col.base == "bool"):
            reqs.append({"setDataValidation": {"range": rng, "rule": {"condition": {"type": "BOOLEAN"}, "strict": True}}})
        elif c.kind == "data" and c.col.base == "enum":
            names = [n for n, _ in enums[c.col.enum]["items"]]
            reqs.append({"setDataValidation": {"range": rng, "rule": {
                "condition": {"type": "ONE_OF_LIST", "values": [{"userEnteredValue": v} for v in names]},
                "strict": True, "showCustomUi": True}}})
        elif c.kind == "data" and c.col.attrs.get("values"):
            reqs.append({"setDataValidation": {"range": rng, "rule": {
                "condition": {"type": "ONE_OF_LIST", "values": [{"userEnteredValue": v} for v in c.col.attrs["values"]]},
                "strict": True, "showCustomUi": True}}})
    return reqs


def number_format_requests(tab: Tab, tabs: dict[str, Tab], sid: int, last_row: int, header: list[str] | None = None) -> list[dict]:
    """u64 열(1조 단위 같은 큰 수)은 천 단위 쉼표로 보이게(기본 서식은 1E+12) — 값(UNFORMATTED_VALUE)은 그대로."""
    cols = layout(tab, tabs)
    pos = {h: i for i, h in enumerate(header)} if header is not None else {c.header: i for i, c in enumerate(cols)}
    reqs = []
    for c in cols:
        if c.kind == "data" and c.col.base == "u64" and c.header in pos:
            i = pos[c.header]
            reqs.append({"repeatCell": {"range": {"sheetId": sid, "startRowIndex": 2, "endRowIndex": last_row, "startColumnIndex": i, "endColumnIndex": i + 1},
                                        "cell": {"userEnteredFormat": {"numberFormat": {"type": "NUMBER", "pattern": "#,##0"}}},
                                        "fields": "userEnteredFormat.numberFormat"}})
    return reqs


def protection_requests(tab: Tab, tabs: dict[str, Tab], sid: int, service_email: str, header: list[str] | None = None) -> list[dict]:
    """1~2행(열 키 · 설명) 잠금 + 이름 보조 열은 고치려 하면 경고(datatool이 덮어쓴다). header = 지금 1행(없으면 layout)"""
    reqs = [{"addProtectedRange": {"protectedRange": {
        "range": {"sheetId": sid, "startRowIndex": 0, "endRowIndex": 2},
        "description": "datatool: 열 키 · 설명(1~2행) — 고치지 않는다",
        "warningOnly": False, "editors": {"users": [service_email]}}}}]
    helpers = {"#" + c.ref for c in layout(tab, tabs) if c.kind == "helper"}
    cols = header if header is not None else [c.header for c in layout(tab, tabs)]
    for i, h in enumerate(cols):
        if h in helpers:
            reqs.append({"addProtectedRange": {"protectedRange": {
                "range": {"sheetId": sid, "startRowIndex": 2, "startColumnIndex": i, "endColumnIndex": i + 1},
                "description": f"datatool: {h} — 이름 보조 열(sync가 채운다)", "warningOnly": True}}})
    return reqs


def push_requests(tab: Tab, tabs: dict[str, Tab], rows: list[dict], enums: dict, existing: dict | None,
                  service_email: str, lookup: dict[str, dict]) -> list[dict]:
    """탭 하나를 만들고(없으면) 값 · 서식 · 드롭다운 · 보호를 한 번의 batchUpdate로."""
    cols = layout(tab, tabs)
    sid = sheet_id(tab.name)
    reqs: list[dict] = []
    nrows = len(rows) + 2
    if existing is None:
        reqs.append({"addSheet": {"properties": {"sheetId": sid, "title": tab.name, "index": 0,
                                                 "gridProperties": {"rowCount": max(nrows + 20, 50), "columnCount": len(cols),
                                                                    "frozenRowCount": 2, "frozenColumnCount": 1}}}})
    else:
        sid = existing["properties"]["sheetId"]
        gp = existing["properties"].get("gridProperties", {})
        # 줄이지 않는다 — 기획자가 덧붙인 메모 열 · 행을 지우지 않게(--force-seed로 다시 심을 때도)
        reqs.append({"updateSheetProperties": {"properties": {"sheetId": sid, "gridProperties": {
            "rowCount": max(nrows + 20, 50, gp.get("rowCount", 0)), "columnCount": max(len(cols), gp.get("columnCount", 0)),
            "frozenRowCount": 2, "frozenColumnCount": 1}},
            "fields": "gridProperties(rowCount,columnCount,frozenRowCount,frozenColumnCount)"}})
        for pr in existing.get("protectedRanges", []) or []:
            reqs.append({"deleteProtectedRange": {"protectedRangeId": pr["protectedRangeId"]}})
    grid_rows = []
    grid_rows.append({"values": [{"userEnteredValue": {"stringValue": c.header},
                                  "userEnteredFormat": {"textFormat": {"bold": True}, "backgroundColor": HEADER_BG}} for c in cols]})
    grid_rows.append({"values": [{"userEnteredValue": {"stringValue": c.desc},
                                  "userEnteredFormat": {"textFormat": {"italic": True, "foregroundColor": DESC_FG}, "wrapStrategy": "CLIP",
                                                        "backgroundColor": HEADER_BG}} for c in cols]})
    for r in rows:
        vals = []
        for c in cols:
            if c.kind == "data":
                vals.append({"userEnteredValue": _cell_value(c.col, r[c.col.key])})
            elif c.kind == "helper":
                vals.append({"userEnteredValue": {"stringValue": helper_value(tab, c, r[c.ref], lookup)},
                             "userEnteredFormat": {"backgroundColor": HELPER_BG, "textFormat": {"foregroundColor": DESC_FG}}})
            else:
                vals.append({"userEnteredValue": {"boolValue": False}})
        grid_rows.append({"values": vals})
    reqs.append({"updateCells": {"rows": grid_rows, "fields": "userEnteredValue,userEnteredFormat",
                                 "start": {"sheetId": sid, "rowIndex": 0, "columnIndex": 0}}})
    reqs += validation_requests(tab, tabs, enums, sid, nrows + 200)
    reqs += number_format_requests(tab, tabs, sid, nrows + 200)
    reqs += protection_requests(tab, tabs, sid, service_email)
    reqs.append({"autoResizeDimensions": {"dimensions": {"sheetId": sid, "dimension": "COLUMNS", "startIndex": 0, "endIndex": len(cols)}}})
    return reqs


def helper_updates(tab: Tab, tabs: dict[str, Tab], values: list[list], lookup: dict[str, dict]) -> list[dict]:
    """이름 보조 열 `#<키>` 중 지금 값과 다른 열만 [{range, values}] — 시트 행 그대로(주석 행 · 빈 행은 비운다)."""
    if not values:
        return []
    header = [str(h).strip() if h is not None else "" for h in values[0]]
    pos = {h: i for i, h in enumerate(header) if h}
    out = []
    for sc in layout(tab, tabs):
        if sc.kind != "helper" or sc.header not in pos or sc.ref not in pos:
            continue
        hi, di = pos[sc.header], pos[sc.ref]
        want, have = [], []
        for row in values[2:]:
            first = row[0] if row else None
            ref = row[di] if di < len(row) else None
            skip = _is_blank(first) or (isinstance(first, str) and first.startswith("#"))
            v = "" if skip or isinstance(ref, bool) else helper_value(tab, sc, ref, lookup)
            want.append(v)
            cur = row[hi] if hi < len(row) else ""
            have.append("" if cur is None else cur)
        if want != have:
            col = col_letter(hi)
            out.append({"range": f"'{tab.name}'!{col}3:{col}{len(values)}", "values": [[v] for v in want]})
    return out


def enums_requests(enums: dict, existing: dict | None, service_email: str) -> list[dict]:
    names = sorted(enums)
    sid = sheet_id(ENUM_TAB)
    reqs = []
    height = max(len(enums[n]["items"]) for n in names) + 1
    if existing is None:
        reqs.append({"addSheet": {"properties": {"sheetId": sid, "title": ENUM_TAB, "index": 0,
                                                 "gridProperties": {"rowCount": height + 5, "columnCount": len(names), "frozenRowCount": 1}}}})
    else:
        sid = existing["properties"]["sheetId"]
        reqs.append({"updateSheetProperties": {"properties": {"sheetId": sid, "gridProperties": {"rowCount": height + 5, "columnCount": len(names), "frozenRowCount": 1}},
                                               "fields": "gridProperties(rowCount,columnCount,frozenRowCount)"}})
        reqs.append({"updateCells": {"range": {"sheetId": sid}, "fields": "userEnteredValue"}})
        for pr in existing.get("protectedRanges", []) or []:
            reqs.append({"deleteProtectedRange": {"protectedRangeId": pr["protectedRangeId"]}})
    rows = [{"values": [{"userEnteredValue": {"stringValue": n}, "userEnteredFormat": {"textFormat": {"bold": True}, "backgroundColor": HEADER_BG}} for n in names]}]
    for i in range(height - 1):
        rows.append({"values": [{"userEnteredValue": {"stringValue": enums[n]["items"][i][0]}} if i < len(enums[n]["items"]) else {} for n in names]})
    reqs.append({"updateCells": {"rows": rows, "fields": "userEnteredValue,userEnteredFormat", "start": {"sheetId": sid, "rowIndex": 0, "columnIndex": 0}}})
    reqs.append({"addProtectedRange": {"protectedRange": {"range": {"sheetId": sid}, "description": "datatool: C++ 열거형에서 생성 — 고치지 않는다",
                                                          "warningOnly": False, "editors": {"users": [service_email]}}}})
    reqs.append({"autoResizeDimensions": {"dimensions": {"sheetId": sid, "dimension": "COLUMNS", "startIndex": 0, "endIndex": len(names)}}})
    return reqs


def text_tab_requests(title: str, lines: list[str], existing: dict | None, service_email: str, index: int | None = None) -> list[dict]:
    sid = sheet_id(title)
    reqs = []
    if existing is None:
        props = {"sheetId": sid, "title": title, "gridProperties": {"rowCount": len(lines) + 5, "columnCount": 1}}
        if index is not None:
            props["index"] = index
        reqs.append({"addSheet": {"properties": props}})
    else:
        sid = existing["properties"]["sheetId"]
        reqs.append({"updateSheetProperties": {"properties": {"sheetId": sid, "gridProperties": {"rowCount": len(lines) + 5, "columnCount": 1}},
                                               "fields": "gridProperties(rowCount,columnCount)"}})
        reqs.append({"updateCells": {"range": {"sheetId": sid}, "fields": "userEnteredValue"}})
        for pr in existing.get("protectedRanges", []) or []:
            reqs.append({"deleteProtectedRange": {"protectedRangeId": pr["protectedRangeId"]}})
    rows = [{"values": [{"userEnteredValue": {"stringValue": l},
                         "userEnteredFormat": {"textFormat": {"bold": l.startswith("■")}, "wrapStrategy": "WRAP"}}]} for l in lines]
    reqs.append({"updateCells": {"rows": rows, "fields": "userEnteredValue,userEnteredFormat", "start": {"sheetId": sid, "rowIndex": 0, "columnIndex": 0}}})
    reqs.append({"updateDimensionProperties": {"range": {"sheetId": sid, "dimension": "COLUMNS", "startIndex": 0, "endIndex": 1},
                                               "properties": {"pixelSize": 900}, "fields": "pixelSize"}})
    reqs.append({"addProtectedRange": {"protectedRange": {"range": {"sheetId": sid}, "description": f"datatool: {title}",
                                                          "warningOnly": False, "editors": {"users": [service_email]}}}})
    return reqs


# ── pull 결과 해석: 셀 → 정본 행 ─────────────────────────────────────────────
@dataclass
class Parsed:
    rows: list[dict] = field(default_factory=list)
    src: list[int] = field(default_factory=list)          # 시트 행 번호(1부터)
    deprecated: dict = field(default_factory=dict)        # key tuple → True
    errors: list[str] = field(default_factory=list)


def _is_blank(v) -> bool:
    return v is None or (isinstance(v, str) and v == "")


def _num(v):
    if isinstance(v, bool):
        return None
    if isinstance(v, (int, float)):
        return v
    return None


def parse_tab(tab: Tab, values: list[list]) -> Parsed:
    out = Parsed()
    where = lambda r, k: f"{tab.name}!{r}" + (f" {k}" if k else "")   # noqa: E731
    if not values:
        out.errors.append(f"{tab.name}!1: 탭이 비었다(1행 열 키 없음)")
        return out
    header = [str(h).strip() if h is not None else "" for h in values[0]]
    want = [c.key for c in tab.columns]
    pos: dict[str, int] = {}
    for i, h in enumerate(header):
        if not h or h.startswith("#"):
            continue
        if h in pos:
            out.errors.append(f"{tab.name}!1: 열 키 {h}가 두 번 있다")
        pos[h] = i
    for k in want:
        if k not in pos:
            out.errors.append(f"{tab.name}!1: 열 {k}가 없다(1행 열 키는 고치지 않는다)")
    for h in pos:
        if h not in want and h != "deprecated":
            out.errors.append(f"{tab.name}!1: 모르는 열 {h}(보조 열은 #로 시작)")
    if out.errors:
        return out
    for ri in range(2, len(values)):
        row = values[ri]
        rnum = ri + 1
        first = row[0] if row else None
        if _is_blank(first) and all(_is_blank(x) for x in row):
            continue
        if _is_blank(first) or (isinstance(first, str) and first.startswith("#")):
            continue
        rec: dict = {}
        bad = False
        for c in tab.columns:
            i = pos[c.key]
            v = row[i] if i < len(row) else None
            val, err = convert(c, v)
            if err:
                out.errors.append(f"{where(rnum, c.key)}: {err}")
                bad = True
            rec[c.key] = val
        if "deprecated" in pos:
            i = pos["deprecated"]
            d = row[i] if i < len(row) else False
            if d not in (True, False, None, ""):
                out.errors.append(f"{where(rnum, 'deprecated')}: 체크박스가 아니다")
            elif d is True:
                out.deprecated[tuple(rec.get(k) for k in tab.key)] = True
        if not bad:
            out.rows.append(rec)
            out.src.append(rnum)
    return out


def convert(c: Column, v):
    """셀 값 → 정본 값. (값, 오류)"""
    b = c.base
    blank = _is_blank(v)
    if blank:
        if c.type == "str?":
            return None, None
        if "default" in c.attrs:
            return c.attrs["default"], None
        if b == "str" and not c.attrs.get("nonempty"):
            return "", None
        return None, "빈 칸(이 열은 값이 있어야 한다)"
    if b in INT_RANGE:
        n = _num(v)
        if n is None:
            return None, f"정수가 아니다({v!r})"
        if isinstance(n, float):
            if not n.is_integer():
                return None, f"정수가 아니다({v!r})"
            n = int(n)
        lo, hi = INT_RANGE[b]
        if not (lo <= n <= hi):
            return None, f"{b} 범위({lo}~{hi})를 넘는다: {n}"
        return n, None
    if b == "f32":
        n = _num(v)
        if n is None:
            return None, f"수가 아니다({v!r})"
        return float(n), None
    if b == "bool":
        if isinstance(v, bool):
            return v, None
        return None, f"체크박스(true/false)가 아니다({v!r})"
    if not isinstance(v, str):
        return None, f"글자 열에 숫자({v!r}) — 글자로 넣으려면 앞에 ' 를 붙인다"
    if b == "color":
        s = v.strip()
        if len(s) != 7 or s[0] != "#" or any(ch not in "0123456789abcdefABCDEF" for ch in s[1:]):
            return None, f"색은 #RRGGBB({v!r})"
        return s.upper(), None
    # str · str? · enum · flags · mask
    if v != v.strip():
        return None, "앞뒤 공백"
    if not unicodedata.is_normalized("NFC", v):
        return None, "NFC가 아닌 한글(조합형) — 다시 입력할 것"
    if any(0xFF01 <= ord(ch) <= 0xFF5E for ch in v):
        return None, "전각 기호(！～ 등)가 섞였다"
    if b in ("enum", "flags", "mask"):
        return v, None
    return v, None
