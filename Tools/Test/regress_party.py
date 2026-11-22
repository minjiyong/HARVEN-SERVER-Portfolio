"""파티 · 모집 게시판 · 처치 보상 회귀 시험 — 파티 관리 · 상태 공유 · 모집 게시판 · 처치 보상(피해 비례 나눔 · 쩔 보장 · 막타 자동 획득 ·
희귀도 알림 · 넘친 전리품 줍기 우선권 · 퀘스트 인정)을 실제 패킷으로 본다.

    python3 Tools/Test/regress_party.py [--server build/linux-memory/GameServer] [--port 4200] [--only manage,exp] [--keep]

HARVEN_IN_MEMORY_DB=ON 빌드를 이 스크립트가 직접 띄운다. 시험용 GameData 사본:
  · PartyConst — 초대 3초 · 끊긴 파티원 3초 · 모집 글 3초 · 상태 모으기 0.2초 · 넘친 전리품 우선권 주인 6초 / 파티 12초(시험 시간을 줄인다)
  · Monster — 모든 종족 HP 4000 · 공격력 1 · 공격 간격 5초 · 걸음 60초(거의 서 있다 — 시험 봇(각인술사)이 둘레 칸으로 걸어가 바로 옆에서 쏜다) ·
    비선공 · 경험치 1000 · 그로시 100%(1000) — 맞아도 죽지 않고, 몫을 셈하기 쉽게
  · TierLoot 희귀 10000bp(일반 · 고급 0) — 처치마다 희귀 필드 장비 하나(희귀 → 알림) · 정수 · 결정 0‰ · LootConst 정예 0‰
    (경제 1차 처치 보상 — 막타가 아닌 사람에게 정수가 가지 않게, 정예 배율이 몫 셈을 흔들지 않게)
  · QuestObjective — 퀘스트 4의 둘째 목표를 "올벤 숲(1)에서 아무 몬스터 50"(KillIn)으로
샌드박스 이름 규칙(Tools/Sandbox/MemDBConnection.cpp): "lvNN_xyX_Y_…" = 레벨 NN 캐릭터가 (X, Y)에서 시작, "xyX_Y_…" = 1레벨,
  "…_FI…" = 가방 가득(지팡이 착용 + 철검 29), "…_Q<번호>…" = 그 퀘스트를 받은 각인술사.
이름은 19바이트 안(NAME_SIZE 20) — 시나리오마다 짧은 꼬리표를 붙인다.

시나리오
  manage   만들기 · 초대(거절 · 만료 · 수락) · 구성 · 파티 채팅 · 대상 공유 · 집중 표시 · 초대 권한 · 위임 · 추방 · 탈퇴 · 해산 · 멱등
  offline  끊긴 파티원(접속 표시 · 파티장 넘김 · 다시 들어와 그대로 · 오래 끊기면 빠짐)
  recruit  모집 글 올리기 · 목록(거르기 · 빈자리) · 레벨 밖 · 신청 · 승인 · 거절 · 자동 가입 · 움직임 없으면 닫힘
  sync     파티원 HP · 위치 줄(PARTY_INF Members)
  exp      파티 경험치: 피해 비례 · 쩔 보장(레벨 차 20 안) · 보장 없음(레벨 차 20 밖) · 합 = 몬스터 경험치
  share    파티가 없는 둘: 경험치 · 그로시를 피해 비례로 나눠 받는다(막타가 아니어도) · 아이템은 막타만(가방에 바로) ·
           희귀 이상은 서버 전체 시스템 채팅(멀리 있는 사람도) · 일반 등급은 알리지 않는다
  mix      모르는 사람 + 파티: 모두에게 피해 비례 → 파티 몫 안에서만 쩔 보장(모르는 사람의 몫은 그대로)
  loot     가방이 가득 찬 막타: 전리품이 발밑에 — 주인만(파티원도 못 줍는다) → 주인 + 파티원(남은 못 줍는다) → 누구나 · 주우면 알림
  quest    퀘스트 처치 목표: 막타 · 피해를 준 사람 모두 오른다, 때리지 않은 사람은 안 오른다
"""
from __future__ import annotations

import argparse
import json
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
sys.path.insert(0, str(REPO / "Tools" / "DataTool"))

T_LOGIN, T_LOGIN_ACK, T_MOVE_REQ, T_MOVE_ACK = 35, 11, 53, 54
T_ADD, T_MOVE_NFY, T_REMOVE, T_DIE, T_RESPAWN = 21, 22, 23, 24, 25
T_DAMAGE, T_SKILL, T_STAT_CHANGE, T_SYSTEM, T_FORCED = 49, 80, 30, 32, 82
T_CS_CHAT, T_SC_CHAT = 88, 52
T_PICKUP_REQ, T_PICKUP_ACK = 9, 19
T_PARTY_REQ, T_PARTY_ACK, T_PARTY_INF, T_PARTY_NFY = 104, 105, 106, 107
T_ADENA, T_QUEST_UPDATE = 46, 75
CAT_USER, CAT_MONSTER, CAT_ITEM = 2, 3, 8
CH_SYSTEM, CH_PARTY = 2, 7
SYS_CHAT_NO_PARTY, SYS_LOOT_ACQUIRED, SYS_LOOT_OVERFLOW = 23, 24, 25
ADENA_PICKUP = 0
QUEST_PROGRESS = 1

# PartyOp
(OP_INFO, OP_CREATE, OP_INVITE, OP_REPLY, OP_LEAVE, OP_KICK, OP_TRANSFER, OP_DISBAND, OP_SETTINGS, OP_TARGET, OP_MARK,
 OP_RLIST, OP_RPOST, OP_RCLOSE, OP_RAPPLY, OP_RACCEPT, OP_RREJECT, OP_RAPPLICANTS, OP_RCANCEL) = range(19)
# PartyResult
(R_OK, R_NOT_IN_PARTY, R_ALREADY, R_NO_PERM, R_FULL, R_TARGET_NOT_FOUND, R_TARGET_IN_PARTY, R_INVITE_EXPIRED,
 R_BUSY, R_INVALID, R_DUPLICATE, R_TEXT_INVALID, R_LEVEL_OUT, R_RECRUIT_NOT_FOUND, R_ALREADY_APPLIED, R_TOO_FAST, R_NOT_APPLIED) = range(17)
# PartyEvent
(EV_INVITE_FROM, EV_INVITE_DECLINED, EV_JOINED, EV_LEFT, EV_KICKED, EV_LEADER, EV_DISBANDED, EV_ONLINE, EV_OFFLINE,
 EV_DIED, EV_REVIVED, EV_TARGET, EV_MARK, EV_SETTINGS, EV_R_APPLIED, EV_R_ACCEPTED, EV_R_REJECTED, EV_R_CLOSED,
 EV_EXP_SHARE, EV_TIMEOUT) = range(20)
INF_ROSTER, INF_MEMBERS, INF_RLIST, INF_APPLICANTS = range(4)
F_ONLINE, F_DEAD, F_COMBAT, F_LEADER = 1, 2, 4, 8
PURPOSE_HUNT, PURPOSE_BOSS, PURPOSE_QUEST = range(3)
CLOSE_LEADER, CLOSE_IDLE, CLOSE_DISBANDED = range(3)

NPOS = (0, 0, 0, 0)
FIXTURE_EXP = 1000
FIXTURE_ADENA = 1000
QUEST_ID, QUEST_KILL_OBJ = 4, 1   # 퀘스트 4의 둘째 목표(시험 사본에서 KillIn 50)
ITEM_TYPE: dict[int, str] = {}    # ItemTableId → eEquipment · eConsumable …(시험 사본 Item.json)
ITEM_NAME: dict[int, str] = {}
CARRY_RANGE, CARRY_PCT = 20, 20
TAG = "".join(random.choice(string.ascii_lowercase) for _ in range(3))


def objid(b: bytes, off: int):
    return struct.unpack_from("<HHIQ", b, off)


def pack_obj(o) -> bytes:
    return struct.pack("<HHIQ", *o)


def cstr(b: bytes) -> str:
    return b.split(b"\0")[0].decode("utf-8", "replace")


class Bot:
    def __init__(self, port: int, name: str, cls: int = 1):
        self.name = name
        self.c = socket.create_connection(("127.0.0.1", port), timeout=5)
        self.c.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.buf = b""
        self.log: list[tuple[float, int, bytes]] = []
        self.monsters: dict[tuple, dict] = {}
        self.items: dict[tuple, dict] = {}
        self.roster: dict = {}
        self.members: dict[int, dict] = {}
        self.exp = None
        self.level = None
        self.seq = 0
        self.pseq = 0
        self.c.sendall(struct.pack("<HB20s20sHB", 46, T_LOGIN, name.encode(), b"pw", 3, cls))
        self.me = None
        self.pos = None
        end = time.time() + 5
        while self.me is None and time.time() < end:
            self.poll(0.2)
        if self.me is None:
            raise RuntimeError(f"{name}: 로그인 응답 없음")
        self.poll(0.4)

    # ── 받기 ──
    def poll(self, seconds: float):
        end = time.time() + seconds
        while True:
            r, _, _ = select.select([self.c], [], [], max(0.0, end - time.time()))
            if r:
                data = self.c.recv(1 << 16)
                if not data:
                    raise ConnectionError(f"{self.name}: 서버가 연결을 끊었다")
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
            self.level = p[27]
            self.exp = struct.unpack_from("<I", p, 28)[0]
        elif t == T_MOVE_ACK and len(p) >= 9:
            self.pos = struct.unpack_from("<hh", p, 5)
        elif t == T_STAT_CHANGE:
            self.level = p[3]
            self.exp = struct.unpack_from("<I", p, 8)[0]
        elif t == T_ADD and len(p) >= 24:
            oid = objid(p, 4)
            x, y = struct.unpack_from("<hh", p, 20)
            if oid[0] == CAT_MONSTER:
                self.monsters[oid] = {"x": x, "y": y, "alive": True}
            elif oid[0] == CAT_ITEM:
                self.items[oid] = {"x": x, "y": y, "item": struct.unpack_from("<H", p, 44)[0], "at": time.time()}
        elif t == T_REMOVE:
            # 시야에서 빠졌다 — 몬스터도 지운다(남겨 두면 옛 자리를 쏘게 된다)
            oid = objid(p, 3)
            self.items.pop(oid, None)
            self.monsters.pop(oid, None)
        elif t == T_MOVE_NFY and len(p) >= 23:
            oid = objid(p, 3)
            if oid in self.monsters:
                self.monsters[oid]["x"], self.monsters[oid]["y"] = struct.unpack_from("<hh", p, 19)
        elif t == T_FORCED:
            # 서버가 옮긴 이동(돌진 · 넉백 …) — 각인탄은 조준한 칸을 맞히므로 몬스터 자리를 늘 맞춰 둔다
            oid = objid(p, 3)
            if oid in self.monsters:
                self.monsters[oid]["x"], self.monsters[oid]["y"] = struct.unpack_from("<hh", p, 23)
            if oid == self.me:
                self.pos = struct.unpack_from("<hh", p, 23)
        elif t == T_DIE:
            oid = objid(p, 3)
            if oid in self.monsters:
                self.monsters[oid]["alive"] = False
        elif t == T_RESPAWN and len(p) >= 24:
            # SUBJECT_RESPAWN_NFY: monster_type(3) · id(4) · x, y(20) — 다시 선 자리는 Zone 안 아무 데나
            oid = objid(p, 4)
            if oid[0] == CAT_MONSTER:
                x, y = struct.unpack_from("<hh", p, 20)
                self.monsters[oid] = {"x": x, "y": y, "alive": True}
        elif t == T_PARTY_INF:
            kind = p[3]
            if kind == INF_ROSTER:
                pid, ver, leader, policy, maxm, total, first, count, rid = struct.unpack_from("<IBBBBBBBI", p, 4)
                if first == 0:
                    self.roster = {"party": pid, "version": ver, "leader": leader, "policy": policy, "max": maxm,
                                   "total": total, "recruit": rid, "mark": objid(p, 19), "rows": {}}
                for i in range(count):
                    o = 35 + i * 49
                    self.roster["rows"][first + i] = {
                        "name": cstr(p[o:o + 20]), "obj": objid(p, o + 20), "level": p[o + 36], "cls": p[o + 37],
                        "flags": p[o + 38], "zone": struct.unpack_from("<H", p, o + 39)[0],
                        "hp": struct.unpack_from("<HHHH", p, o + 41)}
            elif kind == INF_MEMBERS:
                count = p[9]
                for i in range(count):
                    o = 10 + i * 17
                    slot, level, flags, zone, x, y, hp, mhp, mp, mmp = struct.unpack_from("<BBBHhhHHHH", p, o)
                    self.members[slot] = {"level": level, "flags": flags, "zone": zone, "x": x, "y": y, "hp": hp, "maxhp": mhp, "mp": mp}

    # ── 보내기 ──
    def party(self, op, arg8=0, arg32=0, obj=NPOS, purpose=0, minl=0, maxl=0, slots=0, target="", text="", seq=None):
        if seq is None:
            self.pseq += 1
            seq = self.pseq
        pkt = struct.pack("<HBBIBI", 99, T_PARTY_REQ, op, seq, arg8, arg32) + pack_obj(obj) + \
            struct.pack("<BBBB20s46s", purpose, minl, maxl, slots, target.encode(), text.encode("utf-8"))
        assert len(pkt) == 99
        self.c.sendall(pkt)
        return seq

    def chat(self, channel: int, text: str):
        self.c.sendall(struct.pack("<HBB20s128s", 152, T_CS_CHAT, channel, b"", text.encode("utf-8")))

    def attack(self, target, skill_id=7):
        m = self.monsters[target]
        self.seq = (self.seq + 1) & 0xFFFF
        pkt = struct.pack("<HBHBB", 30, T_SKILL, skill_id, 0, 0) + pack_obj(target) + struct.pack("<hhBH", m["x"], m["y"], 0, self.seq)
        self.c.sendall(pkt)

    def step(self, direction: int):
        self.seq = (self.seq + 1) & 0xFFFF
        self.c.sendall(struct.pack("<HBBH", 6, T_MOVE_REQ, direction, self.seq))

    def pickup(self):
        self.c.sendall(struct.pack("<HB", 3, T_PICKUP_REQ))

    # ── 찾기 ──
    def packets(self, t, since=0.0):
        return [p for at, tt, p in self.log if tt == t and at >= since]

    def acks(self, op, since=0.0):
        return [{"result": p[4], "seq": struct.unpack_from("<I", p, 5)[0], "param": struct.unpack_from("<I", p, 9)[0]}
                for p in self.packets(T_PARTY_ACK, since) if p[3] == op]

    def nfys(self, ev, since=0.0):
        out = []
        for p in self.packets(T_PARTY_NFY, since):
            if p[2] == T_PARTY_NFY and p[3] == ev:
                out.append({"arg8": p[4], "arg16": struct.unpack_from("<H", p, 5)[0], "arg32": struct.unpack_from("<I", p, 7)[0],
                            "obj": objid(p, 11), "target": objid(p, 27), "who": cstr(p[43:63]), "by": cstr(p[63:83])})
        return out

    def wait(self, pred, seconds=3.0):
        end = time.time() + seconds
        while time.time() < end:
            if pred():
                return True
            self.poll(0.05)
        return pred()

    def roster_names(self):
        return sorted(r["name"] for r in self.roster.get("rows", {}).values()) if self.roster.get("party") else []

    def leader_name(self):
        rows = self.roster.get("rows", {})
        return rows.get(self.roster.get("leader"), {}).get("name") if self.roster.get("party") else None

    def close(self):
        try:
            self.c.close()
        except OSError:
            pass


def pump(bots, seconds):
    end = time.time() + seconds
    while time.time() < end:
        for b in bots:
            b.poll(0.02)


def walk_each(goals, others=(), seconds=8.0) -> bool:
    """goals: [(bot, x, y)] — 저마다 8방향으로 한 칸씩(막혔으면 다음엔 가로 · 세로로), 걸음 간격은 서버 토큰 버킷 안"""
    dirs = {(0, -1): 0, (0, 1): 1, (-1, 0): 2, (1, 0): 3, (-1, -1): 4, (1, -1): 5, (-1, 1): 6, (1, 1): 7}
    last, tries = {}, {}
    everyone = list({id(b): b for b in [*(g[0] for g in goals), *others]}.values())
    end = time.time() + seconds
    while time.time() < end:
        if all(tuple(w.pos) == (x, y) for w, x, y in goals):
            return True
        for w, x, y in goals:
            if tuple(w.pos) == (x, y):
                continue
            dx, dy = x - w.pos[0], y - w.pos[1]
            sx, sy = (dx > 0) - (dx < 0), (dy > 0) - (dy < 0)
            if last.get(id(w)) == tuple(w.pos):
                tries[id(w)] = tries.get(id(w), 0) + 1
                k = tries[id(w)] % 3
                if k == 1 and sx and sy:
                    sy = 0
                elif k == 2 and sx and sy:
                    sx = 0
            last[id(w)] = tuple(w.pos)
            w.step(dirs[(sx, sy)])
        pump(everyone, 0.32)
    return all(tuple(w.pos) == (x, y) for w, x, y in goals)


def walk_many(walkers, x: int, y: int, others=(), seconds=8.0) -> bool:
    return walk_each([(w, x, y) for w in walkers], others, seconds)


def approach(bots, monster, others=(), seconds=12.0) -> bool:
    """bots를 몬스터 둘레 서로 다른 칸으로 — 바로 옆에서 쏘면 각인탄(사거리 7)이 쏘는 줄의 다른 몬스터를 맞히지 않는다"""
    m = bots[0].monsters[monster]
    ring = [(m["x"] + dx, m["y"] + dy) for dx in (-1, 0, 1) for dy in (-1, 0, 1) if dx or dy]
    goals, used = [], set()
    for b in bots:
        tile = min((t for t in ring if t not in used), key=lambda t: max(abs(t[0] - b.pos[0]), abs(t[1] - b.pos[1])))
        used.add(tile)
        goals.append((b, *tile))
    return walk_each(goals, others, seconds)


def split_exp(total, damages, levels, rng, pct):
    """PartyRule::SplitExp(Database/GameData/PartyRules.h)를 그대로 옮긴 것 — 서버가 나눈 몫과 맞대어 본다"""
    n = len(damages)
    d = [max(1, x) for x in damages]
    s = sum(d)
    base = [total * x // s for x in d]
    rem = [total * x % s for x in d]
    for _ in range(total - sum(base)):
        best = None
        for i in range(n):
            if rem[i] == 0 and best is not None:
                continue
            if best is None or rem[i] > rem[best] or (rem[i] == rem[best] and damages[i] > damages[best]):
                best = i
        base[best] += 1
        rem[best] = 0
    guaranteed = [False] * n
    if pct > 0:
        mx = max(levels)
        elig = [mx - lv <= rng for lv in levels]
        k = sum(elig)
        floor = total * min(pct, 100) // 100
        if k and floor * k > total:
            floor = total // k
        need = [floor - base[i] if elig[i] and base[i] < floor else 0 for i in range(n)]
        excess = [0 if need[i] else max(0, base[i] - (floor if elig[i] else 0)) for i in range(n)]
        sn, se = sum(need), sum(excess)
        if sn and se >= sn:
            take = [sn * excess[i] // se if excess[i] else 0 for i in range(n)]
            frac = [sn * excess[i] % se if excess[i] else 0 for i in range(n)]
            for _ in range(sn - sum(take)):
                best = None
                for i in range(n):
                    if frac[i] > 0 and (best is None or frac[i] > frac[best]):
                        best = i
                if best is None:
                    break
                take[best] += 1
                frac[best] = 0
            for i in range(n):
                base[i] -= take[i]
                if need[i]:
                    base[i] += need[i]
                    guaranteed[i] = True
    return base, guaranteed


def split_weight(total, weights):
    """RewardRule::SplitByWeight(Database/GameData/RewardRules.h)를 그대로 옮긴 것 — 그로시 · 경험치 1단계"""
    n = len(weights)
    w = [max(1, x) for x in weights]
    s = sum(w)
    out = [total * x // s for x in w]
    rem = [total * x % s for x in w]
    for _ in range(total - sum(out)):
        best = None
        for i in range(n):
            if rem[i] == 0 and best is not None:
                continue
            if best is None or rem[i] > rem[best] or (rem[i] == rem[best] and weights[i] > weights[best]):
                best = i
        out[best] += 1
        rem[best] = 0
    return out


def split_kill_exp(total, entries, rng, pct):
    """PartyRule::SplitKillExp — entries: [(피해, 레벨, 파티 번호 0 = 없음)] 피해 기록 순서. (몫, 보장) 목록"""
    base = split_weight(total, [d for d, _, _ in entries])
    out = [(b, False) for b in base]
    for party in {p for _, _, p in entries if p}:
        idx = [i for i, (_, _, p) in enumerate(entries) if p == party]
        if len(idx) < 2:
            continue
        got, g = split_exp(sum(base[i] for i in idx), [entries[i][0] for i in idx], [entries[i][1] for i in idx], rng, pct)
        for k, i in enumerate(idx):
            out[i] = (got[k], g[k])
    return out


def deaths(observer: Bot, since: float) -> list:
    """observer가 본, since 뒤에 쓰러진 몬스터(쓰러진 순서)"""
    seen, out = set(), []
    for p in observer.packets(T_DIE, since):
        o = objid(p, 3)
        if o[0] == CAT_MONSTER and o not in seen:
            seen.add(o)
            out.append(o)
    return out


def killer_of(observer: Bot, monster, since: float):
    hits = [p for p in observer.packets(T_DAMAGE, since) if objid(p, 3) == monster]
    return objid(hits[-1], 19) if hits else None


def sys_msgs(bot: Bot, code: int, since: float) -> list:
    return [struct.unpack_from("<ii", p, 5) for p in bot.packets(T_SYSTEM, since) if struct.unpack_from("<H", p, 3)[0] == code]


def system_chats(bot: Bot, since: float) -> list:
    """SC_CHAT 시스템 채널(전리품 알림) — (보낸 이 ObjID, 이름, 글)"""
    return [(objid(p, 4), cstr(p[20:40]), cstr(p[40:168])) for p in bot.packets(T_SC_CHAT, since) if p[3] == CH_SYSTEM]


def adena_gained(bot: Bot, since: float) -> int:
    return sum(struct.unpack_from("<i", p, 7)[0] for p in bot.packets(T_ADENA, since) if p[11] == ADENA_PICKUP)


def dealt_by(observer: Bot, monster, since: float) -> dict:
    """observer가 본 SUBJECT_DAMAGE_NFY로 사람마다 실제로 깎은 HP(넘친 막타는 남은 HP만큼 — 서버 CDamageLedger와 같다).
    처음 본 한 방은 그 한 방의 피해만 센다 — 앞 시나리오의 봇이 때리다 만(이미 깎인) 몬스터일 수 있다(그 몫은 나간 사람 것이라 서버도 뺀다)."""
    out, hp = {}, None
    for p in observer.packets(T_DAMAGE, since):
        if objid(p, 3) != monster:
            continue
        att = objid(p, 19)
        now_hp, max_hp, dmg = struct.unpack_from("<iii", p, 35)
        actual = min(dmg, max_hp - now_hp) if hp is None else max(0, hp - now_hp)
        out[att] = out.get(att, 0) + actual
        hp = now_hp
    return out


def nm(lv: int, x: int, y: int, role: str) -> str:
    """샌드박스 이름(레벨 · 시작 자리) — 19바이트 안"""
    name = f"lv{lv}_xy{x}_{y}_{TAG}{role}"
    assert len(name) <= 19, name
    return name


def line(x0, y0, x1, y1):
    n = max(abs(x1 - x0), abs(y1 - y0))
    return [(round(x0 + (x1 - x0) * i / n), round(y0 + (y1 - y0) * i / n)) for i in range(n + 1)] if n else [(x0, y0)]


def pick_monster(bots, max_d=9, seconds=30.0, others=()):
    """모두가 보는 가까운 몬스터 — 같은 칸에 다른 몬스터도 · 시험 봇(others 포함)도 없는 것(그 칸에 선 사람이 각인탄을 막는다).
    없으면 다시 태어나기를 기다린다(몬스터는 10초 뒤 Zone 안 아무 데나 다시 선다)."""
    standing = lambda: {tuple(b.pos) for b in [*bots, *others]}
    end = time.time() + seconds
    while True:
        best, bd = None, (2, 99)
        alive = [(o, m) for o, m in bots[0].monsters.items() if m["alive"]]
        for oid, m in alive:
            if any(oid not in b.monsters or not b.monsters[oid]["alive"] for b in bots):
                continue
            d = max(max(abs(m["x"] - b.pos[0]), abs(m["y"] - b.pos[1])) for b in bots)
            if d > max_d:
                continue
            if any(o != oid and (n["x"], n["y"]) == (m["x"], m["y"]) for o, n in alive):
                continue   # 같은 칸에 둘이면 다른 쪽이 맞을 수 있다
            if (m["x"], m["y"]) in standing():
                continue   # 그 칸에 사람이 서 있다
            # 바로 옆 칸에 다른 몬스터가 있으면 뒤로 — 대각선으로 쏜 각인탄이 옆 칸을 스치며 그쪽을 맞힌다
            crowded = int(any(o != oid and max(abs(n["x"] - m["x"]), abs(n["y"] - m["y"])) <= 1 for o, n in alive))
            if (crowded, d) >= bd:
                continue
            best, bd = oid, (crowded, d)
        if best is not None or time.time() >= end:
            return best
        pump(bots, 1.0)


def form_party(leader: Bot, *others: Bot) -> bool:
    leader.party(OP_CREATE)
    if not leader.wait(lambda: leader.roster.get("party")):
        return False
    for o in others:
        t0 = time.time()
        leader.party(OP_INVITE, target=o.name)
        if not o.wait(lambda: o.nfys(EV_INVITE_FROM, t0)):
            return False
        o.party(OP_REPLY, arg8=1, arg32=o.nfys(EV_INVITE_FROM, t0)[-1]["arg32"])
        if not o.wait(lambda: o.roster.get("party") == leader.roster["party"]):
            return False
    pump([leader, *others], 0.5)
    return True


def kill_with(bots, monster, order, seconds=40.0):
    """order: [(bot, hp 문턱)] — 몬스터 HP가 문턱보다 크면 그 bot이 때린다(앞에서부터). 문턱 0 = 죽을 때까지."""
    end = time.time() + seconds
    while time.time() < end:
        pump(bots, 0.3)
        if not bots[0].monsters.get(monster, {}).get("alive", False):
            return True
        dmg = [p for p in bots[0].packets(T_DAMAGE) if objid(p, 3) == monster]
        hp = struct.unpack_from("<i", dmg[-1], 35)[0] if dmg else 4000
        for bot, threshold in order:
            if hp > threshold:
                if monster in bot.monsters:
                    bot.attack(monster)
                break
    return not bots[0].monsters.get(monster, {}).get("alive", False)


# ── 시나리오 ────────────────────────────────────────────────────────────────
def scenario_manage(port: int):
    a, b, c = Bot(port, f"pm{TAG}a"), Bot(port, f"pm{TAG}b"), Bot(port, f"pm{TAG}c")
    notes, ok = [], True
    try:
        def check(label, cond):
            nonlocal ok
            notes.append(f"{label}:{'ok' if cond else 'FAIL'}")
            ok &= bool(cond)

        # 로그인 직후 빈 구성(파티 없음)
        check("빈 구성", a.wait(lambda: any(p[3] == INF_ROSTER for p in a.packets(T_PARTY_INF))) and not a.roster.get("party"))
        # 파티가 없는 사람의 파티 채팅은 거절
        a.chat(CH_PARTY, "hello")
        check("파티 없음 채팅 거절", a.wait(lambda: any(struct.unpack_from("<H", p, 3)[0] == SYS_CHAT_NO_PARTY for p in a.packets(T_SYSTEM))))

        # 초대 → 거절(초대한 사람에게 알림, 파티는 아직 없다 — 받아들일 때 만든다)
        t0 = time.time()
        a.party(OP_INVITE, target=b.name)
        check("초대 받음", b.wait(lambda: b.nfys(EV_INVITE_FROM, t0)))
        inv = b.nfys(EV_INVITE_FROM, t0)[-1]
        check("초대 정보", inv["who"] == a.name and inv["arg8"] == 0)
        b.party(OP_REPLY, arg8=0, arg32=inv["arg32"])
        check("거절 알림", a.wait(lambda: a.nfys(EV_INVITE_DECLINED, t0)) and a.nfys(EV_INVITE_DECLINED, t0)[-1]["who"] == b.name)
        check("거절 뒤 파티 없음", not a.roster.get("party"))

        # 초대 만료(3초)
        t0 = time.time()
        a.party(OP_INVITE, target=b.name)
        b.wait(lambda: b.nfys(EV_INVITE_FROM, t0))
        inv = b.nfys(EV_INVITE_FROM, t0)[-1]
        pump([a, b, c], 4.2)
        t1 = time.time()
        b.party(OP_REPLY, arg8=1, arg32=inv["arg32"])
        check("만료된 초대", b.wait(lambda: b.acks(OP_REPLY, t1)) and b.acks(OP_REPLY, t1)[-1]["result"] == R_INVITE_EXPIRED)

        # 초대 → 수락: 초대한 사람이 파티장인 파티가 생긴다
        t0 = time.time()
        a.party(OP_INVITE, target=b.name)
        b.wait(lambda: b.nfys(EV_INVITE_FROM, t0))
        b.party(OP_REPLY, arg8=1, arg32=b.nfys(EV_INVITE_FROM, t0)[-1]["arg32"])
        check("수락 → 구성", b.wait(lambda: b.roster_names() == sorted([a.name, b.name])) and a.wait(lambda: a.roster_names() == sorted([a.name, b.name])))
        check("파티장 = 초대한 사람", a.leader_name() == a.name and b.leader_name() == a.name)
        check("파티원 접속 표시", all(r["flags"] & F_ONLINE for r in a.roster["rows"].values()))

        # 파티장만 초대(기본) — b가 c를 초대하면 권한 없음
        t0 = time.time()
        b.party(OP_INVITE, target=c.name)
        check("초대 권한(파티장만)", b.wait(lambda: b.acks(OP_INVITE, t0)) and b.acks(OP_INVITE, t0)[-1]["result"] == R_NO_PERM)
        # 설정: 누구나 초대 → b가 c를 초대 · c 수락
        t0 = time.time()
        a.party(OP_SETTINGS, arg8=1)
        check("설정 알림", b.wait(lambda: b.nfys(EV_SETTINGS, t0)) and b.roster.get("policy") == 1)
        t0 = time.time()
        b.party(OP_INVITE, target=c.name)
        c.wait(lambda: c.nfys(EV_INVITE_FROM, t0))
        check("파티원 초대 받음(인원 2)", c.nfys(EV_INVITE_FROM, t0) and c.nfys(EV_INVITE_FROM, t0)[-1]["arg8"] == 2)
        c.party(OP_REPLY, arg8=1, arg32=c.nfys(EV_INVITE_FROM, t0)[-1]["arg32"])
        check("셋", a.wait(lambda: len(a.roster_names()) == 3) and a.nfys(EV_JOINED, t0) and a.nfys(EV_JOINED, t0)[-1]["who"] == c.name)

        # 파티 채팅 — 셋 모두(보낸 사람 포함)
        t0 = time.time()
        b.chat(CH_PARTY, "모로즈 가요")
        got = all(x.wait(lambda x=x: any(p[3] == CH_PARTY for p in x.packets(T_SC_CHAT, t0))) for x in (a, b, c))
        texts = [cstr(p[40:168]) for p in c.packets(T_SC_CHAT, t0) if p[3] == CH_PARTY]
        check("파티 채팅", got and "모로즈 가요" in texts)

        # 대상 공유 · 집중 표시
        fake = (CAT_MONSTER, 3, 0, 1000000001)
        t0 = time.time()
        b.party(OP_TARGET, obj=fake)
        check("대상 공유", a.wait(lambda: a.nfys(EV_TARGET, t0)) and a.nfys(EV_TARGET, t0)[-1]["target"] == fake
              and a.nfys(EV_TARGET, t0)[-1]["obj"] == b.me and not b.nfys(EV_TARGET, t0))
        t0 = time.time()
        b.party(OP_MARK, obj=fake)
        check("집중 표시는 파티장만", b.wait(lambda: b.acks(OP_MARK, t0)) and b.acks(OP_MARK, t0)[-1]["result"] == R_NO_PERM)
        a.party(OP_MARK, obj=fake)
        check("집중 표시", c.wait(lambda: c.nfys(EV_MARK, t0)) and c.nfys(EV_MARK, t0)[-1]["target"] == fake)

        # 멱등 — 같은 순번은 한 번만
        t0 = time.time()
        seq = a.party(OP_SETTINGS, arg8=0)
        a.party(OP_SETTINGS, arg8=0, seq=seq)
        # 겹친 요청의 거절(Duplicate)은 소유 Zone이 바로, 처음 것의 결과는 서비스가 보낸다 — 도착 순서는 정하지 않는다
        check("멱등", a.wait(lambda: len(a.acks(OP_SETTINGS, t0)) == 2) and sorted(x["result"] for x in a.acks(OP_SETTINGS, t0)) == [R_OK, R_DUPLICATE])

        # 위임 → b가 파티장
        t0 = time.time()
        a.party(OP_TRANSFER, target=b.name)
        check("위임", c.wait(lambda: c.nfys(EV_LEADER, t0)) and c.nfys(EV_LEADER, t0)[-1]["who"] == b.name and c.wait(lambda: c.leader_name() == b.name))
        # 추방: 새 파티장 b가 c를
        t0 = time.time()
        a.party(OP_KICK, target=c.name)
        check("추방 권한", a.wait(lambda: a.acks(OP_KICK, t0)) and a.acks(OP_KICK, t0)[-1]["result"] == R_NO_PERM)
        b.party(OP_KICK, target=c.name)
        check("추방", c.wait(lambda: c.nfys(EV_KICKED, t0)) and c.nfys(EV_KICKED, t0)[-1]["by"] == b.name
              and c.wait(lambda: not c.roster.get("party")) and a.wait(lambda: len(a.roster_names()) == 2))
        # 탈퇴: 파티장 b가 나가면 a가 파티장
        t0 = time.time()
        b.party(OP_LEAVE)
        check("파티장 탈퇴 → 넘김", a.wait(lambda: a.leader_name() == a.name and a.roster_names() == [a.name]) and b.wait(lambda: not b.roster.get("party")))
        # 해산
        t0 = time.time()
        a.party(OP_DISBAND)
        check("해산", a.wait(lambda: a.nfys(EV_DISBANDED, t0)) and not a.roster.get("party"))
        t0 = time.time()
        a.party(OP_LEAVE)
        check("해산 뒤 탈퇴 = 파티 없음", a.wait(lambda: a.acks(OP_LEAVE, t0)) and a.acks(OP_LEAVE, t0)[-1]["result"] == R_NOT_IN_PARTY)
        return ok, " · ".join(notes)
    finally:
        for x in (a, b, c):
            x.close()


def scenario_offline(port: int):
    a, b = Bot(port, f"po{TAG}a"), Bot(port, f"po{TAG}b")
    notes, ok = [], True
    try:
        def check(label, cond):
            nonlocal ok
            notes.append(f"{label}:{'ok' if cond else 'FAIL'}")
            ok &= bool(cond)
        check("파티", form_party(a, b))
        # 파티장 a가 끊긴다 → b가 파티장 · a는 접속 끊김 표시
        t0 = time.time()
        a.close()
        check("끊김 알림 · 넘김", b.wait(lambda: b.nfys(EV_OFFLINE, t0) and b.leader_name() == b.name))
        row = next(r for r in b.roster["rows"].values() if r["name"] == a.name)
        check("끊긴 줄", not (row["flags"] & F_ONLINE) and row["obj"] == NPOS)
        # 3초 안에 다시 들어오면 그대로 파티원
        a = Bot(port, a.name)
        check("다시 들어와 그대로", a.wait(lambda: a.roster_names() == sorted([a.name, b.name])) and b.wait(lambda: b.nfys(EV_ONLINE, t0)))
        # 다시 끊기고 3초가 지나면 빠진다
        t0 = time.time()
        a.close()
        check("오래 끊기면 빠짐", b.wait(lambda: b.nfys(EV_TIMEOUT, t0), 6.0) and b.roster_names() == [b.name])
        a = Bot(port, a.name)
        check("빠진 뒤 다시 들어오면 파티 없음", a.wait(lambda: any(p[3] == INF_ROSTER for p in a.packets(T_PARTY_INF))) and not a.roster.get("party"))
        return ok, " · ".join(notes)
    finally:
        a.close()
        b.close()


def scenario_recruit(port: int):
    lead = Bot(port, f"lv30_pr{TAG}l")
    lo = Bot(port, f"lv5_pr{TAG}x")
    mid = Bot(port, f"lv20_pr{TAG}m")
    mid2 = Bot(port, f"lv22_pr{TAG}n")
    auto = Bot(port, f"lv21_pr{TAG}o")
    bots = [lead, lo, mid, mid2, auto]
    notes, ok = [], True
    try:
        def check(label, cond):
            nonlocal ok
            notes.append(f"{label}:{'ok' if cond else 'FAIL'}")
            ok &= bool(cond)

        # 글 올리기 — 파티가 없으면 그때 만든다(파티장)
        t0 = time.time()
        lead.party(OP_RPOST, purpose=PURPOSE_BOSS, minl=15, maxl=30, slots=3, arg8=0, text="모로즈 잡으실 분")
        check("올리기", lead.wait(lambda: lead.acks(OP_RPOST, t0)) and lead.acks(OP_RPOST, t0)[-1]["result"] == R_OK
              and lead.wait(lambda: lead.roster.get("recruit") == lead.acks(OP_RPOST, t0)[-1]["param"]))
        rid = lead.acks(OP_RPOST, t0)[-1]["param"]
        t0 = time.time()
        lead.party(OP_RPOST, purpose=PURPOSE_BOSS, minl=15, maxl=30, slots=3, text="운영자 파티")
        check("금칙어 제목", lead.wait(lambda: lead.acks(OP_RPOST, t0)) and lead.acks(OP_RPOST, t0)[-1]["result"] == R_TEXT_INVALID)

        # 목록 — 목적 거르기 · 빈자리
        def listed(bot, purpose):
            t = time.time()
            bot.party(OP_RLIST, arg8=purpose, arg32=0)
            bot.wait(lambda: any(p[3] == INF_RLIST and p[4] == purpose for p in bot.packets(T_PARTY_INF, t)))
            p = [p for p in bot.packets(T_PARTY_INF, t) if p[3] == INF_RLIST][-1]
            rows = []
            for i in range(p[11]):
                o = 12 + i * 76
                rid_, pur, mn, mx, mem, slots, flags = struct.unpack_from("<IBBBBBB", p, o)
                rows.append({"id": rid_, "purpose": pur, "min": mn, "max": mx, "members": mem, "slots": slots, "flags": flags,
                             "leader": cstr(p[o + 10:o + 30]), "title": cstr(p[o + 30:o + 76])})
            return rows
        rows = listed(mid, PURPOSE_BOSS)
        mine = [r for r in rows if r["id"] == rid]
        check("목록(보스)", mine and mine[0]["title"] == "모로즈 잡으실 분" and mine[0]["leader"] == lead.name
              and mine[0]["members"] == 1 and mine[0]["slots"] == 3)
        mid.poll(0.6)
        check("목록(사냥)엔 없다", not [r for r in listed(mid, PURPOSE_HUNT) if r["id"] == rid])

        # 레벨 밖
        t0 = time.time()
        lo.party(OP_RAPPLY, arg32=rid)
        check("레벨 밖", lo.wait(lambda: lo.acks(OP_RAPPLY, t0)) and lo.acks(OP_RAPPLY, t0)[-1]["result"] == R_LEVEL_OUT)
        # 신청 → 파티장 알림 → 신청자 목록 → 승인
        t0 = time.time()
        mid.party(OP_RAPPLY, arg32=rid)
        check("신청(대기)", mid.wait(lambda: mid.acks(OP_RAPPLY, t0)) and mid.acks(OP_RAPPLY, t0)[-1]["param"] == 0
              and lead.wait(lambda: lead.nfys(EV_R_APPLIED, t0)) and lead.nfys(EV_R_APPLIED, t0)[-1]["who"] == mid.name
              and lead.nfys(EV_R_APPLIED, t0)[-1]["arg8"] == 20)
        mid2.party(OP_RAPPLY, arg32=rid)
        lead.wait(lambda: len(lead.nfys(EV_R_APPLIED, t0)) == 2)
        lead.poll(0.6)
        t1 = time.time()
        lead.party(OP_RAPPLICANTS)
        lead.wait(lambda: any(p[3] == INF_APPLICANTS for p in lead.packets(T_PARTY_INF, t1)))
        ap = [p for p in lead.packets(T_PARTY_INF, t1) if p[3] == INF_APPLICANTS]
        names = [cstr(ap[-1][9 + i * 22:9 + i * 22 + 20]) for i in range(ap[-1][8])] if ap else []
        check("신청자 목록", names == [mid.name, mid2.name])
        t0 = time.time()
        lead.party(OP_RREJECT, target=mid2.name)
        check("거절", mid2.wait(lambda: mid2.nfys(EV_R_REJECTED, t0)))
        lead.party(OP_RACCEPT, target=mid.name)
        check("승인 → 들어옴", mid.wait(lambda: mid.nfys(EV_R_ACCEPTED, t0)) and mid.wait(lambda: mid.roster_names() == sorted([lead.name, mid.name])))
        # 자동 가입 글로 고친다(끌어올림) → 바로 들어온다 → 목표 인원(3) 가득
        t0 = time.time()
        lead.party(OP_RPOST, purpose=PURPOSE_BOSS, minl=15, maxl=30, slots=3, arg8=1, text="자동 가입 보스")
        lead.wait(lambda: lead.acks(OP_RPOST, t0))
        auto.party(OP_RAPPLY, arg32=rid)
        check("자동 가입", auto.wait(lambda: auto.acks(OP_RAPPLY, t0)) and auto.acks(OP_RAPPLY, t0)[-1]["param"] == 1
              and auto.wait(lambda: len(auto.roster_names()) == 3))
        t0 = time.time()
        mid2.party(OP_RAPPLY, arg32=rid)
        check("가득", mid2.wait(lambda: mid2.acks(OP_RAPPLY, t0)) and mid2.acks(OP_RAPPLY, t0)[-1]["result"] == R_FULL)
        mid2.poll(0.6)
        full = [r for r in listed(mid2, 0xFF) if r["id"] == rid]
        check("빈자리 0", full and full[0]["members"] == 3 and full[0]["slots"] == 3 and full[0]["flags"] & 1)
        # 움직임이 없으면(3초) 닫힌다 — 파티장에게 알림, 파티는 그대로
        t0 = time.time()
        check("움직임 없음 → 닫힘", lead.wait(lambda: lead.nfys(EV_R_CLOSED, t0), 6.0)
              and lead.nfys(EV_R_CLOSED, t0)[-1]["arg8"] == CLOSE_IDLE and lead.wait(lambda: lead.roster.get("recruit") == 0)
              and len(lead.roster_names()) == 3)
        return ok, " · ".join(notes)
    finally:
        for x in bots:
            x.close()


def scenario_sync(port: int):
    a, b = Bot(port, nm(30, 749, 251, "a")), Bot(port, nm(30, 750, 251, "b"))
    notes, ok = [], True
    try:
        def check(label, cond):
            nonlocal ok
            notes.append(f"{label}:{'ok' if cond else 'FAIL'}")
            ok &= bool(cond)
        check("파티", form_party(a, b))
        slot_b = next(s for s, r in a.roster["rows"].items() if r["name"] == b.name)
        check("파티원 줄(HP · 위치)", a.wait(lambda: slot_b in a.members and a.members[slot_b]["maxhp"] > 0
                                         and (a.members[slot_b]["x"], a.members[slot_b]["y"]) == tuple(b.pos)))
        # b가 한 칸 걸으면 a가 새 위치를 받는다
        before = b.pos
        b.step(3)
        pump([a, b], 0.6)
        check("위치 갱신", b.pos != before and a.wait(lambda: (a.members[slot_b]["x"], a.members[slot_b]["y"]) == tuple(b.pos)))
        return ok, " · ".join(notes)
    finally:
        a.close()
        b.close()


def scenario_exp(port: int):
    a = Bot(port, nm(40, 749, 251, "a"))   # 쩔 해 주는 사람
    b = Bot(port, nm(25, 750, 251, "b"))   # 레벨 차 15 — 보장 안
    c = Bot(port, nm(12, 748, 251, "c"))   # 레벨 차 28 — 보장 밖
    notes, ok = [], True
    try:
        def check(label, cond):
            nonlocal ok
            notes.append(f"{label}:{'ok' if cond else 'FAIL'}")
            ok &= bool(cond)
        bots = [a, b, c]
        check("파티", form_party(a, b))
        pump(bots, 1.0)

        def one_kill(carried: Bot, carried_level: int, label: str, expect_guarantee: bool):
            # 쩔 받는 사람이 한 번 → 쩔 해 주는 사람이 끝까지. 조준한 칸에 다른 몬스터가 끼어 쩔 받는 사람이 못 맞히면 다른 몬스터로 다시
            for attempt in range(5):
                m = pick_monster([a, carried], others=bots)
                if m is None or not approach([a, carried], m, bots):
                    continue
                t0 = time.time()
                hit = lambda: any(objid(p, 3) == m and objid(p, 19) == carried.me for p in carried.packets(T_DAMAGE, t0))
                for _ in range(3):
                    carried.attack(m)
                    if carried.wait(hit, 2.5):
                        break
                if hit():
                    break
            else:
                mm = carried.monsters.get(m, {}) if m is not None else {}
                mine = [(objid(p, 3)[3], struct.unpack_from("<i", p, 35)[0]) for p in carried.packets(T_DAMAGE) if objid(p, 19) == carried.me]
                other = {v: carried.monsters.get(o) for o in {objid(p, 3) for p in carried.packets(T_DAMAGE) if objid(p, 19) == carried.me} for v in [o[3]]}
                hist = [(round(at, 1), tt) for at, tt, p in carried.log if tt in (T_ADD, T_MOVE_NFY, T_FORCED, T_REMOVE, T_RESPAWN, T_DIE)
                        and any(len(p) >= off + 16 and objid(p, off)[3] in other for off in (3, 4))]   # 19B 패킷(지움)은 자리 3만
                notes.append(f"[진단] 맞힌 몬스터의 내 기록 {other} · 그 몬스터 패킷 {hist[-8:]}")
                notes.append(f"[진단] {carried.name} 자리 {carried.pos} · 몬스터 {m and m[3]} {mm} · 내가 맞힌 것 {mine[-6:]} · 기술 결과(결과, MP) {[(p[7], struct.unpack_from('<H', p, 12)[0]) for p in carried.packets(57)][-6:]}")
                check(f"{label} 쩔 받는 사람이 때림", False)
                return
            t_kill = time.time()
            killed = kill_with(bots, m, [(a, 0)])
            if not killed:
                res = [(p[7], struct.unpack_from("<H", p, 12)[0]) for p in a.packets(57, t_kill)]
                notes.append(f"[진단] 처치 실패 — a 자리 {a.pos} · 몬스터 {a.monsters.get(m)} · 기술 결과(결과, MP) {res[:3]}…{res[-3:]} 총 {len(res)}")
            pump(bots, 0.8)
            # 이 몬스터에 준 피해 전부(앞선 시도에서 때리다 만 것까지 — 서버 피해 기록과 같다). 파티를 나간 b가 때렸으면 b도 모르는 사람으로 받는다
            dealt = {w: d for w, d in dealt_by(a, m, 0).items() if d > 0}
            party = a.roster.get("party", 0)
            level = {a.me: 40, b.me: 25, c.me: 12}
            pid = {a.me: party, carried.me: party}
            order = [w for w in dealt if w in level]
            want = split_kill_exp(FIXTURE_EXP, [(dealt[w], level[w], pid.get(w, 0)) for w in order], CARRY_RANGE, CARRY_PCT)
            want = {w: want[i] for i, w in enumerate(order)}
            got = {}
            for x in (a, b, c):
                n = [q for q in x.nfys(EV_EXP_SHARE, t0) if q["target"] == m]
                if n:
                    got[x.me] = (n[-1]["arg32"], bool(n[-1]["arg16"]))
            da, dc = dealt.get(a.me, 0), dealt.get(carried.me, 0)
            notes.append(f"{label}: 피해 {da}:{dc} → 몫 {got.get(a.me)}:{got.get(carried.me)}(기대 {want.get(a.me)}:{want.get(carried.me)})"
                         + (f" · 파티 밖 b {got.get(b.me)}(기대 {want.get(b.me)})" if b.me in want and carried is not b else ""))
            check(f"{label} 처치", killed)
            check(f"{label} 몫 = 기대", got == want)
            check(f"{label} 합 = 몬스터 경험치", sum(g[0] for g in got.values()) == FIXTURE_EXP)
            check(f"{label} 쩔 보장 {'있음' if expect_guarantee else '없음'}",
                  carried.me in got and got[carried.me][1] == expect_guarantee == want[carried.me][1])

        one_kill(b, 25, "Lv25(보장 안)", True)
        # c로 바꿔 넣는다(b 탈퇴 · c 초대)
        b.party(OP_LEAVE)
        t0 = time.time()
        a.party(OP_INVITE, target=c.name)
        c.wait(lambda: c.nfys(EV_INVITE_FROM, t0))
        c.party(OP_REPLY, arg8=1, arg32=c.nfys(EV_INVITE_FROM, t0)[-1]["arg32"])
        c.wait(lambda: len(c.roster_names()) == 2)
        pump(bots, 0.5)
        one_kill(c, 12, "Lv12(보장 밖)", False)
        return ok, " · ".join(notes)
    finally:
        for x in (a, b, c):
            x.close()


def scenario_share(port: int):
    e = Bot(port, nm(40, 749, 252, "e"))
    f = Bot(port, nm(40, 750, 252, "f"))
    g = Bot(port, nm(40, 749, 767, "g"))   # 마을 광장 — 멀리서도 알림을 받는다(서버 전체)
    bots = [e, f]
    notes, ok = [], True
    try:
        def check(label, cond):
            nonlocal ok
            notes.append(f"{label}:{'ok' if cond else 'FAIL'}")
            ok &= bool(cond)
        pump([e, f, g], 1.0)
        exp_e, exp_f = e.exp, f.exp
        t_start = time.time()
        main = None
        for attempt in range(4):
            m = pick_monster(bots)
            if m is None or not approach(bots, m):
                notes.append(f"[진단] {'몬스터 없음' if m is None else '다가가지 못했다'}")
                continue
            # e가 HP 1200까지(한 방 ≈ 250 — 날아가는 한 방이 더 맞아도 f 몫이 남게), 막타는 f
            if kill_with([e, f, g], m, [(e, 1200), (f, 0)]):
                pump([e, f, g], 0.8)
                d = dealt_by(e, m, 0)
                if killer_of(e, m, 0) == f.me and d.get(e.me, 0) > d.get(f.me, 0) > 0:
                    main = m
                    break
        pump([e, f, g], 1.0)
        check("막타 f · 피해는 e가 더 많은 처치", main is not None)
        # 이 시나리오에서 쓰러진 몬스터마다(조준한 칸의 다른 몬스터를 잡은 것까지) 피해 비례 몫을 더해 맞대어 본다
        want = {e.me: [0, 0], f.me: [0, 0]}
        kills = {e.me: 0, f.me: 0}
        for oid in deaths(e, t_start):
            d = {who: dmg for who, dmg in dealt_by(e, oid, t_start).items() if who in want and dmg > 0}
            if not d:
                continue
            order = list(d)
            ex = split_weight(FIXTURE_EXP, [d[w] for w in order])
            ad = split_weight(FIXTURE_ADENA, [d[w] for w in order])
            for i, w in enumerate(order):
                want[w][0] += ex[i]
                want[w][1] += ad[i]
            k = killer_of(e, oid, t_start)
            if k in kills:
                kills[k] += 1
        got = {e.me: (e.exp - exp_e, adena_gained(e, t_start)), f.me: (f.exp - exp_f, adena_gained(f, t_start))}
        notes.append(f"[몫] 기대 e {want[e.me]} f {want[f.me]} · 받음 e {list(got[e.me])} f {list(got[f.me])} · 막타 e {kills[e.me]} f {kills[f.me]}")
        check("경험치 = 피해 비례(막타가 아니어도)", got[e.me][0] == want[e.me][0] and got[f.me][0] == want[f.me][0])
        check("그로시 = 피해 비례 · 바로 지갑", got[e.me][1] == want[e.me][1] and got[f.me][1] == want[f.me][1])
        if main is not None:
            d = dealt_by(e, main, 0)
            ex = split_weight(FIXTURE_EXP, list(d.values()))
            share = {w: ex[i] for i, w in enumerate(d)}
            nf = {b.name: [n for n in b.nfys(EV_EXP_SHARE, t_start) if n["target"] == main] for b in bots}
            check("ExpShare 알림 = 몫(파티가 아니어도)", all(nf[b.name] and nf[b.name][-1]["arg32"] == share[b.me] for b in bots))
        # 아이템 — 막타만, 가방에 바로(바닥에 떨어지지 않는다)
        acq = {b.me: sys_msgs(b, SYS_LOOT_ACQUIRED, t_start) for b in bots}
        check("막타가 아닌 처치는 아이템 없음", (len(acq[e.me]) == 0) == (kills[e.me] == 0) and len(acq[f.me]) >= kills[f.me] >= 1)
        dropped = [it for it in e.items.values() if it["at"] >= t_start]
        check("바닥에 떨어진 전리품 없음", not dropped)
        # 희귀 이상(장비 — 시험 사본은 늘 희귀 필드 장비)은 서버 전체 시스템 채팅, 일반(소비)은 알리지 않는다
        gear = {w: [i for i, _ in acq[w] if ITEM_TYPE.get(i) == "eEquipment"] for w in acq}
        chats = system_chats(g, t_start)
        mine = [c for c in chats if c[0] == f.me]
        check("멀리 있는 사람도 희귀 알림", len(mine) == len(gear[f.me]) >= 1 and len([c for c in chats if c[0] == e.me]) == len(gear[e.me]))
        check("알림 글 = 이름 · 희귀도 · 아이템", all(f.name in c[2] and "[" in c[2] and any(ITEM_NAME.get(i, "?") in c[2] for i in gear[f.me]) for c in mine))
        notes.append(f"[알림] {mine[0][2] if mine else '-'} · 소비 {len(acq[f.me]) - len(gear[f.me])}개는 알리지 않음")
        return ok, " · ".join(notes)
    finally:
        for x in (e, f, g):
            x.close()


def scenario_mix(port: int):
    a = Bot(port, nm(40, 749, 252, "x"))   # 파티장(쩔 해 주는 사람)
    c = Bot(port, nm(25, 748, 252, "y"))   # 파티원 — 레벨 차 15(보장 안)
    s = Bot(port, nm(40, 750, 252, "z"))   # 모르는 사람
    bots = [a, c, s]
    notes, ok = [], True
    try:
        def check(label, cond):
            nonlocal ok
            notes.append(f"{label}:{'ok' if cond else 'FAIL'}")
            ok &= bool(cond)
        check("파티", form_party(a, c))
        party = a.roster.get("party", 0)
        pump(bots, 0.8)
        done = None
        for attempt in range(4):
            m = pick_monster(bots)
            if m is None or not approach(bots, m):
                continue
            t0 = time.time()
            # 파티원 c가 먼저 한 방(맞을 때까지) → 모르는 사람 s가 HP 2400까지 → 파티장 a가 끝까지
            hit = lambda: any(objid(p, 3) == m and objid(p, 19) == c.me for p in c.packets(T_DAMAGE, t0))
            for _ in range(3):
                c.attack(m)
                if c.wait(hit, 2.5):
                    break
            if not hit() or not kill_with(bots, m, [(s, 2400), (a, 0)]):
                continue
            pump(bots, 0.8)
            d = dealt_by(a, m, 0)
            if all(d.get(b.me, 0) > 0 for b in bots):
                done = (m, d, t0)
                break
        check("셋 다 때린 처치", done is not None)
        if done:
            m, d, t0 = done
            level = {a.me: 40, c.me: 25, s.me: 40}
            pid = {a.me: party, c.me: party, s.me: 0}
            order = list(d)
            want = split_kill_exp(FIXTURE_EXP, [(d[w], level[w], pid[w]) for w in order], CARRY_RANGE, CARRY_PCT)
            want = {w: want[i] for i, w in enumerate(order)}
            nf = {b.me: [n for n in b.nfys(EV_EXP_SHARE, t0) if n["target"] == m] for b in bots}
            got = {w: (nf[w][-1]["arg32"], bool(nf[w][-1]["arg16"])) if nf[w] else None for w in nf}
            notes.append(f"[몫] 피해 s {d[s.me]} · c {d[c.me]} · a {d[a.me]} → 받음 s {got[s.me]} c {got[c.me]} a {got[a.me]} "
                         f"(기대 s {want[s.me]} c {want[c.me]} a {want[a.me]})")
            check("몫 = 기대(모두 비례 → 파티 몫 안에서 쩔)", all(got[w] == want[w] for w in want))
            base = split_weight(FIXTURE_EXP, [d[w] for w in order])
            check("모르는 사람 몫은 비례 그대로", got[s.me] and got[s.me][0] == base[order.index(s.me)])
            check("파티원 쩔 보장", got[c.me] and got[c.me][1])
            check("합 = 몬스터 경험치", sum(g[0] for g in got.values() if g) == FIXTURE_EXP)
        return ok, " · ".join(notes)
    finally:
        for x in bots:
            x.close()


def scenario_loot(port: int):
    a = Bot(port, f"lv40_xy749_253_FI{TAG[:2]}")   # 막타 — 가방 가득(전리품이 발밑으로 넘친다)
    b = Bot(port, nm(40, 750, 253, "b"))           # a의 파티원
    c = Bot(port, nm(40, 748, 253, "c"))           # 파티 밖
    bots = [a, b, c]
    notes, ok = [], True
    try:
        def check(label, cond):
            nonlocal ok
            notes.append(f"{label}:{'ok' if cond else 'FAIL'}")
            ok &= bool(cond)

        def try_pick(bot, oid) -> bool:
            """bot이 서 있는 칸에서 줍기 — 그 물건(oid)이 바닥에서 사라졌으면 참. 같은 칸에 앞 시나리오가 남긴(누구나 주울 수 있는)
            물건이 있으면 그것부터 주울 수 있으니, 성공했는데 oid가 남아 있으면 몇 번 더 줍는다."""
            for _ in range(4):
                t = time.time()
                bot.pickup()
                bot.wait(lambda: bot.packets(T_PICKUP_ACK, t), 2.0)
                acks = bot.packets(T_PICKUP_ACK, t)
                if not acks or acks[-1][3] != 1:
                    return False
                pump(bots, 0.2)
                if oid not in bot.items:
                    return True
            return False

        def drop_after_kill():
            """a 혼자 잡는다 → 그 몬스터가 쓰러진 순간 · 자리 둘레에 넘친 장비(다른 처치의 물건과 섞지 않는다)"""
            for attempt in range(3):   # 막힌 길이면 다른 몬스터로
                m = pick_monster(bots)
                if m is None:
                    notes.append("[진단] 몬스터 없음")
                    return None, None
                if approach([a], m, bots):
                    break
                notes.append("[진단] 다가가지 못했다")
            else:
                return None, None
            t0 = time.time()
            if not kill_with(bots, m, [(a, 0)]):
                notes.append(f"[진단] 처치 실패 {m} 피해 {dealt_by(a, m, t0)}")
                return None, None
            pump(bots, 0.4)
            die = [at for at, tt, p in a.log if tt == T_DIE and objid(p, 3) == m and at >= t0]
            death = die[0] if die else time.time()
            mx, my = a.monsters[m]["x"], a.monsters[m]["y"]
            near = [(oid, it) for oid, it in a.items.items() if it["at"] >= death - 0.05 and max(abs(it["x"] - mx), abs(it["y"] - my)) <= 2]
            gear = [x for x in near if ITEM_TYPE.get(x[1]["item"]) == "eEquipment"]
            return ((gear or near)[0] if near else None), death

        check("파티", form_party(a, b))
        pump(bots, 0.8)
        t_all = time.time()
        # 1) 주인만(6초) — 파티원 b도 · 남 c도 못 줍는다
        drop, death = drop_after_kill()
        check("가방이 차 발밑에 떨어짐", drop is not None and len(sys_msgs(a, SYS_LOOT_OVERFLOW, t_all)) >= 1 and not sys_msgs(a, SYS_LOOT_ACQUIRED, t_all))
        if drop:
            (oid, it) = drop
            check("둘 다 도착", walk_many([b, c], it["x"], it["y"], bots))
            arrived = time.time() - death
            picked_b = try_pick(b, oid)
            notes.append(f"[시각] 도착 {arrived:.1f}s · 파티원 줍기 {picked_b}")
            check("주인 시간: 파티원 못 줍는다", arrived < 5.0 and not picked_b)
            check("주인 시간: 남 못 줍는다", time.time() - death < 5.6 and not try_pick(c, oid))
            # 2) 주인 + 파티(6~12초) — c는 못 줍고 b는 줍는다
            while time.time() - death < 6.6:
                pump(bots, 0.1)
            check("파티 시간: 남 못 줍는다", not try_pick(c, oid))
            t_pick = time.time()
            check("파티 시간: 파티원 줍는다", time.time() - death < 11.0 and try_pick(b, oid))
            pump(bots, 0.5)
            if ITEM_TYPE.get(it["item"]) == "eEquipment":
                # 알림은 줍기 응답 뒤 서버 채팅으로 온다 — 부하가 크면 0.5초를 넘길 수 있어 3초까지 기다린다
                end = time.time() + 3.0
                while time.time() < end and not any(ITEM_NAME.get(it["item"], "?") in x[2] for x in system_chats(c, t_pick) if x[0] == b.me):
                    pump(bots, 0.2)
                heard = [x for x in system_chats(c, t_pick) if x[0] == b.me]
                # 같은 칸에 앞 시나리오가 남긴(누구나 줍는) 희귀 장비가 있으면 b가 그것부터 주워 알림이 하나 더 온다 — 이 물건의 알림이 한 번인지 본다
                mine = [x for x in heard if ITEM_NAME.get(it["item"], "?") in x[2]]
                check(f"넘친 희귀 장비를 주우면 알림(알림 {len(heard)} · 이 물건 {len(mine)})", len(mine) == 1 and b.name in mine[0][2])
        # 3) 누구나(12초 뒤)
        drop, death = drop_after_kill()
        if drop:
            (oid, it) = drop
            check("c 도착", walk_many([c], it["x"], it["y"], bots))
            check("12초 전: 남 못 줍는다", time.time() - death < 11.5 and not try_pick(c, oid))
            while time.time() - death < 12.4:
                pump(bots, 0.1)
            check("12초 뒤: 누구나 줍는다", try_pick(c, oid))
        else:
            check("두 번째 전리품", False)
        return ok, " · ".join(notes)
    finally:
        for x in bots:
            x.close()


def scenario_quest(port: int):
    qa = Bot(port, f"lv40_xy749_254_Q{QUEST_ID}{TAG[0]}a")   # 막타
    qb = Bot(port, f"lv40_xy750_254_Q{QUEST_ID}{TAG[0]}b")   # 피해만(막타 아님)
    qc = Bot(port, f"lv40_xy748_254_Q{QUEST_ID}{TAG[0]}c")   # 곁에 있지만 때리지 않는다
    bots = [qa, qb, qc]
    notes, ok = [], True
    try:
        def check(label, cond):
            nonlocal ok
            notes.append(f"{label}:{'ok' if cond else 'FAIL'}")
            ok &= bool(cond)

        def progress(bot, since):
            ups = [struct.unpack_from("<HBBHHH", p, 3) for p in bot.packets(T_QUEST_UPDATE, since)]
            ups = [u for u in ups if u[0] == QUEST_ID and u[1] == QUEST_PROGRESS and u[2] == QUEST_KILL_OBJ]
            return (len(ups), ups[-1][3 + QUEST_KILL_OBJ] if ups else 0)

        pump(bots, 1.0)
        t_start = time.time()
        main = None
        for attempt in range(3):
            m = pick_monster([qa, qb], others=bots)
            if m is None or not approach([qa, qb], m, bots):
                continue
            if kill_with(bots, m, [(qb, 3600), (qa, 0)]):
                pump(bots, 0.8)
                d = dealt_by(qa, m, 0)
                if killer_of(qa, m, 0) == qa.me and d.get(qb.me, 0) > 0:
                    main = m
                    break
        pump(bots, 0.8)
        check("qb가 때리고 qa가 막타", main is not None)
        want = {b.me: 0 for b in bots}
        for oid in deaths(qa, t_start):
            for who, dmg in dealt_by(qa, oid, t_start).items():
                if who in want and dmg > 0:
                    want[who] += 1
        got = {b.me: progress(b, t_start) for b in bots}
        notes.append(f"[진행] 기대 qa {want[qa.me]} qb {want[qb.me]} qc {want[qc.me]} · 받음(알림 수, 마지막 값) qa {got[qa.me]} qb {got[qb.me]} qc {got[qc.me]}")
        check("막타 오른다", got[qa.me][0] == want[qa.me] >= 1)
        check("피해를 준 사람도 오른다", got[qb.me][0] == want[qb.me] >= 1)
        check("때리지 않은 사람은 안 오른다", got[qc.me][0] == 0 == want[qc.me])
        return ok, " · ".join(notes)
    finally:
        for x in bots:
            x.close()


# ── 뼈대 ────────────────────────────────────────────────────────────────────
def patch_rows(path: Path, fn):
    lines = path.read_text(encoding="utf-8").split("\n")
    out = []
    for line in lines:
        if line.startswith("{") and not line.startswith('{"table"'):
            comma = "," if line.endswith(",") else ""
            row = json.loads(line.rstrip(","))
            fn(row)
            line = "{" + ", ".join(f"{json.dumps(k)}: {json.dumps(v, ensure_ascii=False)}" for k, v in row.items()) + "}" + comma
        out.append(line)
    path.write_text("\n".join(out), encoding="utf-8", newline="\n")


def make_fixture(src: Path, dst: Path):
    from dt import export
    shutil.copytree(src, dst)

    def party(r):
        r.update({"invite_expire_ms": 3000, "offline_keep_sec": 3, "sync_interval_ms": 200, "exp_share_range": 20,
                  "carry_level_range": CARRY_RANGE, "carry_min_exp_pct": CARRY_PCT, "loot_owner_ms": 6000, "loot_party_ms": 12000,
                  "recruit_idle_sec": 3})

    def monster(r):
        if r.get("dummy"):
            return
        r.update({"max_hp": 4000, "attack": 1, "attack_interval_ms": 5000, "move_interval_ms": 60000, "aggressive": False,
                  "exp": FIXTURE_EXP, "adena_chance": 100, "adena_min": FIXTURE_ADENA, "adena_max": FIXTURE_ADENA})

    def tier_loot(r):
        r.update({"common_bp": 0, "uncommon_bp": 0, "rare_bp": 10000, "essence_permille": 0, "crystal_permille": 0})

    def loot_const(r):
        r["elite_permille"] = 0

    def objective(r):
        if r["quest_id"] == QUEST_ID and r["seq"] == QUEST_KILL_OBJ + 1:
            r.update({"kind": "KillIn", "target": 0, "count": 50, "zone": 1})
    patch_rows(dst / "PartyConst.json", party)
    patch_rows(dst / "Monster.json", monster)
    patch_rows(dst / "TierLoot.json", tier_loot)
    patch_rows(dst / "LootConst.json", loot_const)
    patch_rows(dst / "QuestObjective.json", objective)
    for row in json.loads((dst / "Item.json").read_text(encoding="utf-8"))["rows"]:
        ITEM_TYPE[row["id"]] = row["type"]
        ITEM_NAME[row["id"]] = row.get("display_name") or row["name"]
    (dst / "manifest.json").write_text(export.manifest_text(dst), encoding="utf-8", newline="\n")


def wait_port(port: int, seconds: float) -> bool:
    end = time.time() + seconds
    while time.time() < end:
        try:
            socket.create_connection(("127.0.0.1", port), timeout=0.5).close()
            return True
        except OSError:
            time.sleep(0.2)
    return False


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--server", default=str(REPO / "build" / "linux-memory" / "GameServer"))
    ap.add_argument("--port", type=int, default=4200)
    ap.add_argument("--keep", action="store_true", help="시험용 GameData 사본 · 서버 로그를 남긴다")
    ap.add_argument("--only", default="", help="쉼표로 시나리오 이름(manage,offline,recruit,sync,exp,share,mix,loot,quest)")
    args = ap.parse_args()

    work = Path(tempfile.mkdtemp(prefix="harven_party_"))
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
        scenarios = [("manage", scenario_manage), ("offline", scenario_offline), ("recruit", scenario_recruit),
                     ("sync", scenario_sync), ("exp", scenario_exp), ("share", scenario_share), ("mix", scenario_mix),
                     ("loot", scenario_loot), ("quest", scenario_quest)]
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
