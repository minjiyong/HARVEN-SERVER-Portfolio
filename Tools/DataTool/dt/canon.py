"""정본(canonical) JSON 쓰기 — C++ Database/GameData/Load/Canon.h와 바이트가 같아야 한다(3자 일치).

형식(Canon.h 머리말과 같다):
  {"table": "Monster", "rows": [
  {"id": 1, "name": "수액덩이", ...},
  ...
  ]}
  · 한 행 한 줄, 열 순서 = 스키마 순서, 구분자 ", " · ": ", 파일 끝 줄바꿈 하나
  · 정수 10진, bool true/false, 색 "#RRGGBB", f32 = std::to_chars(float) 최단 표기, null
  · 문자열 UTF-8 그대로, 이스케이프는 " \\ 와 제어 문자(\\b \\f \\n \\r \\t, 나머지 \\u00xx)
"""
from __future__ import annotations

import math
import struct


def json_string(s: str) -> str:
    out = ['"']
    for ch in s:
        o = ord(ch)
        if ch == '"':
            out.append('\\"')
        elif ch == "\\":
            out.append("\\\\")
        elif ch == "\b":
            out.append("\\b")
        elif ch == "\f":
            out.append("\\f")
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\r":
            out.append("\\r")
        elif ch == "\t":
            out.append("\\t")
        elif o < 0x20:
            out.append("\\u%04x" % o)
        else:
            out.append(ch)
    out.append('"')
    return "".join(out)


def to_f32(x: float) -> float:
    return struct.unpack("<f", struct.pack("<f", x))[0]


def f32_text(x: float) -> str:
    """std::to_chars(char*, char*, float)와 같은 글자: 왕복 가능한 가장 짧은 표기, 고정 · 지수 중 짧은 쪽(같으면 고정)."""
    v = to_f32(float(x))
    if math.isnan(v) or math.isinf(v):
        raise ValueError("f32: NaN · 무한대는 쓰지 않는다")
    if v == 0.0:
        return "-0" if math.copysign(1.0, v) < 0 else "0"
    # 가장 짧은 유효숫자(1~9자리)로 float에 왕복하는 것
    digits = exp10 = None
    for p in range(1, 10):
        s = "%.*e" % (p - 1, v)
        try:
            same = to_f32(float(s)) == v
        except OverflowError:
            same = False
        if same:
            mant, e = s.split("e")
            digits = mant.replace(".", "").replace("-", "")
            exp10 = int(e)
            break
    assert digits is not None
    neg = v < 0
    digits = digits.rstrip("0") or "0"
    n = len(digits)
    # 고정 표기
    if exp10 >= 0:
        if n <= exp10 + 1:
            # 정수 자리만 남을 때 to_chars는 0을 채우지 않고 float의 정확한 정수 값을 쓴다(같은 길이 중 값에 가장 가까운 것)
            fixed = str(abs(int(v)))
        else:
            fixed = digits[: exp10 + 1] + "." + digits[exp10 + 1:]
    else:
        fixed = "0." + "0" * (-exp10 - 1) + digits
    # 지수 표기(C printf %e 모양: 지수 두 자리 이상, 부호)
    sci_m = digits[0] + ("." + digits[1:] if n > 1 else "")
    sci = sci_m + "e" + ("-" if exp10 < 0 else "+") + ("%02d" % abs(exp10))
    body = fixed if len(fixed) <= len(sci) else sci
    return ("-" if neg else "") + body


def value_text(typ: str, v) -> str:
    base = typ.split(":")[0]
    if v is None:
        if typ != "str?":
            raise ValueError(f"{typ}: null 불가")
        return "null"
    if base in ("u8", "u16", "u32", "u64", "i8", "i16", "i32"):
        if isinstance(v, bool) or not isinstance(v, int):
            raise ValueError(f"{typ}: 정수가 아니다 ({v!r})")
        return str(v)
    if base == "bool":
        if not isinstance(v, bool):
            raise ValueError(f"bool이 아니다 ({v!r})")
        return "true" if v else "false"
    if base == "f32":
        return f32_text(v)
    if base == "color":
        if isinstance(v, int):
            return json_string("#%06X" % v)
        return json_string(v.upper())
    if base in ("str", "str?", "enum", "flags", "mask"):
        return json_string(v)
    raise ValueError(f"모르는 형 {typ}")


def row_text(columns: list[tuple[str, str]], row: dict) -> str:
    parts = []
    for key, typ in columns:
        parts.append(json_string(key) + ": " + value_text(typ, row[key]))
    return "{" + ", ".join(parts) + "}"


def table_text(name: str, columns: list[tuple[str, str]], rows: list[dict]) -> str:
    out = ['{"table": ' + json_string(name) + ', "rows": [\n']
    for i, r in enumerate(rows):
        out.append(row_text(columns, r))
        out.append(",\n" if i + 1 < len(rows) else "\n")
    out.append("]}\n")
    return "".join(out)
