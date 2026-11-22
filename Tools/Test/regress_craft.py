"""경제 1차 회귀 시험 — 가방 50칸(소지품 목록 두 쪽) · 제작 · 분해(CRAFT_REQ 112 / CRAFT_ACK 113) · 강화와 서로 Busy ·
DB 반영 실패(StorageError · 연결 닫기) · 경제 로그(/economy · 하루 경계 한 줄 JSON)를 실제 패킷으로 본다.

    python3 Tools/Test/regress_craft.py [--server build/linux-memory/GameServer] [--port 4300] [--only pages,craft] [--keep]

HARVEN_IN_MEMORY_DB=ON 빌드를 이 스크립트가 직접 띄운다(--dev — /economy 개발 명령). 시험용 GameData는 Bin/GameData 사본(값은 그대로).
환경: HARVEN_ECONOMY_PERIOD_SEC=15(경제 로그 「하루」를 15초로) · HARVEN_DB_BATCH_DELAY_MS=300(제작 트랜잭션이 DB에 머무는 동안 Busy를 본다).
샌드박스 이름 규칙(Tools/Sandbox/MemDBConnection.cpp): "lvNN_xyX_Y_…" = 레벨 NN 캐릭터가 (X, Y)에서 시작 —
  "…_CR…" 제작 꾸러미(0: 하급 정수 61×100 · 1: 61×20 · 2: 장비 파편 66×50 · 3: 잿빛 결정 64×30 · 4: 중급 정수 62×100 · 5: 철검 3 ·
          6: 사슬 갑옷 33(거래 이력) · 7: 강철 장검 4(착용) · 8: 각인 두루마리 13×5 · 9: 구리 장검 67, 그로시 50000)
  "…_CP…" 같은 꾸러미 · 그로시 0 / "…_CF…" 같은 꾸러미 · 나머지 칸 철검(가방 가득) / "…_SF…" 같은 꾸러미 · 제작 DB 반영이 늘 실패 /
  "…_FI…" 가방 가득(지팡이 착용 + 철검 49).
대장장이(NPC 2 도브란)의 집은 (765, 757) — 그 곁 (766, 758)에 세운다(IsNear: 체비셰프 3 + 배회 1).

시나리오
  pages     로그인 소지품 목록이 ITEM_LIST_ACK 두 쪽(30 + 20)으로 칸 0~49를 한 번씩 · 꾸러미가 그대로
  craft     제작(재료 작은 묶음부터 · 수수료 · 결과는 가장 앞 빈 칸 · 재료로 비워진 칸에도) · 같은 seq → Duplicate ·
            어디서나(먼 곳 · NPC 0도 Ok — 대장장이 제한 없음) · 재료 부족 LackMaterial · 제작 불가 NoRecipe · 없는 아이템 Invalid
  dismantle 분해(칸 비움 · 파편 · 정수가 같은 묶음에 합쳐진다) · 거래 이력 장비 → 절반 · 착용 → Equipped · 재료 · 빈 칸 → Invalid
  busy      제작이 DB에 머무는 동안 강화 · 제작은 Busy, 장착은 거부 → 제작 완료(분해로 빈 칸에 결과)
  enchant   강화가 그대로 된다(보호권 없음 0xFFFF) · 보호권 칸 30은 이제 칸(빈 칸 → Invalid)
  adena     그로시 0 → LackAdena
  bagfull   가방 가득 — 제작 BagFull · 분해 BagFull(비운 칸 하나에 둘이 나온다)
  storage   DB 반영 실패 → StorageError · 연결 닫힘 · 다시 들어오면 그대로(재료 · 그로시)
  economy   /economy(한 줄 JSON) · 하루 경계에 economy.log 한 줄 · [Economy] 요약 — 줄을 모두 더하면 이 시험이 한 일과 같다
"""
from __future__ import annotations

import argparse
import json
import os
import random
import select
import shutil
import socket
import string
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]

PROTOCOL_VERSION = 3
T_LOGIN, T_LOGIN_ACK = 35, 11
T_ITEM_LIST, T_SLOT_INF, T_DETAIL, T_ADENA = 44, 45, 71, 46
T_EQUIP_REQ, T_EQUIP_ACK = 5, 15
T_ENCHANT_REQ, T_ENCHANT_ACK = 97, 98
T_CRAFT_REQ, T_CRAFT_ACK = 112, 113
T_CS_CHAT, T_SC_CHAT = 88, 52
BAG_SLOTS, PAGE_SLOTS = 50, 30
NO_SLOT = 0xFFFF
BLACKSMITH = 2
HOME = (765, 757)
NEAR = [(766, 758), (764, 758), (765, 759), (763, 757), (767, 757)]
ADENA_CRAFT = 12                       # AdenaReason::Craft
OP_CRAFT, OP_DISMANTLE = 0, 1
(R_OK, R_INVALID, R_NOT_AT_BLACKSMITH, R_BUSY, R_DUPLICATE, R_NO_RECIPE, R_LACK_MATERIAL, R_LACK_ADENA, R_BAG_FULL,
 R_EQUIPPED, R_STORAGE_ERROR) = range(11)
RESULT_NAME = ["Ok", "Invalid", "NotAtBlacksmith", "Busy", "Duplicate", "NoRecipe", "LackMaterial", "LackAdena", "BagFull",
               "Equipped", "StorageError"]
E_INVALID, E_SUCCESS, E_EVAPORATED, E_PROTECTED, E_NOT_AT, E_BUSY = range(6)
ESSENCE, ESSENCE_MID, SHARD, CRYSTAL = 61, 62, 66, 64
COPPER_SWORD, IRON_SWORD, CHAIN_MAIL, STEEL_SWORD, SCROLL = 67, 3, 33, 4, 13
TRADED_BIT = 1 << 9
KIT = {0: (61, 100, 0), 1: (61, 20, 0), 2: (66, 50, 0), 3: (64, 30, 0), 4: (62, 100, 0),
       5: (3, 1, 0), 6: (33, 1, 0), 7: (4, 1, 1), 8: (13, 5, 0), 9: (67, 1, 0)}
TAG = "".join(random.choice(string.ascii_lowercase) for _ in range(2))
DONE: dict[str, int] = {"crafts": 0, "fee": 0, "ess_out": 0, "shard_out": 0, "dis_ess": 0, "dis_shard": 0, "dis_rare": 0}


class Bot:
    def __init__(self, port: int, name: str, cls: int = 0):
        self.name, self.port = name, port
        self.c = socket.create_connection(("127.0.0.1", port), timeout=5)
        self.c.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.buf = b""
        self.closed = False
        self.me = self.pos = None
        self.slots: dict[int, tuple[int, int, int]] = {}   # 칸 → (아이템, 수량, 착용)
        self.extra: dict[int, int] = {}
        self.pages: list[list[int]] = []                   # ITEM_LIST_ACK 쪽마다 칸 번호
        self.adena = None
        self.adena_inf: list[tuple[int, int, int]] = []    # (지금, 변화, 사유)
        self.craft: list[dict] = []
        self.enchant: list[dict] = []
        self.equip: list[tuple[int, int]] = []
        self.chat: list[tuple[str, str]] = []
        self.seq = 0
        self.eseq = 0
        self.c.sendall(struct.pack("<HB20s20sHB", 46, T_LOGIN, name.encode(), b"pw", PROTOCOL_VERSION, cls))
        end = time.time() + 5
        while self.me is None and time.time() < end:
            self.poll(0.2)
        if self.me is None:
            raise RuntimeError(f"{name}: 로그인 응답 없음")
        self.poll(0.6)

    def poll(self, seconds: float):
        end = time.time() + seconds
        while not self.closed:
            r, _, _ = select.select([self.c], [], [], max(0.0, end - time.time()))
            if r:
                try:
                    data = self.c.recv(1 << 16)
                except OSError:
                    data = b""
                if not data:
                    self.closed = True
                    return
                self.buf += data
                while len(self.buf) >= 3:
                    size = struct.unpack_from("<H", self.buf)[0]
                    if size < 3 or len(self.buf) < size:
                        break
                    p, self.buf = self.buf[:size], self.buf[size:]
                    self.on(p)
            if time.time() >= end:
                return

    def on(self, p: bytes):
        t = p[2]
        if t == T_LOGIN_ACK:
            self.me = p[3:19]
            self.pos = struct.unpack_from("<hh", p, 19)
        elif t == T_ITEM_LIST:
            page = []
            for i in range(min(p[3], PAGE_SLOTS)):
                slot, item, count, eq, _ = struct.unpack_from("<HHHBB", p, 4 + i * 8)
                page.append(slot)
                self.set_slot(slot, item, count, eq)
            self.pages.append(page)
        elif t == T_SLOT_INF:
            slot, item, count, eq, _ = struct.unpack_from("<HHHBB", p, 3)
            self.set_slot(slot, item, count, eq)
        elif t == T_DETAIL:
            slot, item, extra = struct.unpack_from("<HHQ", p, 3)
            self.extra[slot] = extra
        elif t == T_ADENA:
            adena, delta, reason = struct.unpack_from("<IiB", p, 3)
            self.adena = adena
            self.adena_inf.append((adena, delta, reason))
        elif t == T_CRAFT_ACK:
            op, result, seq, item, slot, fee, o0, o1, c0, c1 = struct.unpack_from("<BBIHHIHHHH", p, 3)
            self.craft.append({"op": op, "result": result, "seq": seq, "item": item, "slot": slot, "fee": fee,
                               "out": [(o0, c0), (o1, c1)]})
        elif t == T_ENCHANT_ACK:
            seq, result, slot, item, before, after = struct.unpack_from("<IBHHII", p, 3)
            self.enchant.append({"seq": seq, "result": result, "slot": slot, "item": item, "before": before, "after": after})
        elif t == T_EQUIP_ACK:
            ok, slot = struct.unpack_from("<BH", p, 3)   # success · ITEM_SLOT_DATA.slotIndex
            self.equip.append((slot, ok))
        elif t == T_SC_CHAT:
            sender = p[20:40].split(b"\0")[0].decode("utf-8", "replace")
            text = p[40:168].split(b"\0")[0].decode("utf-8", "replace")
            self.chat.append((sender, text))

    def set_slot(self, slot: int, item: int, count: int, eq: int):
        if item:
            self.slots[slot] = (item, count, eq)
        else:
            self.slots.pop(slot, None)
            self.extra.pop(slot, None)

    def send(self, data: bytes):
        self.c.sendall(data)

    def craft_req(self, op: int, item: int = 0, slot: int = 0, npc: int = BLACKSMITH, seq: int | None = None) -> int:
        if seq is None:
            self.seq += 1
            seq = self.seq
        self.send(struct.pack("<HBBIHHH", 14, T_CRAFT_REQ, op, seq, npc, item, slot))
        return seq

    def wait_craft(self, seq: int, seconds: float = 4.0) -> dict | None:
        end = time.time() + seconds
        while True:
            for a in self.craft:   # 연결이 닫히기 직전에 온 답도 본다(StorageError 뒤 서버가 닫는다)
                if a["seq"] == seq:
                    self.craft.remove(a)
                    self.poll(0.15)   # 뒤따르는 칸 · 그로시 알림
                    return a
            if time.time() >= end or self.closed:
                return None
            self.poll(0.1)

    def do(self, op: int, item: int = 0, slot: int = 0, npc: int = BLACKSMITH, seq: int | None = None) -> dict | None:
        return self.wait_craft(self.craft_req(op, item, slot, npc, seq))

    def enchant_req(self, ticket: int, target: int, protect: int = NO_SLOT) -> int:
        self.eseq += 1
        self.send(struct.pack("<HBIHHHH", 15, T_ENCHANT_REQ, self.eseq, BLACKSMITH, ticket, target, protect))
        return self.eseq

    def wait_enchant(self, seq: int, seconds: float = 4.0) -> dict | None:
        end = time.time() + seconds
        while True:
            for a in self.enchant:
                if a["seq"] == seq:
                    self.enchant.remove(a)
                    self.poll(0.15)
                    return a
            if time.time() >= end or self.closed:
                return None
            self.poll(0.1)

    def say(self, text: str):
        self.send(struct.pack("<HBB20s128s", 152, T_CS_CHAT, 0, b"", text.encode()))

    def close(self):
        try:
            self.c.close()
        except OSError:
            pass


class Checks:
    def __init__(self):
        self.items: list[tuple[bool, str]] = []

    def __call__(self, ok: bool, label: str):
        self.items.append((bool(ok), label))
        return ok

    def result(self) -> tuple[bool, str]:
        ok = all(o for o, _ in self.items)
        return ok, " · ".join(f"{label}:{'ok' if o else '✗'}" for o, label in self.items)


def rname(a: dict | None) -> str:
    return "응답 없음" if a is None else RESULT_NAME[a["result"]] if a["result"] < len(RESULT_NAME) else str(a["result"])


def near_bot(port: int, tag: str, cls: int = 0) -> Bot:
    """대장장이 곁에 세운다 — 걸을 수 없는 칸이면 서버가 마을로 보내므로 들어온 자리를 보고 다음 후보로"""
    for x, y in NEAR:
        name = f"lv20_xy{x}_{y}_{tag}"
        assert len(name.encode()) <= 19, name
        b = Bot(port, name, cls)
        if b.pos == (x, y):
            return b
        b.close()
        tag = tag[:-1] + random.choice(string.ascii_lowercase)
    raise RuntimeError("대장장이 곁에 설 칸이 없다")


def kit_matches(b: Bot) -> bool:
    return all(b.slots.get(s, (0, 0, 0)) == v for s, v in KIT.items())


def count_of(b: Bot, item: int) -> int:
    return sum(c for i, c, _ in b.slots.values() if i == item)


# ── 시나리오 ────────────────────────────────────────────────────────────────
STATE: dict[str, Bot] = {}


def main_bot(port: int) -> Bot:
    if "a" not in STATE or STATE["a"].closed:
        STATE["a"] = near_bot(port, f"CR{TAG}")
    return STATE["a"]


def scenario_pages(port: int):
    ck = Checks()
    a = main_bot(port)
    ck(len(a.pages) == 2, f"목록 {len(a.pages)}쪽")
    ck([len(p) for p in a.pages] == [PAGE_SLOTS, BAG_SLOTS - PAGE_SLOTS], f"쪽 크기 {[len(p) for p in a.pages]}")
    flat = [s for p in a.pages for s in p]
    ck(sorted(flat) == list(range(BAG_SLOTS)) and len(set(flat)) == BAG_SLOTS, "칸 0~49 한 번씩")
    ck(a.pages and a.pages[0] == list(range(PAGE_SLOTS)), "첫 쪽 = 0~29 순서")
    ck(kit_matches(a), "제작 꾸러미")
    ck(a.extra.get(6, 0) & TRADED_BIT, "거래 이력 비트(칸 6)")
    ck(a.adena == 50000, f"그로시 {a.adena}")
    return ck.result()


def scenario_craft(port: int):
    ck = Checks()
    a = main_bot(port)
    adena0 = a.adena
    # 1) 제작 — 하급 정수는 작은 묶음(칸 1: 20)부터, 결과는 가장 앞 빈 칸(10)
    a.adena_inf.clear()
    r = a.do(OP_CRAFT, COPPER_SWORD)
    ck(r and r["result"] == R_OK, f"제작 {rname(r)}")
    if r and r["result"] == R_OK:
        DONE["crafts"] += 1; DONE["fee"] += r["fee"]; DONE["ess_out"] += 15; DONE["shard_out"] += 2
    ck(r and (r["op"], r["item"], r["slot"], r["fee"]) == (OP_CRAFT, COPPER_SWORD, 10, 100), f"ACK {r}")
    ck(a.slots.get(1) == (ESSENCE, 5, 0) and a.slots.get(0) == (ESSENCE, 100, 0), f"작은 묶음부터 {a.slots.get(1)} {a.slots.get(0)}")
    ck(a.slots.get(2) == (SHARD, 48, 0), f"파편 {a.slots.get(2)}")
    ck(a.slots.get(10) == (COPPER_SWORD, 1, 0), f"결과 칸 10 {a.slots.get(10)}")
    ck(a.adena == adena0 - 100 and (adena0 - 100, -100, ADENA_CRAFT) in a.adena_inf, f"수수료 {a.adena_inf}")
    ck(a.extra.get(10, 0) & 0x7 == 0, "희귀도 = 바닥(일반)")
    # 2) 같은 seq · seq 0 → Duplicate(아무것도 바뀌지 않는다)
    before = dict(a.slots)
    r = a.do(OP_CRAFT, COPPER_SWORD, seq=a.seq)
    ck(r and r["result"] == R_DUPLICATE, f"같은 seq {rname(r)}")
    r = a.do(OP_CRAFT, COPPER_SWORD, seq=0)
    ck(r and r["result"] == R_DUPLICATE, f"seq 0 {rname(r)}")
    ck(a.slots == before, "Duplicate는 그대로")
    # 3) 두 번째 — 작은 묶음(칸 1: 5)을 다 쓰고 칸 0에서 10, 결과는 비워진 칸 1
    r = a.do(OP_CRAFT, COPPER_SWORD)
    ck(r and r["result"] == R_OK and r["slot"] == 1, f"둘째 제작 {rname(r)} 칸 {r and r['slot']}")
    if r and r["result"] == R_OK:
        DONE["crafts"] += 1; DONE["fee"] += r["fee"]; DONE["ess_out"] += 15; DONE["shard_out"] += 2
    ck(a.slots.get(1) == (COPPER_SWORD, 1, 0) and a.slots.get(0) == (ESSENCE, 90, 0), f"재료로 비운 칸에 결과 {a.slots.get(1)} {a.slots.get(0)}")
    ck(a.adena == adena0 - 200, f"그로시 {a.adena}")
    # 4) 거절들
    r = a.do(OP_CRAFT, 5)                         # 잉걸 장검(영웅-하) — 상급 정수 · 짙은 결정 없음
    ck(r and r["result"] == R_LACK_MATERIAL, f"재료 부족 {rname(r)}")
    r = a.do(OP_CRAFT, 2)                         # 연습용 목검(일반-하 — Craft 없음)
    ck(r and r["result"] == R_NO_RECIPE, f"제작 불가 장비 {rname(r)}")
    r = a.do(OP_CRAFT, 1)                         # 약초 물약
    ck(r and r["result"] == R_NO_RECIPE, f"소비품 {rname(r)}")
    r = a.do(OP_CRAFT, 9999)
    ck(r and r["result"] == R_INVALID, f"없는 아이템 {rname(r)}")
    r = a.wait_craft(a.craft_req(7, COPPER_SWORD))
    ck(r and r["result"] == R_INVALID, f"모르는 op {rname(r)}")
    ck(a.adena == adena0 - 200 and count_of(a, ESSENCE) == 90, "거절은 그대로")
    # 5) 먼 곳(마을 광장) · NPC 번호 0 — 제작 · 분해는 어디서나(2026-10-13: 소지품 「분해하기」 · 메뉴 「제작」)
    far = Bot(port, f"lv20_CRfar{TAG}")
    try:
        ck(far.pos is not None and max(abs(far.pos[0] - HOME[0]), abs(far.pos[1] - HOME[1])) > 4, f"먼 자리 {far.pos}")
        r = far.do(OP_CRAFT, COPPER_SWORD, npc=0)
        ck(r and r["result"] == R_OK, f"먼 곳 제작 {rname(r)}")
        if r and r["result"] == R_OK:
            DONE["crafts"] += 1; DONE["fee"] += r["fee"]; DONE["ess_out"] += 15; DONE["shard_out"] += 2
        r = far.do(OP_DISMANTLE, slot=5, npc=0)   # 철검(고급) — 파편 3 · 하급 정수 2
        ck(r and r["result"] == R_OK, f"먼 곳 분해 {rname(r)}")
        if r and r["result"] == R_OK:
            DONE["dis_rare"] += 1; DONE["dis_shard"] += 3; DONE["dis_ess"] += 2
    finally:
        far.close()
    return ck.result()


def scenario_dismantle(port: int):
    ck = Checks()
    a = main_bot(port)
    shard0, ess0 = count_of(a, SHARD), count_of(a, ESSENCE)
    shard_slot = next(s for s, v in sorted(a.slots.items()) if v[0] == SHARD)
    # 0) 클라가 고른 물건과 칸이 다르면(그 사이 정리 · 이동) 거절 — 칸 5는 철검(3)인데 강철 장검(4)이라고 보냄
    r = a.do(OP_DISMANTLE, STEEL_SWORD, slot=5)
    ck(r and r["result"] == R_INVALID and a.slots.get(5, (0,))[0] == IRON_SWORD, f"다른 물건 분해 거절 {rname(r)}")
    # 1) 철검(고급) — 파편 3 · 하급 정수 2가 기존 묶음에 합쳐진다(itemId 함께 — 지금 클라)
    r = a.do(OP_DISMANTLE, IRON_SWORD, slot=5)
    ck(r and r["result"] == R_OK, f"분해 {rname(r)}")
    ck(r and (r["item"], r["slot"], r["fee"], r["out"]) == (IRON_SWORD, 5, 0, [(SHARD, 3), (ESSENCE, 2)]), f"ACK {r}")
    if r and r["result"] == R_OK:
        DONE["dis_rare"] += 1; DONE["dis_shard"] += 3; DONE["dis_ess"] += 2
    ck(5 not in a.slots, "칸 5 비움")
    ck(a.slots.get(shard_slot) == (SHARD, shard0 + 3, 0), f"파편 묶음 {a.slots.get(shard_slot)}")
    ck(count_of(a, ESSENCE) == ess0 + 2 and a.slots.get(0, (0, 0))[1] == 92, f"정수 묶음 {a.slots.get(0)}")
    # 2) 거래 이력 사슬 갑옷 — 둘 다 절반(올림): 파편 2 · 정수 1
    r = a.do(OP_DISMANTLE, slot=6)
    ck(r and r["result"] == R_OK and r["out"] == [(SHARD, 2), (ESSENCE, 1)], f"거래 이력 절반 {rname(r)} {r and r['out']}")
    if r and r["result"] == R_OK:
        DONE["dis_rare"] += 1; DONE["dis_shard"] += 2; DONE["dis_ess"] += 1
    ck(6 not in a.slots and count_of(a, SHARD) == shard0 + 5 and count_of(a, ESSENCE) == ess0 + 3, "절반 반영")
    # 3) 거절들
    r = a.do(OP_DISMANTLE, slot=7)
    ck(r and r["result"] == R_EQUIPPED, f"착용 중 {rname(r)}")
    ck(a.slots.get(7) == (STEEL_SWORD, 1, 1), "착용 장검 그대로")
    r = a.do(OP_DISMANTLE, slot=0)
    ck(r and r["result"] == R_INVALID, f"재료 칸 {rname(r)}")
    r = a.do(OP_DISMANTLE, slot=40)
    ck(r and r["result"] == R_INVALID, f"빈 칸 {rname(r)}")
    r = a.do(OP_DISMANTLE, slot=60)
    ck(r and r["result"] == R_INVALID, f"범위 밖 {rname(r)}")
    return ck.result()


def scenario_busy(port: int):
    ck = Checks()
    a = main_bot(port)
    adena0, ess0, shard0 = a.adena, count_of(a, ESSENCE), count_of(a, SHARD)
    free = min(s for s in range(BAG_SLOTS) if s not in a.slots)
    # 제작을 보내고(DB에 300 ms 머문다) 답을 기다리지 않고 강화 · 제작 · 장착을 몰아 넣는다
    seq = a.craft_req(OP_CRAFT, COPPER_SWORD)
    eseq = a.enchant_req(8, 9)
    seq2 = a.craft_req(OP_CRAFT, COPPER_SWORD)
    sword = next(s for s, v in sorted(a.slots.items()) if v[0] == COPPER_SWORD and not v[2])
    a.send(struct.pack("<HBH", 5, T_EQUIP_REQ, sword))
    e = a.wait_enchant(eseq)
    ck(e and e["result"] == E_BUSY, f"제작 중 강화 {e and e['result']}")
    r2 = a.wait_craft(seq2)
    ck(r2 and r2["result"] == R_BUSY, f"제작 중 제작 {rname(r2)}")
    a.poll(0.2)
    ck((sword, 0) in a.equip, f"제작 중 장착 거부 {a.equip}")
    r = a.wait_craft(seq)
    ck(r and r["result"] == R_OK and r["slot"] == free, f"먼저 보낸 제작 {rname(r)} 칸 {r and r['slot']}(기대 {free})")
    if r and r["result"] == R_OK:
        DONE["crafts"] += 1; DONE["fee"] += r["fee"]; DONE["ess_out"] += 15; DONE["shard_out"] += 2
    ck(a.adena == adena0 - 100 and count_of(a, ESSENCE) == ess0 - 15 and count_of(a, SHARD) == shard0 - 2, "완료 반영")
    # 끝난 뒤에는 다시 된다
    a.send(struct.pack("<HBH", 5, T_EQUIP_REQ, sword))
    a.poll(0.4)
    ck((sword, 1) in a.equip, "끝난 뒤 장착")
    return ck.result()


def scenario_enchant(port: int):
    ck = Checks()
    a = main_bot(port)
    target = next(s for s, v in sorted(a.slots.items()) if v[0] == COPPER_SWORD and not v[2])
    scrolls = count_of(a, SCROLL)
    e = a.wait_enchant(a.enchant_req(8, target, NO_SLOT))
    ck(e and e["result"] in (E_SUCCESS, E_EVAPORATED), f"강화(보호권 없음 0xFFFF) {e}")
    ck(count_of(a, SCROLL) == scrolls - 1, "두루마리 하나")
    if e and e["result"] == E_EVAPORATED:
        DONE["break_none"] = DONE.get("break_none", 0) + 1
    target = next((s for s, v in sorted(a.slots.items()) if v[0] == COPPER_SWORD and not v[2]), None)
    if target is not None:
        e = a.wait_enchant(a.enchant_req(8, target, 30))
        ck(e and e["result"] == E_INVALID and count_of(a, SCROLL) == scrolls - 1, f"보호권 칸 30(빈 칸 — 이제 칸이다) {e and e['result']}")
    return ck.result()


def scenario_adena(port: int):
    ck = Checks()
    b = near_bot(port, f"CP{TAG}")
    try:
        ck(b.adena == 0 and kit_matches(b), f"그로시 0 꾸러미 {b.adena}")
        r = b.do(OP_CRAFT, COPPER_SWORD)
        ck(r and r["result"] == R_LACK_ADENA, f"그로시 부족 {rname(r)}")
        ck(kit_matches(b), "그대로")
    finally:
        b.close()
    return ck.result()


def scenario_bagfull(port: int):
    ck = Checks()
    b = near_bot(port, f"CF{TAG}")
    try:
        ck(len(b.slots) == BAG_SLOTS, f"가방 가득 {len(b.slots)}")
        r = b.do(OP_CRAFT, COPPER_SWORD)   # 재료 묶음이 남아 칸이 비지 않는다
        ck(r and r["result"] == R_BAG_FULL, f"제작 {rname(r)}")
        ck(kit_matches(b) and len(b.slots) == BAG_SLOTS, "그대로")
    finally:
        b.close()
    f = near_bot(port, f"FI{TAG}")
    try:
        ck(len(f.slots) == BAG_SLOTS, f"가방 가득(_FI) {len(f.slots)}")
        r = f.do(OP_DISMANTLE, slot=1)     # 철검 — 비운 칸 하나에 파편 · 정수 둘이 나온다
        ck(r and r["result"] == R_BAG_FULL, f"분해 {rname(r)}")
        ck(f.slots.get(1) == (IRON_SWORD, 1, 0), "철검 그대로")
    finally:
        f.close()
    return ck.result()


def scenario_storage(port: int):
    ck = Checks()
    b = near_bot(port, f"SF{TAG}")
    name = b.name
    try:
        ck(kit_matches(b) and b.adena == 50000, "꾸러미")
        r = b.do(OP_CRAFT, COPPER_SWORD)
        ck(r and r["result"] == R_STORAGE_ERROR, f"DB 실패 {rname(r)}")
        b.poll(1.5)
        ck(b.closed, "서버가 연결을 닫음")
    finally:
        b.close()
    time.sleep(1.0)
    c = Bot(port, name)
    try:
        ck(kit_matches(c) and 10 not in c.slots, "다시 들어오면 재료 그대로")
        ck(c.adena == 50000, f"그로시 그대로 {c.adena}")
    finally:
        c.close()
    return ck.result()


def scenario_economy(port: int, work: Path, period: int):
    ck = Checks()
    a = main_bot(port)
    a.chat.clear()
    a.say("/economy")
    a.poll(1.0)
    parts = [t for s, t in a.chat if s == "경제"]
    snap = None
    try:
        snap = json.loads("".join(parts))
    except ValueError:
        pass
    ck(snap is not None and {"adena", "gear", "material", "craftFee"} <= set(snap), f"/economy 한 줄 JSON({len(parts)}줄)")
    # 마지막 일 뒤로 하루 경계(시험은 period초)를 하나 넘기고 economy.log를 모두 더한다
    time.sleep(period + 2)
    log = work / "economy.log"
    lines = [json.loads(x) for x in log.read_text(encoding="utf-8").splitlines() if x.strip()] if log.exists() else []
    ck(lines, f"economy.log {len(lines)}줄")

    def total(path):
        s = 0
        for ln in lines:
            v = ln
            for k in path:
                v = v.get(k, {}) if isinstance(v, dict) else {}
            s += v if isinstance(v, int) else 0
        return s
    got = {
        "craftFee": total(["craftFee"]),
        "adena out Craft": total(["adena", "out", "Craft"]),
        "gear in Craft eNone": total(["gear", "in", "Craft", "eNone"]),
        "gear out Dismantle eRare": total(["gear", "out", "Dismantle", "eRare"]),
        "material out Craft 61": total(["material", "out", "Craft", "61"]),
        "material out Craft 66": total(["material", "out", "Craft", "66"]),
        "material in Dismantle 61": total(["material", "in", "Dismantle", "61"]),
        "material in Dismantle 66": total(["material", "in", "Dismantle", "66"]),
        "gear out EnchantBreak eNone": total(["gear", "out", "EnchantBreak", "eNone"]),
    }
    want = {
        "craftFee": DONE["fee"], "adena out Craft": DONE["fee"], "gear in Craft eNone": DONE["crafts"],
        "gear out Dismantle eRare": DONE["dis_rare"], "material out Craft 61": DONE["ess_out"],
        "material out Craft 66": DONE["shard_out"], "material in Dismantle 61": DONE["dis_ess"],
        "material in Dismantle 66": DONE["dis_shard"], "gear out EnchantBreak eNone": DONE.get("break_none", 0),
    }
    for k in want:
        ck(got[k] == want[k], f"{k} {got[k]}(기대 {want[k]})")
    ck(all("day" in ln and "period" in ln for ln in lines), "줄 머리(day · period)")
    server_log = (work / "server.log").read_text(encoding="utf-8", errors="replace")
    ck("[Economy] adena in=" in server_log, "[Economy] 요약")
    return ck.result()


def wait_port(port: int, seconds: float) -> bool:
    end = time.time() + seconds
    while time.time() < end:
        try:
            socket.create_connection(("127.0.0.1", port), timeout=0.5).close()
            return True
        except OSError:
            time.sleep(0.2)
    return False


def wait_login(port: int, seconds: float, name: str = "ready_probe") -> bool:
    """포트가 열린 뒤에도 Zone 초기화(몬스터 · 행동 트리)가 끝나야 로그인이 답한다 — linux-asan(Debug)은 10초 남짓 걸려
    첫 봇의 로그인 대기(5초)를 넘긴다. 버리는 캐릭터로 로그인이 될 때까지 다시 해 본다."""
    end = time.time() + seconds
    while time.time() < end:
        try:
            Bot(port, name).close()
            return True
        except (RuntimeError, OSError):
            time.sleep(0.5)
    return False


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--server", default=str(REPO / "build" / "linux-memory" / "GameServer"))
    ap.add_argument("--port", type=int, default=4300)
    ap.add_argument("--period", type=int, default=15, help="경제 로그 「하루」(초)")
    ap.add_argument("--keep", action="store_true", help="시험용 GameData 사본 · 서버 로그 · economy.log를 남긴다")
    ap.add_argument("--only", default="", help="쉼표로(pages,craft,dismantle,busy,enchant,adena,bagfull,storage,economy) — 앞 시나리오의 상태를 이어 쓴다")
    args = ap.parse_args()

    work = Path(tempfile.mkdtemp(prefix="harven_craft_"))
    shutil.copytree(REPO / "Bin" / "GameData", work / "GameData")
    env = dict(os.environ, HARVEN_ECONOMY_PERIOD_SEC=str(args.period), HARVEN_DB_BATCH_DELAY_MS="300",
               HARVEN_ECONOMY_LOG=str(work / "economy.log"))
    env.pop("HARVEN_MEMDB_JOURNAL", None)
    log = open(work / "server.log", "w")
    srv = subprocess.Popen([str(Path(args.server).resolve()), "--data-dir", str(work / "GameData"), "--port", str(args.port), "--dev"],
                           stdout=log, stderr=subprocess.STDOUT, cwd=work, env=env)
    results = []
    try:
        if not wait_port(args.port, 120):
            print("FAIL: 서버가 뜨지 않았다")
            return 1
        time.sleep(2.0)
        scenarios = [("pages", scenario_pages), ("craft", scenario_craft), ("dismantle", scenario_dismantle),
                     ("busy", scenario_busy), ("enchant", scenario_enchant), ("adena", scenario_adena),
                     ("bagfull", scenario_bagfull), ("storage", scenario_storage),
                     ("economy", lambda p: scenario_economy(p, work, args.period))]
        only = {s for s in args.only.split(",") if s}
        for name, fn in scenarios:
            if only and name not in only:
                continue
            try:
                ok, msg = fn(args.port)
            except (ConnectionError, OSError, RuntimeError, StopIteration) as e:
                ok, msg = False, f"연결 · 상태 문제 — {e!r}"
            results.append(ok)
            print(f"{'PASS' if ok else 'FAIL'} {name}: {msg}", flush=True)
            if srv.poll() is not None:
                print(f"FAIL: 서버가 죽었다(종료 코드 {srv.returncode})")
                return 1
        alive = srv.poll() is None
        print(f"{'PASS' if alive else 'FAIL'}: 서버 {'살아 있음' if alive else '죽음'}")
        return 0 if alive and all(results) else 1
    finally:
        for b in STATE.values():
            b.close()
        srv.terminate()
        try:
            srv.wait(10)
        except subprocess.TimeoutExpired:
            srv.kill()
        log.close()
        if args.keep:
            print(f"[keep] {work}")
        else:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
