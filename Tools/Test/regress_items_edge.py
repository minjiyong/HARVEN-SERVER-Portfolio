"""소지품 · 경제 경계 회귀 시험(아이템 감사 2026-10-13) — 서로 다른 계통(교환 · 상점 · 경매 · 제작 · 강화 · 줍기)이 같은 칸 · 그로시를
만질 때와 다시 접속했을 때를 실제 패킷으로 본다.

    python3 Tools/Test/regress_items_edge.py [--server build/linux-memory/GameServer] [--port 4380] [--only shop,locks] [--keep]

HARVEN_IN_MEMORY_DB=ON 빌드를 이 스크립트가 직접 띄운다(--dev). 캐릭터는 인메모리 DB 일지(HARVEN_MEMDB_JOURNAL)에 미리 적어
칸 · 그로시를 정확히 정한다(regress_transfer.py와 같은 방식 — 일지 줄 P · S). 시험용 GameData는 regress_economy.py의 사본
(일반 필드 장비 → 획득 귀속 · 1단계 일반 9999bp · 몬스터 HP 600 · 줍기 우선권 주인 2초/파티 4초)를 그대로 쓴다.

시나리오
  shop        착용 장비 판매 거부 · 지갑 상한을 넘는 판매 거부(물건 그대로) · 같은 seq Duplicate
  shopbound   상점에서 산 획득 귀속 장비(시험 사본의 목검 2)의 bit 8이 다시 접속해도 남는다(P_SHOP_TRADE는 Extra를 0으로 쓴다)
  locks       주기 중 내놓은 칸: 팔기 · 버리기 · 자리 바꿈 · 분해 · 강화 · 경매 모두 거부, 다른 칸은 된다 · 받는 장비 bit 9 ·
              주는 사람이 끊으면 취소 → 다시 들어오면 칸이 풀려 있다
  auction     착용 등록 거부 · 묶음 일부 등록 → 취소 → 되받기가 묶음에 합친다 · 가방 가득 · 지갑 상한이면 받기 거부
  sort        정리가 bit 8 · bit 9 · 강화를 따라 옮기고 묶음을 합친다 · 착용 장비를 칸 49로 옮겨도 다시 접속하면 그대로
  boundrelog  가방이 찬 주인의 귀속 장비가 발밑에 넘친 뒤 주인이 다시 접속해도(새 ObjID) 그 장비를 줍는다
  dualequip   옛 DB(2026-09-02 전 서버)에 무기 둘이 장착된 채면 공격력은 한 자루만 · 다른 무기를 장착하면 옛 장착이 모두 풀린다
  enchant     착용 무기 증발(+11) → 칸 · 착용 · 공격력 · 보호권 Protected · +0 → +1 · 다시 들어와도 단계 그대로
  pityrelog   희귀 천장(시험 사본 4): 일반 장비 셋 뒤 다시 접속해도 넷째는 희귀(천장 카운터가 로그아웃 때 저장된다)
  enchantsf   강화 DB 반영 실패(샌드박스 "_SF") — StorageError 응답이 연결을 닫기 전에 닿는다 · 다시 들어오면 그대로
"""
from __future__ import annotations

import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import regress_craft as rc        # noqa: E402 — 봇(칸 · 그로시 · 제작 · 강화)
import regress_transfer as rt     # noqa: E402 — 교환 요청
import regress_economy as rec     # noqa: E402 — 시험용 GameData(귀속 필드 장비) · 사냥 도우미
import regress_party as rp        # noqa: E402

REPO = rc.REPO
T_SHOP_BUY, T_SHOP_SELL, T_SHOP_ACK = 39, 40, 48
T_SWAP, T_DISCARD, T_DISCARD_ACK, T_EQUIP, T_UNEQUIP, T_SORT, T_USE = 7, 8, 18, 5, 6, 73, 10
T_PICKUP, T_PICKUP_ACK, T_SYSTEM = 9, 19, 32
T_AUC_REQ, T_AUC_ACK = 93, 94
T_USER_INFO = 42
SHOP_OK, SHOP_DUP, SHOP_NO_ADENA, SHOP_FULL, SHOP_FAR, SHOP_BAD_ITEM, SHOP_BAD_COUNT, SHOP_EQUIPPED, SHOP_STATE = range(9)
AUC_REGISTER, AUC_BUY, AUC_CANCEL, AUC_CLAIM = 1, 2, 3, 6
BLACKSMITH = 2
BOUND_BIT, TRADED_BIT = 1 << 8, 1 << 9


class Bot(rt.Bot):
    """regress_transfer.Bot + 상점 · 경매 · 칸 요청 · 시스템 알림"""

    def __init__(self, port, name):
        self.shop: list[dict] = []
        self.auc: list[dict] = []
        self.system: list[tuple[int, int, int]] = []
        self.discards: list[int] = []
        self.pickups: list[int] = []
        self.attack_stat = None
        self.sseq = 0
        self.aseq = 0
        super().__init__(port, name)

    def on(self, p):
        super().on(p)
        t = p[2]
        if t == T_SHOP_ACK:
            kind, result, seq, item, count, delta = struct.unpack_from("<BBIHHi", p, 3)
            self.shop.append({"kind": kind, "result": result, "seq": seq, "item": item, "count": count, "delta": delta})
        elif t == T_AUC_ACK:
            op, result, seq, id_, adena, delta, item, count = struct.unpack_from("<BBIIIiHH", p, 3)
            self.auc.append({"op": op, "result": result, "seq": seq, "id": id_, "item": item, "count": count})
        elif t == T_SYSTEM:
            self.system.append(struct.unpack_from("<Hii", p, 3))
        elif t == T_DISCARD_ACK:
            self.discards.append(p[3])
        elif t == T_PICKUP_ACK:
            self.pickups.append(p[3])
        elif t == T_USER_INFO:
            self.attack_stat = struct.unpack_from("<H", p, 16)[0]   # USER_INFO_INF.attack

    def wait_for(self, pred, seconds=3.0):
        end = time.time() + seconds
        while time.time() < end and not self.closed:
            v = pred()
            if v is not None:
                return v
            self.poll(0.05)
        return pred()

    def buy(self, item, count=1, npc=BLACKSMITH, seq=None):
        if seq is None:
            self.sseq += 1
            seq = self.sseq
        n = len(self.shop)
        self.send(struct.pack("<HBHHHI", 13, T_SHOP_BUY, npc, item, count, seq))
        return self.wait_for(lambda: next((a for a in self.shop[n:] if a["seq"] == seq and a["kind"] == 0), None))

    def sell(self, slot, count=1, npc=BLACKSMITH, seq=None):
        if seq is None:
            self.sseq += 1
            seq = self.sseq
        n = len(self.shop)
        self.send(struct.pack("<HBHHHI", 13, T_SHOP_SELL, npc, slot, count, seq))
        return self.wait_for(lambda: next((a for a in self.shop[n:] if a["seq"] == seq and a["kind"] == 1), None))

    def auction(self, op, slot=0, count=0, price=0, duration=0, id_=0, item=0):
        self.aseq += 1
        seq = self.aseq
        n = len(self.auc)
        self.send(struct.pack("<HBBI8BH26sHHIBIH", 59, T_AUC_REQ, op, seq, 0, 0, 0, 0, 0, 0, 0, 0, 0, b"",
                              slot, count, price, duration, id_, item))
        return self.wait_for(lambda: next((a for a in self.auc[n:] if a["seq"] == seq), None), 4.0)

    def swap(self, a, b):
        self.send(struct.pack("<HBHH", 7, T_SWAP, a, b))
        self.poll(0.25)

    def discard(self, slot, count=1):
        n = len(self.discards)
        self.send(struct.pack("<HBHH", 7, T_DISCARD, slot, count))
        return self.wait_for(lambda: self.discards[n] if len(self.discards) > n else None)

    def pickup(self):
        n = len(self.pickups)
        self.send(struct.pack("<HB", 3, T_PICKUP))
        return self.wait_for(lambda: self.pickups[n] if len(self.pickups) > n else None)

    def equip_req(self, slot):
        self.send(struct.pack("<HBH", 5, T_EQUIP, slot))
        self.poll(0.25)

    def sort(self):
        self.send(struct.pack("<HBB", 4, T_SORT, 0))
        self.poll(0.4)


class Checks(rc.Checks):
    pass


# ── 일지(인메모리 DB) 씨앗 ─────────────────────────────────────────────────
SEEDS: list[str] = []
NEXT_PID = [500]


def seed(name: str, x: int, y: int, adena: int, slots: dict[int, tuple], level: int = 20) -> str:
    """slots: 칸 → (아이템, 수량[, 착용[, 강화[, extra]]])"""
    pid = NEXT_PID[0]
    NEXT_PID[0] += 1
    assert len(name.encode()) <= 19, name
    SEEDS.append(f"P|{name.encode().hex()}|{b'pw'.hex()}|{pid}|{x}|{y}|{level}|0|{adena}\n")
    for s, v in slots.items():
        item, count, eq, ench, extra = (*v, 0, 0, 0)[:5]
        SEEDS.append(f"S|{pid}|{s}|1|{item}|{count}|{eq}|{ench}|{extra}\n")
    return name


SHOPPER = seed("ie_shop", 766, 758, 1_999_999_990, {0: (1, 98), 1: (3, 1, 1), 2: (33, 1), 3: (60, 5)})
LOCK_A = seed("ie_lockA", 766, 758, 100000, {0: (1, 20), 1: (3, 1), 2: (33, 1), 3: (13, 5), 4: (3, 1), 5: (4, 1, 1)})
LOCK_B = seed("ie_lockB", 767, 758, 100000, {0: (1, 10), 1: (2, 1)})
DC_A = seed("ie_dcA", 766, 758, 5000, {0: (1, 20), 1: (3, 1)})
DC_B = seed("ie_dcB", 767, 758, 5000, {})


def scenario_shop(port: int):
    """상점: 착용 장비 판매 거부 · 지갑 상한을 넘는 판매 거부(물건 그대로) · 같은 seq는 Duplicate"""
    ck = Checks()
    s = Bot(port, SHOPPER)
    try:
        r = s.sell(1)
        ck(r and r["result"] == SHOP_EQUIPPED, f"착용 판매 {r}")
        r = s.sell(2)
        ck(r and r["result"] == SHOP_BAD_COUNT and s.slots.get(2, (0,))[0] == 33, f"지갑 상한 판매 {r} {s.slots.get(2)}")
        r = s.buy(59)
        ck(r and r["result"] == SHOP_OK, f"구매 {r}")
        r = s.buy(59, seq=s.sseq)
        ck(r and r["result"] == SHOP_DUP, f"같은 seq {r}")
    finally:
        s.close()
    return ck.result()


def scenario_locks(port: int):
    """교환(주기) 중 내놓은 칸은 팔기 · 버리기 · 자리 바꿈 · 분해 · 강화 · 경매가 모두 막히고 다른 칸은 된다 · 끊으면 취소되고 칸이 풀린다"""
    ck = Checks()
    a, b = Bot(port, LOCK_A), Bot(port, LOCK_B)
    try:
        rt.start(a, b, rt.GIFT)
        rt.item(a, b, 4, 1)
        rt.item(a, b, 0, 5)
        r = a.sell(4)
        ck(r and r["result"] == SHOP_STATE, f"교환 칸 판매 {r}")
        ck(a.discard(4) == 0 and a.slots.get(4, (0,))[0] == 3, "교환 칸 버리기 거부")
        a.swap(4, 20)
        ck(a.slots.get(4, (0,))[0] == 3 and 20 not in a.slots, f"교환 칸 자리 바꿈 거부 {a.slots.get(4)} {a.slots.get(20)}")
        d = a.do(rc.OP_DISMANTLE, slot=4)
        ck(d and d["result"] == rc.R_BUSY, f"교환 중 분해 {rc.rname(d)}")
        e = a.wait_enchant(a.enchant_req(3, 4))
        ck(e and e["result"] == rc.E_BUSY, f"교환 중 강화 {e}")
        r = a.auction(AUC_REGISTER, slot=4, count=1, price=100, item=3)
        ck(r and r["result"] == 11, f"교환 중 경매 {r}")
        r = a.sell(2)
        ck(r and r["result"] == SHOP_OK, f"다른 칸 판매 {r}")
        ck(a.discard(1) == 1, "다른 칸 버리기")
        b.equip_req(1)
        ck(b.slots.get(1, (0, 0, 0))[2] == 0, f"받는 쪽 장착(교환 중) {b.slots.get(1)}")
        rt.finish(a, b)
        ck(a.transfer["phase"] == 4, f"완료 {a.transfer}")
        got = [s_ for s_, v in b.slots.items() if v[0] == 3]
        ck(got and b.extra.get(got[0], 0) & TRADED_BIT, f"받은 장비 bit 9 {got} {b.extra}")
        ck(rt.total(b, 1) == 15 and rt.total(a, 1) == 15, f"물약 {rt.total(a, 1)} {rt.total(b, 1)}")
    finally:
        a.close()
        b.close()
    a, b = Bot(port, DC_A), Bot(port, DC_B)
    try:
        rt.start(a, b, rt.GIFT)
        rt.item(a, b, 1, 1)
        a.close()
        rt.pump(b, duration=0.6)
        ck(b.transfer["phase"] == 5, f"끊김 → 취소 {b.transfer}")
        time.sleep(0.5)
        a2 = Bot(port, DC_A)
        try:
            ck(a2.slots.get(1, (0,))[0] == 3, f"다시 들어오면 그대로 {a2.slots}")
            r = a2.sell(1)
            ck(r and r["result"] == SHOP_OK, f"풀린 칸 판매 {r}")
        finally:
            a2.close()
    finally:
        b.close()
    return ck.result()


AU_S = seed("ie_aucS", 729, 757, 1_999_999_000, {0: (1, 20), 1: (3, 1), 2: (33, 1, 1)})
AU_B = seed("ie_aucB", 730, 756, 100000, {0: (1, 99), **{i: (3, 1) for i in range(1, 50)}})
SORTER = seed("ie_sort", 766, 758, 1000, {7: (3, 1, 0, 3, 512 | 2), 3: (33, 1, 1, 0, 256 | 1), 9: (1, 5), 1: (1, 7), 30: (61, 998), 31: (61, 5)})


def scenario_auction(port: int):
    """경매: 착용 등록 거부 · 묶음 일부 등록 → 취소 → 되받으면 묶음에 합친다 · 가방 가득 / 지갑 상한이면 받기 거부(정산함에 남는다)"""
    ck = Checks()
    s, b = Bot(port, AU_S), Bot(port, AU_B)
    try:
        ck(s.pos == (729, 757) and b.pos == (730, 756), f"경매인 곁 {s.pos} {b.pos}")
        r = s.auction(AUC_REGISTER, slot=2, count=1, price=1000, item=33)
        ck(r and r["result"] == 7, f"착용 등록 {r}")
        r = s.auction(AUC_REGISTER, slot=0, count=10, price=1000, item=1)
        ck(r and r["result"] == 0 and s.slots.get(0) == (1, 10, 0), f"묶음 일부 등록 {r} {s.slots.get(0)}")
        lid = r and r["id"]
        r = s.auction(AUC_REGISTER, slot=1, count=1, price=2000, item=3)
        ck(r and r["result"] == 0, f"철검 등록 {r}")
        lid2 = r and r["id"]
        r = s.auction(AUC_CANCEL, id_=lid)
        ck(r and r["result"] == 0, f"취소 {r}")
        r = s.auction(AUC_CLAIM, id_=0)
        ck(r and r["result"] == 0 and s.slots.get(0) == (1, 20, 0), f"되받기 묶음에 합침 {r} {s.slots.get(0)}")
        r = b.auction(AUC_BUY, price=2000, id_=lid2)
        ck(r and r["result"] == 0, f"구매 {r}")
        r = b.auction(AUC_CLAIM, id_=0)
        ck(r and r["result"] == 8, f"가방 가득 받기 {r}")
        r = s.auction(AUC_CLAIM, id_=0)
        ck(r and r["result"] == 8 and s.adena == 1_999_999_000 - 10 - 20, f"지갑 상한 받기 {r} {s.adena}")
    finally:
        s.close()
        b.close()
    return ck.result()


def scenario_sort(port: int):
    """정리: 귀속(bit 8) · 거래 이력(bit 9) · 강화가 따라가고 묶음을 합친다 · 착용 장비를 칸 49로 옮겨도 다시 접속하면 그대로"""
    ck = Checks()
    t = Bot(port, SORTER)
    try:
        before = {v[0]: t.extra.get(k, 0) for k, v in t.slots.items()}
        t.sort()
        after = {v[0]: t.extra.get(k, 0) for k, v in t.slots.items()}
        ck(before.get(3) == after.get(3) == 514 and before.get(33) == after.get(33) == 257, f"정리 뒤 extra {before} {after}")
        ck(sorted(t.slots) == list(range(len(t.slots))), f"앞 칸부터 {sorted(t.slots)}")
        ck(rc.count_of(t, 61) == 1003 and rc.count_of(t, 1) == 12, "수량 그대로")
        eq = [k for k, v in t.slots.items() if v[2]]
        t.swap(eq[0], 49)
        t.close()
        time.sleep(0.4)
        t = Bot(port, SORTER)
        ck(t.slots.get(49, (0, 0, 0))[2] == 1 and t.extra.get(49) == 257, f"착용 칸 49 다시 들어와도 {t.slots.get(49)} {t.extra.get(49)}")
    finally:
        t.close()
    return ck.result()


def scenario_boundrelog(port: int):
    """가방이 찬 주인의 귀속 장비가 발밑에 넘친 뒤 주인이 다시 접속하면(ObjID가 바뀐다) 그 장비를 여전히 주울 수 있어야 한다 —
    귀속 넘침은 주인만 줍는다(CanLoot)라 주인을 놓치면 누구도 못 줍고 2분 뒤 사라진다(보스 · 전설 장비가 증발)."""
    ck = Checks()
    name = f"lv9_xy751_1249_FI{rec.TAG}r"
    a = rec.Bot(port, name)
    drop = None
    try:
        rp.pump([a], 0.8)
        for _ in range(5):
            m = rp.pick_monster([a])
            if m is None or not rp.approach([a], m):
                continue
            t1 = time.time()
            if not rec.kill_staged([a], m, [(a, 0)]):
                continue
            rp.pump([a], 0.5)
            mx, my = a.monsters[m]["x"], a.monsters[m]["y"]
            near = [(oid, it) for oid, it in a.items.items()
                    if it["at"] >= t1 and max(abs(it["x"] - mx), abs(it["y"] - my)) <= 2 and rec.ITEMS.get(it["item"], {}).get("bind") == "eAcquire"]
            if near:
                drop = near[0]
                break
    finally:
        a.close()
    if not ck(drop is not None, "귀속 장비가 발밑으로 넘침"):
        return ck.result()
    oid, it = drop
    time.sleep(0.6)
    a2 = rec.Bot(port, name)
    try:
        rp.pump([a2], 0.6)
        ck(a2.me != a.me, "다시 접속하면 새 ObjID")
        ck(oid in a2.items, "넘친 장비가 아직 바닥에 있다")
        # 칸 하나를 비운다(떨어진 자리와 다른 칸에서 버린다 — 줍기가 버린 철검을 먼저 집지 않게)
        if tuple(a2.pos) == (it["x"], it["y"]):
            rp.walk_many([a2], it["x"] + 1, it["y"], [])
        slot = next(s for s, (item, _n) in sorted(a2.slots().items()) if item == 3)
        a2.c.sendall(struct.pack("<HBHH", 7, T_DISCARD, slot, 1))
        rp.pump([a2], 0.4)
        ck(rp.walk_many([a2], it["x"], it["y"], []), f"떨어진 자리로 {a2.pos} → {(it['x'], it['y'])}")
        t2 = time.time()
        a2.pickup()
        a2.wait(lambda: a2.packets(rp.T_PICKUP_ACK, t2), 2.0)
        acks = a2.packets(rp.T_PICKUP_ACK, t2)
        rp.pump([a2], 0.3)
        ck(acks and acks[-1][3] == 1 and any(i == it["item"] for i, _n in a2.slots().values()),
           f"다시 접속한 주인이 귀속 넘침을 줍는다 ack={acks and acks[-1][3]}")
    finally:
        a2.close()
    return ck.result()


DUAL = seed("ie_dual", 766, 758, 1000, {0: (3, 1, 1), 1: (4, 1, 1), 2: (88, 1)})
SOLO3 = seed("ie_solo3", 766, 758, 1000, {0: (3, 1, 1)})
SOLO4 = seed("ie_solo4", 766, 758, 1000, {1: (4, 1, 1)})
SOLO88 = seed("ie_solo88", 766, 758, 1000, {2: (88, 1, 1)})


def scenario_dualequip(port: int):
    """옛 DB(2026-09-02 전 서버는 무기를 여러 개 장착할 수 있었다)에 같은 부위가 둘 장착된 채 남아 있으면 — 능력치는 한 부위에 하나만
    센다(외형 · GetEquippedParts와 같은 칸), 다른 무기를 장착하면 그 부위의 옛 장착이 모두 풀린다"""
    ck = Checks()
    solo = {}
    for name, item in ((SOLO3, 3), (SOLO4, 4), (SOLO88, 88)):
        b = Bot(port, name)
        try:
            b.poll(0.3)
            solo[item] = b.attack_stat
        finally:
            b.close()
    d = Bot(port, DUAL)
    try:
        d.poll(0.3)
        ck(d.attack_stat in (solo[3], solo[4]), f"공격력 {d.attack_stat} — 한 자루만(철검 {solo[3]} · 강철 장검 {solo[4]})")
        d.equip_req(2)
        d.poll(0.3)
        weapons = sorted(v[0] for v in d.slots.values() if v[2])
        ck(weapons == [88], f"새 무기를 장착하면 옛 장착이 모두 풀린다 {weapons}")
        ck(d.attack_stat == solo[88], f"공격력 {d.attack_stat}(기대 {solo[88]})")
    finally:
        d.close()
    time.sleep(0.4)
    d = Bot(port, DUAL)
    try:
        weapons = sorted(v[0] for v in d.slots.values() if v[2])
        ck(weapons == [88], f"다시 들어와도 한 자루 {weapons}")
    finally:
        d.close()
    return ck.result()


SHOPBOUND = seed("ie_shopb", 766, 758, 50000, {})


def scenario_shopbound(port: int):
    """상점에서 산 획득 귀속 장비(시험 사본 — 연습용 목검 2가 일반 필드 장비라 eAcquire)의 bit 8이 다시 접속해도 남는다.
    P_SHOP_TRADE는 칸의 Extra를 0으로 쓴다(인메모리 DB도 같게) — 서버가 extra 있는 칸을 바로 뒤에 다시 써야 한다."""
    ck = Checks()
    b = Bot(port, SHOPBOUND)
    try:
        r = b.buy(2)
        ck(r and r["result"] == SHOP_OK, f"목검 구매 {r}")
        b.poll(0.3)
        slot = next((s_ for s_, v in b.slots.items() if v[0] == 2), None)
        ck(slot is not None and b.extra.get(slot, 0) & BOUND_BIT, f"산 직후 bit 8 {slot} {b.extra}")
    finally:
        b.close()
    time.sleep(0.5)
    b = Bot(port, SHOPBOUND)
    try:
        slot = next((s_ for s_, v in b.slots.items() if v[0] == 2), None)
        ck(slot is not None and b.extra.get(slot, 0) & BOUND_BIT, f"다시 들어와도 bit 8 {slot} {b.extra}")
        ck(b.adena == 50000 - 100, f"그로시 {b.adena}")
    finally:
        b.close()
    return ck.result()


ENCH = seed("ie_ench", 766, 758, 1000, {0: (3, 1, 1, 11), 1: (13, 3), 2: (60, 1), 3: (4, 1, 0, 12), 4: (67, 1)})


def scenario_enchant(port: int):
    """강화: 착용 무기 증발(+11 — 성공 몫 1조 분의 1) → 칸 · 착용 · 공격력이 함께 빠진다 · 보호권 → Protected(단계 그대로, 보호권 · 두루마리 하나씩) ·
    +0 → +1(100%) · 다시 들어와도 단계 그대로"""
    ck = Checks()
    e = Bot(port, ENCH)
    try:
        e.poll(0.3)
        armed = e.attack_stat
        r = e.wait_enchant(e.enchant_req(1, 0))
        ck(r and r["result"] == rc.E_EVAPORATED and 0 not in e.slots, f"착용 무기 증발 {r} {e.slots.get(0)}")
        e.poll(0.3)
        ck(e.attack_stat is not None and armed is not None and e.attack_stat < armed, f"공격력 {armed} → {e.attack_stat}")
        r = e.wait_enchant(e.enchant_req(1, 3, 2))
        ck(r and r["result"] == rc.E_PROTECTED and r["after"] == 12 and 2 not in e.slots and e.slots.get(1, (0, 0))[1] == 1,
           f"보호권 {r} 보호권 칸 {e.slots.get(2)} 두루마리 {e.slots.get(1)}")
        r = e.wait_enchant(e.enchant_req(1, 4))
        ck(r and r["result"] == rc.E_SUCCESS and r["after"] == 1 and 1 not in e.slots, f"+0 → +1 {r} 두루마리 {e.slots.get(1)}")
    finally:
        e.close()
    time.sleep(0.4)
    e = Bot(port, ENCH)
    try:
        ck(e.levels.get(3) == 12 and e.levels.get(4) == 1 and 0 not in e.slots, f"다시 들어와도 {e.levels} {sorted(e.slots)}")
    finally:
        e.close()
    return ck.result()


def scenario_pityrelog(port: int):
    """희귀 천장(시험 사본 4 — 1단계 야스나 초원: 일반 9999bp · 희귀 1bp): 천장 직전(받은 장비 수 % 4 == 3)에서 다시 접속해도
    다음 장비는 희귀(천장 카운터는 로그아웃 때 진행 기록에 저장된다). 각인탄이 길목의 다른 몬스터를 함께 쓰러뜨려 장비가 한 번에
    둘 이상 올 수 있어 「셋」이 아니라 「% 4 == 3」에서 멈춘다."""
    ck = Checks()
    name = f"lv40_xy751_1249_{rec.TAG}pr"
    pattern = (["eNone"] * 3 + ["eEpic"]) * 8

    def hunt_until(bot, t0, done, seconds):
        end = time.time() + seconds
        while time.time() < end and not done(bot.gear_got(t0)):
            m = rp.pick_monster([bot], seconds=10.0)
            if m is None or not rp.approach([bot], m):
                continue
            rec.kill_staged([bot], m, [(bot, 0)])
            rp.pump([bot], 0.6)
        return bot.gear_got(t0)
    a = rec.Bot(port, name)
    t0 = time.time()
    try:
        rp.pump([a], 0.8)
        gear = hunt_until(a, t0, lambda g: len(g) % 4 == 3, 150.0)
    finally:
        a.close()
    grades = [rec.ITEMS[i]["grade"] for i, _ in gear]
    if not ck(len(gear) % 4 == 3 and grades == pattern[:len(gear)], f"천장 직전까지 {grades}"):
        return ck.result()
    time.sleep(0.6)
    b = rec.Bot(port, name)
    t1 = time.time()
    try:
        rp.pump([b], 0.8)
        got = hunt_until(b, t1, lambda g: len(g) >= 1, 90.0)
        ck(got and rec.ITEMS[got[0][0]]["grade"] == "eEpic", f"다시 접속한 뒤 다음 장비는 희귀 {[rec.ITEMS[i]['grade'] for i, _ in got]}")
    finally:
        b.close()
    return ck.result()


ENCH_SF = seed("ie_enc_SF", 766, 758, 1000, {0: (13, 2), 1: (67, 1)})


def scenario_enchantsf(port: int):
    """강화 DB 반영 실패(샌드박스 "_SF") — StorageError 응답이 연결을 닫기 전에 닿는다(제작과 같은 CloseAfterFlush) · 다시 들어오면 그대로"""
    ck = Checks()
    e = Bot(port, ENCH_SF)
    try:
        seq = e.enchant_req(0, 1)
        r = e.wait_enchant(seq, 4.0)
        ck(r is not None and r["result"] == 7, f"StorageError 응답 {r}")
        e.poll(1.0)
        ck(e.closed, "서버가 연결을 닫음")
    finally:
        e.close()
    time.sleep(0.6)
    e = Bot(port, ENCH_SF)
    try:
        ck(e.slots.get(0) == (13, 2, 0) and e.slots.get(1) == (67, 1, 0) and e.levels.get(1, 0) == 0, f"다시 들어오면 그대로 {e.slots} {e.levels.get(1)}")
    finally:
        e.close()
    return ck.result()


SCENARIOS = [("shop", scenario_shop), ("shopbound", scenario_shopbound), ("locks", scenario_locks), ("auction", scenario_auction), ("sort", scenario_sort),
             ("boundrelog", scenario_boundrelog), ("dualequip", scenario_dualequip), ("enchant", scenario_enchant),
             ("pityrelog", scenario_pityrelog), ("enchantsf", scenario_enchantsf)]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--server", default=str(REPO / "build" / "linux-memory" / "GameServer"))
    ap.add_argument("--port", type=int, default=4380)
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--only", default="")
    args = ap.parse_args()
    work = Path(tempfile.mkdtemp(prefix="harven_items_"))
    rec.make_fixture(REPO / "Bin" / "GameData", work / "GameData")
    (work / "journal").write_text("".join(SEEDS))
    env = dict(os.environ, HARVEN_MEMDB_JOURNAL=str(work / "journal"), HARVEN_TRANSFER_EDIT_MS="20000")
    log = open(work / "server.log", "w")
    srv = subprocess.Popen([str(Path(args.server).resolve()), "--data-dir", str(work / "GameData"), "--port", str(args.port), "--dev"],
                           stdout=log, stderr=subprocess.STDOUT, cwd=work, env=env)
    results = []
    try:
        if not rc.wait_port(args.port, 120) or not rc.wait_login(args.port, 120, "ie_ready"):
            print("FAIL: 서버가 뜨지 않았다")
            return 1
        only = {s for s in args.only.split(",") if s}
        for name, fn in SCENARIOS:
            if only and name not in only:
                continue
            try:
                ok, msg = fn(args.port)
            except (ConnectionError, OSError, RuntimeError, AssertionError) as e:
                ok, msg = False, f"예외 — {e!r}"
            results.append(ok)
            print(f"{'PASS' if ok else 'FAIL'} {name}: {msg}", flush=True)
            if srv.poll() is not None:
                print(f"FAIL: 서버가 죽었다(종료 코드 {srv.returncode})")
                return 1
        alive = srv.poll() is None
        print(f"{'PASS' if alive else 'FAIL'}: 서버 {'살아 있음' if alive else '죽음'}")
        return 0 if alive and all(results) else 1
    finally:
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
