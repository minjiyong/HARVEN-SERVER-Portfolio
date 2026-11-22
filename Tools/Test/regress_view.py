"""시야 회귀 시험 — 플레이어 여럿이 정해진 걸음으로 칸(섹터) 경계 · 시야 끝(9 ↔ 10칸) · Zone 경계를 오가며, 각자 받은
다른 플레이어의 SUBJECT_ADD · SUBJECT_MOVE · SUBJECT_REMOVE를 걸음마다 적는다.

    python3 Tools/Test/regress_view.py [--server build/linux-memory/GameServer] [--base 옛 서버] [--port 4170]
                                       [--timeout 1.5(느린 빌드는 3)] [--dump 기록.json]

  늘 보는 것: 걸음마다 각 플레이어가 아는 이웃(ADD로 들어오고 REMOVE로 나간다)과 그 좌표(ADD · MOVE)가 서버 규칙으로 다시
              계산한 시야 — 체비셰프 viewRange(SERVER_CONFIG_INF) 칸 안 · 같은 Zone(CSubjectHelper::CanSee · Zone마다 따로인
              섹터 격자) — 와 같은지. 같은 이웃을 두 번 ADD하거나 모르는 이웃을 MOVE · REMOVE해도 틀린 것으로 센다.
  --base가 있으면: 같은 걸음을 옛 서버로도 돌려 걸음마다 받은 알림(종류 · 차례 · 좌표)이 같은지 맞대어 본다 — 시야 · 섹터
                   격자 구조를 고칠 때 "보이는 것과 알림 차례가 그대로인가"를 확인하는 A/B.

HARVEN_IN_MEMORY_DB=ON 빌드를 이 스크립트가 직접 띄운다(Bin/GameData 그대로). 샌드박스 이름 규칙: "lvNN_xyX_Y_…" = 레벨 NN
캐릭터가 (X, Y)에서 시작(Tools/Sandbox/MemDBConnection.cpp). 마을 훈련장(몬스터는 움직이지 않는 허수아비뿐)과 Zone 0 | 1
경계(x 499 → 500)에서 걷는다. 몬스터 · NPC 알림은 적지 않는다(돌아다니는 몬스터는 돌릴 때마다 다르다).
Debug(-D_DEBUG) 빌드면 섹터 격자의 칸 항목 좌표 · 칸마다 플레이어 수 대조(CSector::AppendWithin · MayHold)와 소유 Zone 검사 assert도 함께 돈다.
"""
from __future__ import annotations

import argparse
import json
import select
import socket
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]

T_LOGIN, T_LOGIN_ACK, T_MOVE_REQ, T_MOVE_ACK, T_CONFIG = 35, 11, 53, 54, 55
T_ADD, T_MOVE_NFY, T_REMOVE, T_FORCED = 21, 22, 23, 82
CAT_USER = 2                       # EnumCategory::eUser — ObjID의 첫 필드
UP, DOWN, LEFT, RIGHT, UL, UR, DL, DR = range(8)
REGION, REGIONS_X = 500, 4         # Zone 하나 = 500×500칸(WorldData::kRegionSize), 가로 4개
MOVE_OK, MOVE_TOO_FAST = 0, 1

# (이름, 시작 좌표, 걸음) — 한 걸음씩 돌아가며 걷는다. 모두 걸을 수 있는 칸 위다(TownGrid ground — 훈련장 모래 's' · 그 위 길 '.').
# 훈련장(마을 Zone 5, x 775~789 · y 748~761): v2 · v3은 v1 · v2에게서 9 ↔ 10칸을 오가고, v1 · v4는 칸 경계(x 779|780 · y 759|760)를 넘는다.
# Zone 0 | 1 경계(y 6, x 499|500): 다른 Zone이면 9칸 안이어도 보이지 않는다 — 넘을 때 옛 Zone에서 빠지고 새 Zone에 든다.
BOTS = [
    ("v1", (775, 757), [DOWN] * 4 + [UP] * 4 + [RIGHT] * 5 + [LEFT] * 5),
    ("v2", (785, 757), [LEFT, RIGHT] * 7),
    ("v3", (780, 748), [UP, DOWN] * 6),
    ("v4", (789, 761), [UL] * 9 + [DR] * 9),
    ("z1", (495, 6), [RIGHT] * 8 + [LEFT] * 8),
    ("z2", (503, 6), [LEFT] * 2 + [RIGHT] * 2 + [LEFT] * 6 + [RIGHT] * 6),
]


def objid(b: bytes, off: int):
    return struct.unpack_from("<HHIQ", b, off)


def zone_of(x: int, y: int) -> int:
    return (y // REGION) * REGIONS_X + (x // REGION)


class Bot:
    def __init__(self, port: int, name: str, pos):
        self.name = name
        self.c = socket.create_connection(("127.0.0.1", port), timeout=5)
        self.c.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.buf = b""
        self.me = None
        self.pos = None
        self.view = None
        self.known = {}          # ObjID → (x, y) — 이 플레이어가 아는 다른 플레이어
        self.events = []         # [(걸음 번호, 알림)] — ObjID는 끝에 이름으로 바꾼다
        self.problems = []
        self.acks = []           # (걸음 번호, 결과)
        self.step_ms = 0
        self.seq = 0
        self.closed = False
        lv_name = f"lv60_xy{pos[0]}_{pos[1]}_{name}"
        self.c.sendall(struct.pack("<HB20s20sHB", 46, T_LOGIN, lv_name.encode(), b"pw", 3, 0))

    def feed(self, step: int):
        data = self.c.recv(65536)
        if not data:
            raise ConnectionError(f"{self.name}: server closed the connection")
        self.buf += data
        while len(self.buf) >= 3:
            size = struct.unpack_from("<H", self.buf)[0]
            if size < 3 or len(self.buf) < size:
                break
            self.on_packet(step, self.buf[2], self.buf[:size])
            self.buf = self.buf[size:]

    def on_packet(self, step: int, t: int, p: bytes):
        if t == T_LOGIN_ACK:
            self.me = objid(p, 3)
            self.pos = struct.unpack_from("<hh", p, 19)
        elif t == T_CONFIG:
            self.view = p[5]
        elif t == T_MOVE_ACK:
            _seq, x, y, result, step_ms = struct.unpack_from("<HhhBH", p, 3)
            self.pos = (x, y)
            self.step_ms = step_ms
            self.acks.append((step, result))
            self.events.append((step, ("ACK", x, y, result)))
        elif t == T_ADD:
            oid = objid(p, 4)
            if oid[0] != CAT_USER:
                return
            x, y = struct.unpack_from("<hh", p, 20)
            if oid == self.me:
                self.events.append((step, ("SELF_ADD", x, y)))
                return
            if oid in self.known:
                self.problems.append(f"걸음 {step}: 이미 아는 {oid}를 다시 ADD")
            self.known[oid] = (x, y)
            self.events.append((step, ("ADD", oid, x, y)))
        elif t == T_MOVE_NFY:
            oid = objid(p, 3)
            if oid[0] != CAT_USER:
                return
            x, y = struct.unpack_from("<hh", p, 19)
            if oid == self.me:
                self.events.append((step, ("SELF", x, y)))   # Zone을 넘는 걸음 — 옛 Zone이 보낸 내 좌표
                return
            if oid not in self.known:
                self.problems.append(f"걸음 {step}: 모르는 {oid}의 MOVE")
            self.known[oid] = (x, y)
            self.events.append((step, ("MOVE", oid, x, y)))
        elif t == T_REMOVE:
            oid = objid(p, 3)
            if oid[0] != CAT_USER:
                return
            if oid == self.me:
                self.events.append((step, ("SELF_REMOVE",)))
                return
            if oid not in self.known:
                self.problems.append(f"걸음 {step}: 모르는 {oid}의 REMOVE")
            self.known.pop(oid, None)
            self.events.append((step, ("REMOVE", oid)))
        elif t == T_FORCED:
            oid = objid(p, 3)
            if oid[0] != CAT_USER:
                return
            _fx, _fy, tx, ty = struct.unpack_from("<hhhh", p, 19)
            if oid == self.me:
                self.pos = (tx, ty)
            elif oid in self.known:
                self.known[oid] = (tx, ty)
            self.events.append((step, ("FORCED", oid, tx, ty)))
            self.problems.append(f"걸음 {step}: 서버가 옮김(몬스터 기술?) — 시험 자리를 바꿔야 한다")

    def move(self, direction: int):
        self.seq = (self.seq + 1) & 0xFFFF
        self.c.sendall(struct.pack("<HBBH", 6, T_MOVE_REQ, direction, self.seq))

    def close(self):
        if not self.closed:
            self.closed = True
            try:
                self.c.close()
            except OSError:
                pass


class World:
    def __init__(self, port: int):
        self.port = port
        self.bots: list[Bot] = []
        self.step = 0

    def poll(self, seconds: float):
        end = time.time() + seconds
        while True:
            live = [b for b in self.bots if not b.closed]
            if not live:
                return
            r, _, _ = select.select([b.c for b in live], [], [], max(0.0, end - time.time()))
            for b in live:
                if b.c in r:
                    b.feed(self.step)
            if time.time() >= end:
                return

    def expected(self, b: Bot):
        out = {}
        for o in self.bots:
            if o is b or o.closed or o.me is None or o.pos is None:
                continue
            if zone_of(*o.pos) != zone_of(*b.pos):
                continue
            if max(abs(o.pos[0] - b.pos[0]), abs(o.pos[1] - b.pos[1])) <= b.view:
                out[o.me] = tuple(o.pos)
        return out

    def matches(self) -> bool:
        return all(b.known == self.expected(b) for b in self.bots if not b.closed and b.pos is not None)

    def settle(self, timeout: float, quiet: float):
        """모두가 아는 이웃 = 계산한 시야가 될 때까지(최대 timeout) — 그 뒤 quiet만큼 더 받아 늦게 온 알림까지 이 걸음에 적는다"""
        end = time.time() + timeout
        while time.time() < end:
            self.poll(0.02)
            if self.matches():
                break
        self.poll(quiet)

    def check(self, label: str):
        for b in self.bots:
            if b.closed or b.pos is None:
                continue
            want = self.expected(b)
            if b.known != want:
                missing = [k for k in want if k not in b.known]
                extra = [k for k in b.known if k not in want]
                moved = [k for k in want if k in b.known and b.known[k] != want[k]]
                b.problems.append(f"{label}: 시야가 다르다 — 빠짐 {missing} · 남음 {extra} · 좌표 다름 {moved}")
                b.known = dict(want)   # 다음 걸음은 맞은 상태에서 다시 본다(한 번 틀린 것이 계속 번지지 않게)


def wait_port(port: int, seconds: float) -> bool:
    end = time.time() + seconds
    while time.time() < end:
        try:
            socket.create_connection(("127.0.0.1", port), timeout=0.5).close()
            return True
        except OSError:
            time.sleep(0.3)
    return False


def run_server(binary: str, port: int, work: Path, label: str, timeout: float, quiet: float):
    log = open(work / f"server_{label}.log", "w")
    srv = subprocess.Popen([str(Path(binary).resolve()), "--data-dir", str(REPO / "Bin" / "GameData"), "--port", str(port)],
                           stdout=log, stderr=subprocess.STDOUT, cwd=work)
    w = World(port)
    try:
        if not wait_port(port, 90):
            raise RuntimeError(f"{label}: 서버가 뜨지 않았다")
        time.sleep(1.5)

        # 접속 — 한 명씩(들어올 때 서로 ADD)
        for name, pos, _ in BOTS:
            bot = Bot(port, name, pos)
            w.bots.append(bot)
            end = time.time() + 15
            while time.time() < end and (bot.me is None or bot.view is None):
                w.poll(0.05)
            if bot.me is None or bot.view is None:
                raise RuntimeError(f"{label}: {name} 접속 실패")
            if tuple(bot.pos) != tuple(pos):
                raise RuntimeError(f"{label}: {name} 시작 칸 {pos}이 막혀 {bot.pos}로 옮겨졌다 — 시험 자리를 바꿔야 한다")
            w.settle(timeout, quiet)
            w.check(f"접속 {name}")
            w.step += 1

        # 걸음 — 돌아가며 한 걸음씩. 같은 플레이어의 다음 걸음은 걸음 시간(+여유)이 지난 뒤
        plans = [list(steps) for _, _, steps in BOTS]
        ready = [0.0] * len(BOTS)
        while any(plans):
            for i, bot in enumerate(w.bots):
                if not plans[i]:
                    continue
                direction = plans[i].pop(0)
                wait = ready[i] - time.time()
                if wait > 0:
                    w.poll(wait)
                acks = len(bot.acks)
                bot.move(direction)
                end = time.time() + 5
                while time.time() < end and len(bot.acks) == acks:
                    w.poll(0.02)
                if len(bot.acks) == acks:
                    raise RuntimeError(f"{label}: {bot.name} 이동 응답이 없다(걸음 {w.step})")
                ready[i] = time.time() + max(bot.step_ms, 400) / 1000.0 + 0.05
                w.settle(timeout, quiet)
                w.check(f"걸음 {w.step} {bot.name}")
                w.step += 1
                if srv.poll() is not None:
                    raise RuntimeError(f"{label}: 서버가 죽었다(종료 코드 {srv.returncode}) — 걸음 {w.step}")

        # 나감 — 한 명씩(남은 이웃에게 REMOVE)
        for bot in reversed(list(w.bots)):
            bot.close()
            w.settle(timeout, quiet)
            w.check(f"나감 {bot.name}")
            w.step += 1
        if srv.poll() is not None:
            raise RuntimeError(f"{label}: 서버가 죽었다(종료 코드 {srv.returncode})")
    finally:
        for bot in w.bots:
            bot.close()
        srv.terminate()
        try:
            srv.wait(10)
        except subprocess.TimeoutExpired:
            srv.kill()
        log.close()

    names = {b.me: b.name for b in w.bots}
    trace, problems, stats = {}, [], {"ADD": 0, "REMOVE": 0, "MOVE": 0, "SELF": 0, "moved": 0, "too_fast": 0}
    for b in w.bots:
        rec = []
        last = None
        for step, ev in b.events:
            ev = tuple(names.get(v, "other") if isinstance(v, tuple) else v for v in ev)
            rec.append((step, ev))
            if ev[0] in stats:
                stats[ev[0]] += 1
            if ev[0] == "ACK":
                if ev[3] == MOVE_TOO_FAST:
                    stats["too_fast"] += 1
                if last is not None and (ev[1], ev[2]) != last:
                    stats["moved"] += 1
                last = (ev[1], ev[2])
        trace[b.name] = rec
        problems += [f"{label} {b.name}: {p}" for p in b.problems]
    if stats["too_fast"]:
        problems.append(f"{label}: 너무 빠른 걸음으로 거절 {stats['too_fast']}번(시험 간격 문제)")
    return trace, problems, stats


def compare(old, new):
    out = []
    for name in new:
        a, b = old.get(name, []), new[name]
        steps = sorted({s for s, _ in a} | {s for s, _ in b})
        for s in steps:
            ea = [e for t, e in a if t == s]
            eb = [e for t, e in b if t == s]
            if ea != eb:
                out.append(f"{name} 걸음 {s}: 옛 {ea} ≠ 새 {eb}")
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--server", default=str(REPO / "build" / "linux-memory" / "GameServer"))
    ap.add_argument("--base", default="")
    ap.add_argument("--port", type=int, default=4170)
    ap.add_argument("--timeout", type=float, default=1.5, help="걸음마다 시야가 맞을 때까지 기다리는 최대 시간(초)")
    ap.add_argument("--quiet", type=float, default=0.08, help="맞은 뒤 더 받는 시간(초) — 늦게 온 알림까지 그 걸음에")
    ap.add_argument("--dump", default="", help="기록(새 서버)을 JSON으로 남길 파일")
    args = ap.parse_args()

    work = Path(tempfile.mkdtemp(prefix="harven_view_"))
    new, problems, stats = run_server(args.server, args.port, work, "new", args.timeout, args.quiet)
    if args.dump:
        Path(args.dump).write_text(json.dumps(new, ensure_ascii=False, indent=0), encoding="utf-8")
    for p in problems:
        print(f"FAIL {p}")
    print(f"새 서버: ADD {stats['ADD']} · REMOVE {stats['REMOVE']} · MOVE {stats['MOVE']} · Zone 넘음 {stats['SELF']} · "
          f"움직인 걸음 {stats['moved']}")
    bad = len(problems)
    # 시험이 시야 경계를 실제로 넘나들었는지(자리가 막혀 못 걸으면 아무것도 보지 못한 채 통과한다)
    if stats["ADD"] < 20 or stats["REMOVE"] < 20 or stats["SELF"] < 2 or stats["moved"] < 60:
        print("FAIL: 시야 · Zone 경계를 충분히 넘나들지 못했다(걸을 수 없는 자리?)")
        bad += 1
    if args.base:
        old, old_problems, _ = run_server(args.base, args.port + 1, work, "old", args.timeout, args.quiet)
        for p in old_problems:
            print(f"(옛 서버) {p}")
        diffs = compare(old, new)
        for d in diffs[:30]:
            print(f"DIFF {d}")
        print(f"A/B: 다른 걸음 {len(diffs)}개")
        bad += len(diffs)
    print(f"{'PASS' if bad == 0 else 'FAIL'}: 시야 회귀 — 문제 {bad}건 (기록 {work})")
    return 0 if bad == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
