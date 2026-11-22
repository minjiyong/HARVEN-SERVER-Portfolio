"""결사 성장 회귀 시험 — 레벨 · 경험치 · 기여도 · 주간 임무 · 출석 · 협력 처치 · 업적 · 칭호 · 게시판 · 기록 · 직위 권한 ·
가입 신청 · 결사 상점(금고) · 결사장 위임 청구 · 주/날 넘김 · 다시 켜기(저장)를 실제 패킷으로 본다(Shared/Document/GUILD.md).

    python3 Tools/Test/regress_guild.py [--server build/linux-memory/GameServer] [--port 4250] [--only growth,board] [--keep]

HARVEN_IN_MEMORY_DB=ON 빌드를 이 스크립트가 직접 띄운다(일지 HARVEN_MEMDB_JOURNAL — persist 시나리오가 서버를 다시 켠다).
시험용 GameData 사본:
  · Monster — HP 300 · 공격력 1 · 비선공 · 걸음 60초 · 경험치 1000 · TierLoot 일반 10000bp(처치마다 필드 장비 → Gear 임무) · 정수 · 결정 0‰ · 정예 0‰
  · GuildConst — 처치 경험치 50% · 출석 1분 · 위임 청구 3초 · 기다림 3초 · 글 3개 · 신청 2개 · 덧보상 2명 50%
  · GuildLevel — 1레벨 인원 3 · 2레벨(1500) 인원 5 · GuildMission — Kill 3 · Gear 2 · Active(2명 × 1분) — 1레벨에 셋 다 뽑힌다
  · GuildAchievement — 처치 1(인장 200 · 칭호 2) · 레벨 2 · 인원 3 / GuildAttend — 1명 · 2명 / GuildShop — 약초 물약(주 1회) · 비싼 것 · 2레벨 것
  week 시나리오만 따로 서버를 하나 더 띄운다(주 60초 · 날 30초).
시나리오
  growth   세우기 · 초대 · 인원 한도(1레벨 3) · 함께 사냥 → 경험치 · 기여 · 협력 · 출석 · 임무 달성(덧보상) · 업적 · 칭호 · 레벨 2 → 한도 5 · 기여도 · 기록
  perms    직위 권한표: 정예에게 추방을 주면 추방 · 부관의 초대를 거두면 거절 · 결사장만 권한을 바꾼다
  board    공지 분류는 공지 권한 · 쓰기 · 읽기 · 고정(게시판 권한) · 남의 글 내리기 거절 · 한도 넘으면 오래된 글이 내려감 · 금칙어
  shop     금고 인장으로 사기(소지품 칸 · 금고 − · 기록) · 주 한도 · 인장 부족 · 레벨 부족 · 상점 권한
  recruit  가입 안내 · 모집 목록 · 공개 정보 · 신청(레벨 미달 · 중복) · 승인 → 결사원 · 거절 · 거두기
  claim    결사장이 오래 안 들어오면 부관이 청구 → 기다림 뒤 결사장이 된다 · 청구 중 결사장이 들어오면 취소
  persist  서버를 다시 켜도 레벨 · 경험치 · 금고 · 기여 · 게시판 · 기록이 남는다
  week     주 · 날 넘김(주 60초 · 날 30초): 이번 주 기여 · 상점 한도 · 출석이 비워지고 임무를 새로 뽑는다
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import regress_party as rp   # noqa: E402 — 봇 · 걷기 · 사냥 도우미를 같이 쓴다

REPO = rp.REPO
TAG = rp.TAG
T_GUILD_REQ, T_GUILD_ACK, T_GUILD_INF, T_GUILD_NFY = 89, 90, 91, 92
T_GX_REQ, T_GX_ACK, T_GX_INF, T_GX_NFY = 108, 109, 110, 111
T_ITEM_SLOT = 45
# GuildOp
G_INFO, G_MEMBERS, G_CREATE, G_INVITE, G_REPLY, G_LEAVE, G_KICK, G_SETRANK, G_TRANSFER, G_NOTICE, G_DISBAND = range(11)
GR_OK, GR_NOPERM, GR_FULL = 0, 3, 8
# GuildExOp
(X_OVERVIEW, X_MISSIONS, X_STATS, X_LOG, X_BOARD, X_READ, X_WRITE, X_DELETE, X_PIN, X_INTRO, X_RLIST, X_RVIEW, X_APPLY,
 X_APPLY_CANCEL, X_APPLICANTS, X_APPLY_REPLY, X_PERMS, X_TITLE, X_SHOP, X_BUY, X_ACH, X_TITLES, X_CLAIM) = range(23)
# GuildExResult
(XR_OK, XR_NOT_IN_GUILD, XR_NOPERM, XR_BUSY, XR_INVALID, XR_NOT_FOUND, XR_NO_SEALS, XR_LEVEL_LOW, XR_LIMIT, XR_INV_FULL,
 XR_BAD_TEXT, XR_DUP, XR_IN_GUILD, XR_FULL, XR_ALREADY, XR_NOT_RECRUITING, XR_NOT_YET, XR_TOO_MANY) = range(18)
# GuildExInfKind
INF_OV, INF_MIS, INF_ST, INF_LOG, INF_BRD, INF_POST, INF_PUB, INF_REC, INF_APP, INF_SHOP, INF_ACH, INF_TIT = range(12)
# GuildExEvent
(E_LEVEL, E_MPROG, E_MDONE, E_ACH, E_ATTEND, E_SEALS, E_BOUGHT, E_POSTED, E_APPLIED, E_APPLY_RESULT, E_CLAIM_START,
 E_CLAIM_CANCEL, E_CLAIM_DONE, E_PULSE, E_TITLE, E_PERMS, E_INTRO, E_TITLE_UNLOCK) = range(18)
# GuildEvent(옛 결사 알림)
GE_INVITE_FROM, GE_LEADER = 0, 5
# GuildLogKind
(L_JOINED, L_LEFT, L_KICKED, L_RANK, L_LEADER, L_CLAIM_START, L_CLAIM_CANCEL, L_CLAIM_DONE, L_LEVEL, L_MISSION, L_ACH, L_ATTEND,
 L_SEALS_IN, L_SEALS_OUT, L_PERMS, L_TITLE, L_NOTICE, L_INTRO, L_EMBLEM, L_ACCEPTED) = range(20)
P_INVITE, P_APPROVE, P_KICK, P_NOTICE, P_BOARD, P_RANK, P_SHOP = 1, 2, 4, 8, 16, 32, 64
HALL = (747, 743)
FIELD = (750, 253)
OV_FMT = "<IBBBBBBBBIIIIBBBBIIIBBBBHHIIIIIIBBHIIIHH20sII"


class GBot(rp.Bot):
    def __init__(self, port: int, name: str, cls: int = 1):
        self.gseq = 0
        self.xseq = 0
        self.ov = {}
        super().__init__(port, name, cls)

    def on(self, p: bytes):
        super().on(p)
        if p[2] == T_GX_INF and p[3] == INF_OV and len(p) >= 128:
            f = struct.unpack_from(OV_FMT, p, 4)
            keys = ["guild", "level", "frame", "title", "rank", "members", "max", "recruiting", "minlv", "exp", "expLevel", "expNext",
                    "seals", "p0", "p1", "p2", "p3", "week", "weekLeft", "dayLeft", "attendToday", "attendTier", "attendNext",
                    "attended", "activeToday", "attendMin", "contribWeek", "contribTotal", "joinedAt", "expToday", "expCap", "titles",
                    "achDone", "achTotal", "missionsDone", "kills", "bossKills", "coopKills", "ageDays", "attendDays", "claimant",
                    "claimLeft", "claimIn"]
            self.ov = dict(zip(keys, f))
            self.ov["claimant"] = rp.cstr(self.ov["claimant"])

    # ── 보내기 ──
    def guild(self, op, arg8=0, arg32=0, target="", text=""):
        self.gseq += 1
        pkt = struct.pack("<HBBIBI20s92s", 125, T_GUILD_REQ, op, self.gseq, arg8, arg32, target.encode(), text.encode("utf-8"))
        self.c.sendall(pkt)
        return self.gseq

    def gx(self, op, arg8=0, arg32=0, target="", title="", body=""):
        self.xseq += 1
        pkt = struct.pack("<HBBIBI20s31s151s", 215, T_GX_REQ, op, self.xseq, arg8, arg32, target.encode(),
                          title.encode("utf-8"), body.encode("utf-8"))
        assert len(pkt) == 215
        self.c.sendall(pkt)
        return self.xseq

    # ── 찾기 ──
    def gacks(self, op, since=0.0):
        return [{"result": p[4], "seq": struct.unpack_from("<I", p, 5)[0], "param": struct.unpack_from("<I", p, 9)[0]}
                for p in self.packets(T_GUILD_ACK, since) if p[3] == op]

    def xacks(self, op, since=0.0):
        return [{"result": p[4], "seq": struct.unpack_from("<I", p, 5)[0], "param": struct.unpack_from("<I", p, 9)[0]}
                for p in self.packets(T_GX_ACK, since) if p[3] == op]

    def xack(self, op, since, seconds=4.0):
        """op의 since 뒤 첫 응답(없으면 None)"""
        self.wait(lambda: self.xacks(op, since), seconds)
        a = self.xacks(op, since)
        return a[0] if a else None

    def xnfys(self, ev, since=0.0):
        out = []
        for p in self.packets(T_GX_NFY, since):
            if p[3] == ev:
                v, v2 = struct.unpack_from("<ii", p, 4)
                out.append({"value": v, "value2": v2, "who": rp.cstr(p[12:32]), "text": rp.cstr(p[32:63])})
        return out

    def ginfo(self, since=0.0):
        rows = [p for p in self.packets(T_GUILD_INF, since) if p[3] == 0]
        if not rows:
            return None
        p = rows[-1]
        return {"guild": struct.unpack_from("<I", p, 4)[0], "rank": p[9], "members": p[10], "name": rp.cstr(p[12:37]),
                "leader": rp.cstr(p[129:149])}

    def gnfys(self, ev, since=0.0):
        return [{"guild": struct.unpack_from("<I", p, 6)[0], "who": rp.cstr(p[26:46]), "by": rp.cstr(p[46:66])}
                for p in self.packets(T_GUILD_NFY, since) if p[3] == ev]

    def inf(self, kind, since=0.0):
        rows = [p for p in self.packets(T_GX_INF, since) if p[3] == kind]
        return rows[-1] if rows else None

    def ask(self, op, kind, arg8=0, arg32=0, seconds=3.0):
        """묻기(요청 간격 250ms — 기다렸다 보낸다) → kind 정보 패킷"""
        time.sleep(0.3)
        t0 = time.time()
        self.gx(op, arg8, arg32)
        self.wait(lambda: self.inf(kind, t0) is not None, seconds)
        return self.inf(kind, t0)

    def overview(self):
        t0 = time.time()
        time.sleep(0.3)
        self.gx(X_OVERVIEW)
        self.wait(lambda: self.inf(INF_OV, t0) is not None, 3.0)
        return dict(self.ov)

    def missions(self):
        p = self.ask(X_MISSIONS, INF_MIS)
        if p is None:
            return []
        n = p[14]
        out = []
        for i in range(n):
            f = struct.unpack_from("<HBBIIIIIIBBH31s", p, 15 + i * 63)
            out.append({"id": f[0], "kind": f[1], "done": f[2], "target": f[3], "progress": f[4], "sub": f[5], "exp": f[6], "seals": f[7],
                        "mine": f[8], "contributors": f[9], "bonus": f[10], "contrib": f[11], "name": rp.cstr(f[12])})
        return out

    def log_rows(self, filt=0, page=0):
        p = self.ask(X_LOG, INF_LOG, page, filt)
        if p is None:
            return []
        return [dict(zip(("at", "kind", "value", "who", "what"), struct.unpack_from("<IBi20s31s", p, 8 + i * 60))) for i in range(p[7])]

    def all_logs(self, filt=0):
        """모든 페이지(최근 것부터)"""
        out, page, pages = [], 0, 1
        while page < pages and page < 20:
            p = self.ask(X_LOG, INF_LOG, page, filt)
            if p is None:
                break
            pages = p[6]
            out += [dict(zip(("at", "kind", "value", "who", "what"), struct.unpack_from("<IBi20s31s", p, 8 + i * 60))) for i in range(p[7])]
            page += 1
        return out

    def board(self, cat=0xFF, page=0):
        p = self.ask(X_BOARD, INF_BRD, page, cat)
        if p is None:
            return []
        rows = []
        for i in range(p[7]):
            f = struct.unpack_from("<IIBB20s31s", p, 8 + i * 61)
            rows.append({"id": f[0], "cat": f[2], "pinned": f[3], "author": rp.cstr(f[4]), "title": rp.cstr(f[5])})
        return rows

    def stats(self, sort=0):
        p = self.ask(X_STATS, INF_ST, 0, sort)
        if p is None:
            return []
        return [dict(zip(("name", "joinedDay", "week", "total", "active", "flags"), struct.unpack_from("<20sHIIHB", p, 8 + i * 33)))
                for i in range(p[7])]

    def shop(self):
        p = self.ask(X_SHOP, INF_SHOP)
        if p is None:
            return None, []
        seals = struct.unpack_from("<I", p, 7)[0]
        rows = [dict(zip(("id", "item", "count", "price", "minlv", "limit", "bought"), struct.unpack_from("<HHHIBBB", p, 11 + i * 13)))
                for i in range(p[6])]
        return seals, rows


def check_factory(notes):
    state = {"ok": True}

    def check(label, cond):
        if not cond:
            state["ok"] = False
            notes.append("✗ " + label)
        return cond
    return check, state


def make_guild(leader: GBot, name: str, *members: GBot) -> int:
    t0 = time.time()
    leader.guild(G_CREATE, arg8=3, text=name)
    if not leader.wait(lambda: leader.gacks(G_CREATE, t0), 5.0) or leader.gacks(G_CREATE, t0)[0]["result"] != GR_OK:
        return 0
    gid = leader.gacks(G_CREATE, t0)[0]["param"]
    for m in members:
        if not invite(leader, m, gid):
            return 0
    return gid


def invite(leader: GBot, m: GBot, gid: int) -> bool:
    t0 = time.time()
    leader.guild(G_INVITE, target=m.name)
    if not m.wait(lambda: m.gnfys(GE_INVITE_FROM, t0), 4.0):
        return False
    m.guild(G_REPLY, arg8=1, arg32=gid)
    return m.wait(lambda: (m.ginfo(t0) or {}).get("guild") == gid, 4.0)


def hunt(bots, kills: int, seconds=90.0) -> int:
    """bots가 함께(모두 피해를 주게) 몬스터를 kills마리 쓰러뜨린다. 쓰러뜨린 수를 돌려준다."""
    done = 0
    end = time.time() + seconds
    while done < kills and time.time() < end:
        mon = rp.pick_monster(bots, max_d=8, seconds=20.0)
        if mon is None:
            continue
        if not rp.approach(bots, mon, seconds=12.0):
            continue
        order = [(b, 300 - 80 * (i + 1)) for i, b in enumerate(bots[:-1])] + [(bots[-1], 0)]
        if rp.kill_with(bots, mon, order, seconds=30.0):
            done += 1
    rp.pump(bots, 0.5)
    return done


# ── 시나리오 ────────────────────────────────────────────────────────────────
CTX: dict = {}


def scenario_growth(port: int):
    notes = []
    check, st = check_factory(notes)
    lead = GBot(port, rp.nm(20, *HALL, "L"))
    m1 = GBot(port, rp.nm(40, *FIELD, "a"))
    m2 = GBot(port, rp.nm(40, FIELD[0] + 1, FIELD[1], "b"))
    m3 = GBot(port, rp.nm(40, FIELD[0] - 1, FIELD[1], "c"))
    CTX.update(lead=lead, m1=m1, m2=m2, m3=m3)
    gid = make_guild(lead, f"서약{TAG}", m1, m2)
    if not check("세우기 · 초대 둘", gid != 0):
        return False, "; ".join(notes)
    CTX["gid"] = gid
    ov = lead.overview()
    check(f"처음: 레벨 1 · 인원 3/3 · 인원 업적(인장 10) · 기본 권한 {ov}", ov.get("level") == 1 and ov.get("max") == 3 and ov.get("members") == 3
          and ov.get("seals") == 10 and ov.get("achDone") == 1 and (ov["p0"], ov["p1"], ov["p2"], ov["p3"]) == (0x7F, 0x7F, P_INVITE | P_SHOP, P_SHOP))
    t0 = time.time()
    lead.guild(G_INVITE, target=m3.name)
    check("1레벨 인원 한도 — 넷째 초대는 Full", lead.wait(lambda: lead.gacks(G_INVITE, t0), 3.0) and lead.gacks(G_INVITE, t0)[0]["result"] == GR_FULL)
    mis = m1.missions()
    check(f"이번 주 임무 셋(Kill · Gear · Active) {[(m['id'], m['kind']) for m in mis]}", sorted(m["id"] for m in mis) == [1, 2, 3])

    t0 = time.time()
    killed = hunt([m1, m2], 3)
    check(f"함께 세 마리 쓰러뜨림({killed})", killed == 3)
    rp.pump([lead, m1, m2], 3.5)
    lv = lead.xnfys(E_LEVEL, t0)
    check(f"레벨 2 알림(새 인원 한도 5) {lv}", any(e["value"] == 2 and e["value2"] == 5 for e in lv))
    done = {e["value"] for e in m1.xnfys(E_MDONE, t0)}
    check(f"임무 달성 — Kill 3 · Gear 2 · Active 2명 {done}", done == {1, 2, 3})
    ach = {e["value"] for e in lead.xnfys(E_ACH, t0)}
    check(f"업적 — 처치 1 · 레벨 2(인원 3은 세울 때) {ach}", ach == {1, 2} and {e["value"] for e in lead.xnfys(E_ACH)} == {1, 2, 3})
    check("칭호 열림(처치 업적의 칭호 2 · 레벨 2의 칭호 1)", {e["value"] for e in lead.xnfys(E_TITLE_UNLOCK, t0)} >= {1, 2})
    att = sorted(e["value"] for e in m1.xnfys(E_ATTEND, t0))
    check(f"출석 1 · 2단계 {att}", att == [1, 2])
    seals = [e for e in lead.xnfys(E_SEALS, t0)]
    check(f"금고에 인장이 들어왔다(임무 · 업적 · 출석) {len(seals)}", len(seals) >= 5 and seals[-1]["value2"] > 0)
    pulse = m1.xnfys(E_PULSE, t0)
    check("기여 막대(Pulse)", bool(pulse) and pulse[-1]["value2"] > 0)
    ov = m1.overview()
    check(f"개요 — 레벨 2 · 한도 5 · 처치 ≥3 · 협력 처치 ≥3(시간 넘긴 처치가 뒤늦게 쓰러질 수 있다) · 출석 2 · 내 기여 > 0 {ov.get('level')} {ov.get('max')} {ov.get('kills')} {ov.get('coopKills')}",
          ov.get("level") == 2 and ov.get("max") == 5 and ov.get("kills", 0) >= 3 and 3 <= ov.get("coopKills", 0) <= ov.get("kills", 0)
          and ov.get("attendToday") == 2 and ov.get("contribWeek", 0) > 0 and ov.get("titles", 0) & 0b110 == 0b110)
    # 덧보상(참여 2명 ≥ 2 → 50%): Kill 임무 인장 30 → 45
    mdone = [e for e in m1.xnfys(E_MDONE, t0) if e["value"] == 1]
    check(f"참여 덧보상 50% — Kill 임무 인장 30 → 45 {mdone}", bool(mdone) and mdone[0]["value2"] == 45)
    sts = m1.stats()
    check(f"기여도 — 둘 다 이번 주 기여 > 0 · 새 결사원 표시 {[(rp.cstr(s['name']), s['week'], s['flags']) for s in sts]}",
          len([s for s in sts if s["week"] > 0]) >= 2 and all(s["flags"] & 2 for s in sts))
    kinds = {r["kind"] for r in lead.all_logs(3)}
    check(f"기록 — 성장 거르기에 레벨 · 임무 · 업적 · 출석 {sorted(kinds)}", kinds == {L_LEVEL, L_MISSION, L_ACH, L_ATTEND})
    check("기록 — 들어옴(결사원 거르기)", sum(1 for r in lead.all_logs(1) if r["kind"] == L_JOINED) == 2)
    trea = lead.log_rows(2)
    check("기록 — 금고 거르기에는 인장 줄만", bool(trea) and all(r["kind"] in (L_SEALS_IN, L_SEALS_OUT) for r in trea))
    check("2레벨 — 넷째 초대 됨", invite(lead, m3, gid))
    CTX["seals"] = lead.overview().get("seals", 0)
    return st["ok"], "; ".join(notes) or f"레벨 2 · 임무 3 · 업적 {len(ach)} · 금고 {CTX['seals']}"


def scenario_perms(port: int):
    notes = []
    check, st = check_factory(notes)
    lead, m1, m2, m3 = CTX["lead"], CTX["m1"], CTX["m2"], CTX["m3"]
    t0 = time.time()
    lead.guild(G_SETRANK, arg8=1, target=m1.name)   # 부관(구조 변경은 결사마다 한 번에 하나 — 응답을 기다렸다 다음)
    lead.wait(lambda: lead.gacks(G_SETRANK, t0), 4.0)
    lead.guild(G_SETRANK, arg8=2, target=m2.name)   # 정예
    lead.wait(lambda: len(lead.gacks(G_SETRANK, t0)) >= 2, 4.0)
    check("직위 — 부관 · 정예", [a["result"] for a in lead.gacks(G_SETRANK, t0)] == [GR_OK, GR_OK])
    t1 = time.time()
    m2.guild(G_KICK, target=m3.name)
    check("정예(기본 권한)는 추방 못 함", m2.wait(lambda: m2.gacks(G_KICK, t1), 3.0) and m2.gacks(G_KICK, t1)[0]["result"] == GR_NOPERM)
    t2 = time.time()
    m1.gx(X_PERMS, 2, P_INVITE | P_KICK | P_SHOP)
    check("결사장만 권한을 바꾼다(부관 거절)", (m1.xack(X_PERMS, t2) or {}).get("result") == XR_NOPERM)
    t3 = time.time()
    lead.gx(X_PERMS, 2, P_INVITE | P_KICK | P_SHOP)
    check("결사장이 정예에게 추방 권한", (lead.xack(X_PERMS, t3) or {}).get("result") == XR_OK)
    check("권한 바뀜 알림", m2.wait(lambda: m2.xnfys(E_PERMS, t3), 3.0))
    t4 = time.time()
    m2.guild(G_KICK, target=m3.name)
    check("정예가 결사원을 추방", m2.wait(lambda: m2.gacks(G_KICK, t4), 3.0) and m2.gacks(G_KICK, t4)[0]["result"] == GR_OK)
    t5 = time.time()
    lead.gx(X_PERMS, 1, 0x7F & ~P_INVITE)
    lead.xack(X_PERMS, t5)
    t6 = time.time()
    m1.guild(G_INVITE, target=m3.name)
    check("부관의 초대 권한을 거두면 초대 거절", m1.wait(lambda: m1.gacks(G_INVITE, t6), 3.0) and m1.gacks(G_INVITE, t6)[0]["result"] == GR_NOPERM)
    ov = m1.overview()
    check(f"개요의 권한표 {ov.get('p1')} {ov.get('p2')}", ov.get("p1") == 0x7F & ~P_INVITE and ov.get("p2") == P_INVITE | P_KICK | P_SHOP)
    t7 = time.time()
    lead.gx(X_PERMS, 1, 0x7F)
    lead.xack(X_PERMS, t7)
    check("기록 — 권한 · 직위 · 추방", {L_PERMS, L_RANK, L_KICKED} <= {r["kind"] for r in lead.all_logs(1)})
    return st["ok"], "; ".join(notes) or "권한표 · 추방 · 초대 거절"


def scenario_board(port: int):
    notes = []
    check, st = check_factory(notes)
    lead, m1, m2 = CTX["lead"], CTX["m1"], CTX["m2"]
    t0 = time.time()
    m2.gx(X_WRITE, 0, title="공지입니다", body="정예는 공지를 못 쓴다")
    check("공지 분류는 공지 권한(정예 거절)", (m2.xack(X_WRITE, t0) or {}).get("result") == XR_NOPERM)
    time.sleep(5.2)   # 글 쓰기 간격(5초 — 거절된 글도 센다)
    t1 = time.time()
    m2.gx(X_WRITE, 4, title="사냥 같이", body="올벤 숲에서 함께해요")
    a = m2.xack(X_WRITE, t1)
    check("정예의 자유 글", (a or {}).get("result") == XR_OK)
    post1 = (a or {}).get("param", 0)
    check("다른 결사원에게 글 알림", lead.wait(lambda: lead.xnfys(E_POSTED, t1), 3.0))
    t2 = time.time()
    lead.gx(X_WRITE, 0, title="이번 주 목표", body="임무 셋 모두 달성")
    a2 = lead.xack(X_WRITE, t2)
    check("결사장의 공지", (a2 or {}).get("result") == XR_OK)
    t3 = time.time()
    lead.gx(X_PIN, 1, (a2 or {}).get("param", 0))
    check("고정(게시판 권한)", (lead.xack(X_PIN, t3) or {}).get("result") == XR_OK)
    t4 = time.time()
    m2.gx(X_PIN, 1, post1)
    check("정예는 고정 못 함", (m2.xack(X_PIN, t4) or {}).get("result") == XR_NOPERM)
    rows = m1.board()
    check(f"목록 — 고정 글 먼저 {[(r['title'], r['pinned']) for r in rows]}", len(rows) == 2 and rows[0]["pinned"] == 1 and rows[0]["title"] == "이번 주 목표")
    p = m1.ask(X_READ, INF_POST, 0, post1)
    check("글 읽기 — 본문 · 게시판 권한(부관)은 내릴 수 있다", p is not None and rp.cstr(p[66:217]) == "올벤 숲에서 함께해요" and p[14] == 1)
    p2 = m2.ask(X_READ, INF_POST, 0, (a2 or {}).get("param", 0))
    check("남의 글은 내릴 수 없다(정예)", p2 is not None and rp.cstr(p2[35:66]) == "이번 주 목표" and p2[14] == 0)
    t5 = time.time()
    m2.gx(X_DELETE, 0, (a2 or {}).get("param", 0))
    check("남의 글 내리기 거절", (m2.xack(X_DELETE, t5) or {}).get("result") == XR_NOPERM)
    time.sleep(5.2)   # 글 쓰기 간격(5초)
    t6 = time.time()
    m2.gx(X_WRITE, 4, title="운영자 공지", body="금칙어")
    check("금칙어는 BadText", (m2.xack(X_WRITE, t6) or {}).get("result") == XR_BAD_TEXT)
    t7 = time.time()
    m1.gx(X_WRITE, 3, title="희귀 단검", body="서리 단검 나왔어요")
    check("부관의 아이템 글", (m1.xack(X_WRITE, t7) or {}).get("result") == XR_OK)
    time.sleep(5.2)
    t8 = time.time()
    m1.gx(X_WRITE, 2, title="보스 공략", body="모로즈는 서리 장판을 피하세요")   # 넷째 — 한도 3: 고정 안 된 가장 오래된 글(post1)이 내려간다
    check("한도 넘은 넷째 글", (m1.xack(X_WRITE, t8) or {}).get("result") == XR_OK)
    rows = m1.board()
    check(f"한도 3 — 고정 글은 남고 가장 오래된 글이 내려갔다 {[r['title'] for r in rows]}",
          len(rows) == 3 and "사냥 같이" not in [r["title"] for r in rows] and "이번 주 목표" in [r["title"] for r in rows])
    gp = m1.board(2)
    check("분류 거르기(공략)", [r["title"] for r in gp] == ["보스 공략"])
    t9 = time.time()
    m1.gx(X_DELETE, 0, gp[0]["id"] if gp else 0)
    check("내 글 내리기", (m1.xack(X_DELETE, t9) or {}).get("result") == XR_OK)
    CTX["posts"] = [r["title"] for r in m1.board()]
    return st["ok"], "; ".join(notes) or f"글 {len(CTX['posts'])}"


def scenario_shop(port: int):
    notes = []
    check, st = check_factory(notes)
    lead, m1, m2 = CTX["lead"], CTX["m1"], CTX["m2"]
    seals, rows = m1.shop()
    check(f"상점 목록 · 금고 {seals}", seals and len(rows) == 3)
    t0 = time.time()
    m1.gx(X_BUY, 0, 1)
    a = m1.xack(X_BUY, t0, 6.0)
    check(f"사기 — 약초 물약 5개(인장 50) {a}", (a or {}).get("result") == XR_OK)
    slots = [p for p in m1.packets(T_ITEM_SLOT, t0) if struct.unpack_from("<H", p, 5)[0] == 1]
    check("소지품 칸이 왔다(약초 물약)", bool(slots))
    lead.wait(lambda: lead.xnfys(E_BOUGHT, t0), 3.0)
    ev = lead.xnfys(E_SEALS, t0)
    check(f"금고 − 50 알림 {ev[-1] if ev else None}", bool(ev) and ev[-1]["value"] == -50 and ev[-1]["value2"] == seals - 50 and ev[-1]["who"] == m1.name)
    check("구매 알림", lead.wait(lambda: lead.xnfys(E_BOUGHT, t0), 2.0))
    t1 = time.time()
    m1.gx(X_BUY, 0, 1)
    check("주 한도(1)", (m1.xack(X_BUY, t1) or {}).get("result") == XR_LIMIT)
    t2 = time.time()
    m1.gx(X_BUY, 0, 2)
    check("인장 부족", (m1.xack(X_BUY, t2) or {}).get("result") == XR_NO_SEALS)
    t3 = time.time()
    m1.gx(X_BUY, 0, 3)
    check("결사 레벨 부족(3레벨 물건)", (m1.xack(X_BUY, t3) or {}).get("result") == XR_LEVEL_LOW)
    t4 = time.time()
    lead.gx(X_PERMS, 2, P_INVITE)   # 정예: 상점 권한 거둠
    lead.xack(X_PERMS, t4)
    t5 = time.time()
    m2.gx(X_BUY, 0, 1)
    check("상점 권한이 없으면 거절", (m2.xack(X_BUY, t5) or {}).get("result") == XR_NOPERM)
    trea = lead.log_rows(2)
    out = [r for r in trea if r["kind"] == L_SEALS_OUT]
    check(f"기록 — 금고에서 나감(산 사람 · 아이템) {out[:1]}", bool(out) and rp.cstr(out[0]["who"]) == m1.name and rp.cstr(out[0]["what"]) == "약초 물약")
    CTX["seals"] = lead.overview().get("seals", 0)
    return st["ok"], "; ".join(notes) or f"금고 {seals} → {CTX['seals']}"


def scenario_recruit(port: int):
    notes = []
    check, st = check_factory(notes)
    lead = CTX["lead"]
    o = GBot(port, rp.nm(40, FIELD[0], FIELD[1] + 1, "o"))
    q = GBot(port, rp.nm(10, FIELD[0], FIELD[1] - 1, "q"))
    r = GBot(port, rp.nm(40, FIELD[0] + 1, FIELD[1] + 1, "r"))
    CTX["o"] = o
    try:
        t0 = time.time()
        lead.gx(X_INTRO, 1, 30, body="올벤 숲 사냥 결사 — 레벨 30부터")
        check("가입 안내 · 모집 열기", (lead.xack(X_INTRO, t0) or {}).get("result") == XR_OK)
        p = o.ask(X_RLIST, INF_REC)
        found = [struct.unpack_from("<I", p, 7 + i * 36)[0] for i in range(p[6])] if p else []
        check("결사가 없는 사람의 모집 목록", CTX["gid"] in found)
        pub = o.ask(X_RVIEW, INF_PUB, 0, CTX["gid"])
        check("공개 정보 — 가입 안내 · 레벨 · 최소 레벨", pub is not None and rp.cstr(pub[87:208]).startswith("올벤 숲") and pub[9] == 2 and pub[15] == 30)
        t1 = time.time()
        q.gx(X_APPLY, 0, CTX["gid"])
        check("레벨 미달 신청", (q.xack(X_APPLY, t1) or {}).get("result") == XR_LEVEL_LOW)
        t2 = time.time()
        o.gx(X_APPLY, 0, CTX["gid"])
        check("신청", (o.xack(X_APPLY, t2) or {}).get("result") == XR_OK)
        check("승인 권한이 있는 결사원에게 신청 알림", lead.wait(lambda: lead.xnfys(E_APPLIED, t2), 3.0))
        t3 = time.time()
        o.gx(X_APPLY, 0, CTX["gid"])
        check("두 번 신청", (o.xack(X_APPLY, t3) or {}).get("result") == XR_ALREADY)
        t4 = time.time()
        r.gx(X_APPLY, 0, CTX["gid"])
        r.xack(X_APPLY, t4)
        ap = lead.ask(X_APPLICANTS, INF_APP)
        names = [rp.cstr(ap[5 + i * 26: 25 + i * 26]) for i in range(ap[4])] if ap else []
        check(f"신청자 목록 {names}", names == [o.name, r.name])
        t5 = time.time()
        lead.gx(X_APPLY_REPLY, 0, target=r.name)
        check("거절", (lead.xack(X_APPLY_REPLY, t5) or {}).get("result") == XR_OK and r.wait(lambda: r.xnfys(E_APPLY_RESULT, t5), 3.0)
              and r.xnfys(E_APPLY_RESULT, t5)[0]["value"] == 0)
        t6 = time.time()
        lead.gx(X_APPLY_REPLY, 1, target=o.name)
        check("승인", (lead.xack(X_APPLY_REPLY, t6) or {}).get("result") == XR_OK)
        check("승인된 사람 — 결과 알림 · 결사 정보", o.wait(lambda: o.xnfys(E_APPLY_RESULT, t6) and (o.ginfo(t6) or {}).get("guild") == CTX["gid"], 4.0))
        t7 = time.time()
        r.gx(X_APPLY_CANCEL)
        check("거둘 신청이 없다", (r.xack(X_APPLY_CANCEL, t7) or {}).get("result") == XR_NOT_FOUND)
        check("기록 — 신청 승인", L_ACCEPTED in {x["kind"] for x in lead.log_rows(1)})
    finally:
        q.close()
        r.close()
    return st["ok"], "; ".join(notes) or "모집 · 신청 · 승인 · 거절"


def scenario_claim(port: int):
    notes = []
    check, st = check_factory(notes)
    lead, m1, m2 = CTX["lead"], CTX["m1"], CTX["m2"]
    t0 = time.time()
    m1.gx(X_CLAIM)
    check("결사장이 접속 중이면 청구 못 함", (m1.xack(X_CLAIM, t0) or {}).get("result") == XR_NOT_YET)
    lead_name = lead.name
    lead.close()
    rp.pump([m1, m2], 4.0)
    t1 = time.time()
    m2.gx(X_CLAIM)
    check("부관이 있으면 정예는 청구 못 함", (m2.xack(X_CLAIM, t1) or {}).get("result") == XR_NOPERM)
    t2 = time.time()
    m1.gx(X_CLAIM)
    check("부관의 청구", (m1.xack(X_CLAIM, t2) or {}).get("result") == XR_OK)
    check("청구 알림", m2.wait(lambda: m2.xnfys(E_CLAIM_START, t2), 3.0))
    ov = m2.overview()
    check(f"개요에 청구한 사람 {ov.get('claimant')}", ov.get("claimant") == m1.name)
    check("기다림 뒤 결사장이 된다", m1.wait(lambda: m1.xnfys(E_CLAIM_DONE, t2), 8.0))
    check("결사 정보 — 새 결사장", m2.wait(lambda: (m2.ginfo(t2) or {}).get("leader") == m1.name, 3.0))
    # 취소: 새 결사장(m1)이 나가고, 예전 결사장(이제 부관)이 청구 → m1이 돌아오면 취소
    old = GBot(port, lead_name)
    CTX["lead"] = old
    m1_name = m1.name
    m1.close()
    rp.pump([old, m2], 4.0)
    t3 = time.time()
    old.gx(X_CLAIM)
    check("예전 결사장(부관)의 청구", (old.xack(X_CLAIM, t3) or {}).get("result") == XR_OK)
    back = GBot(port, m1_name)
    CTX["m1"] = back
    check("결사장이 돌아오면 청구 취소", old.wait(lambda: old.xnfys(E_CLAIM_CANCEL, t3), 4.0))
    rp.pump([old, back, m2], 4.0)
    check("취소 뒤 결사장 그대로", (back.ginfo() or {}).get("leader") == m1_name and not old.xnfys(E_CLAIM_DONE, t3))
    kinds = {r["kind"] for r in back.all_logs(1)}
    check(f"기록 — 청구 · 넘김 · 취소 {sorted(kinds)}", {L_CLAIM_START, L_CLAIM_DONE, L_CLAIM_CANCEL} <= kinds)
    CTX["leader_name"] = m1_name
    return st["ok"], "; ".join(notes) or "청구 → 결사장 · 돌아오면 취소"


def snapshot_for_persist():
    """다시 켜기 전 — 저장(10초 모음)을 기다리고 기대값을 적어 둔다"""
    who = CTX["m1"]
    ov = who.overview()
    CTX["persist"] = {"name": who.name, "level": ov.get("level"), "exp": ov.get("exp"), "seals": ov.get("seals"),
                      "contribTotal": ov.get("contribTotal"), "kills": ov.get("kills"), "achDone": ov.get("achDone"),
                      "posts": CTX.get("posts", []), "logs": len(who.log_rows(0)) > 0}
    rp.pump([b for b in (CTX["m1"], CTX["m2"]) if b], 12.0)
    for k in ("lead", "m1", "m2", "m3", "o"):
        if CTX.get(k):
            CTX[k].close()


def scenario_persist(port: int):
    notes = []
    check, st = check_factory(notes)
    want = CTX["persist"]
    b = GBot(port, want["name"])
    try:
        ov = b.overview()
        for k in ("level", "exp", "seals", "contribTotal", "kills", "achDone"):
            check(f"{k} {want[k]} → {ov.get(k)}", ov.get(k) == want[k])
        check(f"게시판 {want['posts']}", sorted(r["title"] for r in b.board()) == sorted(want["posts"]))
        check("기록", len(b.log_rows(0)) > 0)
        check("이번 주 임무가 그대로(달성 표시)", all(m["done"] for m in b.missions()))
    finally:
        b.close()
    return st["ok"], "; ".join(notes) or f"레벨 {want['level']} · 금고 {want['seals']}"


def scenario_week(port: int):
    """따로 띄운 서버(주 60초 · 날 30초)"""
    notes = []
    check, st = check_factory(notes)
    lead = GBot(port, rp.nm(20, *HALL, "W"))
    w1 = GBot(port, rp.nm(40, FIELD[0], FIELD[1] + 2, "w"))
    w2 = GBot(port, rp.nm(40, FIELD[0] + 1, FIELD[1] + 2, "x"))
    try:
        gid = make_guild(lead, f"주간{TAG}", w1, w2)
        check("세우기", gid != 0)
        week0 = w1.overview().get("week")
        killed = hunt([w1, w2], 1)
        check("한 마리", killed == 1)
        rp.pump([lead, w1, w2], 3.5)
        ov = w1.overview()
        check(f"기여 · 출석이 오른다 {ov.get('contribWeek')} {ov.get('attendToday')}", ov.get("contribWeek", 0) > 0 and ov.get("attendToday", 0) >= 1)
        t0 = time.time()
        w1.gx(X_BUY, 0, 1)
        check("상점(주 1회)", (w1.xack(X_BUY, t0, 6.0) or {}).get("result") in (XR_OK, XR_NO_SEALS))
        # 다음 주까지
        left = ov.get("weekLeft", 60)
        rp.pump([lead, w1, w2], min(left, 65) + 2.0)
        ov2 = w1.overview()
        check(f"새 주 — 주 번호가 바뀌고 이번 주 기여 0 · 누적은 그대로 {week0}→{ov2.get('week')} {ov2.get('contribWeek')} {ov2.get('contribTotal')}",
              ov2.get("week") != week0 and ov2.get("contribWeek") == 0 and ov2.get("contribTotal", 0) == ov.get("contribTotal"))
        check("새 날 — 오늘 출석 0", ov2.get("attendToday") == 0)
        mis = w1.missions()
        check(f"새 주의 임무(진행 0) {[(m['id'], m['progress']) for m in mis]}", len(mis) == 3 and all(m["progress"] == 0 for m in mis))
        _, rows = w1.shop()
        check("새 주 — 상점 한도가 비었다", all(r["bought"] == 0 for r in rows))
    finally:
        for b in (lead, w1, w2):
            b.close()
    return st["ok"], "; ".join(notes) or "주 · 날 넘김"


# ── 뼈대 ────────────────────────────────────────────────────────────────────
def make_fixture(src: Path, dst: Path, week: bool):
    from dt import export
    shutil.copytree(src, dst)
    patch = rp.patch_rows

    def monster(r):
        if not r.get("dummy"):
            r.update({"max_hp": 300, "attack": 1, "attack_interval_ms": 5000, "move_interval_ms": 60000, "aggressive": False, "exp": 1000})

    def const(r):
        r.update({"kill_exp_pct": 50, "member_daily_exp_cap": 1000000, "attend_active_min": 1, "mission_slots": 3,
                  "mission_bonus_members": 2, "mission_bonus_pct": 50, "leader_absent_sec": 3, "claim_grace_sec": 3,
                  "post_max": 3, "log_keep": 50, "apply_expire_sec": 600, "apply_max": 2, "coop_new_days": 7})
        if week:
            # 주 40초 · 날 20초 — 지금 시각이 주의 처음 쪽에 오게(봇이 사냥할 틈)
            now = int(time.time())
            r.update({"week_period_sec": 60, "week_offset_sec": now % 60, "day_period_sec": 30, "day_offset_sec": now % 30})

    def write(name, rows):
        body = ",\n".join("{" + ", ".join(f"{json.dumps(k)}: {json.dumps(v, ensure_ascii=False)}" for k, v in row.items()) + "}" for row in rows)
        (dst / f"{name}.json").write_text('{"table": "%s", "rows": [\n%s\n]}\n' % (name, body), encoding="utf-8", newline="\n")
    patch(dst / "Monster.json", monster)
    patch(dst / "TierLoot.json", lambda r: r.update({"common_bp": 10000, "uncommon_bp": 0, "rare_bp": 0, "essence_permille": 0, "crystal_permille": 0}))
    patch(dst / "LootConst.json", lambda r: r.update({"elite_permille": 0}))
    patch(dst / "GuildConst.json", const)
    write("GuildLevel", [{"level": 1, "exp": 0, "max_members": 3, "frame": 0, "title_id": 0},
                         {"level": 2, "exp": 1500, "max_members": 5, "frame": 1, "title_id": 1},
                         {"level": 3, "exp": 900000, "max_members": 6, "frame": 2, "title_id": 0}])
    write("GuildTitle", [{"id": 1, "name": "첫 깃발"}, {"id": 2, "name": "사냥의 시작"}])
    write("GuildMission", [
        {"id": 1, "name": "세 마리", "kind": "Kill", "target": 3, "sub": 0, "min_level": 1, "exp": 100, "seals": 30, "contrib": 5, "weight": 10},
        {"id": 2, "name": "장비 둘", "kind": "Gear", "target": 2, "sub": 0, "min_level": 1, "exp": 100, "seals": 20, "contrib": 5, "weight": 10},
        {"id": 3, "name": "둘이 함께", "kind": "Active", "target": 2, "sub": 1, "min_level": 1, "exp": 100, "seals": 20, "contrib": 5, "weight": 10},
        {"id": 4, "name": "의뢰", "kind": "Quest", "target": 1, "sub": 0, "min_level": 3, "exp": 100, "seals": 10, "contrib": 5, "weight": 10}])
    write("GuildAchievement", [
        {"id": 1, "name": "첫 처치", "kind": "Kills", "target": 1, "seals": 200, "exp": 0, "title_id": 2},
        {"id": 2, "name": "두 번째 깃발", "kind": "Level", "target": 2, "seals": 50, "exp": 0, "title_id": 0},
        {"id": 3, "name": "셋이 모임", "kind": "Members", "target": 3, "seals": 10, "exp": 0, "title_id": 0}])
    write("GuildAttend", [{"tier": 1, "members": 1, "seals": 5, "exp": 10, "contrib": 1},
                          {"tier": 2, "members": 2, "seals": 10, "exp": 20, "contrib": 1}])
    write("GuildShop", [{"id": 1, "item_id": 1, "count": 5, "price": 50, "min_level": 1, "weekly_limit": 1},
                        {"id": 2, "item_id": 10, "count": 1, "price": 100000, "min_level": 1, "weekly_limit": 0},
                        {"id": 3, "item_id": 8, "count": 1, "price": 10, "min_level": 3, "weekly_limit": 0}])
    (dst / "manifest.json").write_text(export.manifest_text(dst), encoding="utf-8", newline="\n")


class Server:
    def __init__(self, exe: str, data: Path, port: int, work: Path, journal: Path, tag: str):
        env = dict(os.environ, HARVEN_MEMDB_JOURNAL=str(journal))
        self.log = open(work / f"server_{tag}.log", "w")
        self.p = subprocess.Popen([exe, "--data-dir", str(data), "--port", str(port)], stdout=self.log, stderr=subprocess.STDOUT, cwd=work, env=env)

    def stop(self):
        self.p.terminate()
        try:
            self.p.wait(10)
        except subprocess.TimeoutExpired:
            self.p.kill()
        self.log.close()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--server", default=str(REPO / "build" / "linux-memory" / "GameServer"))
    ap.add_argument("--port", type=int, default=4250)
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--only", default="", help="쉼표로(growth,perms,board,shop,recruit,claim,persist,week) — persist는 앞 시나리오가 필요하다")
    args = ap.parse_args()
    exe = str(Path(args.server).resolve())
    only = {s for s in args.only.split(",") if s}
    work = Path(tempfile.mkdtemp(prefix="harven_guild_"))
    make_fixture(REPO / "Bin" / "GameData", work / "GameData", week=False)
    journal = work / "memdb.journal"
    results = []

    def run(name, fn, port):
        try:
            ok, msg = fn(port)
        except (ConnectionError, OSError, RuntimeError) as e:
            ok, msg = False, f"연결 문제 — {e}"
        results.append(ok)
        print(f"{'PASS' if ok else 'FAIL'} {name}: {msg}", flush=True)
        return ok

    main_set = [("growth", scenario_growth), ("perms", scenario_perms), ("board", scenario_board), ("shop", scenario_shop),
                ("recruit", scenario_recruit), ("claim", scenario_claim)]
    try:
        if not only or only & {n for n, _ in main_set} or "persist" in only:
            srv = Server(exe, work / "GameData", args.port, work, journal, "a")
            try:
                if not rp.wait_port(args.port, 60):
                    print("FAIL: 서버가 뜨지 않았다")
                    return 1
                time.sleep(1.5)
                for name, fn in main_set:
                    if only and name not in only and "persist" not in only:
                        continue
                    if not run(name, fn, args.port) and name == "growth":
                        break
                    if srv.p.poll() is not None:
                        print(f"FAIL: 서버가 죽었다(종료 코드 {srv.p.returncode})")
                        return 1
                alive = srv.p.poll() is None
                print(f"{'PASS' if alive else 'FAIL'}: 서버 {'살아 있음' if alive else '죽음'}", flush=True)
                results.append(alive)
                if (not only or "persist" in only) and "m1" in CTX:
                    snapshot_for_persist()
            finally:
                srv.stop()
            if (not only or "persist" in only) and "persist" in CTX:
                srv = Server(exe, work / "GameData", args.port, work, journal, "b")
                try:
                    if rp.wait_port(args.port, 60):
                        time.sleep(1.5)
                        run("persist", scenario_persist, args.port)
                finally:
                    srv.stop()
        if not only or "week" in only:
            make_fixture(REPO / "Bin" / "GameData", work / "GameDataWeek", week=True)
            srv = Server(exe, work / "GameDataWeek", args.port + 1, work, work / "week.journal", "w")
            try:
                if rp.wait_port(args.port + 1, 60):
                    time.sleep(1.5)
                    run("week", scenario_week, args.port + 1)
            finally:
                srv.stop()
        print(f"[regress_guild] done pass={sum(results)} fail={len(results) - sum(results)}")
        return 0 if results and all(results) else 1
    finally:
        if args.keep:
            print(f"[keep] {work}")
        else:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
