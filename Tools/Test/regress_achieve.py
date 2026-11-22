"""개인 업적 회귀 시험 — ACHIEVE_REQ · ACK · INF(121~123)와 서버가 세는 사건(처치 · 정예 · 보스 · 파티 · 레벨 · 쓰러짐 · 사냥 그로시 ·
장비 · 강화 · 제작 · 분해 · 정수 · 퀘스트 · 결사 · 경매 판매 · 구매 · 교환 · 지역)을 실제 패킷으로 본다.

    python3 Tools/Test/regress_achieve.py [--server build/linux-memory/GameServer] [--port 4400] [--only kill,bag] [--keep]

HARVEN_IN_MEMORY_DB=ON 빌드를 이 스크립트가 직접 띄운다(--dev — /economy). 봇 · 걷기 · 사냥 도우미는 regress_party · regress_economy ·
regress_craft · regress_guild · regress_transfer의 것을 쓴다(업적 패킷은 Ach 섞개가 받는다).
환경: HARVEN_DB_BATCH_DELAY_MS=300(제작 트랜잭션이 DB에 머무는 동안 받기가 Busy) · HARVEN_MEMDB_JOURNAL(지갑 상한 · 진행 기록 판 3 · 4 ·
  깨진 업적 절 캐릭터를 미리 넣고, 반영 차례를 읽는다) · HARVEN_DB_TRANSFER_DELAY_MS=3000(거래 커밋이 DB에 머무는 동안 처치).
시험용 GameData 사본(Bin/GameData + 아래):
  · Monster(보스 · 허수아비 밖) — HP 400 · 공격력 1 · 비선공 · 걸음 60초 · 경험치 1(레벨이 오르지 않게) · 그로시 100(100%)
  · 모로즈(29) — HP 400 · 공격력 1 · 비선공, MonsterAI 단계 · 기술 없음
  · LootConst — 정예 1000‰(3단계 이상 지역) · 정예 HP 100% · 레벨 차 200 · TierLoot 1 · 2단계 — 처치마다 희귀(eEpic) 필드 장비 · 정수 61 1000‰ · 결정 없음
  · QuestObjective · Quest — 퀘스트 4를 「올벤 숲(1)에서 아무 몬스터 1」 둘 · 보고 없이 완료(turn_in 0)
  · Achievement — 시험 시간을 줄이려 목표만 낮춘다: 2(처치) 100 → 3 · 8(파티) 100 → 2 · 25(그로시) 100000 → 300 · 40(정수) 500 → 3 · 45(경매 구매) 10 → 1
샌드박스 이름(Tools/Sandbox/MemDBConnection.cpp): "lvNN_xyX_Y_…" = 레벨 NN 캐릭터가 (X, Y)에서(로그인마다 그 자리) · "_FI" 가방 가득 ·
  "_ak" 경매 꾸러미(철검 +2) · "_CR" 제작 꾸러미 · "_Q<번호>" 그 퀘스트를 받은 채.

시나리오
  kill     로그인 Summary(소급 레벨) · 첫 처치 → Completed(1) + 시스템 채팅 + 요약 값 · 장비(희귀 → 고급 · 희귀 이상) · 정수 · 사냥 그로시 ·
           List(두 장 · 분야 거르기 · 잘못된 분야 · TooFast) · Claim(아이템 · 그로시 Achievement · Claimed INF · 상태 2) · 두 번 → AlreadyClaimed ·
           NotDone · 없는 업적 Invalid · 같은 seq → Duplicate(claimed = 마지막 seq) · ClaimAll · 다시 → NothingToClaim · /economy ·
           다시 접속: 진척 · 받음이 남고 seq는 1부터
  bag      가방 가득(_FI · Lv12 — 소급 5 · 10): Claim BagFull(아무것도 받지 않음) · 한 칸 비우면 ClaimAll 일부(하나 받고 하나 남김) · 또 비우면 받기
  wallet   지갑 상한(그로시 20억 — 저널로 넣은 캐릭터): 그로시 보상 Claim → BagFull(지갑) · 그로시는 그대로
  retro    Lv40 경매 꾸러미(철검 +2): 로그인 소급 — 레벨 5~40 · 강화 +1 · 진척(레벨 40/50 · 강화 2/5)
  party    파티 둘 + 모르는 사람 하나가 함께 두 마리: 파티 처치는 파티원만 · 처치는 셋 모두
  elite    캄파나 폐허(3단계 — 모두 정예): 첫 정예
  boss     모로즈: 보스 처치(종 29) — 「모로즈 토벌」
  death    PvP로 쓰러짐 → 「다시 일어서다」 · 마을에서 다시 서면 지역 하나 더
  craft    제작 · 분해 · 강화 +1 · 제작이 DB에 머무는 동안 받기 Busy → 끝난 뒤 받기
  quest    퀘스트 완료(보고 없이) → 「첫 의뢰」
  guild    결사 세우기 · 가입 → 「결사의 일원」(둘 다)
  auction  경매 구매(커밋) → 구매자 「경매장 단골」 · 판매 대금 받기 → 판 사람 「첫 판매」
  trade    교환 성립 → 「손에서 손으로」(둘 다)
  zone     걸어서 Zone 경계(0 → 1) · 길잡이 옛길로 15곳(다시 접속하며 — 들어가 본 지역이 남는다) → 16곳 모두(「하르벤 일주」 — 모든 지역에 갈 수 있다)
  tradeadena 거래(값 1,000) 커밋이 DB에 머무는 동안(HARVEN_DB_TRANSFER_DELAY_MS) 막타 — 미룬 사냥 그로시가 대금 뒤에 한 번 ·
           사냥 그로시 진척 = 받은 사냥 그로시(두 번 세지 않는다) · 판 대금은 사냥 그로시가 아니다
  frozen   업적 절이 잘린 판 4(일지로 넣음): Summary 0 · Claim · ClaimAll Invalid · 나갈 때 원본 절을 그대로 되쓴다 · 로그 한 줄 /
           모르는 kind의 통계 줄: 건너뛰고 읽는다(받음 비트가 남아 AlreadyClaimed) · 되쓴 기록에 그 줄이 그대로
  enchretro 판 4 기록(강화 2) + 가방에 철검 +9 → 강화 진척 2 그대로(다시 접속해도) / 판 3 + +9 → 한 번 소급(31~34) · 판 4로 저장
  claimorder 받기 차례(일지): 받음 비트를 담은 진행 기록(G)이 그 보상의 칸(S) · 그로시(A)보다 먼저 반영 · BagFull은 기록을 쓰지 않는다 ·
           가방에 두 칸만 비면 ClaimAll은 정확히 둘(마지막 기록의 받음 비트도 둘)
"""
from __future__ import annotations

import argparse
import json
import os
import random
import shutil
import string
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import regress_party as rp      # noqa: E402 — 봇 · 걷기 · 사냥
import regress_economy as rec   # noqa: E402 — 단계 처치 · 경매
import regress_craft as rc      # noqa: E402 — 제작 · 강화
import regress_guild as rg      # noqa: E402 — 결사
import regress_transfer as rt   # noqa: E402 — 교환

REPO = rp.REPO
T_ACH_REQ, T_ACH_ACK, T_ACH_INF = 121, 122, 123
OP_LIST, OP_CLAIM, OP_CLAIM_ALL = 0, 1, 2
(R_OK, R_INVALID, R_NOT_DONE, R_ALREADY, R_BAG_FULL, R_BUSY, R_DUPLICATE, R_TOO_FAST, R_NOTHING) = range(9)
RESULT = ["Ok", "Invalid", "NotDone", "AlreadyClaimed", "BagFull", "Busy", "Duplicate", "TooFast", "NothingToClaim"]
INF_SUMMARY, INF_PAGE, INF_COMPLETED, INF_CLAIMED = range(4)
CAT_HUNT, CAT_BOSS, CAT_GROWTH, CAT_GEAR, CAT_CRAFT, CAT_WORLD, CAT_ALL = 0, 1, 2, 3, 4, 5, 0xFF
ADENA_ACHIEVEMENT = 14            # AdenaReason::Achievement
T_DISCARD_REQ, T_DISCARD_ACK = 8, 18
T_TELEPORT_REQ, T_SHOP_ACK = 41, 48
T_RESTART = 36
T_SKILL = 80
HP, BOSS, BOSS_HP = 400, 29, 400
MATEO = (744, 769)                # 길잡이 마테오의 집
TAG = "".join(random.choice(string.ascii_lowercase) for _ in range(2))
ACH: dict[int, dict] = {}         # 시험 사본의 업적 표
TARGETS = {2: 3, 8: 2, 25: 300, 40: 3, 45: 1}
WORK = Path(".")                  # main이 정한다(서버 로그 · 일지)
JOURNAL = Path("journal")
FROZEN_SECTION = b""              # ach_frozen의 잘린 업적 절(main이 일지에 넣는다)
TRANSFER_DELAY_MS = 3000          # 거래 커밋을 DB가 붙드는 시간(tradeadena — 그동안 처치)


class Ach:
    """ACHIEVE_ACK · INF를 받고 ACHIEVE_REQ를 보내는 섞개 — 시험 봇 클래스마다 앞에 섞는다(class X(Ach, rp.Bot))"""

    def on(self, p: bytes):
        super().on(p)
        if p[2] in (T_ACH_ACK, T_ACH_INF):
            if not hasattr(self, "ach_log"):
                self.ach_log = []
            self.ach_log.append((time.time(), p))

    def _log(self):
        return getattr(self, "ach_log", [])

    def ach_send(self, op: int, seq: int = 0, ach: int = 0, cat: int = CAT_ALL):
        pkt = struct.pack("<HBBIHB", 11, T_ACH_REQ, op, seq, ach, cat)
        assert len(pkt) == 11
        self.c.sendall(pkt)

    def ach_acks(self, since: float = 0.0) -> list[dict]:
        out = []
        for at, p in self._log():
            if at >= since and p[2] == T_ACH_ACK:
                op, result, seq, ach, claimed, adena, item, count = struct.unpack_from("<BBIHHIHH", p, 3)
                out.append({"op": op, "result": result, "seq": seq, "ach": ach, "claimed": claimed, "adena": adena,
                            "item": item, "count": count})
        return out

    def ach_infs(self, kind: int | None = None, since: float = 0.0) -> list[dict]:
        out = []
        for at, p in self._log():
            if at < since or p[2] != T_ACH_INF or (kind is not None and p[3] != kind):
                continue
            k, points, done, total, claimable, page, pages, count = struct.unpack_from("<BIHHHBBB", p, 3)
            entries = [struct.unpack_from("<HIB", p, 17 + i * 7) for i in range(count)]
            out.append({"kind": k, "points": points, "done": done, "total": total, "claimable": claimable, "page": page,
                        "pages": pages, "count": count, "entries": entries})
        return out

    def ach_until(self, pred, seconds: float = 4.0) -> bool:
        end = time.time() + seconds
        while time.time() < end:
            if pred():
                return True
            self.poll(0.05)
        return bool(pred())

    def completed(self, since: float = 0.0) -> list[int]:
        return [i["entries"][0][0] for i in self.ach_infs(INF_COMPLETED, since) if i["count"] == 1]

    def wait_completed(self, ach_id: int, since: float, seconds: float = 4.0) -> bool:
        return self.ach_until(lambda: ach_id in self.completed(since), seconds)

    def summary(self, since: float = 0.0, seconds: float = 2.0) -> dict | None:
        self.ach_until(lambda: self.ach_infs(INF_SUMMARY, since), seconds)   # 로그인 뒤 한 번(다른 접속 패킷 뒤)
        s = self.ach_infs(INF_SUMMARY, since)
        return s[-1] if s else None

    def ack(self, op: int, seq: int = 0, ach: int = 0, cat: int = CAT_ALL, seconds: float = 4.0) -> dict | None:
        t = time.time()
        self.ach_send(op, seq, ach, cat)
        self.ach_until(lambda: [a for a in self.ach_acks(t) if a["op"] == op and a["seq"] == seq], seconds)
        got = [a for a in self.ach_acks(t) if a["op"] == op and a["seq"] == seq]
        return got[0] if got else None

    def listing(self, cat: int = CAT_ALL, seconds: float = 4.0) -> tuple[dict[int, tuple[int, int]], list[dict], dict | None]:
        """List → ({id: (진척, 상태)}, Page INF들, ACK(거절이면))"""
        t = time.time()
        self.ach_send(OP_LIST, 0, 0, cat)

        def done():
            pages = self.ach_infs(INF_PAGE, t)
            return (pages and len(pages) >= pages[0]["pages"]) or [a for a in self.ach_acks(t) if a["op"] == OP_LIST]
        self.ach_until(done, seconds)
        pages = self.ach_infs(INF_PAGE, t)
        acks = [a for a in self.ach_acks(t) if a["op"] == OP_LIST]
        rows = {e[0]: (e[1], e[2]) for pg in pages for e in pg["entries"]}
        return rows, pages, (acks[0] if acks else None)


class ABot(Ach, rp.Bot):
    pass


class EBot(Ach, rec.Bot):
    pass


class CBot(Ach, rc.Bot):
    pass


class GBot(Ach, rg.GBot):
    pass


class TBot(Ach, rt.Bot):
    pass


class Checks:
    def __init__(self):
        self.ok, self.notes = True, []

    def __call__(self, cond, label: str):
        self.notes.append(f"{label}:{'ok' if cond else 'FAIL'}")
        self.ok &= bool(cond)
        return bool(cond)

    def result(self):
        return self.ok, " · ".join(self.notes)


def points_of(ids) -> int:
    return sum(ACH[i]["points"] for i in ids)


def kill_one(bots, ck: Checks, stages=None, label="처치", others=(), seconds: float = 120.0) -> bool:
    """bots[0] 곁의 몬스터 하나를 bots가 stages(regress_economy.kill_staged)로 쓰러뜨린다"""
    got = rec.hunt(bots, stages or [(bots[0], 0)], 1, ck, seconds=seconds, others=others)
    return bool(got)


# ── 시나리오 ────────────────────────────────────────────────────────────────
def scenario_kill(port: int):
    ck = Checks()
    name = rp.nm(20, 749, 254, "k")
    a = ABot(port, name)
    try:
        s = a.summary()
        retro = [18, 19, 20]   # Lv20 — 레벨 5 · 10 · 20(로그인 소급)
        ck(s is not None and s["done"] == 3 and s["claimable"] == 3 and s["points"] == points_of(retro) and s["total"] == len(ACH),
           f"로그인 Summary(소급 Lv20) {s and (s['done'], s['claimable'], s['points'], s['total'])}")
        t0 = time.time()
        if not ck(kill_one([a], ck), "첫 처치"):
            return ck.result()
        ck(a.wait_completed(1, t0), "Completed(1)")
        first = next((i for i in a.ach_infs(INF_COMPLETED, t0) if i["entries"][0][0] == 1), None)
        ck(first and first["entries"][0] == (1, 1, 1) and first["count"] == 1 and first["pages"] == 1, f"Completed 줄 {first and first['entries']}")
        a.ach_until(lambda: {27, 28} <= set(a.completed(t0)), 3.0)
        done_now = retro + [i for i in a.completed(t0)]
        last = a.ach_infs(INF_COMPLETED, t0)[-1]
        ck(last["points"] == points_of(done_now) and last["done"] == len(done_now) and last["claimable"] == len(done_now),
           f"요약 값 {last['points']}/{points_of(done_now)} · {last['done']}/{len(done_now)}")
        ck({27, 28} <= set(a.completed(t0)) and 29 not in a.completed(t0), f"희귀 장비 → 고급 · 희귀 이상 {a.completed(t0)}")
        chats = [x[2] for x in rp.system_chats(a, t0)]
        ck(f"업적 달성: {ACH[1]['name']}" in chats, f"시스템 채팅 {chats[:3]}")
        # 두 마리 더 — 처치 3 · 정수 3 · 사냥 그로시 300. 길목에서 대신 맞아 쓰러진 몬스터도 이 봇의 처치다(regress_economy our_deaths) —
        # 첫 사냥에서 이미 셋이 될 수 있으니 달성은 t0부터 본다
        rec.hunt([a], [(a, 0)], 2, ck)
        a.ach_until(lambda: {2, 40, 25} <= set(a.completed(t0)), 4.0)
        ck({2, 40, 25} <= set(a.completed(t0)), f"처치 3 · 정수 3 · 그로시 300 → 2 · 40 · 25 {a.completed(t0)}")
        # List — 모두(두 장) · TooFast · 분야 · 잘못된 분야
        a.poll(0.6)
        rows, pages, rej = a.listing()
        ck(rej is None and len(pages) == 2 and [p["page"] for p in pages] == [0, 1] and [p["count"] for p in pages] == [30, len(ACH) - 30],
           f"목록 두 장 {[(p['page'], p['pages'], p['count']) for p in pages]}")
        ck(sorted(rows) == sorted(ACH), "목록 = 표의 업적 모두(id 순)")
        kills = rows.get(3, (0, 0))[0]
        ck(rows.get(1) == (1, 1) and rows.get(3, (0, 9))[1] == 0 and 3 <= kills < 1000, f"진척 — 1 {rows.get(1)} · 3 {rows.get(3)}")
        mine = [m for m, _d, k in rec.our_deaths(a, [a], t0) if k == a.me]
        ck(kills == len(mine), f"처치 진척 = 이 봇이 막타로 쓰러뜨린 몬스터(두 번 세지 않는다) {kills} = {len(mine)}")
        ck(rows.get(4, (0, 0))[0] == kills, "같은 키(처치)의 업적은 같은 진척")
        t2 = time.time()
        a.ach_send(OP_LIST, 0, 0, CAT_ALL)
        a.ach_until(lambda: [x for x in a.ach_acks(t2) if x["op"] == OP_LIST], 2.0)
        tf = [x for x in a.ach_acks(t2) if x["op"] == OP_LIST]
        ck(tf and tf[0]["result"] == R_TOO_FAST, f"500ms 안 List → TooFast {tf and RESULT[tf[0]['result']]}")
        a.poll(0.6)
        rows_b, pages_b, _ = a.listing(CAT_BOSS)
        ck(len(pages_b) == 1 and sorted(rows_b) == sorted(i for i, r in ACH.items() if r["category"] == "eBoss"), f"분야 보스 {sorted(rows_b)}")
        a.poll(0.6)
        _, pages_x, rej_x = a.listing(9)
        ck(not pages_x and rej_x and rej_x["result"] == R_INVALID, f"분야 9 → Invalid {rej_x and RESULT[rej_x['result']]}")
        # Claim 1 — 아이템 1 × 10 · 그로시 100(Achievement)
        t3 = time.time()
        r = a.ack(OP_CLAIM, 1, 1)
        ck(r and r["result"] == R_OK and r["claimed"] == 1 and r["adena"] == 100 and (r["item"], r["count"]) == (1, 10), f"Claim 1 {r}")
        a.ach_until(lambda: a.ach_infs(INF_CLAIMED, t3), 2.0)
        cl = a.ach_infs(INF_CLAIMED, t3)
        ck(cl and cl[0]["entries"] == [(1, 1, 2)] and cl[0]["kind"] == INF_CLAIMED, f"Claimed INF {cl and (cl[0]['entries'], cl[0]['claimable'])}")
        slots = [struct.unpack_from("<HHH", p, 3) for p in a.packets(45, t3)]   # ITEM_SLOT_INF: 칸 · 아이템 · 수량
        ck(any(item == 1 and count >= 10 for _, item, count in slots), f"보상 아이템이 가방에(약초 물약 10) {slots}")
        ad = [struct.unpack_from("<IiB", p, 3) for p in a.packets(rp.T_ADENA, t3)]
        ck(any(d == 100 and why == ADENA_ACHIEVEMENT for _, d, why in ad), f"ADENA_INF +100 Achievement {ad}")
        # 두 번 · 달성 전 · 없는 업적 · 같은 seq · seq 0
        r = a.ack(OP_CLAIM, 2, 1)
        ck(r and r["result"] == R_ALREADY, f"두 번 받기 → AlreadyClaimed {r and RESULT[r['result']]}")
        r = a.ack(OP_CLAIM, 3, 4)
        ck(r and r["result"] == R_NOT_DONE, f"달성 전 → NotDone {r and RESULT[r['result']]}")
        r = a.ack(OP_CLAIM, 4, 999)
        ck(r and r["result"] == R_INVALID, f"없는 업적 → Invalid {r and RESULT[r['result']]}")
        r = a.ack(OP_CLAIM, 4, 2)
        ck(r and r["result"] == R_DUPLICATE and r["claimed"] == 4, f"같은 seq → Duplicate(claimed = 4) {r}")
        r = a.ack(OP_CLAIM_ALL, 0)
        ck(r and r["result"] == R_DUPLICATE, "seq 0 → Duplicate")
        # ClaimAll — 남은 것 모두(그로시 합 · 첫 아이템)
        a.poll(0.6)
        before, _, _ = a.listing()
        claimable = sorted(i for i, (_, st) in before.items() if st == 1)
        t4 = time.time()
        r = a.ack(OP_CLAIM_ALL, 5)
        want_adena = sum(ACH[i]["reward_adena"] for i in claimable)
        first_item = next((ACH[i]["reward_item"] for i in claimable if ACH[i]["reward_item"]), 0)
        ck(r and r["result"] == R_OK and r["claimed"] == len(claimable) and r["adena"] == want_adena and r["item"] == first_item,
           f"ClaimAll {r and (RESULT[r['result']], r['claimed'], r['adena'], r['item'])} 기대 ({len(claimable)}, {want_adena}, {first_item})")
        a.ach_until(lambda: a.ach_infs(INF_CLAIMED, t4), 2.0)
        cl = a.ach_infs(INF_CLAIMED, t4)
        ck(cl and sorted(e[0] for c in cl for e in c["entries"]) == claimable and all(e[2] == 2 for c in cl for e in c["entries"])
           and cl[-1]["claimable"] == 0, "Claimed INF — 받은 것 모두 상태 2 · 받을 것 0")
        r = a.ack(OP_CLAIM_ALL, 6)
        ck(r and r["result"] == R_NOTHING, f"다시 ClaimAll → NothingToClaim {r and RESULT[r['result']]}")
        # 경제 로그 — 그로시 Achievement
        t5 = time.time()
        a.chat(0, "/economy")
        a.wait(lambda: [p for p in a.packets(rp.T_SC_CHAT, t5) if rp.cstr(p[20:40]) == "경제"], 3.0)
        a.poll(0.5)
        econ = "".join(rp.cstr(p[40:168]) for p in a.packets(rp.T_SC_CHAT, t5) if rp.cstr(p[20:40]) == "경제")
        ck('"Achievement"' in econ, "경제 로그에 Achievement")
        s_before = a.ach_infs(INF_CLAIMED)[-1]
        a.poll(0.3)
        kills_end = len([m for m, _d, k in rec.our_deaths(a, [a], t0) if k == a.me])   # 받는 동안 다가온 몬스터를 반격해 쓰러뜨렸을 수 있다
        a.close()
        # 다시 접속 — 진척 · 받음이 남는다, seq는 1부터
        time.sleep(0.5)
        b = ABot(port, name)
        try:
            s = b.summary()
            ck(s and (s["points"], s["done"], s["claimable"]) == (s_before["points"], s_before["done"], 0),
               f"다시 접속 Summary {s and (s['points'], s['done'], s['claimable'])} = {(s_before['points'], s_before['done'], 0)}")
            r = b.ack(OP_CLAIM, 1, 1)
            ck(r and r["result"] == R_ALREADY, f"새 접속 seq 1 → 처리(AlreadyClaimed — Duplicate 아님) {r and RESULT[r['result']]}")
            r = b.ack(OP_CLAIM, 1, 1)
            ck(r and r["result"] == R_DUPLICATE and r["claimed"] == 1, "새 접속에서 같은 seq → Duplicate(1)")
            rows2, _, _ = b.listing()
            ck(rows2.get(3, (0, 9))[0] == kills_end and rows2.get(1) == (1, 2) and all(rows2[i][1] == 2 for i in claimable),
               f"진척 · 받음 남음 — 3 {rows2.get(3)} = 처치 {kills_end} · 1 {rows2.get(1)}")
        finally:
            b.close()
        return ck.result()
    finally:
        a.close()


def scenario_bag(port: int):
    ck = Checks()
    a = EBot(port, f"lv12_FI{TAG}b")
    try:
        s = a.summary()
        ck(s and s["claimable"] == 2 and s["done"] == 2, f"Lv12 소급 5 · 10 {s and (s['done'], s['claimable'])}")
        slots = a.slots()
        ck(len(slots) == 50, f"가방 가득 {len(slots)}칸")
        t0 = time.time()
        r = a.ack(OP_CLAIM, 1, 18)
        ck(r and r["result"] == R_BAG_FULL, f"가방 가득 → BagFull {r and RESULT[r['result']]}")
        a.poll(0.3)
        ck(not [p for p in a.packets(rp.T_ADENA, t0) if p[11] == ADENA_ACHIEVEMENT], "BagFull — 그로시도 받지 않았다")
        a.discard(1, 1)
        a.wait(lambda: a.packets(rec.T_DISCARD_ACK, t0), 3.0)
        t1 = time.time()
        r = a.ack(OP_CLAIM_ALL, 2)
        ck(r and r["result"] == R_OK and r["claimed"] == 1 and r["adena"] == ACH[18]["reward_adena"] and r["item"] == ACH[18]["reward_item"],
           f"한 칸 비우고 ClaimAll — 하나만 {r}")
        a.ach_until(lambda: a.ach_infs(INF_CLAIMED, t1), 2.0)
        cl = a.ach_infs(INF_CLAIMED, t1)
        ck(cl and cl[0]["entries"] == [(18, 5, 2)] and cl[0]["claimable"] == 1, f"Claimed — 18 · 남은 것 1 {cl and (cl[0]['entries'], cl[0]['claimable'])}")
        a.poll(0.3)
        ck(a.slots().get(1) == (ACH[18]["reward_item"], ACH[18]["reward_count"]), f"빈 칸에 보상 {a.slots().get(1)}")
        r = a.ack(OP_CLAIM, 3, 19)
        ck(r and r["result"] == R_BAG_FULL, "19 — 아직 BagFull")
        t2 = time.time()
        a.discard(2, 1)
        a.wait(lambda: a.packets(rec.T_DISCARD_ACK, t2), 3.0)
        r = a.ack(OP_CLAIM, 4, 19)
        ck(r and r["result"] == R_OK and r["adena"] == ACH[19]["reward_adena"], f"비운 뒤 19 받기 {r}")
        return ck.result()
    finally:
        a.close()


def scenario_wallet(port: int):
    ck = Checks()
    a = EBot(port, "ach_wallet_cap")   # 저널로 넣은 Lv12 · 그로시 20억
    try:
        s = a.summary()
        ck(s and s["claimable"] == 2, f"Lv12 소급 {s and s['claimable']}")
        t0 = time.time()
        r = a.ack(OP_CLAIM, 1, 18)
        ck(r and r["result"] == R_BAG_FULL, f"지갑 상한 → BagFull {r and RESULT[r['result']]}")
        a.poll(0.3)
        ck(not [p for p in a.packets(rp.T_ADENA, t0) if p[11] == ADENA_ACHIEVEMENT] and ACH[18]["reward_item"] not in [i for i, _ in a.slots().values()],
           "아무것도 받지 않았다(그로시 · 아이템)")
        rows, _, _ = a.listing(CAT_GROWTH)
        ck(rows.get(18, (0, 0))[1] == 1, "여전히 받을 수 있다(상태 1)")
        return ck.result()
    finally:
        a.close()


def scenario_retro(port: int):
    ck = Checks()
    a = ABot(port, f"lv40_ak{TAG}r")
    try:
        s = a.summary()
        want = [18, 19, 20, 21, 22, 31]
        ck(s and s["done"] == len(want) and s["claimable"] == len(want) and s["points"] == points_of(want),
           f"소급 — 레벨 5~40 · 강화 +1 {s and (s['done'], s['claimable'], s['points'])}")
        ck(not a.ach_infs(INF_COMPLETED), "로그인 소급은 Completed 없이")
        rows, _, _ = a.listing(CAT_GROWTH)
        ck(all(rows.get(i, (0, 0))[1] == 1 for i in (18, 19, 20, 21, 22)) and rows.get(23) == (40, 0), f"레벨 40/50 {rows.get(23)}")
        a.poll(0.6)
        rows, _, _ = a.listing(CAT_GEAR)
        ck(rows.get(31) == (1, 1) and rows.get(32) == (2, 0), f"강화 +2 — 31 {rows.get(31)} · 32 {rows.get(32)}")
        return ck.result()
    finally:
        a.close()


def scenario_party(port: int):
    ck = Checks()
    a, b = ABot(port, rp.nm(20, 749, 251, "a")), ABot(port, rp.nm(20, 750, 251, "b"))
    c = ABot(port, rp.nm(20, 748, 251, "c"))   # 파티 밖 — 함께 때린다
    bots = [a, b, c]
    try:
        ck(rp.form_party(a, b), "파티 a · b")
        rp.pump(bots, 0.5)
        t0 = time.time()
        rec.hunt([a, b, c], [(c, 1), (b, 1), (a, 0)], 2, ck)
        rp.pump(bots, 1.0)
        ck(a.wait_completed(8, t0) and b.wait_completed(8, t0), "파티원 둘 — 「함께 사냥」(파티 처치 2)")
        ck(8 not in c.completed(t0), "파티 밖은 파티 처치를 세지 않는다")
        rows, _, _ = c.listing(CAT_HUNT)
        ck(rows.get(8, (9, 9))[0] == 0 and rows.get(2, (0, 0))[0] >= 2, f"c — 파티 처치 0 · 처치 {rows.get(2)}")
        return ck.result()
    finally:
        for x in bots:
            x.close()


def scenario_elite(port: int):
    ck = Checks()
    a = ABot(port, f"lv40_xy1249_751_{TAG}")
    try:
        rp.pump([a], 1.0)
        t0 = time.time()
        kill_one([a], ck)
        ck(a.wait_completed(5, t0), "「첫 정예」")
        return ck.result()
    finally:
        a.close()


def scenario_boss(port: int):
    ck = Checks()
    a = ABot(port, rp.nm(40, 263, 268, "o"))
    try:
        rp.pump([a], 1.0)
        boss = next((o for o in a.monsters if o[1] == BOSS), None)
        if not ck(boss is not None, "보스가 보인다"):
            return ck.result()
        bm = a.monsters[boss]
        if max(abs(a.pos[0] - bm["x"]), abs(a.pos[1] - bm["y"])) > 5:
            ck(rp.approach([a], boss), "보스 곁으로")
        t0 = time.time()
        ck(rec.kill_staged([a], boss, [(a, 0)], seconds=60.0), "모로즈 처치")
        ck(a.wait_completed(10, t0), f"「모로즈 토벌」 {a.completed(t0)}")
        rows, _, _ = a.listing(CAT_BOSS)
        ck(rows.get(16, (0, 0))[0] == 1, f"보스 처치(아무 보스) 1/10 {rows.get(16)}")
        return ck.result()
    finally:
        a.close()


def scenario_death(port: int):
    ck = Checks()
    k = ABot(port, rp.nm(40, 749, 256, "x"))
    v = ABot(port, rp.nm(1, 750, 256, "v"))
    try:
        rp.pump([k, v], 1.0)
        t0 = time.time()
        end = time.time() + 20
        seq = 0
        while time.time() < end and not v.packets(rp.T_DIE, t0):
            seq += 1
            # flags 1 = 강제 공격(SKILLREQ_FORCE — Ctrl, 다른 플레이어도 맞힌다)
            pkt = struct.pack("<HBHBB", 30, T_SKILL, 7, 0, 1) + rp.pack_obj(v.me) + struct.pack("<hhBH", v.pos[0], v.pos[1], 0, seq)
            k.c.sendall(pkt)
            rp.pump([k, v], 0.6)
        died = [p for p in v.packets(rp.T_DIE, t0) if rp.objid(p, 3) == v.me]
        if not ck(died, "PvP로 쓰러짐"):
            return ck.result()
        ck(v.wait_completed(24, t0), "「다시 일어서다」(쓰러짐 1)")
        t1 = time.time()
        v.c.sendall(struct.pack("<HB", 3, T_RESTART))
        v.wait(lambda: v.packets(rp.T_RESPAWN, t1), 4.0)
        rp.pump([v], 1.0)
        v.poll(0.6)
        rows, _, _ = v.listing(CAT_WORLD)
        ck(rows.get(47, (0, 0))[0] == 2, f"마을에서 다시 서면 지역 2(올벤 숲 · 데니차) {rows.get(47)}")
        return ck.result()
    finally:
        k.close()
        v.close()


def scenario_craft(port: int):
    ck = Checks()
    a = None
    for x, y in rc.NEAR:
        cand = CBot(port, f"lv20_xy{x}_{y}_CRc")   # 19바이트 안 — 서버마다 새 메모리 DB라 꼬리표가 없어도 겹치지 않는다
        if cand.pos == (x, y):
            a = cand
            break
        cand.close()
    if not ck(a is not None, "대장장이 곁"):
        return ck.result()
    try:
        t0 = time.time()
        r = a.do(rc.OP_CRAFT, rc.COPPER_SWORD)
        ck(r and r["result"] == rc.R_OK, f"제작 {rc.rname(r)}")
        ck(a.wait_completed(35, t0), "「첫 제작」")
        r = a.do(rc.OP_DISMANTLE, slot=5, item=rc.IRON_SWORD)
        ck(r and r["result"] == rc.R_OK, f"분해 {rc.rname(r)}")
        ck(a.wait_completed(38, t0), "「첫 분해」")
        e = a.wait_enchant(a.enchant_req(8, 9))
        ck(e and e["result"] == rc.E_SUCCESS and e["after"] == 1, f"강화 +1 {e}")
        ck(a.wait_completed(31, t0), "「첫 강화」")
        # 제작이 DB에 머무는 동안(300ms) 받기 → Busy, 끝난 뒤 받기
        seq = a.craft_req(rc.OP_CRAFT, rc.COPPER_SWORD)
        r = a.ack(OP_CLAIM, 1, 35, seconds=2.0)
        ck(r and r["result"] == R_BUSY, f"제작 중 받기 → Busy {r and RESULT[r['result']]}")
        c2 = a.wait_craft(seq)
        ck(c2 and c2["result"] == rc.R_OK, f"그 제작은 끝난다 {rc.rname(c2)}")
        r = a.ack(OP_CLAIM, 2, 35)
        ck(r and r["result"] == R_OK, f"끝난 뒤 받기 {r and RESULT[r['result']]}")
        return ck.result()
    finally:
        a.close()


def scenario_quest(port: int):
    ck = Checks()
    a = ABot(port, f"lv40_xy749_254_Q4{TAG}")
    try:
        rp.pump([a], 1.0)
        t0 = time.time()
        kill_one([a], ck)
        ck(a.wait_completed(41, t0, 5.0), f"퀘스트 완료 → 「첫 의뢰」 {a.completed(t0)}")
        return ck.result()
    finally:
        a.close()


def scenario_guild(port: int):
    ck = Checks()
    lead = GBot(port, rp.nm(20, *rg.HALL, "G"))
    mem = GBot(port, rp.nm(20, 749, 760, "M"))
    try:
        t0 = time.time()
        gid = rg.make_guild(lead, f"업적{TAG}", mem)
        ck(gid, "결사 세우기 · 가입")
        ck(lead.wait_completed(43, t0), "세운 사람 「결사의 일원」")
        ck(mem.wait_completed(43, t0), "들어온 사람 「결사의 일원」")
        # 다시 접속 — 결사원이면 적어도 1(두 번 세지 않는다)
        name = lead.name
        lead.close()
        time.sleep(0.4)
        lead = GBot(port, name)
        lead.poll(1.0)
        rows, _, _ = lead.listing(CAT_WORLD)
        ck(rows.get(43) == (1, 1), f"다시 접속 — 결사 가입 1 · 달성 {rows.get(43)}")
        return ck.result()
    finally:
        lead.close()
        mem.close()


def scenario_auction(port: int):
    ck = Checks()
    seller = EBot(port, "lv20_xy729_757_CRs")
    buyer = EBot(port, "lv20_xy730_756_CRu")
    bots = [seller, buyer]
    try:
        rp.pump(bots, 1.0)
        reg = seller.auction_ack(seller.auction(rec.AUC_REGISTER, slot=5, count=1, price=500, item=3))
        if not ck(reg is not None and reg["result"] == rec.AR_OK, f"철검 등록 {reg}"):
            return ck.result()
        t0 = time.time()
        buy = buyer.auction_ack(buyer.auction(rec.AUC_BUY, price=500, id_=reg["id"]))
        ck(buy is not None and buy["result"] == rec.AR_OK, f"구매 {buy}")
        ck(buyer.wait_completed(45, t0), "구매자 「경매장 단골」(구매 1 — 시험 목표)")
        ck(44 not in seller.completed(t0), "대금을 받기 전에는 판매를 세지 않는다")
        claim = seller.auction_ack(seller.auction(rec.AUC_CLAIM, id_=0))
        ck(claim is not None and claim["result"] == rec.AR_OK, f"판매 대금 받기 {claim}")
        ck(seller.wait_completed(44, t0), "판 사람 「첫 판매」")
        return ck.result()
    finally:
        for x in bots:
            x.close()


def scenario_trade(port: int):
    ck = Checks()
    a = TBot(port, "lv20_xy766_758_CRt")
    b = TBot(port, "lv20_xy767_758_CRw")
    try:
        ck(a.pos == (766, 758) and b.pos == (767, 758), f"곁에 섰다 {a.pos} {b.pos}")
        t0 = time.time()
        rt.start(a, b, rt.EXCHANGE)
        rt.item(a, b, 5, 1)
        rt.item(b, a, 5, 1)
        rt.finish(a, b)
        wait = TRANSFER_DELAY_MS / 1000 + 4.0   # 커밋을 DB가 붙드는 동안
        ck(a.wait_completed(46, t0, wait) and b.wait_completed(46, t0, wait), "둘 다 「손에서 손으로」")
        return ck.result()
    except AssertionError as e:
        ck(False, f"교환 흐름 {e}")
        return ck.result()
    finally:
        a.close()
        b.close()


def scenario_zone(port: int):
    ck = Checks()
    # 걸어서 Zone 경계(0 → 1)
    w = ABot(port, f"lv5_xy499_6_{TAG}zw")
    try:
        ck(tuple(w.pos) == (499, 6), f"경계 칸 {w.pos}")
        t0 = time.time()
        w.step(3)   # +x
        w.wait(lambda: tuple(w.pos) == (500, 6), 3.0)
        w.poll(0.8)
        rows, _, _ = w.listing(CAT_WORLD)
        ck(rows.get(47, (0, 0))[0] == 2, f"걸어서 경계를 넘으면 지역 2 {rows.get(47)}")
    finally:
        w.close()
    # 길잡이 곁에서 옛길로 15곳 — 이름이 자리를 정해 다시 접속하면 늘 길잡이 곁(들어가 본 지역은 진행 기록에 남는다)
    spot = None
    for dx, dy in [(1, 1), (0, 1), (1, 0), (-1, 1), (1, -1), (0, 2), (2, 0)]:
        x, y = MATEO[0] + dx, MATEO[1] + dy
        b = ABot(port, f"lv20_xy{x}_{y}_{TAG}z")
        ok = tuple(b.pos) == (x, y)
        b.close()
        if ok:
            spot = (x, y)
            break
    if not ck(spot is not None, "길잡이 곁 칸"):
        return ck.result()
    name = f"lv20_xy{spot[0]}_{spot[1]}_{TAG}z"
    regions = [r for r in range(16) if r != 5]
    done, fails = [], []
    for i, dest in enumerate(regions):
        time.sleep(0.3)
        b = ABot(port, name)
        try:
            t = time.time()
            b.c.sendall(struct.pack("<HBHHI", 11, T_TELEPORT_REQ, 3, dest, 1))
            b.wait(lambda: [p for p in b.packets(T_SHOP_ACK, t) if p[3] == 2], 3.0)
            acks = [p for p in b.packets(T_SHOP_ACK, t) if p[3] == 2]
            if not acks or acks[-1][4] != 0:
                fails.append((dest, acks and acks[-1][4]))
                continue
            b.poll(1.0)   # 새 Zone 입장(HandleEnterZone)이 지역을 센다
            done += b.completed(t)
            if i == len(regions) - 1:
                s = b.ach_infs(INF_COMPLETED, t)
                rows, _, _ = b.listing(CAT_WORLD)
                ck(rows.get(49) == (16, 1) and rows.get(48) == (10, 1) and rows.get(47) == (5, 1), f"지역 16/16 {rows.get(47)} {rows.get(48)} {rows.get(49)}")
        finally:
            b.close()
    ck(not fails, f"옛길 15곳 모두 성공 {fails}")
    ck(sorted(set(done)) == [47, 48, 49], f"5 · 10 · 16곳에서 달성 {done}")
    return ck.result()


# ── 직거래 + 사냥(tradeadena) ───────────────────────────────────────────────
class HBot(Ach, rp.Bot):
    """사냥 + 직거래(TRANSFER_REQ 114 · ACK 115 · STATE 116 — regress_transfer.Bot과 같은 바이트, ObjID는 regress_party 꼴)"""

    def __init__(self, port: int, name: str):
        self.transfer, self.tstates, self.tacks, self.tseq = None, [], [], 0
        super().__init__(port, name)

    def on(self, p: bytes):
        super().on(p)
        if p[2] == rt.ACK:
            _op, result, seq, _tid, _rev = struct.unpack_from("<BBIQI", p, 3)
            self.tacks.append((seq, result))
        elif p[2] == rt.STATE:
            tid, rev, kind, phase, result, side = struct.unpack_from("<QIBBBB", p, 3)
            self.transfer = {"id": tid, "rev": rev, "kind": kind, "phase": phase, "result": result, "side": side}
            self.tstates.append((time.time(), dict(self.transfer)))

    def treq(self, op: int, kind: int | None = None, peer=None, slot: int = 0, count: int = 0, price: int = 0, wait: bool = True):
        self.tseq += 1
        seq = self.tseq
        t = self.transfer or {"id": 0, "rev": 0, "kind": 0}
        kind = t["kind"] if kind is None else kind
        tid, rev = (0, 0) if op == rt.REQUEST else (t["id"], t["rev"])
        self.c.sendall(struct.pack("<HBBBIQI16sHHI20s", 65, rt.REQ, op, kind, seq, tid, rev,
                                   rp.pack_obj(peer.me) if peer else bytes(16), slot, count, price, b""))
        if not wait:
            return seq
        self.wait(lambda: any(s == seq for s, _ in self.tacks), 6.0)
        return next((r for s, r in self.tacks if s == seq), None)

    def adena_infs(self, since: float = 0.0) -> list[tuple[float, int, int]]:
        """(받은 시각, 변화, 사유)"""
        return [(at, struct.unpack_from("<i", p, 7)[0], p[11]) for at, tt, p in self.log if tt == rp.T_ADENA and at >= since]


def scenario_tradeadena(port: int):
    """거래 확정 중(DB가 HARVEN_DB_TRANSFER_DELAY_MS 동안 붙든다)에 처치한 그로시는 미뤄졌다가 확정 뒤 한 번 들어온다 —
    사냥 그로시(25 · 26) 진척은 그 몫만큼 정확히 한 번. 판 대금(DirectTrade 1,000)은 사냥 그로시가 아니다."""
    ck = Checks()
    a = HBot(port, rp.nm(20, 749, 251, "j"))   # 사는 쪽 · 사냥
    b = HBot(port, "lv20_xy750_251_CRj")       # 파는 쪽(제작 꾸러미 — 칸 5 철검)
    bots = [a, b]
    try:
        ck(tuple(a.pos) == (749, 251) and tuple(b.pos) == (750, 251), f"곁에 섰다 {a.pos} {b.pos}")
        rows_a, _, _ = a.listing()
        rows_b, _, _ = b.listing()
        hunt_a0, hunt_b0 = rows_a.get(26, (0, 0))[0], rows_b.get(26, (0, 0))[0]
        t_start = time.time()
        # 1) 몬스터를 거의 다 깎아 둔다(죽이지 않는다)
        m, ready = None, False
        for _ in range(4):
            m = rp.pick_monster(bots)
            if m is None or not rp.approach(bots, m):
                continue
            t0, top = time.time() - 0.05, 1
            while time.time() - t0 < 30 and a.monsters.get(m, {}).get("alive"):
                dealt = rp.dealt_by(a, m, t0).get(a.me, 0)
                top = max([top] + [struct.unpack_from("<i", p, 43)[0] for p in a.packets(rp.T_DAMAGE, t0) if rp.objid(p, 3) == m])
                if HP - dealt <= max(2 * top, HP // 4):
                    ready = True
                    break
                a.attack(m)
                rp.pump(bots, 0.35)
            if ready and a.monsters.get(m, {}).get("alive"):
                break
            ready = False
        if not ck(ready, "몬스터를 거의 다 깎았다"):
            return ck.result()
        # 2) 거래(파는 쪽 b · 값 1,000) — 사는 쪽이 먼저 확정, 파는 쪽 확정이 DB 커밋을 연다
        ck(b.treq(rt.REQUEST, rt.SALE, peer=a) == rt.OK, "거래 신청")
        rp.pump(bots, 0.15)
        ck(a.treq(rt.ACCEPT) == rt.OK, "수락")
        rp.pump(bots, 0.15)
        ck(b.treq(rt.SET_ITEM, slot=5, count=1) == rt.OK, "철검 올림")
        rp.pump(bots, 0.15)
        ck(b.treq(rt.SET_PRICE, price=1000) == rt.OK, "값 1,000")
        rp.pump(bots, 0.15)
        for x in (b, a):
            ck(x.treq(rt.READY) == rt.OK, "준비")
            rp.pump(bots, 0.15)
        ck(a.treq(rt.CONFIRM) == rt.OK, "사는 쪽 확정")
        rp.pump(bots, 0.15)
        t_commit = time.time()
        b.treq(rt.CONFIRM, wait=False)
        # 3) 커밋이 DB에 머무는 동안 막타
        while time.time() - t_commit < 4.0 and a.monsters.get(m, {}).get("alive"):
            a.attack(m)
            rp.pump(bots, 0.25)
        t_kill = next((at for at, tt, p in a.log if tt == rp.T_DIE and at >= t_commit and rp.objid(p, 3) == m), None)
        a.wait(lambda: any(st["phase"] == 4 for at, st in a.tstates if at >= t_commit), 8.0)
        rp.pump(bots, 0.6)
        t_done = next((at for at, st in a.tstates if at >= t_commit and st["phase"] == 4), None)
        ck(t_done is not None and any(st["phase"] == 4 for at, st in b.tstates if at >= t_commit), "거래 성립(둘 다 Completed)")
        ck(t_kill is not None and t_done is not None and t_kill < t_done, f"처치가 확정 중에 {t_kill and t_kill - t_commit:.2f}s · 성립 {t_done and t_done - t_commit:.2f}s")
        infs = a.adena_infs(t_commit)
        trade_at = next((i for i, (_, d, why) in enumerate(infs) if why == 13), None)
        pick_after = [d for i, (_, d, why) in enumerate(infs) if why == 0 and trade_at is not None and i > trade_at]
        ck(trade_at is not None and infs[trade_at][1] == -1000 and pick_after, f"미룬 사냥 그로시가 대금 뒤에 들어왔다 {infs}")
        picked = sum(d for _, d, why in a.adena_infs(t_start) if why == 0)
        ck(any(d == 1000 and why == 13 for _, d, why in b.adena_infs(t_commit)), "파는 쪽 +1,000 DirectTrade")
        time.sleep(0.6)
        rows_a, _, _ = a.listing()
        rows_b, _, _ = b.listing()
        ck(picked > 0 and rows_a.get(26, (0, 0))[0] - hunt_a0 == picked, f"사냥 그로시 진척 = 받은 사냥 그로시 한 번 {rows_a.get(26, (0, 0))[0] - hunt_a0} = {picked}")
        ck(rows_b.get(26, (0, 0))[0] == hunt_b0, f"판 대금은 사냥 그로시가 아니다 {rows_b.get(26, (0, 0))[0]} = {hunt_b0}")
        return ck.result()
    finally:
        for x in bots:
            x.close()


# ── 메모리 DB 일지(HARVEN_MEMDB_JOURNAL) — 반영한 차례대로 한 줄씩(Tools/Sandbox/MemDBConnection.cpp) ─────────
#   P|이름hex|암호hex|pid|x|y|레벨|경험치|그로시 · S|pid|칸|있음|아이템|개수|착용|강화|extra · A|pid|그로시 · G|pid|진행 기록hex
AK_KILL = 0                       # AchKind
AK_UNKNOWN = 30                   # 이 서버가 모르는 kind(새 판의 서버가 쓴 통계 줄)


def journal_lines() -> list[str]:
    try:
        return JOURNAL.read_text(encoding="utf-8").split("\n")[:-1]   # 끝이 잘린 줄은 아직 반영 중
    except OSError:
        return []


def pid_of(name: str) -> int | None:
    key = name.encode().hex()
    for ln in journal_lines():
        f = ln.split("|")
        if f[0] == "P" and f[1] == key:
            return int(f[3])
    return None


def bits_of(data: bytes) -> set[int]:
    return {b * 8 + k for b, v in enumerate(data) for k in range(8) if v & (1 << k)}


def bits_bytes(ids) -> bytes:
    out = bytearray((max(ids) // 8 + 1) if ids else 0)
    for i in ids:
        out[i // 8] |= 1 << (i % 8)
    return bytes([len(out)]) + bytes(out)


def section_bytes(stats=(), done=(), claimed=(), regions: int = 0) -> bytes:
    """업적 절(AchieveBook::Write) — stats: [(kind, param, 값)]"""
    return bytes([len(stats)]) + b"".join(struct.pack("<BHI", k, p, v) for k, p, v in stats) + bits_bytes(done) + bits_bytes(claimed) + \
        struct.pack("<H", regions)


def blob(version: int, section: bytes = b"", cls: int = 0) -> bytes:
    """진행 기록(ProgressCodec) — 능력치 배분 0 · 천장 0 · 퀘스트 없음 · 대기 없음 + 업적 절(판 4~)"""
    return bytes([ord("P"), ord("2"), version, cls]) + bytes(12 + 4) + bytes([0]) + bytes(32) + bytes([0]) + section


def split_blob(data: bytes) -> tuple[int, bytes]:
    """(판, 업적 절 바이트) — 퀘스트 절을 건너뛴다"""
    at = 20
    active = data[at]
    at += 1 + active * 8 + 32
    cds = data[at]
    at += 1 + cds * 6
    return data[2], data[at:]


def parse_section(sec: bytes) -> dict:
    n, at, stats = sec[0], 1, []
    for _ in range(n):
        stats.append(struct.unpack_from("<BHI", sec, at))
        at += 7
    done_len = sec[at]
    done = bits_of(sec[at + 1:at + 1 + done_len])
    at += 1 + done_len
    cl_len = sec[at]
    claimed = bits_of(sec[at + 1:at + 1 + cl_len])
    at += 1 + cl_len
    return {"stats": stats, "done": done, "claimed": claimed, "regions": struct.unpack_from("<H", sec, at)[0], "end": at + 2}


def journal_of(pid: int, since: int = 0) -> list[tuple[int, str, list[str]]]:
    """since번째 줄부터 그 사람의 (줄 번호, op, 칸들)"""
    out = []
    for i, ln in enumerate(journal_lines()):
        if i < since:
            continue
        f = ln.split("|")
        if len(f) > 1 and f[0] in ("G", "S", "A", "T", "X") and f[1].lstrip("-").isdigit() and int(f[1]) == pid:
            out.append((i, f[0], f[2:]))
    return out


def last_blob(pid: int, since: int = 0) -> bytes | None:
    gs = [bytes.fromhex(f[0]) for _, op, f in journal_of(pid, since) if op == "G" and f[0] != "-"]
    return gs[-1] if gs else None


def scenario_frozen(port: int):
    """깨진 업적 절(잘림)은 비우지 않고 얼린다 — 받기 Invalid · 소급 없음 · 나갈 때 원본 절을 그대로 되쓴다. 모르는 kind의 통계 줄은 건너뛰고 지킨다."""
    ck = Checks()
    a = ABot(port, "ach_frozen")
    try:
        s = a.summary()
        ck(s and (s["done"], s["claimable"], s["points"]) == (0, 0, 0), f"얼린 책 — Summary 0 {s and (s['done'], s['claimable'], s['points'])}")
        r = a.ack(OP_CLAIM, 1, 19)
        ck(r and r["result"] == R_INVALID, f"얼린 책 Claim → Invalid {r and RESULT[r['result']]}")
        r = a.ack(OP_CLAIM_ALL, 2)
        ck(r and r["result"] == R_INVALID, f"얼린 책 ClaimAll → Invalid {r and RESULT[r['result']]}")
        mark = len(journal_lines())
    finally:
        a.close()
    time.sleep(1.0)
    last = last_blob(9002, mark)
    ck(last is not None and split_blob(last)[1] == FROZEN_SECTION, f"나갈 때 원본 절 그대로 {last and split_blob(last)[1].hex()}")
    log = (WORK / "server.log").read_text(encoding="utf-8", errors="replace")
    ck(log.count("ach_frozen(9002) 업적 기록이 깨져 읽기 전용") == 1, "로그 한 줄(접속마다 한 번)")
    b = ABot(port, "ach_unknown")
    try:
        s = b.summary()
        ck(s and (s["done"], s["claimable"]) == (3, 2), f"모르는 kind를 건너뛰고 읽었다 — 달성 1 · 18 · 19(소급) · 받을 것 1 · 19 {s and (s['done'], s['claimable'])}")
        r = b.ack(OP_CLAIM, 1, 18)
        ck(r and r["result"] == R_ALREADY, f"받음 비트가 남았다 — 18 AlreadyClaimed {r and RESULT[r['result']]}")
        mark = len(journal_lines())
    finally:
        b.close()
    time.sleep(1.0)
    last = last_blob(9003, mark)
    sec = parse_section(split_blob(last)[1]) if last else {}
    ck(last and (AK_UNKNOWN, 7, 12345) in sec["stats"] and (AK_KILL, 0, 1) in sec["stats"] and sec["claimed"] == {18} and {1, 18, 19} <= sec["done"],
       f"되쓴 기록 — 모르는 줄 · 처치 · 받음 {sec}")
    return ck.result()


AK_ENCHANT = 8                    # AchKind


def scenario_enchretro(port: int):
    """「가장 높았던」 강화 소급은 판 4 미만을 처음 읽을 때 한 번만 — 판 4 기록 + 가방의 +9(빌린 것)는 진척을 바꾸지 않는다"""
    ck = Checks()
    a = ABot(port, "ach_ench_v4")
    try:
        s = a.summary()
        rows, _, _ = a.listing(CAT_GEAR)
        ck(rows.get(31) == (1, 1) and rows.get(32) == (2, 0) and rows.get(34) == (2, 0),
           f"판 4 + 가방 +9 — 진척 2 그대로 {rows.get(31)} {rows.get(32)} {rows.get(34)}")
    finally:
        a.close()
    time.sleep(0.5)
    a = ABot(port, "ach_ench_v4")
    try:
        rows, _, _ = a.listing(CAT_GEAR)
        ck(rows.get(34) == (2, 0), f"다시 접속해도 2 {rows.get(34)}")
    finally:
        a.close()
    b = ABot(port, "ach_ench_v3")
    try:
        rows, _, _ = b.listing(CAT_GEAR)
        ck(all(rows.get(i, (0, 0))[1] == 1 for i in (31, 32, 33, 34)) and rows.get(34) == (9, 1), f"판 3 — 한 번 소급 +9 {rows.get(34)}")
        mark = len(journal_lines())
    finally:
        b.close()
    time.sleep(1.0)
    last = last_blob(9005, mark)
    ck(last is not None and split_blob(last)[0] == 4 and (AK_ENCHANT, 0, 9) in parse_section(split_blob(last)[1])["stats"], "판 4로 저장(강화 9)")
    b = ABot(port, "ach_ench_v3")
    try:
        rows, _, _ = b.listing(CAT_GEAR)
        ck(rows.get(34) == (9, 1), f"다시 접속 — 남는다 {rows.get(34)}")
    finally:
        b.close()
    return ck.result()


def scenario_claimorder(port: int):
    """받기 차례(크래시가 보상을 두 번 주지 않는다): 받음 비트를 담은 진행 기록(G)이 그 보상의 칸(S) · 그로시(A)보다 먼저 반영된다.
    가방에 두 칸만 비면 ClaimAll은 정확히 둘 — 마지막 기록의 받음 비트도 그 둘뿐."""
    ck = Checks()
    name = f"lv30_FI{TAG}o"
    a = EBot(port, name)
    try:
        pid = pid_of(name)
        if not ck(pid is not None, f"일지에서 사람 번호 {pid}"):
            return ck.result()
        s = a.summary()
        ck(s and s["claimable"] == 4, f"Lv30 소급 18~21 {s and s['claimable']}")
        mark0 = len(journal_lines())
        r = a.ack(OP_CLAIM, 1, 18)
        ck(r and r["result"] == R_BAG_FULL, f"가방 가득 → BagFull {r and RESULT[r['result']]}")
        a.poll(0.5)
        ck(not [1 for _, op, f in journal_of(pid, mark0) if op == "G" and parse_section(split_blob(bytes.fromhex(f[0]))[1])["claimed"]],
           "BagFull — 받음 비트를 켠 기록을 쓰지 않았다")
        t0 = time.time()
        a.discard(1, 1)
        a.discard(2, 1)
        a.wait(lambda: len(a.packets(rec.T_DISCARD_ACK, t0)) >= 2, 3.0)
        a.poll(0.5)
        mark = len(journal_lines())
        r = a.ack(OP_CLAIM_ALL, 2)
        want = ACH[18]["reward_adena"] + ACH[19]["reward_adena"]
        ck(r and r["result"] == R_OK and r["claimed"] == 2 and r["adena"] == want, f"두 칸 — ClaimAll 정확히 둘 {r}")
        items = {ACH[18]["reward_item"]: 18, ACH[19]["reward_item"]: 19}
        a.wait(lambda: len([1 for _, op, f in journal_of(pid, mark) if op == "S" and int(f[2]) in items]) >= 2 and
               len([1 for _, op, _ in journal_of(pid, mark) if op == "A"]) >= 2, 4.0)
        rows = journal_of(pid, mark)
        first_g = {}
        for i, op, f in rows:
            if op == "G":
                for ach in parse_section(split_blob(bytes.fromhex(f[0]))[1])["claimed"]:
                    first_g.setdefault(ach, i)
        s_at = {items[int(f[2])]: i for i, op, f in rows if op == "S" and f[1] == "1" and int(f[2]) in items}
        a_at = [i for i, op, _ in rows if op == "A"]
        ck(set(first_g) == {18, 19}, f"받음 비트를 담은 기록 {sorted(first_g)}")
        ck(all(first_g.get(x, 1 << 30) < s_at.get(x, -1) for x in (18, 19)),
           f"받음 기록(G)이 보상 칸(S)보다 먼저 — G {first_g} · S {s_at}")
        ck(len(a_at) >= 2 and first_g.get(18, 1 << 30) < a_at[0] and first_g.get(19, 1 << 30) < a_at[1] and a_at[0] < first_g.get(19, -1),
           f"받음 기록(G)이 그로시(A)보다 먼저 — G {first_g} · A {a_at}")
        last = last_blob(pid, mark)
        sec = parse_section(split_blob(last)[1]) if last else {}
        ck(last and sec["claimed"] == {18, 19} and {18, 19, 20, 21} <= sec["done"], f"마지막 기록 — 받음 {sec.get('claimed')} · 달성 {sec.get('done')}")
        r = a.ack(OP_CLAIM_ALL, 3)
        ck(r and r["result"] == R_BAG_FULL, f"다시 가득 — ClaimAll BagFull {r and RESULT[r['result']]}")
        return ck.result()
    finally:
        a.close()


# ── 뼈대 ────────────────────────────────────────────────────────────────────
def make_fixture(src: Path, dst: Path):
    from dt import export
    shutil.copytree(src, dst)
    patch = rp.patch_rows

    def monster(r):
        if r.get("dummy"):
            return
        if r["id"] == BOSS:
            r.update({"max_hp": BOSS_HP, "attack": 1, "attack_interval_ms": 5000, "aggressive": False})
        elif r.get("boss"):
            r.update({"attack": 1, "aggressive": False})
        else:
            r.update({"max_hp": HP, "attack": 1, "attack_interval_ms": 5000, "move_interval_ms": 60000, "aggressive": False,
                      "exp": 1, "adena_chance": 100, "adena_min": 100, "adena_max": 100})

    def loot_const(r):
        r.update({"elite_permille": 1000, "elite_min_tier": 3, "elite_hp_percent": 100, "elite_share_percent": 10, "boss_share_percent": 3,
                  "level_gap": 200})   # 레벨 차로 정수 확률이 반이 되지 않게(처치마다 정수 하나)

    def tier_loot(r):
        if r["tier"] in (1, 2):
            r.update({"common_bp": 0, "uncommon_bp": 0, "rare_bp": 10000, "essence_item": 61, "essence_permille": 1000,
                      "crystal_item": 0, "crystal_permille": 0})

    def boss_ai(r):
        if r["monster_id"] == BOSS:
            r.update({"phase2_hp": 0, "phase3_hp": 0, "phase2_skill": 0, "phase3_skill": 0})

    def quest(r):
        if r["id"] == 4:
            r["turn_in"] = 0

    def objective(r):
        if r["quest_id"] == 4:
            r.update({"kind": "KillIn", "target": 0, "count": 1, "zone": 1})

    def achievement(r):
        if r["id"] in TARGETS:
            r["target"] = TARGETS[r["id"]]
    patch(dst / "Monster.json", monster)
    patch(dst / "LootConst.json", loot_const)
    patch(dst / "TierLoot.json", tier_loot)
    patch(dst / "MonsterAI.json", boss_ai)
    patch(dst / "Quest.json", quest)
    patch(dst / "QuestObjective.json", objective)
    patch(dst / "Achievement.json", achievement)
    p = dst / "MonsterAISkill.json"   # 모로즈의 기술 칸을 지운다(넉백 · 장판이 봇을 밀어내지 않게)
    lines = p.read_text(encoding="utf-8").split("\n")
    keep = [ln for ln in lines if not ln.startswith('{"monster_id": %d,' % BOSS)]
    body = [ln.rstrip(",") for ln in keep if ln.startswith("{") and not ln.startswith('{"table"')]
    p.write_text(keep[0] + "\n" + ",\n".join(body) + "\n]}\n", encoding="utf-8", newline="\n")
    for row in json.loads((dst / "Achievement.json").read_text(encoding="utf-8"))["rows"]:
        ACH[row["id"]] = row
    for row in json.loads((dst / "Item.json").read_text(encoding="utf-8"))["rows"]:
        rec.ITEMS[row["id"]] = row
        rp.ITEM_TYPE[row["id"]] = row["type"]
        rp.ITEM_NAME[row["id"]] = row.get("display_name") or row["name"]
    (dst / "manifest.json").write_text(export.manifest_text(dst), encoding="utf-8", newline="\n")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--server", default=str(REPO / "build" / "linux-memory" / "GameServer"))
    ap.add_argument("--port", type=int, default=4400)
    ap.add_argument("--keep", action="store_true", help="시험용 GameData 사본 · 서버 로그를 남긴다")
    ap.add_argument("--only", default="", help="쉼표로(kill,bag,wallet,retro,party,elite,boss,death,craft,quest,guild,auction,trade,zone,"
                                                         "tradeadena,frozen,enchretro,claimorder)")
    args = ap.parse_args()

    global WORK, JOURNAL, FROZEN_SECTION
    work = Path(tempfile.mkdtemp(prefix="harven_achieve_"))
    WORK, JOURNAL = work, work / "journal"
    make_fixture(REPO / "Bin" / "GameData", work / "GameData")
    # 메모리 DB 일지로 미리 넣는 캐릭터(regress_transfer와 같은 형식) — 모두 Lv12(소급 18 · 19)
    #   9001 지갑 상한(그로시 20억) · 9002 업적 절이 잘린 판 4(얼린다) · 9003 모르는 kind의 통계 줄 + 받음 18 ·
    #   9004 판 4(강화 2) + 가방에 철검 +9 · 9005 판 3 + 가방에 철검 +9
    FROZEN_SECTION = section_bytes([(AK_KILL, 0, 4)], done=[1, 18], claimed=[1, 18], regions=1)[:-1]   # 지역 칸 한 바이트가 잘렸다
    unknown = section_bytes([(AK_UNKNOWN, 7, 12345), (AK_KILL, 0, 1)], done=[1, 18], claimed=[18])
    seeds = [(9001, "ach_wallet_cap", 2000000000, None), (9002, "ach_frozen", 100, blob(4, FROZEN_SECTION)),
             (9003, "ach_unknown", 100, blob(4, unknown)), (9004, "ach_ench_v4", 100, blob(4, section_bytes([(AK_ENCHANT, 0, 2)]))),
             (9005, "ach_ench_v3", 100, blob(3))]
    lines = []
    for pid, name, wallet, data in seeds:
        lines.append(f"P|{name.encode().hex()}|{b'pw'.hex()}|{pid}|749|767|12|0|{wallet}\n")
        if data is not None:
            lines.append(f"G|{pid}|{data.hex()}\n")
        if pid in (9004, 9005):
            lines.append(f"S|{pid}|0|1|3|1|0|9|0\n")   # 철검 +9(착용하지 않은 채)
    JOURNAL.write_text("".join(lines), encoding="utf-8")
    log = open(work / "server.log", "w")
    env = dict(os.environ, HARVEN_ECONOMY_LOG=str(work / "economy.log"), HARVEN_DB_BATCH_DELAY_MS="300",
               HARVEN_MEMDB_JOURNAL=str(JOURNAL), HARVEN_DB_TRANSFER_DELAY_MS=str(TRANSFER_DELAY_MS))
    srv = subprocess.Popen([str(Path(args.server).resolve()), "--data-dir", str(work / "GameData"), "--port", str(args.port), "--dev"],
                           stdout=log, stderr=subprocess.STDOUT, cwd=work, env=env)
    results = []
    try:
        if not rp.wait_port(args.port, 90) or not rc.wait_login(args.port, 120, f"probe{TAG}"):
            print("FAIL: 서버가 뜨지 않았다")
            return 1
        time.sleep(1.0)
        scenarios = [("kill", scenario_kill), ("bag", scenario_bag), ("wallet", scenario_wallet), ("retro", scenario_retro),
                     ("party", scenario_party), ("elite", scenario_elite), ("boss", scenario_boss), ("death", scenario_death),
                     ("craft", scenario_craft), ("quest", scenario_quest), ("guild", scenario_guild), ("auction", scenario_auction),
                     ("trade", scenario_trade), ("zone", scenario_zone), ("tradeadena", scenario_tradeadena), ("frozen", scenario_frozen),
                     ("enchretro", scenario_enchretro), ("claimorder", scenario_claimorder)]
        only = {s for s in args.only.split(",") if s}
        for name, fn in scenarios:
            if only and name not in only:
                continue
            t = time.time()
            try:
                ok, msg = fn(args.port)
            except (ConnectionError, OSError, RuntimeError) as e:
                ok, msg = False, f"연결 문제 — {e}"
            results.append(ok)
            print(f"{'PASS' if ok else 'FAIL'} {name} ({time.time() - t:.0f}s): {msg}", flush=True)
            if srv.poll() is not None:
                print(f"FAIL: 서버가 죽었다(종료 코드 {srv.returncode})")
                return 1
        alive = srv.poll() is None
        print(f"{'PASS' if alive else 'FAIL'}: 서버 살아 있음")
        results.append(alive)
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
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
