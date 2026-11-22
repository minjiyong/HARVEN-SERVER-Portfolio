"""몬스터 AI 회귀 시험 — 행동 트리 · 기술(시전 → 발사 → 타격 → 후딜) · 장판 · 버프 · 보스 단계를 실제 패킷으로 본다.

    python3 Tools/Test/regress_monster_ai.py [--server build/linux-memory/GameServer] [--port 4100] [--keep]

HARVEN_IN_MEMORY_DB=ON 빌드를 이 스크립트가 직접 띄운다(시험용 GameData 사본 — 보스 HP · 공격력만 낮춘다, 나머지는 그대로).
샌드박스 이름 규칙: "lvNN_xyX_Y_…" = 레벨 NN 캐릭터가 (X, Y)에서 시작(Tools/Sandbox/MemDBConnection.cpp).

시나리오
  dash   올벤 숲(늑대 · 들개): 돌진 예고(직선) → SUBJECT_FORCED_MOVE_NFY(Dash) 시간 = 칸당 move_ms × 칸 수(표 그대로)
  field  그락 요새(그락 전사): 전쟁 깃발 장판 → 동료 몬스터에 광폭(Empower) 상태
  heal   이레린 숲(잎새 정령): 동료가 다치면 치유의 이슬(PriestHeal 효과) — 정령은 비선공이지만 동료를 돕는다(assist)
  boss   벨고라 설원(모로즈): 서리 강타(BossSlamCast) · 2단계 혹한의 포효(넉백 + 혹한 오라 둔화) · 3단계 빙하의 분노 · 처치
"""
from __future__ import annotations

import argparse
import json
import os
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

T_LOGIN, T_LOGIN_ACK, T_MOVE_REQ, T_MOVE_ACK = 35, 11, 53, 54
T_ADD, T_MOVE_NFY, T_REMOVE, T_DIE, T_RESPAWN = 21, 22, 23, 24, 25
T_DAMAGE, T_EFFECT, T_STATUS, T_TELEGRAPH, T_FORCED, T_SKILL = 49, 50, 59, 63, 82, 80
CAT_USER, CAT_MONSTER = 2, 3
FM_DASH, FM_BLINK, FM_KNOCKBACK, FM_PULL, FM_ROLL, FM_LAUNCH = range(6)
FX_BOSS_SLAM_CAST, FX_BOSS_SLAM_IMPACT, FX_PRIEST_HEAL, FX_EMBER = 9, 10, 20, 27
ST_SLOW, ST_EMPOWER, ST_FORTIFY, ST_SWIFT = 5, 16, 17, 18
DAMAGE_BOSS_SKILL = 1 << 4
PAT_CHARGE = 1
SHAPE_LINE = 1

STAMP = int(time.time()) % 100000


def objid(b: bytes, off: int):
    cat, kind, serial, dbid = struct.unpack_from("<HHIQ", b, off)
    return (cat, kind, serial, dbid)


class Bot:
    def __init__(self, port: int, name: str, cls: int = 0):
        self.c = socket.create_connection(("127.0.0.1", port), timeout=5)
        self.c.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.buf = b""
        self.log: list[tuple[float, int, bytes]] = []
        self.monsters: dict[tuple, dict] = {}
        self.c.sendall(struct.pack("<HB20s20sHB", 46, T_LOGIN, name.encode(), b"pw", 3, cls))
        self.me = None
        self.pos = None
        end = time.time() + 5
        while self.me is None and time.time() < end:
            self.poll(0.2)
        if self.me is None:
            raise RuntimeError(f"{name}: 로그인 응답 없음")
        self.seq = 0

    def poll(self, seconds: float):
        end = time.time() + seconds
        while True:
            r, _, _ = select.select([self.c], [], [], max(0.0, end - time.time()))
            if r:
                data = self.c.recv(1 << 16)
                if not data:
                    raise ConnectionError("서버가 연결을 끊었다")
                self.buf += data
                while len(self.buf) >= 3:
                    size = struct.unpack_from("<H", self.buf)[0]
                    if size < 3 or len(self.buf) < size:
                        break
                    p = self.buf[:size]
                    self.buf = self.buf[size:]
                    self.on(p)
            if time.time() >= end:
                return

    def on(self, p: bytes):
        t = p[2]
        self.log.append((time.time(), t, p))
        if t == T_LOGIN_ACK:
            self.me = objid(p, 3)
            self.pos = struct.unpack_from("<hh", p, 19)
        elif t == T_MOVE_ACK and len(p) >= 9:
            self.pos = struct.unpack_from("<hh", p, 5)
        elif t == T_ADD and len(p) >= 24:
            oid = objid(p, 4)
            if oid[0] == CAT_MONSTER:
                x, y = struct.unpack_from("<hh", p, 20)
                self.monsters[oid] = {"x": x, "y": y, "alive": True}
        elif t in (T_MOVE_NFY,) and len(p) >= 23:
            oid = objid(p, 3)
            if oid in self.monsters:
                self.monsters[oid]["x"], self.monsters[oid]["y"] = struct.unpack_from("<hh", p, 19)
        elif t == T_FORCED:
            oid = objid(p, 3)
            if oid in self.monsters:
                self.monsters[oid]["x"], self.monsters[oid]["y"] = struct.unpack_from("<hh", p, 23)
            if oid == self.me:
                self.pos = struct.unpack_from("<hh", p, 23)
        elif t == T_DIE:
            oid = objid(p, 3)
            if oid in self.monsters:
                self.monsters[oid]["alive"] = False
        elif t == T_RESPAWN:
            oid = objid(p, 3)
            if oid in self.monsters:
                self.monsters[oid]["alive"] = True

    def nearest(self, kinds=None, max_d=1):
        best, bd = None, 99
        for oid, m in self.monsters.items():
            if not m["alive"] or (kinds and oid[1] not in kinds):
                continue
            d = max(abs(m["x"] - self.pos[0]), abs(m["y"] - self.pos[1]))
            if d <= max_d and d < bd:
                best, bd = oid, d
        return best

    def attack(self, target, skill_id=1):
        m = self.monsters[target]
        self.seq = (self.seq + 1) & 0xFFFF
        cat, kind, serial, dbid = target
        pkt = struct.pack("<HBHBBHHIQhhBH", 30, T_SKILL, skill_id, 0, 0, cat, kind, serial, dbid, m["x"], m["y"], 0, self.seq)
        self.c.sendall(pkt)

    def step(self, direction: int):
        """한 칸 걷기(0 위 · 1 아래 · 2 왼쪽 · 3 오른쪽) — 걸음마다 근처 선공 몬스터가 깬다"""
        self.seq = (self.seq + 1) & 0xFFFF
        self.c.sendall(struct.pack("<HBBH", 6, T_MOVE_REQ, direction, self.seq))

    def packets(self, t):
        return [p for _, tt, p in self.log if tt == t]

    def close(self):
        try:
            self.c.close()
        except OSError:
            pass


def fight(bot: Bot, seconds: float, kinds=None, skills=(1,), until=None, walk=False, hunt=False):
    """붙은 몬스터를 기술로 때리며 버틴다(대부분 몬스터가 다가온다).
    walk면 붙은 몬스터가 없을 때 네모로 걸어 다닌다. hunt면 가장 가까운(kinds) 몬스터 쪽으로 한 칸씩 다가간다."""
    end = time.time() + seconds
    i, w = 0, 0
    loop = [3] * 5 + [1] * 5 + [2] * 5 + [0] * 5
    while time.time() < end:
        bot.poll(0.35)
        tgt = bot.nearest(kinds, 1)
        if tgt is not None:
            bot.attack(tgt, skills[i % len(skills)])
            i += 1
        elif hunt and (far := bot.nearest(kinds, 9)) is not None:
            m = bot.monsters[far]
            dx, dy = m["x"] - bot.pos[0], m["y"] - bot.pos[1]
            primary = (3 if dx > 0 else 2) if abs(dx) >= abs(dy) else (1 if dy > 0 else 0)
            secondary = (1 if dy > 0 else 0) if abs(dx) >= abs(dy) else (3 if dx > 0 else 2)
            stuck = getattr(bot, "_last", None) == bot.pos
            bot._last = bot.pos
            bot.step(secondary if stuck and (dx and dy) else (loop[w % len(loop)] if stuck else primary))
            w += 1 if stuck else 0
        elif walk:
            bot.step(loop[w % len(loop)])
            w += 1
        if until and until():
            return True
    return False


def make_fixture(src: Path, dst: Path):
    """보스 모로즈만 약하게(HP 1200 · 공격력 12) — 레벨 40 기사가 단계를 모두 넘기게. 정예 0‰(경제 1차 — HP ×4 정예가 시간을 흔들지 않게).
    나머지 표는 그대로."""
    from dt import export
    shutil.copytree(src, dst)
    p = dst / "Monster.json"
    text = p.read_text(encoding="utf-8")
    lines = text.split("\n")
    out = []
    for line in lines:
        if line.startswith('{"id": 29,'):
            row = json.loads(line.rstrip(","))
            row["max_hp"], row["attack"] = 1200, 12
            comma = "," if line.endswith(",") else ""
            line = "{" + ", ".join(f"{json.dumps(k)}: {json.dumps(v, ensure_ascii=False)}" for k, v in row.items()) + "}" + comma
        out.append(line)
    p.write_text("\n".join(out), encoding="utf-8", newline="\n")
    lc = dst / "LootConst.json"
    lines = lc.read_text(encoding="utf-8").split("\n")
    for i, line in enumerate(lines):
        if line.startswith('{"rare_pity"'):
            row = json.loads(line.rstrip(","))
            row["elite_permille"] = 0
            lines[i] = "{" + ", ".join(f"{json.dumps(k)}: {json.dumps(v, ensure_ascii=False)}" for k, v in row.items()) + "}" + ("," if line.endswith(",") else "")
    lc.write_text("\n".join(lines), encoding="utf-8", newline="\n")
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


def skill_rows():
    rows = json.loads((REPO / "Bin" / "GameData" / "MonsterSkill.json").read_text(encoding="utf-8"))["rows"]
    return {r["name"]: r for r in rows}


# ── 시나리오 ────────────────────────────────────────────────────────────────
def scenario_dash(port: int) -> tuple[bool, str]:
    sk = skill_rows()
    speed = {3: sk["들개 돌진"]["move_ms"], 5: sk["늑대 돌진"]["move_ms"]}
    bot = Bot(port, f"lv30_xy749_251_dash{STAMP}")
    try:
        fight(bot, 20.0, walk=True)
        teles = [p for p in bot.packets(T_TELEGRAPH) if p[31] == PAT_CHARGE and p[19] == SHAPE_LINE]
        dashes, bad = [], []
        for p in bot.packets(T_FORCED):
            oid = objid(p, 3)
            if oid[0] != CAT_MONSTER or p[29] != FM_DASH:
                continue
            fx, fy, tx, ty, dur = struct.unpack_from("<hhhhH", p, 19)
            tiles = max(abs(tx - fx), abs(ty - fy))
            dashes.append((oid[1], tiles, dur))
            if oid[1] in speed and dur != speed[oid[1]] * tiles:
                bad.append((oid[1], tiles, dur))
        hits = [p for p in bot.packets(T_DAMAGE) if objid(p, 3) == bot.me and (p[47] & DAMAGE_BOSS_SKILL)]
        ok = len(teles) > 0 and len(dashes) > 0 and not bad
        return ok, (f"돌진 예고 {len(teles)} · 돌진 {len(dashes)}(종족 · 칸 · ms 예: {dashes[:4]}) · 시간 틀림 {bad[:3]} · "
                    f"기술 피해 {len(hits)}")
    finally:
        bot.close()


def scenario_field(port: int) -> tuple[bool, str]:
    bot = Bot(port, f"lv40_xy1750_750_grak{STAMP}")
    try:
        got = lambda: any(p[19] == ST_EMPOWER and objid(p, 3)[0] == CAT_MONSTER and p[20] > 0 for p in bot.packets(T_STATUS))
        fight(bot, 35.0, until=got, walk=True)
        emp = [p for p in bot.packets(T_STATUS) if p[19] == ST_EMPOWER and p[20] > 0]
        givers = {objid(p, 25)[1] for p in emp}
        takers = {objid(p, 3) for p in emp}
        embers = [p for p in bot.packets(T_EFFECT) if struct.unpack_from("<H", p, 19)[0] == FX_EMBER]
        ok = len(emp) > 0 and 12 in givers
        return ok, f"광폭(Empower) 걸림 {len(emp)} · 받은 몬스터 {len(takers)} · 건 종족 {sorted(givers)} · 잔불 효과 {len(embers)}"
    finally:
        bot.close()


def scenario_heal(port: int) -> tuple[bool, str]:
    bot = Bot(port, f"lv40_xy249_751_leaf{STAMP}")
    try:
        # 치유 효과(PriestHeal) 중 회복량(param) > 0 — 정령이 시전할 때 자기 몸에 띄우는 효과(param 0)는 빼고, 실제로 차오른 동료만
        is_heal = lambda p: (struct.unpack_from("<H", p, 19)[0] == FX_PRIEST_HEAL and objid(p, 3)[0] == CAT_MONSTER
                             and struct.unpack_from("<i", p, 25)[0] > 0)
        healed = lambda: any(is_heal(p) for p in bot.packets(T_EFFECT))
        # 정령이 동료를 고칠 틈이 있도록 정령은 때리지 않는다(이슬덩이 · 버섯 · 곰만)
        fight(bot, 40.0, kinds={4, 6, 7}, until=healed, walk=True, hunt=True)
        heals = [p for p in bot.packets(T_EFFECT) if is_heal(p)]
        leaves = [p for p in bot.packets(T_TELEGRAPH) if objid(p, 3)[1] == 11]
        amount = [struct.unpack_from("<i", p, 25)[0] for p in heals]
        return len(heals) > 0, f"몬스터 치유 효과 {len(heals)}(회복량 {amount[:5]}) · 정령 예고 {len(leaves)}"
    finally:
        bot.close()


def scenario_boss(port: int) -> tuple[bool, str]:
    bot = Bot(port, f"lv40_xy263_268_boss{STAMP}")
    try:
        boss = lambda: next((o for o in bot.monsters if o[1] == 29), None)
        dead = lambda: boss() is not None and not bot.monsters[boss()]["alive"]
        fight(bot, 75.0, kinds={29}, skills=(1, 1, 1, 2), until=dead)
        b = boss()
        fx = [(struct.unpack_from("<H", p, 19)[0], struct.unpack_from("<i", p, 25)[0]) for p in bot.packets(T_EFFECT) if b and objid(p, 3) == b]
        casts = [r for e, r in fx if e == FX_BOSS_SLAM_CAST]
        knock = [p for p in bot.packets(T_FORCED) if objid(p, 3) == bot.me and p[29] == FM_KNOCKBACK]
        slow = [p for p in bot.packets(T_STATUS) if objid(p, 3) == bot.me and p[19] == ST_SLOW and b and objid(p, 25) == b]
        emp = [p for p in bot.packets(T_STATUS) if b and objid(p, 3) == b and p[19] == ST_EMPOWER and p[20] > 0]
        cut = [p for p in bot.packets(T_TELEGRAPH) if b and objid(p, 3) == b and struct.unpack_from("<H", p, 29)[0] == 0]
        teles = [p for p in bot.packets(T_TELEGRAPH) if b and objid(p, 3) == b]
        ok = b is not None and dead() and 3 in casts and len(knock) > 0 and len(slow) > 0 and len(emp) > 0
        return ok, (f"보스 {'처치' if dead() else '생존'} · 내려찍기 경고 반경 {sorted(set(casts))} · 예고 {len(teles)}(끊김 {len(cut)}) · "
                    f"넉백 {len(knock)} · 둔화 {len(slow)} · 보스 광폭 {len(emp)}")
    finally:
        bot.close()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--server", default=str(REPO / "build" / "linux-memory" / "GameServer"))
    ap.add_argument("--port", type=int, default=4100)
    ap.add_argument("--keep", action="store_true", help="시험용 GameData 사본 · 서버 로그를 남긴다")
    ap.add_argument("--only", default="", help="쉼표로 시나리오 이름(dash,field,heal,boss)")
    args = ap.parse_args()

    work = Path(tempfile.mkdtemp(prefix="harven_mai_"))
    make_fixture(REPO / "Bin" / "GameData", work / "GameData")
    log = open(work / "server.log", "w")
    srv = subprocess.Popen([str(Path(args.server).resolve()), "--data-dir", str(work / "GameData"), "--port", str(args.port)],
                           stdout=log, stderr=subprocess.STDOUT, cwd=work)
    results = []
    try:
        if not wait_port(args.port, 60):
            print("FAIL: 서버가 뜨지 않았다")
            return 1
        time.sleep(2.0)
        scenarios = [("dash", scenario_dash), ("field", scenario_field), ("heal", scenario_heal), ("boss", scenario_boss)]
        only = {s for s in args.only.split(",") if s}
        for name, fn in scenarios:
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
