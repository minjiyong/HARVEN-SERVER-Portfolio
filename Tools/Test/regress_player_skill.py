"""플레이어 기술 회귀 시험 — 세 직업의 모든 기술 칸을 훈련장 허수아비에게 정해진 순서로 써 보고, 시전자가 받은
기술 패킷(SKILL_RESULT_ACK · SKILL_NFY · SUBJECT_DAMAGE_NFY · STATUS_NFY · EFFECT_NFY · SUBJECT_FORCED_MOVE_NFY ·
COMBAT_GAUGE_INF)을 시각과 함께 적는다.

    python3 Tools/Test/regress_player_skill.py [--server build/linux-memory/GameServer] [--base 옛 서버] [--port 4150]
                                               [--classes 0,1,2] [--configs base,a,b] [--only 이름조각] [--keep]

  --base가 없으면: 시나리오마다 기대한 단계(누름 → 알림 → 타격 · 끊김 · 재사용 대기 거절 …)가 나오는지만 본다.
  --base가 있으면: 같은 시나리오를 옛 서버(--base)와 새 서버(--server)로 각각 돌려 기록을 맞대어 본다 — 기술 구조를
                   고칠 때 "손맛이 그대로인가"(같은 패킷 · 같은 순서 · 같은 시각 ±허용)를 확인하는 A/B.
  비교에서 빼는 것: 피해량 · HP(무작위 굴림), 치명타 비트, MP(자연 회복 시각), 서버 시각.

HARVEN_IN_MEMORY_DB=ON 빌드를 이 스크립트가 직접 띄운다. 시험용 GameData 사본은 기술 재사용 대기를 1초 이하로,
MP 소모를 0으로 바꾼다(고유 게이지를 빨리 채우고, 자연 회복 시각에 따라 MP 부족이 섞이지 않게 — 두 서버가 같은 사본을 쓴다).
샌드박스 이름 규칙: "lvNN_xyX_Y_…" = 레벨 NN 캐릭터가 (X, Y)에서 시작(Tools/Sandbox/MemDBConnection.cpp).
시나리오마다 새로 접속한다(자리 · 재사용 대기 · 게이지가 처음 상태). 기술 성장(갈래)은 같은 이름이면 남는다.
"""
from __future__ import annotations

import argparse
import json
import select
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "Tools" / "DataTool"))

T_LOGIN, T_LOGIN_ACK, T_MOVE_REQ, T_ADD = 35, 11, 53, 21
T_RESULT, T_SKILL_NFY, T_DAMAGE, T_EFFECT, T_STATUS, T_FORCED, T_GAUGE, T_STATS = 57, 81, 49, 50, 59, 82, 83, 60
T_SKILL_REQ, T_GROW_REQ, T_GROW_ACK = 80, 86, 87
PRESS, RELEASE, CANCEL = 0, 1, 2
UP, DOWN, LEFT, RIGHT = 0, 1, 2, 3
DAMAGE_CRIT = 1
DUMMIES = {(778, 755): "d1", (781, 755): "d2", (784, 755): "d3"}
STAMP = int(time.time()) % 100000
TOL_MS = 70
FUZZ_MS = 30   # 시각에서 나온 값(남은 재사용 대기 · 상태 남은 시간)은 이만큼 흔들려도 같다고 본다


def fz(v: int):
    """시각에서 나온 숫자 — 비교할 때 ±FUZZ_MS를 허용한다"""
    return ("~", int(v))


def objid(b: bytes, off: int):
    return struct.unpack_from("<HHIQ", b, off)


# ── 시나리오 정의 ───────────────────────────────────────────────────────────
class S:
    """기술 요청 하나(t ms에). tgt = 허수아비 이름(d1 · d2 · d3) — aim이 없으면 대상 칸을 조준한다."""
    def __init__(self, skill, t, tgt=None, aim=None, phase=PRESS, force=False):
        self.skill, self.t, self.tgt, self.aim, self.phase, self.force = skill, t, tgt, aim, phase, force


class M:
    """한 칸 걷기(t ms에)"""
    def __init__(self, direction, t):
        self.dir, self.t = direction, t


class P:
    def __init__(self, name, cls, pos, steps, settle=1500, fill=None):
        self.name, self.cls, self.pos, self.steps, self.settle, self.fill = name, cls, pos, steps, settle, fill


def basic_spam(skill, tgt, n, every, start=0):
    return [S(skill, start + i * every, tgt) for i in range(n)]


# 고유 게이지 채우기(시나리오 전에 — 기록하지 않는다): (기술, 대상, 횟수, 간격)
FILL_KNIGHT = [(3, None, 7, 1800)]                 # 회전 베기(홀딩): 틱마다 둘레 허수아비 — 기술 타격은 +22
FILL_ENGRAVER = [(8, "d2", 1, 0), (9, None, 1, 0), (11, "d2", 1, 0), (27, "d2", 1, 0), (28, "d2", 1, 0)] * 5   # 시전마다 +55
FILL_HUNTER = [(13, "d1", 16, 450)]                # 허수아비 등 뒤에서 기본 공격: 타격마다 +70


def scenarios():
    K, E, H = 0, 1, 2
    at = (780, 755)       # d1(778)과 d2(781) 사이 — d2가 바로 옆
    near1 = (779, 755)    # d1 바로 동쪽(허수아비는 서쪽을 본다 — 등 뒤)
    out = []
    # ── 서약기사 ──
    out += [
        P("K.basic", K, at, basic_spam(1, "d2", 6, 330) + [S(1, 2100, "d2"), S(1, 2150, "d2")]),
        P("K.bash", K, at, [S(2, 0, "d2"), S(2, 300, "d2")]),
        P("K.spin.release", K, at, [S(3, 0), S(3, 700, phase=RELEASE)]),
        P("K.spin.timeout", K, at, [S(3, 0)], settle=2600),
        P("K.spin.cancel", K, at, [S(3, 0), S(3, 400, phase=CANCEL)]),
        P("K.spin.walk", K, at, [S(3, 0), M(DOWN, 450)]),
        P("K.spin.other", K, at, [S(3, 0), S(2, 500, "d2")]),
        P("K.cleave.early", K, at, [S(19, 0, aim=(784, 755)), S(19, 200, aim=(784, 755), phase=RELEASE)]),
        P("K.cleave.mid", K, at, [S(19, 0, aim=(784, 755)), S(19, 820, aim=(784, 755), phase=RELEASE)]),
        P("K.cleave.full", K, at, [S(19, 0, aim=(784, 755)), S(19, 1180, aim=(784, 755), phase=RELEASE)]),
        P("K.cleave.timeout", K, at, [S(19, 0, aim=(784, 755))], settle=2400),
        P("K.cleave.cancel", K, at, [S(19, 0, aim=(784, 755)), S(19, 300, phase=CANCEL)]),
        P("K.cleave.walk", K, at, [S(19, 0, aim=(784, 755)), M(UP, 300)]),
        P("K.throw.chain", K, at, [S(20, 0, "d3"), S(20, 700, "d3"), S(20, 1300, "d3")], settle=1800),
        P("K.throw.single", K, at, [S(20, 0, "d1")], settle=3600),
        P("K.dash", K, (776, 755), [S(4, 0, aim=(781, 755))]),
        P("K.guard.off", K, at, [S(5, 0), S(5, 1700), S(5, 1900)]),
        P("K.guard.expire", K, at, [S(5, 0)], settle=7000),
        P("K.rally", K, at, [S(21, 0), S(1, 400, "d2")]),
        P("K.bell", K, at, [S(22, 0, aim=(781, 755))], settle=2000),
        P("K.bell.walk", K, at, [S(22, 0, aim=(781, 755)), M(DOWN, 300)], settle=2000),
        P("K.bell.walklate", K, at, [S(22, 0, aim=(781, 755)), M(DOWN, 800)], settle=2000),
        P("K.bell.roll", K, at, [S(22, 0, aim=(781, 755)), S(23, 900, aim=(780, 758))], settle=2000),
        P("K.bell.twice", K, at, [S(22, 0, aim=(781, 755)), S(22, 1200, aim=(784, 755))], settle=2600),
        P("K.judgement", K, at, [S(6, 0, "d2")], settle=2400),
        P("K.roll", K, at, [S(23, 0, aim=(780, 758)), S(23, 100, aim=(780, 752))]),
        P("K.oath", K, at, [S(24, 0), S(1, 300, "d2"), S(2, 700, "d2")], fill=FILL_KNIGHT, settle=2000),
    ]
    # ── 각인술사 ──
    out += [
        P("E.basic", E, (777, 757), basic_spam(7, "d2", 5, 380)),
        P("E.frost", E, (777, 757), [S(8, 0, aim=(781, 755)), S(8, 150, aim=(781, 755))], settle=4200),
        P("E.ring", E, at, [S(9, 0)], settle=4200),
        P("E.chain", E, (777, 757), [S(11, 0, "d2")]),
        P("E.lance.early", E, (776, 755), [S(25, 0, aim=(784, 755)), S(25, 150, aim=(784, 755), phase=RELEASE)], settle=4200),
        P("E.lance.full", E, (776, 755), [S(25, 0, aim=(784, 755)), S(25, 1060, aim=(784, 755), phase=RELEASE)], settle=4200),
        P("E.lance.timeout", E, (776, 755), [S(25, 0, aim=(784, 755))], settle=2400),
        P("E.lance.walk", E, (776, 755), [S(25, 0, aim=(784, 755)), M(DOWN, 250)]),
        P("E.ember", E, (777, 757), [S(26, 0, aim=(781, 755))], settle=3600),
        P("E.ember.walk", E, (777, 757), [S(26, 0, aim=(781, 755)), M(DOWN, 400)], settle=3600),
        P("E.ember.blink", E, (777, 757), [S(26, 0, aim=(781, 755)), S(10, 1500, aim=(777, 760))], settle=3600),
        P("E.ember.other", E, (777, 757), [S(26, 0, aim=(781, 755)), S(8, 1200, aim=(784, 755))], settle=4200),
        P("E.barrier.resonance", E, (777, 757), [S(27, 0, aim=(781, 755)), S(30, 700)], fill=FILL_ENGRAVER, settle=1500),
        P("E.barrier", E, (777, 757), [S(27, 0, aim=(781, 755))], settle=1200),
        P("E.bolt", E, (777, 757), [S(28, 0, "d2")], settle=1800),
        P("E.blizzard.release", E, (776, 755), [S(29, 0, aim=(784, 755)), S(29, 900, aim=(784, 755), phase=RELEASE)]),
        P("E.blizzard.timeout", E, (776, 755), [S(29, 0, aim=(784, 755))], settle=3600),
        P("E.blizzard.walk", E, (776, 755), [S(29, 0, aim=(784, 755)), M(UP, 500)]),
        P("E.meteor", E, (777, 757), [S(12, 0, aim=(781, 755))], settle=2400),
        P("E.blink", E, at, [S(10, 0, aim=(780, 759))], settle=1800),
    ]
    # ── 밤사냥꾼 ──
    out += [
        P("H.basic", H, near1, basic_spam(13, "d1", 7, 300)),
        P("H.poison", H, at, [S(15, 0, "d2"), S(13, 400, "d2")]),
        P("H.shadow", H, (776, 757), [S(14, 0, "d2"), S(13, 300, "d2"), S(17, 600, "d2")], settle=4200),
        P("H.backstab", H, near1, [S(17, 0, "d1")]),
        P("H.storm.release", H, at, [S(31, 0), S(31, 800, phase=RELEASE)], settle=4200),
        P("H.storm.timeout", H, at, [S(31, 0)], settle=2600),
        P("H.storm.walk", H, at, [S(31, 0), M(DOWN, 500)]),
        P("H.clones", H, at, [S(32, 0), S(13, 300, "d2"), S(15, 700, "d2"), S(13, 1050, "d2")]),   # 콤보 창(0.8초) 경계를 피한다
        P("H.daggers", H, (777, 755), [S(33, 0, aim=(784, 755)), S(33, 500, aim=(784, 755)), S(33, 1000, aim=(784, 755)),
                                        S(33, 1400, aim=(784, 755))], settle=1800),
        P("H.mist", H, (777, 757), [S(34, 0, aim=(781, 755))], settle=7200),
        P("H.moon.mid", H, (776, 755), [S(35, 0, aim=(784, 755)), S(35, 500, aim=(784, 755), phase=RELEASE)], settle=4200),
        P("H.moon.full", H, (776, 755), [S(35, 0, aim=(784, 755)), S(35, 1000, aim=(784, 755), phase=RELEASE)], settle=4200),
        P("H.moon.timeout", H, (776, 755), [S(35, 0, aim=(784, 755))], settle=4200),
        P("H.dusk", H, at, [S(18, 0)], settle=2600),
        P("H.dodge", H, at, [S(16, 0, aim=(780, 758))]),
        P("H.veil", H, near1, [S(36, 0), S(13, 400, "d1"), S(17, 800, "d1")], fill=FILL_HUNTER, settle=1800),
    ]
    return out


# 갈래 시험 구성: base = 갈래 없음, a = 레벨 7 + 갈래 Ⅰ·Ⅱ 모두 1번, b = 모두 2번
GROWABLE = {0: [2, 3, 4, 5, 19, 20, 21, 22], 1: [8, 9, 11, 25, 26, 27, 28, 29], 2: [14, 15, 17, 31, 32, 33, 34, 35]}


# ── 접속 · 기록 ─────────────────────────────────────────────────────────────
class Bot:
    def __init__(self, port: int, name: str, cls: int):
        self.c = socket.create_connection(("127.0.0.1", port), timeout=5)
        self.c.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.buf = b""
        self.me = None
        self.ids = {}            # ObjID → 이름(me · d1 · d2 · d3)
        self.dummy = {}          # 이름 → ObjID
        self.rec = None          # 기록 중이면 [(t, key)]
        self.t0 = 0.0
        self.seq = 0
        self.grow_seq = 0
        self.grow_acks = []
        self.c.sendall(struct.pack("<HB20s20sHB", 46, T_LOGIN, name.encode(), b"pw", 3, cls))

    def wait_ready(self, need_dummies: bool, seconds: float = 8.0) -> bool:
        """로그인 응답(과 허수아비 셋)이 올 때까지 — 느린 빌드(ASan)는 첫 접속이 오래 걸린다"""
        end = time.time() + seconds
        while time.time() < end:
            self.poll(0.1)
            if self.me is not None and (not need_dummies or len(self.dummy) >= 3):
                self.poll(0.3)   # 접속 직후 쏟아지는 패킷(시야 · 소지품 · 게이지)을 흘려보낸다
                return True
        return False

    def name_of(self, oid):
        return self.ids.get(oid, "other")

    def poll(self, seconds: float):
        end = time.time() + seconds
        while True:
            r, _, _ = select.select([self.c], [], [], max(0.0, end - time.time()))
            if r:
                data = self.c.recv(65536)
                if not data:
                    raise ConnectionError("server closed the connection")
                self.buf += data
                while len(self.buf) >= 3:
                    size = struct.unpack_from("<H", self.buf)[0]
                    if size < 3 or len(self.buf) < size:
                        break
                    self.on_packet(self.buf[2], self.buf[:size])
                    self.buf = self.buf[size:]
            if time.time() >= end:
                return

    def on_packet(self, t: int, p: bytes):
        if t == T_LOGIN_ACK:
            self.me = objid(p, 3)
            self.ids[self.me] = "me"
            return
        if t == T_ADD and len(p) >= 24:
            oid = objid(p, 4)                      # SUBJECT_ADD_NFY: monster_type 다음에 id
            x, y = struct.unpack_from("<hh", p, 20)
            if (x, y) in DUMMIES and oid[0] == 3:
                self.ids[oid] = DUMMIES[(x, y)]
                self.dummy[DUMMIES[(x, y)]] = oid
            return
        if t == T_GROW_ACK:
            self.grow_acks.append(p)
            return
        if self.rec is None:
            return
        key = None
        if t == T_RESULT:
            seq, skill, result, cd = struct.unpack_from("<HHBI", p, 3)
            key = ("RESULT", skill, result, fz(cd))
        elif t == T_SKILL_NFY:
            caster = objid(p, 3)
            if caster != self.me:
                return
            skill, phase, stage, flags, d, fx, fy, ax, ay = struct.unpack_from("<HBBBBhhhh", p, 19)
            tgt = objid(p, 33)
            cast_ms = struct.unpack_from("<H", p, 49)[0]
            key = ("NFY", skill, phase, stage, flags, d, fx, fy, ax, ay, self.name_of(tgt) if tgt != (0, 0, 0, 0) else "-", cast_ms)
        elif t == T_DAMAGE:
            victim, attacker = objid(p, 3), objid(p, 19)
            if attacker != self.me and victim != self.me:
                return
            flags = p[47]
            skill, combo, flags2, hit = struct.unpack_from("<HBBB", p, 48)
            key = ("DMG", self.name_of(victim), self.name_of(attacker), flags & ~DAMAGE_CRIT, skill, combo, flags2, hit)
        elif t == T_EFFECT:
            subj = objid(p, 3)
            if self.name_of(subj) == "other":
                return
            eff, x, y, param = struct.unpack_from("<Hhhi", p, 19)
            key = ("FX", self.name_of(subj), eff, x, y, param)
        elif t == T_STATUS:
            subj = objid(p, 3)
            st, stacks, dur = struct.unpack_from("<BBI", p, 19)
            src = objid(p, 25)
            if self.name_of(subj) == "other" and src != self.me:
                return
            key = ("ST", self.name_of(subj), st, stacks, fz(dur), self.name_of(src))
        elif t == T_FORCED:
            oid = objid(p, 3)
            if self.name_of(oid) == "other":
                return
            fx, fy, tx, ty, ms, kind = struct.unpack_from("<hhhhHB", p, 19)
            key = ("MOVE", self.name_of(oid), fx, fy, tx, ty, ms, kind)
        elif t == T_GAUGE:
            ident, flags, active = struct.unpack_from("<HBI", p, 3)
            key = ("GAUGE", ident, flags, fz(active))
        elif t == T_STATS:
            key = ("STATS",)
        if key is not None:
            self.rec.append((int((time.time() - self.t0) * 1000), key))

    def skill(self, s: S):
        self.seq = (self.seq + 1) & 0xFFFF
        tgt = self.dummy.get(s.tgt, (0, 0, 0, 0)) if s.tgt else (0, 0, 0, 0)
        if s.aim is not None:
            ax, ay = s.aim
        elif s.tgt:
            ax, ay = next(k for k, v in DUMMIES.items() if v == s.tgt)
        else:
            ax, ay = 0, 0
        pkt = struct.pack("<HBHBBHHIQhhBH", 30, T_SKILL_REQ, s.skill, s.phase, 1 if s.force else 0, *tgt, ax, ay, 0, self.seq)
        self.c.sendall(pkt)

    def step(self, direction: int):
        self.seq = (self.seq + 1) & 0xFFFF
        self.c.sendall(struct.pack("<HBBH", 6, T_MOVE_REQ, direction, self.seq))

    def grow(self, op: int, skill: int, tier: int = 0, choice: int = 0):
        self.grow_seq += 1
        self.c.sendall(struct.pack("<HBBHBBH", 10, T_GROW_REQ, op, skill, tier, choice, self.grow_seq))

    def close(self):
        try:
            self.c.close()
        except OSError:
            pass


def run_steps(bot: Bot, steps, record: bool, settle_ms: int):
    steps = sorted(steps, key=lambda s: s.t)
    bot.rec = [] if record else None
    bot.t0 = time.time()
    for s in steps:
        wait = bot.t0 + s.t / 1000.0 - time.time()
        if wait > 0:
            bot.poll(wait)
        if record:
            bot.rec.append((int((time.time() - bot.t0) * 1000), ("REQ", "move", s.dir) if isinstance(s, M) else ("REQ", s.skill, s.phase)))
        if isinstance(s, M):
            bot.step(s.dir)
        else:
            bot.skill(s)
    last = steps[-1].t if steps else 0
    bot.poll(max(0.0, bot.t0 + (last + settle_ms) / 1000.0 - time.time()))
    rec, bot.rec = bot.rec, None
    return rec


def set_growth(port: int, name: str, cls: int, config: str):
    if config == "base":
        return
    choice = 1 if config == "a" else 2
    bot = Bot(port, name, cls)
    try:
        if not bot.wait_ready(False):
            raise RuntimeError("기술 성장: 접속 실패")
        for sid in GROWABLE[cls]:
            for _ in range(6):
                bot.grow(0, sid)   # LevelUp
            bot.grow(2, sid, 0, choice)
            bot.grow(2, sid, 1, choice)
        bot.poll(1.0)
        bad = [a for a in bot.grow_acks if a[6] != 0]   # SKILL_GROW_ACK.result
        if bad:
            raise RuntimeError(f"기술 성장 실패 {len(bad)}건(첫 결과 {bad[0][6]})")
    finally:
        bot.close()
    time.sleep(0.3)


def run_pattern(port: int, pat: P, config: str):
    x, y = pat.pos
    name = f"lv60_xy{x}_{y}_{config}{pat.cls}{STAMP % 1000}"
    bot = Bot(port, name, pat.cls)
    try:
        if not bot.wait_ready(True):
            raise RuntimeError(f"접속 · 허수아비 확인 실패(me={bot.me}, dummies={len(bot.dummy)})")
        if pat.fill:
            steps, t = [], 0
            for skill, tgt, n, every in pat.fill:
                for i in range(n):
                    steps.append(S(skill, t, tgt))
                    t += every if every else 1050
            run_steps(bot, steps, False, 1200)
            # 채우는 동안 움직였을 수 있다 — 다시 접속해 같은 자리에서(게이지는 접속마다 0이라 그대로 둔다)
        return run_steps(bot, pat.steps, True, pat.settle)
    finally:
        bot.close()
        time.sleep(0.15)


# ── 서버 ────────────────────────────────────────────────────────────────────
def make_fixture(src: Path, dst: Path):
    """기술 재사용 대기 ≤ 1초 · MP 소모 0(두 서버가 같은 사본을 쓴다)."""
    from dt import export
    shutil.copytree(src, dst)

    def rewrite(fname, fix):
        p = dst / fname
        out = []
        for line in p.read_text(encoding="utf-8").split("\n"):
            s = line.strip()
            if s.startswith('{"id":'):
                comma = s.endswith(",")
                row = json.loads(s.rstrip(","))
                fix(row)
                line = json.dumps(row, ensure_ascii=False) + ("," if comma else "")
            out.append(line)
        p.write_text("\n".join(out), encoding="utf-8", newline="\n")

    def skill(row):
        row["cooldown_ms"] = min(row.get("cooldown_ms", 0), 1000)
        row["mp"] = 0

    def action(row):
        if row.get("cooldown_ms", 0) > 0:
            row["cooldown_ms"] = min(row["cooldown_ms"], 1000)

    rewrite("Skill.json", skill)
    rewrite("SkillAction.json", action)
    (dst / "manifest.json").write_text(export.manifest_text(dst), encoding="utf-8", newline="\n")


def wait_port(port: int, seconds: float) -> bool:
    end = time.time() + seconds
    while time.time() < end:
        try:
            socket.create_connection(("127.0.0.1", port), timeout=0.5).close()
            return True
        except OSError:
            time.sleep(0.3)
    return False


def run_server(binary: str, port: int, work: Path, label: str, pats, configs, classes):
    log = open(work / f"server_{label}.log", "w")
    srv = subprocess.Popen([str(Path(binary).resolve()), "--data-dir", str(work / "GameData"), "--port", str(port)],
                           stdout=log, stderr=subprocess.STDOUT, cwd=work)
    traces = {}
    try:
        if not wait_port(port, 60):
            raise RuntimeError(f"{label}: 서버가 뜨지 않았다")
        time.sleep(1.5)
        for config in configs:
            for cls in classes:
                todo = [p for p in pats if p.cls == cls]
                if not todo:
                    continue
                for x, y in sorted({p.pos for p in todo}):
                    set_growth(port, f"lv60_xy{x}_{y}_{config}{cls}{STAMP % 1000}", cls, config)
                for pat in todo:
                    traces[f"{config}/{pat.name}"] = run_pattern(port, pat, config)
                    if srv.poll() is not None:
                        raise RuntimeError(f"{label}: 서버가 죽었다(종료 코드 {srv.returncode}) — {config}/{pat.name}")
        if srv.poll() is not None:
            raise RuntimeError(f"{label}: 서버가 죽었다(종료 코드 {srv.returncode})")
    finally:
        srv.terminate()
        try:
            srv.wait(10)
        except subprocess.TimeoutExpired:
            srv.kill()
        log.close()
    return traces


# ── 비교 · 점검 ─────────────────────────────────────────────────────────────
def same_key(a, b) -> bool:
    if len(a) != len(b):
        return False
    for x, y in zip(a, b):
        fx = isinstance(x, (list, tuple)) and len(x) == 2 and x[0] == "~"
        fy = isinstance(y, (list, tuple)) and len(y) == 2 and y[0] == "~"
        if fx and fy:
            if abs(x[1] - y[1]) > FUZZ_MS:
                return False
        elif x != y:
            return False
    return True


def compare(old, new):
    """같은 열쇠를 시각 ±TOL_MS 안에서 짝짓는다. (짝 없는 옛 기록, 짝 없는 새 기록)"""
    used = [False] * len(new)
    missing = []
    for t, k in old:
        found = False
        for i, (t2, k2) in enumerate(new):
            if not used[i] and same_key(k2, k) and abs(t2 - t) <= TOL_MS:
                used[i] = True
                found = True
                break
        if not found:
            missing.append((t, k))
    extra = [(t, k) for i, (t, k) in enumerate(new) if not used[i]]
    return missing, extra


def check_alone(name, rec):
    """--base 없이: 요청마다 결과가 오고, 받아들여진 누름에는 시전 알림이 있다."""
    reqs = [k for _, k in rec if k[0] == "REQ" and k[1] != "move" and k[2] == PRESS]
    results = [k for _, k in rec if k[0] == "RESULT"]
    nfys = [k for _, k in rec if k[0] == "NFY"]
    problems = []
    if reqs and not results and not nfys:
        problems.append("결과 · 알림이 하나도 없다")
    if any(k[2] == 0 for k in results) and not nfys:
        problems.append("받아들여진 누름이 있는데 시전 알림이 없다")
    return problems


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--server", default=str(REPO / "build" / "linux-memory" / "GameServer"))
    ap.add_argument("--base", default="")
    ap.add_argument("--port", type=int, default=4150)
    ap.add_argument("--classes", default="0,1,2")
    ap.add_argument("--configs", default="base,a,b")
    ap.add_argument("--only", default="")
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--dump", default="", help="기록을 JSON으로 남길 파일(새 서버)")
    args = ap.parse_args()

    classes = [int(c) for c in args.classes.split(",") if c != ""]
    configs = [c for c in args.configs.split(",") if c]
    pats = [p for p in scenarios() if not args.only or any(o in p.name for o in args.only.split(","))]
    work = Path(tempfile.mkdtemp(prefix="harven_psk_"))
    make_fixture(REPO / "Bin" / "GameData", work / "GameData")
    try:
        if args.base:
            # 옛 서버 · 새 서버를 함께 돌린다(시나리오는 각자 같은 차례 — 서로 다른 포트 · 다른 캐릭터 이름)
            import threading
            box = {}

            def run(label, binary, port):
                try:
                    box[label] = run_server(binary, port, work, label, pats, configs, classes)
                except Exception as e:   # noqa: BLE001 — 스레드 밖으로 알린다
                    box[label] = e

            ts = [threading.Thread(target=run, args=("new", args.server, args.port)),
                  threading.Thread(target=run, args=("old", args.base, args.port + 1))]
            for t in ts:
                t.start()
            for t in ts:
                t.join()
            for label in ("new", "old"):
                if isinstance(box.get(label), Exception):
                    raise box[label]
            new, old = box["new"], box["old"]
        else:
            new = run_server(args.server, args.port, work, "new", pats, configs, classes)
        if args.dump:
            Path(args.dump).write_text(json.dumps(new, ensure_ascii=False, indent=0), encoding="utf-8")
        if not args.base:
            bad = 0
            for name, rec in new.items():
                for p in check_alone(name, rec):
                    print(f"FAIL {name}: {p}")
                    bad += 1
            print(f"{'PASS' if bad == 0 else 'FAIL'}: 시나리오 {len(new)}개 — 문제 {bad}건")
            return 0 if bad == 0 else 1
        bad = 0
        for name in new:
            missing, extra = compare(old.get(name, []), new[name])
            if missing or extra:
                bad += 1
                print(f"DIFF {name}: 옛 기록에만 {len(missing)}개 · 새 기록에만 {len(extra)}개")
                for t, k in missing[:12]:
                    print(f"    - {t:5d}ms {k}")
                for t, k in extra[:12]:
                    print(f"    + {t:5d}ms {k}")
        total = sum(len(v) for v in new.values())
        print(f"{'PASS' if bad == 0 else 'FAIL'}: 시나리오 {len(new)}개 · 기록 {total}줄 — 다른 시나리오 {bad}개")
        return 0 if bad == 0 else 1
    finally:
        if args.keep:
            print(f"[keep] {work}")
        else:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
