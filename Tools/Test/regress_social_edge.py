"""파티 · 결사 가장자리 회귀 시험 — 나가고 · 쫓겨나고 · 흩어지고 · 다시 들어온 뒤에도 다시 만들고 · 들어가고 · 신청할 수 있는가.

    python3 Tools/Test/regress_social_edge.py [--server build/linux-memory/GameServer] [--port 4600] [--only rejoin,seq] [--keep]

사용자 보고 「파티에 가입했다가 나가면 다시 파티를 만들거나 재가입이 안 된다」에서 시작했다. 클라가 나간 뒤 순번(seq)을 0으로
되돌린 것이 원인이었고(서버 멱등 창이 Duplicate로 막는다), 서버 쪽은 모든 경로에서 상태가 남지 않는지 · 순번 계약이 분명한지를 본다.
HARVEN_IN_MEMORY_DB=ON 빌드를 이 스크립트가 직접 띄운다. 시험용 GameData 사본: PartyConst — 초대 10초 · 끊긴 파티원 5초 ·
모집 글 600초 · 상태 모으기 0.2초. GuildConst — 신청 만료 600초 · 처치 경험치 50% · 하루 한도 100. Monster — HP 300 · 공격력 1 · 비선공 · 경험치 1000.
봇 · 걷기 도우미는 regress_party.py · regress_guild.py의 것을 같이 쓴다.

시나리오(Shared/Document/PARTY.md 9장 · GUILD.md 7장)
  rejoin    파티 탈퇴 → 새로 만들기 → 해산 → 다시 초대받아 들어가기 → 추방 → 파티가 없는 사람의 초대로 새 파티 → 탈퇴 →
            모집 게시판 신청 · 승인 → 해산 → 다시 만들기(순번은 이어서 늘린다)
  seq       순번 계약: 같은 세션에서 순번을 되돌리면 Duplicate(param = 마지막으로 받은 순번) → param + 1로 다시 → 된다 ·
            다시 접속하면(새 세션) 1부터 · 0은 언제나 Duplicate
  inviter   초대한 사람이 나갔거나 · 쫓겨났거나 · 초대 권한을 잃은 뒤의 수락은 InviteExpired(예전: 그대로 들어갔다) ·
            해산한 파티의 초대 · 그 뒤 스스로 만들기
  recruit   가득 찬 파티의 승인 → Full이고 신청은 대기로 남는다(예전: 알림 없이 사라졌다) · 신청자가 다른 파티에 들어가면 신청이 거둬진다 ·
            글을 올린 파티가 흩어지면 신청자에게 RecruitClosed(Disbanded) · 혼자 남은 파티장이 나가면 글이 목록에서 사라지고 다시 올릴 수 있다
  relog     파티 중 다시 접속(순번 1부터) · 같은 계정 중복 접속(먼저 세션을 끊는다) · 오래 끊겨 빠진 뒤 만들기 · 초대
  last      마지막 남은 사람 · 혼자 남은 파티의 초대 · 탈퇴 = 해산 · 인원 한도
  guild     결사 탈퇴 → 다시 초대 · 추방 → 다시 들어오기 · 스스로 세우기 → 해산 → 같은 이름으로 다시 세우기 ·
            나간 사람에게 빈 개요(GUILD_EX_INF Overview 결사 0)도 · 순번 Duplicate의 param · 다시 접속(순번 1부터)
  gapply    가입 신청 뒤 스스로 결사를 세우면 신청이 거둬진다(예전: 남아 승인 목록 · AlreadyApplied) ·
            신청 대기 중 결사가 해산하면 신청자에게 ApplyResult 2(예전: 알림 없음) · 그 뒤 다른 결사에 신청
  glimit    하루 경험치 한도(시험 사본 100)에 닿은 결사원이 나갔다 다시 들어와도 오늘 몫이 이어진다(예전: 0부터 — 한도 · 출석 · 주 상점 한도 우회)
"""
from __future__ import annotations

import argparse
import shutil
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import regress_party as rp    # noqa: E402
import regress_guild as rg    # noqa: E402

REPO = rp.REPO
TAG = rp.TAG
HALL = rg.HALL


def checker(notes: list):
    state = {"ok": True}

    def check(label, cond):
        notes.append(f"{label}:{'ok' if cond else 'FAIL'}")
        if not cond:
            state["ok"] = False
        return bool(cond)
    return check, state


def ack(bot: rp.Bot, op: int, since: float, seconds: float = 3.0):
    """since 뒤 op의 첫 PARTY_ACK(없으면 {})"""
    bot.wait(lambda: bot.acks(op, since), seconds)
    a = bot.acks(op, since)
    return a[0] if a else {}


def req(bot: rp.Bot, op: int, **kw):
    """요청 하나 → 그 순번의 응답(없으면 {})"""
    t0 = time.time()
    seq = bot.party(op, **kw)
    bot.pseq = max(bot.pseq, seq)   # 순번을 직접 준 뒤에도 다음 자동 순번은 그보다 크게
    bot.wait(lambda: [a for a in bot.acks(op, t0) if a["seq"] == seq], 3.0)
    got = [a for a in bot.acks(op, t0) if a["seq"] == seq]
    return got[0] if got else {}


def invite_reply(inviter: rp.Bot, invitee: rp.Bot, accept: bool = True):
    """inviter가 invitee를 초대 → invitee가 답한다. (초대 응답, 답 응답)"""
    t0 = time.time()
    ia = req(inviter, rp.OP_INVITE, target=invitee.name)
    if ia.get("result") != rp.R_OK:
        return ia, {}
    if not invitee.wait(lambda: invitee.nfys(rp.EV_INVITE_FROM, t0), 3.0):
        return ia, {}
    inv = invitee.nfys(rp.EV_INVITE_FROM, t0)[-1]
    return ia, req(invitee, rp.OP_REPLY, arg8=1 if accept else 0, arg32=inv["arg32"])


def roster_ok(bot: rp.Bot, names) -> bool:
    return bot.wait(lambda: bot.roster_names() == sorted(names), 3.0)


def no_party(bot: rp.Bot) -> bool:
    return bot.wait(lambda: not bot.roster.get("party"), 3.0)


def post(bot: rp.Bot, slots=3, auto=0, title="같이 사냥해요"):
    return req(bot, rp.OP_RPOST, purpose=rp.PURPOSE_HUNT, minl=1, maxl=50, slots=slots, arg8=auto, text=title)


def recruit_list(bot: rp.Bot):
    time.sleep(0.55)   # 묻기 간격 0.5초
    t = time.time()
    bot.party(rp.OP_RLIST, arg8=0xFF, arg32=0)
    bot.wait(lambda: any(p[3] == rp.INF_RLIST for p in bot.packets(rp.T_PARTY_INF, t)), 3.0)
    rows = []
    for p in [p for p in bot.packets(rp.T_PARTY_INF, t) if p[3] == rp.INF_RLIST][-1:]:
        for i in range(p[11]):
            o = 12 + i * 76
            rid, pur, mn, mx, mem, slots, flags = struct.unpack_from("<IBBBBBB", p, o)
            rows.append({"id": rid, "members": mem, "slots": slots, "flags": flags, "leader": rp.cstr(p[o + 10:o + 30])})
    return rows


def applicants(bot: rp.Bot):
    time.sleep(0.55)
    t = time.time()
    bot.party(rp.OP_RAPPLICANTS)
    bot.wait(lambda: any(p[3] == rp.INF_APPLICANTS for p in bot.packets(rp.T_PARTY_INF, t)) or bot.acks(rp.OP_RAPPLICANTS, t), 3.0)
    ap = [p for p in bot.packets(rp.T_PARTY_INF, t) if p[3] == rp.INF_APPLICANTS]
    return [rp.cstr(ap[-1][9 + i * 22:9 + i * 22 + 20]) for i in range(ap[-1][8])] if ap else None


def close_all(bots):
    for b in bots:
        b.close()


# ── 파티 ────────────────────────────────────────────────────────────────────
def scenario_rejoin(port: int):
    """사용자 보고 그대로: 들어갔다 나간 뒤 만들기 · 다시 들어가기 · 모집 신청 — 순번은 이어서 늘린다(올바른 클라)"""
    a, b, c = rp.Bot(port, f"sr{TAG}a"), rp.Bot(port, f"sr{TAG}b"), rp.Bot(port, f"sr{TAG}c")
    notes = []
    check, st = checker(notes)
    try:
        check("a 만들기", req(a, rp.OP_CREATE).get("result") == rp.R_OK)
        check("b 초대 수락", invite_reply(a, b)[1].get("result") == rp.R_OK and roster_ok(b, [a.name, b.name]))
        check("b 탈퇴", req(b, rp.OP_LEAVE).get("result") == rp.R_OK and no_party(b) and roster_ok(a, [a.name]))
        r = req(b, rp.OP_CREATE)
        check("탈퇴 뒤 b가 만들기", r.get("result") == rp.R_OK and r.get("param") != a.roster.get("party") and roster_ok(b, [b.name]))
        check("b 탈퇴 = 해산(혼자)", req(b, rp.OP_LEAVE).get("result") == rp.R_OK and no_party(b))
        check("다시 초대받아 들어가기", invite_reply(a, b)[1].get("result") == rp.R_OK and roster_ok(b, [a.name, b.name]))
        t0 = time.time()
        check("추방", req(a, rp.OP_KICK, target=b.name).get("result") == rp.R_OK and b.wait(lambda: b.nfys(rp.EV_KICKED, t0)) and no_party(b))
        # 파티가 없는 c의 초대 — 받아들이면 c가 파티장인 새 파티
        check("추방 뒤 다른 사람의 초대로 새 파티", invite_reply(c, b)[1].get("result") == rp.R_OK and roster_ok(b, [b.name, c.name])
              and b.leader_name() == c.name)
        check("탈퇴", req(b, rp.OP_LEAVE).get("result") == rp.R_OK and no_party(b))
        # 모집 게시판: a의 글에 신청 → 승인
        r = post(a, slots=4)
        rid = r.get("param", 0)
        check("a 글 올리기", r.get("result") == rp.R_OK and rid != 0)
        t0 = time.time()
        r = req(b, rp.OP_RAPPLY, arg32=rid)
        check("탈퇴 뒤 모집 신청", r.get("result") == rp.R_OK and r.get("param") == 0 and a.wait(lambda: a.nfys(rp.EV_R_APPLIED, t0)))
        check("승인 → 들어옴", req(a, rp.OP_RACCEPT, target=b.name).get("result") == rp.R_OK and roster_ok(b, [a.name, b.name]))
        t0 = time.time()
        check("해산", req(a, rp.OP_DISBAND).get("result") == rp.R_OK and b.wait(lambda: b.nfys(rp.EV_DISBANDED, t0)) and no_party(b))
        check("해산 뒤 b 만들기", req(b, rp.OP_CREATE).get("result") == rp.R_OK and roster_ok(b, [b.name]))
        check("해산 뒤 a가 b의 파티에", invite_reply(b, a)[1].get("result") == rp.R_OK and roster_ok(a, [a.name, b.name]))
        # 파티 채팅이 새 파티로 간다(Zone 사본 m_PartyId가 새 번호)
        t0 = time.time()
        a.chat(rp.CH_PARTY, "다시 왔어요")
        check("새 파티 채팅", b.wait(lambda: any(p[3] == rp.CH_PARTY for p in b.packets(rp.T_SC_CHAT, t0)))
              and not any(p[3] == rp.CH_PARTY for p in c.packets(rp.T_SC_CHAT, t0)))
        return st["ok"], " · ".join(notes)
    finally:
        close_all([a, b, c])


def scenario_seq(port: int):
    """순번 계약 — 멱등은 지키고(같은 순번은 한 번), 되돌린 클라가 스스로 맞출 수 있게 Duplicate가 마지막 순번을 알려 준다"""
    a, b = rp.Bot(port, f"sq{TAG}a"), rp.Bot(port, f"sq{TAG}b")
    notes = []
    check, st = checker(notes)
    try:
        check("순번 0은 Duplicate", req(b, rp.OP_CREATE, seq=0).get("result") == rp.R_DUPLICATE)
        check("만들기(순번 5)", req(b, rp.OP_CREATE, seq=5).get("result") == rp.R_OK)
        check("탈퇴(순번 6)", req(b, rp.OP_LEAVE, seq=6).get("result") == rp.R_OK and no_party(b))
        # 같은 순번을 다시 보내면(재전송) 한 번만 — 결과는 Duplicate, 상태는 그대로
        r = req(b, rp.OP_LEAVE, seq=6)
        check("재전송 = Duplicate(param 6)", r.get("result") == rp.R_DUPLICATE and r.get("param") == 6)
        # 나간 뒤 클라가 순번을 1로 되돌렸다(사용자 보고의 원인) — 거절되지만 param이 마지막 순번
        r = req(b, rp.OP_CREATE, seq=1)
        check(f"되돌린 순번 = Duplicate · param 6 {r}", r.get("result") == rp.R_DUPLICATE and r.get("param") == 6)
        check("Duplicate는 상태를 바꾸지 않는다", not b.roster.get("party"))
        # 클라가 param + 1로 맞춰 다시 보내면 된다
        check("param + 1로 다시 → 만들기", req(b, rp.OP_CREATE, seq=7).get("result") == rp.R_OK and roster_ok(b, [b.name]))
        check("Duplicate 뒤에도 창은 그대로(param 7)", req(b, rp.OP_CREATE, seq=7).get("param") == 7)
        # 묻기(Info)는 순번을 보지 않는다
        time.sleep(0.55)
        t0 = time.time()
        b.party(rp.OP_INFO, seq=1)
        check("묻기는 순번과 무관", b.wait(lambda: any(p[3] == rp.INF_ROSTER for p in b.packets(rp.T_PARTY_INF, t0))) and not b.acks(rp.OP_INFO, t0))
        # 다시 접속하면 새 세션 — 1부터
        b.close()
        b = rp.Bot(port, b.name)
        check("다시 접속 — 파티 그대로", roster_ok(b, [b.name]))
        check("다시 접속 뒤 순번 1 탈퇴", req(b, rp.OP_LEAVE, seq=1).get("result") in (rp.R_OK, rp.R_NOT_IN_PARTY) and no_party(b))
        check("순번 2 만들기", req(b, rp.OP_CREATE, seq=2).get("result") == rp.R_OK)
        check("초대(순번 3) → a 수락", invite_reply(b, a)[0].get("seq") == 3 and roster_ok(a, [a.name, b.name]))
        return st["ok"], " · ".join(notes)
    finally:
        close_all([a, b])


def scenario_inviter(port: int):
    """초대는 초대한 사람의 지금 권한으로만 — 나갔거나 · 쫓겨났거나 · 권한을 잃었으면 InviteExpired"""
    L = rp.Bot(port, f"si{TAG}L")
    m = rp.Bot(port, f"si{TAG}m")
    n = rp.Bot(port, f"si{TAG}n")
    t1, t2, t3, t4 = (rp.Bot(port, f"si{TAG}t{i}") for i in range(1, 5))
    bots = [L, m, n, t1, t2, t3, t4]
    notes = []
    check, st = checker(notes)
    try:
        check("파티", req(L, rp.OP_CREATE).get("result") == rp.R_OK)
        check("누구나 초대", req(L, rp.OP_SETTINGS, arg8=1).get("result") == rp.R_OK)
        check("m · n 들어옴", invite_reply(L, m)[1].get("result") == rp.R_OK and invite_reply(L, n)[1].get("result") == rp.R_OK
              and roster_ok(L, [L.name, m.name, n.name]))

        def pending(inviter, target):
            t0 = time.time()
            ok = req(inviter, rp.OP_INVITE, target=target.name).get("result") == rp.R_OK and target.wait(lambda: target.nfys(rp.EV_INVITE_FROM, t0))
            return target.nfys(rp.EV_INVITE_FROM, t0)[-1]["arg32"] if ok else 0

        # 1) 파티원 m이 t1을 초대 → 파티장이 m을 쫓아낸다 → t1이 받아들여도 들어가지 않는다
        inv = pending(m, t1)
        check("m → t1 초대", inv != 0)
        check("m 추방", req(L, rp.OP_KICK, target=m.name).get("result") == rp.R_OK and no_party(m))
        r = req(t1, rp.OP_REPLY, arg8=1, arg32=inv)
        check(f"쫓겨난 사람의 초대 = 만료 {r}", r.get("result") == rp.R_INVITE_EXPIRED and not t1.roster.get("party")
              and roster_ok(L, [L.name, n.name]))
        # 2) n이 t2를 초대 → n이 나간다 → 만료
        inv = pending(n, t2)
        check("n 탈퇴", inv != 0 and req(n, rp.OP_LEAVE).get("result") == rp.R_OK)
        r = req(t2, rp.OP_REPLY, arg8=1, arg32=inv)
        check(f"나간 사람의 초대 = 만료 {r}", r.get("result") == rp.R_INVITE_EXPIRED and roster_ok(L, [L.name]))
        # 3) 파티원이 초대한 뒤 파티장이 「파티장만 초대」로 바꾸면 만료
        check("n 다시 들어옴", invite_reply(L, n)[1].get("result") == rp.R_OK)
        inv = pending(n, t3)
        check("파티장만 초대로", inv != 0 and req(L, rp.OP_SETTINGS, arg8=0).get("result") == rp.R_OK)
        r = req(t3, rp.OP_REPLY, arg8=1, arg32=inv)
        check(f"권한을 잃은 사람의 초대 = 만료 {r}", r.get("result") == rp.R_INVITE_EXPIRED and roster_ok(L, [L.name, n.name]))
        # 4) 파티장의 초대는 그대로 된다(기준)
        check("파티장의 초대는 된다", invite_reply(L, t3)[1].get("result") == rp.R_OK and roster_ok(t3, [L.name, n.name, t3.name]))
        # 5) 해산한 파티의 초대 → 만료, 그 뒤 스스로 만들 수 있다
        inv = pending(L, t4)
        t0 = time.time()
        check("해산", inv != 0 and req(L, rp.OP_DISBAND).get("result") == rp.R_OK and t3.wait(lambda: t3.nfys(rp.EV_DISBANDED, t0)))
        check("해산한 파티의 초대 = 만료", req(t4, rp.OP_REPLY, arg8=1, arg32=inv).get("result") == rp.R_INVITE_EXPIRED)
        check("그 뒤 t4가 만들기", req(t4, rp.OP_CREATE).get("result") == rp.R_OK and roster_ok(t4, [t4.name]))
        # 6) 쫓겨난 m은 아무 일 없이 다시 초대받는다
        check("쫓겨난 m을 t4가 초대", invite_reply(t4, m)[1].get("result") == rp.R_OK and roster_ok(m, [m.name, t4.name]))
        return st["ok"], " · ".join(notes)
    finally:
        close_all(bots)


def scenario_recruit(port: int):
    L, x, p1 = rp.Bot(port, f"sc{TAG}L"), rp.Bot(port, f"sc{TAG}x"), rp.Bot(port, f"sc{TAG}p1")
    L2, p2, c = rp.Bot(port, f"sc{TAG}M"), rp.Bot(port, f"sc{TAG}p2"), rp.Bot(port, f"sc{TAG}c")
    L3, p3 = rp.Bot(port, f"sc{TAG}N"), rp.Bot(port, f"sc{TAG}p3")
    bots = [L, x, p1, L2, p2, c, L3, p3]
    notes = []
    check, st = checker(notes)
    try:
        # 1) 목표 2명 글 · p1 신청(대기) → 파티장이 x를 초대해 2명 → p1 승인은 Full, 신청은 남는다
        rid = post(L, slots=2).get("param", 0)
        r = req(p1, rp.OP_RAPPLY, arg32=rid)
        check("p1 신청(대기)", rid != 0 and r.get("result") == rp.R_OK and r.get("param") == 0)
        check("x 초대로 목표 인원", invite_reply(L, x)[1].get("result") == rp.R_OK and roster_ok(L, [L.name, x.name]))
        r = req(L, rp.OP_RACCEPT, target=p1.name)
        check(f"가득 찬 파티의 승인 = Full {r}", r.get("result") == rp.R_FULL)
        check("p1은 여전히 신청자 목록에", applicants(L) == [p1.name])
        check("p1은 여전히 신청 중(거두기 Ok)", req(p1, rp.OP_RCANCEL).get("result") == rp.R_OK)
        check("거둔 뒤에는 NotApplied", req(p1, rp.OP_RCANCEL).get("result") == rp.R_NOT_APPLIED)
        # 다시 신청 → 파티장이 승인하기 전에 자리가 차면 Full이지만 대기는 남고, x가 나가 자리가 나면 승인된다
        check("x 탈퇴 → 자리", req(x, rp.OP_LEAVE).get("result") == rp.R_OK and roster_ok(L, [L.name]))
        check("p1 다시 신청", req(p1, rp.OP_RAPPLY, arg32=rid).get("result") == rp.R_OK)
        check("x 다시 들어옴(가득)", invite_reply(L, x)[1].get("result") == rp.R_OK)
        check("가득 찬 동안 승인 = Full", req(L, rp.OP_RACCEPT, target=p1.name).get("result") == rp.R_FULL and applicants(L) == [p1.name])
        check("x 추방 → 자리", req(L, rp.OP_KICK, target=x.name).get("result") == rp.R_OK)
        check("자리가 나면 승인", req(L, rp.OP_RACCEPT, target=p1.name).get("result") == rp.R_OK and roster_ok(p1, [L.name, p1.name]))
        # 2) 신청자가 다른 파티에 들어가면 신청이 거둬진다
        rid2 = post(L2, slots=4).get("param", 0)
        check("p2 신청", req(p2, rp.OP_RAPPLY, arg32=rid2).get("result") == rp.R_OK)
        check("p2가 c의 파티에", invite_reply(c, p2)[1].get("result") == rp.R_OK)
        check("거둬진 신청자 승인 = 없음", req(L2, rp.OP_RACCEPT, target=p2.name).get("result") == rp.R_TARGET_NOT_FOUND
              and applicants(L2) == [])
        check("p2 신청 상태 없음", req(p2, rp.OP_RCANCEL).get("result") == rp.R_NOT_APPLIED)
        # 3) 글을 올린 파티가 흩어지면 신청자에게 닫힘(Disbanded) → 다른 글에 신청할 수 있다
        rid3 = post(L3, slots=4).get("param", 0)
        check("p3 신청", req(p3, rp.OP_RAPPLY, arg32=rid3).get("result") == rp.R_OK)
        t0 = time.time()
        check("L3 탈퇴(혼자) = 해산", req(L3, rp.OP_LEAVE).get("result") == rp.R_OK)
        check("신청자에게 RecruitClosed(Disbanded)", p3.wait(lambda: p3.nfys(rp.EV_R_CLOSED, t0))
              and p3.nfys(rp.EV_R_CLOSED, t0)[-1]["arg8"] == rp.CLOSE_DISBANDED and p3.nfys(rp.EV_R_CLOSED, t0)[-1]["arg32"] == rid3)
        check("목록에서 사라짐", rid3 not in [row["id"] for row in recruit_list(p3)])
        check("p3 다른 글에 신청", req(p3, rp.OP_RAPPLY, arg32=rid2).get("result") == rp.R_OK)
        r = post(L3, slots=3)
        check("L3 다시 글 올리기(새 파티)", r.get("result") == rp.R_OK and r.get("param") not in (0, rid3) and roster_ok(L3, [L3.name]))
        return st["ok"], " · ".join(notes)
    finally:
        close_all(bots)


def scenario_relog(port: int):
    a, b = rp.Bot(port, f"sl{TAG}a"), rp.Bot(port, f"sl{TAG}b")
    notes = []
    check, st = checker(notes)
    try:
        check("파티", rp.form_party(a, b) and roster_ok(b, [a.name, b.name]))
        # 1) 다시 접속(파티원으로 남는다) — 새 세션의 순번은 1부터
        b.close()
        b = rp.Bot(port, b.name)
        check("다시 접속 — 그대로 파티원", roster_ok(b, [a.name, b.name]))
        check("새 세션 순번 1 탈퇴", req(b, rp.OP_LEAVE, seq=1).get("result") == rp.R_OK and no_party(b) and roster_ok(a, [a.name]))
        check("순번 2 만들기", req(b, rp.OP_CREATE, seq=2).get("result") == rp.R_OK)
        check("b 해산", req(b, rp.OP_DISBAND).get("result") == rp.R_OK and no_party(b))
        check("다시 a의 파티로", invite_reply(a, b)[1].get("result") == rp.R_OK and roster_ok(b, [a.name, b.name]))
        # 2) 같은 계정 중복 접속 — 먼저 있던 세션이 끊기고 새 세션이 파티원
        old = b
        b = rp.Bot(port, old.name)
        try:
            old.poll(1.0)
        except ConnectionError:
            pass
        check("중복 접속 — 새 세션 파티원 · 접속 표시", roster_ok(b, [a.name, b.name])
              and a.wait(lambda: all(r["flags"] & rp.F_ONLINE for r in a.roster["rows"].values())))
        old.close()
        check("중복 접속 뒤 탈퇴", req(b, rp.OP_LEAVE, seq=1).get("result") == rp.R_OK and no_party(b))
        check("중복 접속 뒤 만들기", req(b, rp.OP_CREATE, seq=2).get("result") == rp.R_OK)
        check("b 탈퇴", req(b, rp.OP_LEAVE).get("result") == rp.R_OK)
        # 3) 오래 끊겨 빠진 뒤(5초) — 파티 없음 · 만들기 · 초대
        check("다시 a의 파티로", invite_reply(a, b)[1].get("result") == rp.R_OK)
        t0 = time.time()
        b.close()
        check("오래 끊기면 빠짐", a.wait(lambda: a.nfys(rp.EV_TIMEOUT, t0), 10.0) and roster_ok(a, [a.name]))
        b = rp.Bot(port, b.name)
        check("빠진 뒤 접속 = 파티 없음", b.wait(lambda: any(p[3] == rp.INF_ROSTER for p in b.packets(rp.T_PARTY_INF))) and not b.roster.get("party"))
        check("빠진 뒤 초대 수락", invite_reply(a, b)[1].get("result") == rp.R_OK and roster_ok(b, [a.name, b.name]))
        return st["ok"], " · ".join(notes)
    finally:
        close_all([a, b])


def scenario_last(port: int):
    a, b, c = rp.Bot(port, f"sz{TAG}a"), rp.Bot(port, f"sz{TAG}b"), rp.Bot(port, f"sz{TAG}c")
    extra = [rp.Bot(port, f"sz{TAG}e{i}") for i in range(7)]
    notes = []
    check, st = checker(notes)
    try:
        check("셋", rp.form_party(a, b, c) and roster_ok(a, [a.name, b.name, c.name]))
        check("b · c 탈퇴 → 혼자", req(b, rp.OP_LEAVE).get("result") == rp.R_OK and req(c, rp.OP_LEAVE).get("result") == rp.R_OK
              and roster_ok(a, [a.name]) and a.leader_name() == a.name)
        check("혼자 남은 파티의 초대", invite_reply(a, b)[1].get("result") == rp.R_OK and roster_ok(a, [a.name, b.name]))
        t0 = time.time()
        check("파티장 탈퇴 → b가 파티장 · 혼자", req(a, rp.OP_LEAVE).get("result") == rp.R_OK and b.wait(lambda: b.leader_name() == b.name)
              and roster_ok(b, [b.name]))
        check("마지막 사람 탈퇴 = 해산", req(b, rp.OP_LEAVE).get("result") == rp.R_OK and no_party(b)
              and req(b, rp.OP_LEAVE).get("result") == rp.R_NOT_IN_PARTY)
        # 인원 한도(8): 가득 → 초대 Full · 한 명 나가면 다시
        check("만들기", req(a, rp.OP_CREATE).get("result") == rp.R_OK)
        for e in extra:
            invite_reply(a, e)
        check("8명", roster_ok(a, [a.name, *[e.name for e in extra]]))
        check("가득 → Full", req(a, rp.OP_INVITE, target=b.name).get("result") == rp.R_FULL)
        check("한 명 탈퇴 → 다시 초대", req(extra[0], rp.OP_LEAVE).get("result") == rp.R_OK
              and invite_reply(a, b)[1].get("result") == rp.R_OK and roster_ok(b, [a.name, b.name, *[e.name for e in extra[1:]]]))
        return st["ok"], " · ".join(notes)
    finally:
        close_all([a, b, c, *extra])


# ── 결사 ────────────────────────────────────────────────────────────────────
def gack(bot: rg.GBot, op: int, since: float, seconds: float = 5.0):
    bot.wait(lambda: bot.gacks(op, since), seconds)
    a = bot.gacks(op, since)
    return a[0] if a else {}


def greq(bot: rg.GBot, op: int, **kw):
    t0 = time.time()
    bot.guild(op, **kw)
    return gack(bot, op, t0)


def overview_guild(bot: rg.GBot, since: float):
    """since 뒤 마지막 GUILD_EX_INF Overview의 결사 번호(없으면 None)"""
    p = bot.inf(rg.INF_OV, since)
    return struct.unpack_from("<I", p, 4)[0] if p is not None else None


def g_invite(leader: rg.GBot, m: rg.GBot, gid: int) -> bool:
    return rg.invite(leader, m, gid)


GR_ALREADY, GR_LEADER_MUST, GR_DUP = 2, 16, 15


def scenario_guild(port: int):
    L = rg.GBot(port, rp.nm(20, *HALL, "L"))
    m = rg.GBot(port, rp.nm(20, *HALL, "M"))
    k = rg.GBot(port, rp.nm(20, *HALL, "K"))
    notes = []
    check, st = checker(notes)
    try:
        gid = rg.make_guild(L, f"재입{TAG}")
        check("세우기", gid != 0)
        check("m 들어옴", g_invite(L, m, gid))
        t0 = time.time()
        check("m 탈퇴", greq(m, rg.G_LEAVE).get("result") == rg.GR_OK and m.wait(lambda: (m.ginfo(t0) or {}).get("guild") == 0))
        check("나간 사람에게 빈 개요(결사 0)", m.wait(lambda: overview_guild(m, t0) == 0, 3.0))
        check("탈퇴 뒤 다시 초대", g_invite(L, m, gid))
        t0 = time.time()
        check("추방", greq(L, rg.G_KICK, target=m.name).get("result") == rg.GR_OK and m.wait(lambda: (m.ginfo(t0) or {}).get("guild") == 0))
        check("쫓겨난 사람에게 빈 개요", m.wait(lambda: overview_guild(m, t0) == 0, 3.0))
        check("추방 뒤 다시 들어옴", g_invite(L, m, gid))
        check("m 다시 탈퇴", greq(m, rg.G_LEAVE).get("result") == rg.GR_OK)
        # 탈퇴한 m이 스스로 세운다 → 해산 → 같은 이름으로 다시
        name2 = f"새집{TAG}"
        gid2 = rg.make_guild(m, name2)
        check("탈퇴 뒤 스스로 세우기", gid2 != 0 and gid2 != gid)
        check("결사장은 탈퇴 대신 해산", greq(m, rg.G_LEAVE).get("result") == GR_LEADER_MUST)
        t0 = time.time()
        check("해산", greq(m, rg.G_DISBAND).get("result") == rg.GR_OK and m.wait(lambda: (m.ginfo(t0) or {}).get("guild") == 0)
              and m.wait(lambda: overview_guild(m, t0) == 0, 3.0))
        gid3 = rg.make_guild(m, name2)
        check("같은 이름으로 다시 세우기", gid3 != 0 and gid3 != gid2)
        check("다른 사람(k)을 새 결사에", g_invite(m, k, gid3))
        # 순번: 되돌리면 Duplicate · param = 마지막 순번 / 다시 접속하면 1부터
        check("권한 없는 공지(순번은 오른다)", greq(k, rg.G_NOTICE, text="공지").get("result") == rg.GR_NOPERM)
        last = k.gseq
        t0 = time.time()
        k.c.sendall(struct.pack("<HBBIBI20s92s", 125, rg.T_GUILD_REQ, rg.G_LEAVE, 1, 0, 0, b"", b""))
        r = gack(k, rg.G_LEAVE, t0)
        check(f"되돌린 순번 = Duplicate · param {last} {r}", r.get("result") == GR_DUP and r.get("param") == last)
        k.close()
        k = rg.GBot(port, k.name)
        check("다시 접속 — 결사원 그대로", k.wait(lambda: (k.ginfo() or {}).get("guild") == gid3))
        check("새 세션 순번 1 탈퇴", greq(k, rg.G_LEAVE).get("result") == rg.GR_OK and k.gseq == 1)
        check("다시 k를 L의 결사로", g_invite(L, k, gid))
        return st["ok"], " · ".join(notes)
    finally:
        close_all([L, m, k])


def scenario_gapply(port: int):
    L = rg.GBot(port, rp.nm(20, *HALL, "P"))
    a = rg.GBot(port, rp.nm(20, *HALL, "Q"))
    b = rg.GBot(port, rp.nm(20, *HALL, "R"))
    notes = []
    check, st = checker(notes)

    def xreq(bot, op, arg8=0, arg32=0, **kw):
        time.sleep(0.3)
        t0 = time.time()
        bot.gx(op, arg8, arg32, **kw)
        return bot.xack(op, t0) or {}

    def applicant_names(leader):
        ap = leader.ask(rg.X_APPLICANTS, rg.INF_APP)
        return [rp.cstr(ap[5 + i * 26: 25 + i * 26]) for i in range(ap[4])] if ap else None
    try:
        gid = rg.make_guild(L, f"모집{TAG}")
        check("세우기 · 모집 열기", gid != 0 and xreq(L, rg.X_INTRO, 1, 1, body="누구나 오세요").get("result") == rg.XR_OK)
        check("a 신청", xreq(a, rg.X_APPLY, 0, gid).get("result") == rg.XR_OK and applicant_names(L) == [a.name])
        # a가 신청해 둔 채 스스로 결사를 세운다 → 신청은 거둬진다
        gid_a = rg.make_guild(a, f"딴집{TAG}")
        check("신청 뒤 스스로 세우기", gid_a != 0)
        check("세운 사람의 신청은 거둬진다(승인 목록에서 빠짐)", applicant_names(L) == [])
        r = xreq(L, rg.X_APPLY_REPLY, 1, target=a.name)
        check(f"승인할 신청이 없다 {r}", r.get("result") == rg.XR_NOT_FOUND)
        # b가 a의 결사에 신청한 채 a가 해산 → b에게 결과(2 = 끝남)
        check("a 모집 열기", xreq(a, rg.X_INTRO, 1, 1, body="환영").get("result") == rg.XR_OK)
        check("b가 a의 결사에 신청", xreq(b, rg.X_APPLY, 0, gid_a).get("result") == rg.XR_OK)
        t0 = time.time()
        check("a 해산", greq(a, rg.G_DISBAND).get("result") == rg.GR_OK)
        res = lambda: [e for e in b.xnfys(rg.E_APPLY_RESULT, t0) if e["value2"] == gid_a]
        check("해산한 결사의 신청자에게 결과 2", b.wait(lambda: res(), 3.0) and res()[0]["value"] == 2)
        # 해산한 a는 L의 결사에 다시 신청할 수 있다(남은 신청이 없다)
        r = xreq(a, rg.X_APPLY, 0, gid)
        check(f"해산 뒤 a가 다시 신청 {r}", r.get("result") == rg.XR_OK)
        check("b도 L의 결사에 신청", xreq(b, rg.X_APPLY, 0, gid).get("result") == rg.XR_OK and applicant_names(L) == [a.name, b.name])
        check("승인 → a 결사원", xreq(L, rg.X_APPLY_REPLY, 1, target=a.name).get("result") == rg.XR_OK
              and a.wait(lambda: (a.ginfo() or {}).get("guild") == gid, 4.0))
        return st["ok"], " · ".join(notes)
    finally:
        close_all([L, a, b])


def scenario_glimit(port: int):
    """나갔다 다시 들어와도 오늘 · 이번 주 한도 몫은 잇는다 — 하루 경험치 한도(시험 사본 100)가 새로 시작하지 않는다"""
    L = rg.GBot(port, rp.nm(20, *HALL, "G"))
    w = rg.GBot(port, rp.nm(20, *rg.FIELD, "w"))
    notes = []
    check, st = checker(notes)
    try:
        gid = rg.make_guild(L, f"한도{TAG}", w)
        check("세우기 · w 들어옴", gid != 0)
        check("w 사냥 1마리", rg.hunt([w], 1, seconds=90.0) == 1)
        ov = w.overview()
        check(f"오늘 올려 준 경험치 = 한도 {ov.get('expToday')}/{ov.get('expCap')}", ov.get("expToday") == 100 and ov.get("expCap") == 100)
        exp0 = ov.get("exp")
        check("w 탈퇴", greq(w, rg.G_LEAVE).get("result") == rg.GR_OK)
        check("다시 초대", g_invite(L, w, gid))
        ov = w.overview()
        check(f"다시 들어와도 오늘 몫 그대로 {ov.get('expToday')}", ov.get("expToday") == 100)
        check("w 사냥 1마리 더", rg.hunt([w], 1, seconds=90.0) == 1)
        ov = w.overview()
        check(f"결사 경험치가 한도를 넘지 않는다 {exp0}→{ov.get('exp')}", ov.get("exp") == exp0 and ov.get("expToday") == 100)
        return st["ok"], " · ".join(notes)
    finally:
        close_all([L, w])


# ── 뼈대 ────────────────────────────────────────────────────────────────────
def make_fixture(src: Path, dst: Path):
    from dt import export
    shutil.copytree(src, dst)
    rp.patch_rows(dst / "PartyConst.json", lambda r: r.update({"invite_expire_ms": 10000, "offline_keep_sec": 5, "sync_interval_ms": 200,
                                                                "recruit_idle_sec": 600}))
    # 결사: 신청 만료 600초 · 처치 경험치 50%(몬스터 1000 → 500) · 하루 한도 100(첫 처치에 닿는다) · 출석은 닿지 않게
    rp.patch_rows(dst / "GuildConst.json", lambda r: r.update({"apply_expire_sec": 600, "apply_max": 4, "kill_exp_pct": 50,
                                                                "member_daily_exp_cap": 100, "attend_active_min": 600}))

    def monster(r):
        if not r.get("dummy"):
            r.update({"max_hp": 300, "attack": 1, "attack_interval_ms": 5000, "move_interval_ms": 60000, "aggressive": False, "exp": 1000})
    rp.patch_rows(dst / "Monster.json", monster)
    rp.patch_rows(dst / "LootConst.json", lambda r: r.update({"elite_permille": 0}))
    (dst / "manifest.json").write_text(export.manifest_text(dst), encoding="utf-8", newline="\n")


SCENARIOS = [("rejoin", scenario_rejoin), ("seq", scenario_seq), ("inviter", scenario_inviter), ("recruit", scenario_recruit),
             ("relog", scenario_relog), ("last", scenario_last), ("guild", scenario_guild), ("gapply", scenario_gapply),
             ("glimit", scenario_glimit)]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--server", default=str(REPO / "build" / "linux-memory" / "GameServer"))
    ap.add_argument("--port", type=int, default=4600)
    ap.add_argument("--keep", action="store_true", help="시험용 GameData 사본 · 서버 로그를 남긴다")
    ap.add_argument("--only", default="", help="쉼표로 시나리오 이름(" + ",".join(n for n, _ in SCENARIOS) + ")")
    args = ap.parse_args()

    work = Path(tempfile.mkdtemp(prefix="harven_social_"))
    make_fixture(REPO / "Bin" / "GameData", work / "GameData")
    log = open(work / "server.log", "w")
    srv = subprocess.Popen([str(Path(args.server).resolve()), "--data-dir", str(work / "GameData"), "--port", str(args.port)],
                           stdout=log, stderr=subprocess.STDOUT, cwd=work)
    results = []
    try:
        if not rp.wait_port(args.port, 90):
            print("FAIL: 서버가 뜨지 않았다")
            return 1
        time.sleep(2.0)
        # 포트가 열린 뒤에도 Zone이 다 서기 전에는 로그인 응답이 늦다(검사기 빌드 · 부하) — 한 번 들어와 볼 때까지 기다린다
        end = time.time() + 90
        while True:
            try:
                rp.Bot(args.port, f"warm{TAG}").close()
                break
            except (ConnectionError, OSError, RuntimeError):
                if time.time() >= end:
                    print("FAIL: 로그인이 되지 않는다")
                    return 1
                time.sleep(1.0)
        only = {s for s in args.only.split(",") if s}
        for name, fn in SCENARIOS:
            if only and name not in only:
                continue
            try:
                ok, msg = fn(args.port)
            except (ConnectionError, OSError, RuntimeError) as e:
                ok, msg = False, f"연결 문제 — {e}"
            results.append(ok)
            print(f"{'PASS' if ok else 'FAIL'} {name}: {msg}", flush=True)
            if srv.poll() is not None:
                print(f"FAIL: 서버가 죽었다(종료 코드 {srv.returncode})")
                return 1
        alive = srv.poll() is None
        print(f"{'PASS' if alive else 'FAIL'}: 서버 {'살아 있음' if alive else '죽음'}")
        return 0 if alive and results and all(results) else 1
    finally:
        srv.terminate()
        try:
            srv.wait(15)
        except subprocess.TimeoutExpired:
            srv.kill()
        log.close()
        if args.keep:
            print(f"[keep] {work}")
        else:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
