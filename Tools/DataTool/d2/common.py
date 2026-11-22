"""D2 사례를 만드는 도우미 — GameData 행을 고치는 함수들."""
from __future__ import annotations


def set_cell(table: str, index: int, key: str, value):
    def f(data):
        data[table][index][key] = value
    f.desc = f"{table}[{index}].{key} = {value!r}"
    return f


def drop_key(table: str, index: int, key: str):
    def f(data):
        del data[table][index][key]
    f.desc = f"{table}[{index}]에서 {key} 열을 뺀다"
    return f


def add_key(table: str, index: int, key: str, value):
    def f(data):
        data[table][index][key] = value
    f.desc = f"{table}[{index}]에 모르는 열 {key}"
    return f


def multi(*fs):
    def f(data):
        for g in fs:
            g(data)
    f.desc = " · ".join(getattr(g, "desc", "") for g in fs)
    return f


def custom(desc: str, fn):
    fn.desc = desc
    return fn
