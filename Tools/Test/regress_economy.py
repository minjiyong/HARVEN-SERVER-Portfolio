"""경제 1차 로직 2부 회귀 시험 — 처치 보상(필드 장비 · 희귀 천장 · 정수 · 결정 · 레벨 차) · 정예(표시 · 배율 · 자격자 보상) ·
보스 개인 굴림(피해 몫 · 파티원 · 계정당 한 번) · 귀속(경매 등록 · 버리기 거부) · 거래 이력(경매로 산 장비 bit 9) · 경제 로그를 실제 패킷으로 본다.

    python3 Tools/Test/regress_economy.py [--server build/linux-memory/GameServer] [--port 4350] [--only field,pity] [--keep]

HARVEN_IN_MEMORY_DB=ON 빌드를 이 스크립트가 직접 띄운다(--dev — /economy). 봇 · 걷기 · 사냥 도우미는 regress_party.py의 것을 쓴다.
시험용 GameData 사본(Bin/GameData + 아래):
  · Monster(보스 · 허수아비 밖) — HP 600 · 레벨 25 · 공격력 1 · 비선공 · 걸음 60초 · 경험치 1000 · 그로시 1000(100%)
  · 모로즈(29) — HP 3000 · 공격력 1 · 비선공 · 그로시 1000(100%), MonsterAI 단계 · 기술 없음(넉백이 봇을 밀어내지 않게)
  · LootConst — 희귀 천장 4 · 직업 무기 바꾸기 100% · 레벨 차 10 · 정예 1000‰(3단계 이상 지역만) · HP 400% · 경험치 · 그로시 300% · 정예 자격 10% · 보스 자격 3%
  · TierLoot — 1단계: 일반 9999 · 희귀 1bp(천장만 희귀를 낸다) · 정수 0 / 2단계: 일반 4000 · 고급 3000 · 희귀 3000bp(처치마다 장비) ·
                정수 61 1000‰ · 결정 64 1000‰ / 3단계: 정예 장비 100%
  · BossLoot(모로즈) — 「없음」 행 가중치 0(자격자마다 이레린 세트 하나)
샌드박스 이름(Tools/Sandbox/MemDBConnection.cpp): "lvNN_xyX_Y_…" = 레벨 NN 각인술사가 (X, Y)에서, "…_CR…" = 제작 꾸러미(2칸 장비 파편 66×50 · 5칸 철검 · 그로시 50000).

시나리오
  field    올벤 숲(2단계): 막타(Lv25)만 필드 장비 — 처치마다 하나 · route Field · 등급 일반~희귀 · 희귀도 = 등급 바닥 · 무기는 내 직업(바꾸기 100%) ·
           정수 · 결정은 처치마다 정확히 한 사람(같은 사람이 둘 다) · 피해를 더 준 사람이 더 자주
  gap      베레그 해안(2단계 — field가 잡은 자리와 떨어진 곳): Lv40 혼자(몬스터 Lv25 — 레벨 차 15 ≥ 10) — 정수 · 결정 확률 절반(0도 전부도 아니다)
  pity     야스나 초원(1단계 — 일반 9999 · 희귀 1bp): 희귀 천장 4 — 장비 등급이 일반 · 일반 · 일반 · 희귀 · 일반 · 일반 · 일반 · 희귀
  elite    캄파나 폐허(3단계 — 모두 정예): SUBJECT_INFO_NFY flags 16 · 최대 HP 600×400% · 경험치 합 ×300% · 그로시 합 ×300% ·
           피해 10% 이상(두 사람)만 결정 · 정수 ×5 · 정예 장비(route Elite), 10% 아래는 아무것도
  boss     벨고라 설원 모로즈: 피해 3% 이상 둘 · 그 파티원(때리지 않음 · 공유 범위 안) — 저마다 보스 장비 정확히 하나(귀속 bit 8 · 영웅 바닥) ·
           3% 아래 모르는 사람은 없음 · 귀속 보스 장비는 경매 등록 거부(Bound)
  bound    장비 파편(66 — 재료 획득 귀속): 경매 등록 → Bound + ItemBound · 버리기 → 실패 + ItemBound / 철검을 올려 다른 사람이 사서 받으면 거래 이력 bit 9
  economy  /economy — 이 시험이 받은 필드 · 정예 · 보스 장비 수 · 처치 · 정예 재료 수와 같다
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
import regress_party as rp   # noqa: E402 — 봇 · 걷기 · 사냥 도우미

REPO = rp.REPO
T_INFO, T_DETAIL, T_SLOT = 43, 71, 45
T_AUCTION_REQ, T_AUCTION_ACK = 93, 94
T_DISCARD_REQ, T_DISCARD_ACK = 8, 18
SYS_LOOT_ACQUIRED, SYS_LOOT_OVERFLOW, SYS_ITEM_BOUND = 24, 25, 26
FLAG_ELITE = 16
AUC_REGISTER, AUC_BUY, AUC_CLAIM = 1, 2, 6
AR_OK, AR_BOUND = 0, 14
BOUND_BIT, TRADED_BIT = 1 << 8, 1 << 9

HP, LEVEL, EXP, ADENA = 600, 25, 1000, 1000
BOSS, BOSS_HP = 29, 3000
ELITE_HP_PCT, ELITE_EXP_PCT, ELITE_ADENA_PCT = 400, 300, 300
RARE_PITY = 4
ESSENCE_T2, CRYSTAL_T2, ESSENCE_T3, CRYSTAL_T3 = 61, 64, 62, 64
FLOOR = {"eNone": 0, "eRare": 1, "eEpic": 2, "eUnique": 3, "eLegendary": 4, "eMythic": 5}
ITEMS: dict[int, dict] = {}
TAG = "".join(random.choice(string.ascii_lowercase) for _ in range(2))
DONE = {"field": 0, "elite_gear": 0, "boss_gear": 0, "m61_kill": 0, "m64_kill": 0, "m62_elite": 0, "m64_elite": 0}


def nm(lv: int, x: int, y: int, role: str) -> str:
    name = f"lv{lv}_xy{x}_{y}_{TAG}{role}"
    assert len(name) <= 19, name
    return name


class Bot(rp.Bot):
    """regress_party.Bot + 경제 시험에 쓰는 받기(전리품 알림 · 인스턴스 · 경매 · 버리기)"""

    def __init__(self, port: int, name: str, cls: int = 1):
        self.aseq = 0
        super().__init__(port, name, cls)

    def acquired(self, since: float = 0.0) -> list[tuple[int, int]]:
        return rp.sys_msgs(self, SYS_LOOT_ACQUIRED, since)

    def bound_msgs(self, since: float = 0.0) -> list[tuple[int, int]]:
        return rp.sys_msgs(self, SYS_ITEM_BOUND, since)

    def details(self, since: float = 0.0) -> list[tuple[int, int, int]]:
        return [struct.unpack_from("<HHQ", p, 3) for p in self.packets(T_DETAIL, since)]

    def slots(self) -> dict[int, tuple[int, int]]:
        out = {}
        for p in [*self.packets(44), *self.packets(T_SLOT)]:
            if p[2] == 44:
                for i in range(min(p[3], 30)):
                    slot, item, count, _eq, _ = struct.unpack_from("<HHHBB", p, 4 + i * 8)
                    out[slot] = (item, count)
            else:
                slot, item, count, _eq, _ = struct.unpack_from("<HHHBB", p, 3)
                out[slot] = (item, count)
        return {s: v for s, v in out.items() if v[0]}

    def gear_got(self, since: float) -> list[tuple[int, int]]:
        """since 뒤 가방에 새로 들어온 장비(아이템, extra) — ITEM_DETAIL_INF(칸 알림 바로 뒤) 차례대로"""
        return [(item, extra) for _slot, item, extra in self.details(since) if ITEMS.get(item, {}).get("type") == "eEquipment"]

    def auction(self, op: int, slot: int = 0, count: int = 0, price: int = 0, duration: int = 0, id_: int = 0, item: int = 0) -> int:
        self.aseq += 1
        pkt = struct.pack("<HBBI8BH26sHHIBIH", 59, T_AUCTION_REQ, op, self.aseq, 0, 0, 0, 0, 0, 0, 0, 0, 0, b"",
                          slot, count, price, duration, id_, item)
        assert len(pkt) == 59
        self.c.sendall(pkt)
        return self.aseq

    def auction_ack(self, seq: int, seconds: float = 4.0) -> dict | None:
        def find():
            for p in self.packets(T_AUCTION_ACK):
                op, result, s, id_, adena, delta, item, count = struct.unpack_from("<BBIIIiHH", p, 3)
                if s == seq:
                    return {"op": op, "result": result, "id": id_, "item": item, "count": count}
            return None
        self.wait(lambda: find() is not None, seconds)
        return find()

    def discard(self, slot: int, count: int):
        self.c.sendall(struct.pack("<HBHH", 7, T_DISCARD_REQ, slot, count))

    def say(self, text: str):
        self.chat(0, text)


class Checks:
    def __init__(self):
        self.ok, self.notes = True, []

    def __call__(self, cond, label: str):
        self.notes.append(f"{label}:{'ok' if cond else 'FAIL'}")
        self.ok &= bool(cond)
        return bool(cond)

    def note(self, text: str):
        self.notes.append(text)

    def result(self):
        return self.ok, " · ".join(self.notes)


def kill_staged(bots, monster, stages, seconds: float = 40.0) -> bool:
    """stages: [(bot, 피해)] — 앞에서부터 그 bot이 준 피해(bots[0]이 본 SUBJECT_DAMAGE_NFY)가 그만큼이 될 때까지 때린다. 피해 0 = 죽을 때까지."""
    t0 = time.time() - 0.05
    end = time.time() + seconds
    while time.time() < end:
        rp.pump(bots, 0.3)
        if not bots[0].monsters.get(monster, {}).get("alive", False):
            return True
        dealt = rp.dealt_by(bots[0], monster, t0)
        for bot, want in stages:
            if want == 0 or dealt.get(bot.me, 0) < want:
                if monster in bot.monsters:
                    bot.attack(monster)
                break
    return not bots[0].monsters.get(monster, {}).get("alive", False)


def our_deaths(observer, bots, since: float) -> list[tuple]:
    """since 뒤 쓰러진 몬스터 중 bots가 때린 것(조준한 몬스터 · 각인탄 길목에서 대신 맞은 몬스터 모두) — (몬스터, 피해 {봇: 피해}, 막타)"""
    me = {b.me for b in bots}
    out = []
    for m in rp.deaths(observer, since):
        d = {who: v for who, v in rp.dealt_by(observer, m, since).items() if who in me and v > 0}
        if d:
            out.append((m, d, rp.killer_of(observer, m, since)))
    return out


def hunt(bots, stages, kills: int, ck: Checks, seconds: float = 480.0, others=()) -> list:
    """bots가 몬스터를 kills마리 잡는다(stages — kill_staged). 잡은 몬스터 목록."""
    done, end = [], time.time() + seconds

    def skip(m):
        # 다가갈 수 없거나(막힌 둘레) 끝내 못 잡은 몬스터 — 다시 고르지 않게 이 봇들의 목록에서 쓰러진 것으로 둔다(다시 태어나면 새로 보인다)
        for x in [*bots, *others]:
            if m in x.monsters:
                x.monsters[m]["alive"] = False
    while len(done) < kills and time.time() < end:
        m = rp.pick_monster(bots, others=others)
        if m is None:
            rp.pump(bots, 1.0)
            continue
        if not rp.approach(bots, m, others):
            skip(m)
            continue
        if kill_staged([*bots, *others], m, stages):
            done.append(m)
            rp.pump([*bots, *others], 0.4)
        else:
            skip(m)
    ck(len(done) == kills, f"처치 {len(done)}/{kills}")
    return done


# ── 시나리오 ────────────────────────────────────────────────────────────────
def scenario_field(port: int):
    """올벤 숲(2단계): 필드 장비(막타) · 정수 · 결정(피해 비례 한 사람)"""
    ck = Checks()
    a = Bot(port, nm(25, 749, 251, "a"))   # 막타 · 피해 대부분
    b = Bot(port, nm(5, 750, 251, "b"))    # 한두 방(Lv5)
    try:
        rp.pump([a, b], 1.0)
        t0 = time.time()
        hunt([a, b], [(b, 1), (a, 0)], 14, ck)
        rp.pump([a, b], 1.0)
        kills = our_deaths(a, [a, b], t0)   # 길목에서 대신 맞아 쓰러진 몬스터도 보상이 있다
        dmg = {a.me: 0, b.me: 0}
        for _m, d, _k in kills:
            for who, v in d.items():
                dmg[who] += v
        by = {a.me: sum(1 for *_x, k in kills if k == a.me), b.me: sum(1 for *_x, k in kills if k == b.me)}
        acq = {x.name: x.acquired(t0) for x in (a, b)}
        gear = a.gear_got(t0)
        ck(len(gear) == by[a.me] and len(b.gear_got(t0)) == by[b.me],
           f"막타만 필드 장비 처치마다 하나(a {len(gear)}/{by[a.me]} · b {len(b.gear_got(t0))}/{by[b.me]} — 쓰러진 {len(kills)})")
        grades = sorted({ITEMS[i]["grade"] for i, _ in gear})
        ck(all("Field" in ITEMS[i]["route"].split("|") for i, _ in gear), "route에 Field")
        ck(set(grades) <= {"eNone", "eRare", "eEpic"} and len(grades) >= 2, f"등급 일반~희귀 {grades}")
        ck(all((x & 7) == FLOOR[ITEMS[i]["grade"]] for i, x in gear), "희귀도 = 등급 바닥")
        weapons = [i for i, _ in gear if ITEMS[i]["equip_slot"] in ("Weapon", "None")]
        ck(all(ITEMS[i]["class_mask"] in ("", "Engraver") for i in weapons), f"무기는 내 직업(각인술사) — {weapons}")
        # 정수 · 결정: 처치마다 정확히 한 사람, 같은 사람이 둘 다(정수 다음 결정)
        seq = {x.name: [i for i, _ in acq[x.name] if i in (ESSENCE_T2, CRYSTAL_T2)] for x in (a, b)}
        n61 = sum(s.count(ESSENCE_T2) for s in seq.values())
        n64 = sum(s.count(CRYSTAL_T2) for s in seq.values())
        ck(n61 == len(kills) and n64 == len(kills), f"처치마다 정확히 한 사람 — 정수 {n61} · 결정 {n64} = 쓰러진 {len(kills)}")
        ck(all(s == [ESSENCE_T2, CRYSTAL_T2] * (len(s) // 2) for s in seq.values()), "정수와 결정은 같은 사람")
        share = dmg[a.me] / max(1, dmg[a.me] + dmg[b.me])
        ck(seq[a.name].count(ESSENCE_T2) > seq[b.name].count(ESSENCE_T2), f"피해 비례(a {share:.0%}) — a {seq[a.name].count(ESSENCE_T2)} · b {seq[b.name].count(ESSENCE_T2)}")
        DONE["field"] += len(gear) + len(b.gear_got(t0))
        DONE["m61_kill"] += n61
        DONE["m64_kill"] += n64
        return ck.result()
    finally:
        a.close()
        b.close()


def scenario_gap(port: int):
    """레벨 차: Lv40이 Lv25 몬스터 — 정수 · 결정 1000‰ → 500‰(베레그 해안 — 2단계, 올벤 숲은 field가 한참 잡은 뒤라 몬스터가 드물다)"""
    ck = Checks()
    c = Bot(port, nm(40, 750, 1750, "g"))
    try:
        rp.pump([c], 1.0)
        t0 = time.time()
        hunt([c], [(c, 0)], 14, ck)
        rp.pump([c], 1.0)
        kills = our_deaths(c, [c], t0)
        got = [i for i, _ in c.acquired(t0)]
        e, k = got.count(ESSENCE_T2), got.count(CRYSTAL_T2)
        ck(0 < e < len(kills) and 0 < k < len(kills), f"절반 확률 — 정수 {e} · 결정 {k} / 처치 {len(kills)}")
        gear = c.gear_got(t0)
        ck(len(gear) == len(kills), f"필드 장비 {len(gear)} = 쓰러진 {len(kills)}")
        DONE["field"] += len(gear)
        DONE["m61_kill"] += e
        DONE["m64_kill"] += k
        return ck.result()
    finally:
        c.close()


def scenario_pity(port: int):
    """야스나 초원(1단계 — 일반 9999 · 희귀 1bp): 희귀 천장 4"""
    ck = Checks()
    d = Bot(port, nm(40, 751, 1249, "p"))
    try:
        rp.pump([d], 1.0)
        t0 = time.time()
        hunt([d], [(d, 0)], 8, ck)
        rp.pump([d], 1.0)
        kills = our_deaths(d, [d], t0)
        gear = d.gear_got(t0)
        grades = [ITEMS[i]["grade"] for i, _ in gear]
        want = (["eNone"] * (RARE_PITY - 1) + ["eEpic"]) * (len(gear) // RARE_PITY + 1)
        ck(len(gear) == len(kills) >= 8 and grades == want[:len(gear)], f"천장 {RARE_PITY}: {grades}")
        ck(all((x & 7) == FLOOR[ITEMS[i]["grade"]] for i, x in gear), "희귀도 = 등급 바닥")
        DONE["field"] += len(gear)
        return ck.result()
    finally:
        d.close()


def scenario_elite(port: int):
    """캄파나 폐허(3단계 — 모두 정예): 표시 · 배율 · 피해 10% 이상만 보상"""
    ck = Checks()
    a = Bot(port, nm(40, 1249, 751, "a"))   # 막타 · 피해 대부분
    b = Bot(port, nm(40, 1250, 751, "b"))   # 두세 방 — 10% 넘게
    s = Bot(port, nm(5, 1248, 751, "s"))    # 한 방(Lv5) — 10% 아래
    bots = [a, b, s]
    try:
        rp.pump(bots, 1.5)
        infos = {}
        for p in a.packets(T_INFO):
            oid = rp.objid(p, 3)
            if oid[0] == rp.CAT_MONSTER:
                infos[oid] = (p[22], *struct.unpack_from("<HH", p, 24))
        elite = [o for o, (f, hp, mhp) in infos.items() if f & FLAG_ELITE]
        ck(len(infos) > 0 and len(elite) == len(infos), f"보이는 몬스터 모두 정예 표시(INFO flags 16) {len(elite)}/{len(infos)}")
        ck(all(mhp == HP * ELITE_HP_PCT // 100 and hp == mhp for f, hp, mhp in infos.values()), f"최대 HP {HP}×{ELITE_HP_PCT}% = {HP * ELITE_HP_PCT // 100}")
        seen = [o for o in a.monsters if o[0] == rp.CAT_MONSTER]
        ck(all(o in infos for o in seen), f"ADD 뒤 늘 INFO({len(infos)}/{len(seen)})")
        t0 = time.time()
        big = HP * ELITE_HP_PCT // 100
        target = hunt([a, b, s], [(b, big * 15 // 100), (s, 1), (a, 0)], 1, ck, seconds=150.0)
        rp.pump(bots, 1.5)
        deaths = our_deaths(a, bots, t0)   # 동료 정예가 각인탄 길목에서 대신 맞아 쓰러질 수도 있다 — 쓰러진 정예마다 셈한다
        if not target or not deaths:
            return ck.result()
        pct_of = lambda d, x: d.get(x.me, 0) * 100 / max(1, sum(d.values()))
        main = next((t for t in deaths if t[0] == target[0]), None)
        if not ck(main is not None, "조준한 정예가 쓰러졌다"):
            return ck.result()
        m, d, killer = main
        ck(killer == a.me, "막타 a")
        ck(pct_of(d, a) >= 10 and pct_of(d, b) >= 10 and 0 < pct_of(d, s) < 10,
           f"피해 몫 a {pct_of(d, a):.0f}% · b {pct_of(d, b):.0f}% · s {pct_of(d, s):.1f}%(쓰러진 정예 {len(deaths)})")
        # 경험치 · 그로시 ×300%(나눠 받는 사람 모두의 합 — 셋 다 곁에 있다)
        exp = sum(n["arg32"] for x in bots for n in x.nfys(rp.EV_EXP_SHARE, t0) if n["target"] == m)
        adena = sum(rp.adena_gained(x, t0) for x in bots)
        ck(exp == EXP * ELITE_EXP_PCT // 100, f"경험치 합 {exp}(기대 {EXP * ELITE_EXP_PCT // 100})")
        ck(adena == ADENA * ELITE_ADENA_PCT // 100 * len(deaths), f"그로시 합 {adena}(기대 {ADENA * ELITE_ADENA_PCT // 100} × {len(deaths)})")
        # 자격(피해 ≥ 10%)마다 결정 ×1 · 정수 ×5 · 정예 장비 하나, 아니면 아무것도
        for x in bots:
            n = sum(1 for _m, dd, _k in deaths if pct_of(dd, x) >= 10)
            got = x.acquired(t0)
            crystals = sum(c for i, c in got if i == CRYSTAL_T3)
            essences = sum(c for i, c in got if i == ESSENCE_T3)
            g = x.gear_got(t0)
            elite_gear = [i for i, _ in g if "Elite" in ITEMS[i]["route"].split("|")]
            ck(crystals == n and essences == 5 * n, f"{x.name[-1]}: 자격 {n}번 — 결정 {crystals} · 정수 {essences}")
            ck(len(elite_gear) == n and all(ITEMS[i]["grade"] in ("eEpic", "eUnique") for i in elite_gear), f"{x.name[-1]}: 정예 장비 {elite_gear}")
            ck(len(g) == len(elite_gear), f"{x.name[-1]}: 필드 장비 굴림 없음")
            DONE["elite_gear"] += len(elite_gear)
            DONE["m62_elite"] += essences
            DONE["m64_elite"] += crystals
        ck(all(c == 5 for x in bots for i, c in x.acquired(t0) if i == ESSENCE_T3), "일반 정수 굴림(×1) 없음 — 정예 정수 ×5만")
        return ck.result()
    finally:
        for x in bots:
            x.close()


def scenario_boss(port: int):
    """모로즈: 피해 3% 이상 · 그 파티원(공유 범위) — 계정당 한 번, 3% 아래 모르는 사람은 없음"""
    ck = Checks()
    a = Bot(port, nm(40, 263, 268, "a"))   # 파티장 · 막타
    b = Bot(port, nm(40, 263, 268, "b"))   # 모르는 사람 · 3% 넘게
    s = Bot(port, nm(5, 263, 268, "s"))    # 모르는 사람 · 한 방(3% 아래)
    p = Bot(port, nm(40, 263, 268, "p"))   # a의 파티원 — 때리지 않는다(보스 곁 — 공유 범위 20 안)
    bots = [a, b, s]
    try:
        ck(rp.form_party(a, p), "파티 a · p")
        rp.pump([*bots, p], 1.0)
        boss = next((o for o in a.monsters if o[1] == BOSS), None)
        if not ck(boss is not None, "보스가 보인다"):
            return ck.result()
        bm = a.monsters[boss]
        far = [x for x in bots if max(abs(x.pos[0] - bm["x"]), abs(x.pos[1] - bm["y"])) > 5]
        if far and not ck(rp.approach(far, boss, [*[x for x in bots if x not in far], p]), f"보스 곁으로 {len(far)}"):
            return ck.result()
        t0 = time.time()
        killed = kill_staged([*bots, p], boss, [(b, BOSS_HP * 6 // 100), (s, 1), (a, 0)], seconds=90.0)
        rp.pump([*bots, p], 1.5)
        if not ck(killed, "보스 처치"):
            return ck.result()
        d = rp.dealt_by(a, boss, t0)
        total = sum(d.values())
        pct = {x.name: d.get(x.me, 0) * 100 / max(1, total) for x in [*bots, p]}
        ck(pct[a.name] >= 3 and pct[b.name] >= 3 and pct[s.name] < 3 and pct[p.name] == 0,
           f"피해 몫 a {pct[a.name]:.0f}% · b {pct[b.name]:.0f}% · s {pct[s.name]:.1f}% · p {pct[p.name]:.0f}%")
        sets = {51, 52, 53, 54}
        got = {x.name: [(i, e) for i, e in x.gear_got(t0) if i in sets] for x in [*bots, p]}
        for x in (a, b, p):
            g = got[x.name]
            ck(len(g) == 1, f"{x.name[-1]}: 보스 장비 하나 {[i for i, _ in g]}")
            ck(all(e & BOUND_BIT and (e & 7) == FLOOR["eUnique"] for _, e in g), f"{x.name[-1]}: 귀속 bit 8 · 영웅")
        ck(not got[s.name], "3% 아래 모르는 사람은 없음")
        DONE["boss_gear"] += sum(len(v) for v in got.values())
        # 벨고라 설원은 3단계 — 보스 곁 몬스터는 모두 정예다. 각인탄 길목에서 대신 맞아 쓰러진 정예의 보상도 경제 로그에 들어가니 받은 만큼 센다
        for x in (*bots, p):
            acq = x.acquired(t0)
            DONE["m62_elite"] += sum(c for i, c in acq if i == ESSENCE_T3)
            DONE["m64_elite"] += sum(c for i, c in acq if i == CRYSTAL_T3)
            DONE["elite_gear"] += len([i for i, _ in x.gear_got(t0) if i not in sets])
        return ck.result()
    finally:
        for x in (*bots, p):
            x.close()


def scenario_boundloot(port: int):
    """야스나 초원(1단계 — 시험 사본에서 일반 필드 장비를 획득 귀속으로): 가방이 찬 주인의 귀속 장비가 발밑에 넘치면
    파티원 · 남은 줍기 단계가 지나도 못 줍고, 30초가 지나도 사라지지 않는다(귀속 넘침은 2분)"""
    ck = Checks()
    a = Bot(port, f"lv9_xy751_1249_FI{TAG}")   # 막타 — 가방 가득
    b = Bot(port, nm(9, 752, 1249, "b"))       # a의 파티원
    c = Bot(port, nm(9, 750, 1249, "c"))       # 파티 밖
    bots = [a, b, c]

    def try_pick(bot, oid) -> bool:
        for _ in range(3):
            t = time.time()
            bot.pickup()
            bot.wait(lambda: bot.packets(rp.T_PICKUP_ACK, t), 2.0)
            acks = bot.packets(rp.T_PICKUP_ACK, t)
            if not acks or acks[-1][3] != 1:
                return False
            rp.pump(bots, 0.2)
            if oid not in bot.items:
                return True
        return False
    try:
        ck(rp.form_party(a, b), "파티 a · b")
        rp.pump(bots, 0.8)
        t0 = time.time()
        drop = None
        for _ in range(4):
            m = rp.pick_monster(bots)
            if m is None or not rp.approach([a], m, bots):
                continue
            t1 = time.time()
            if not kill_staged(bots, m, [(a, 0)]):
                continue
            rp.pump(bots, 0.5)
            mx, my = a.monsters[m]["x"], a.monsters[m]["y"]
            near = [(oid, it) for oid, it in a.items.items()
                    if it["at"] >= t1 and max(abs(it["x"] - mx), abs(it["y"] - my)) <= 2 and ITEMS.get(it["item"], {}).get("bind") == "eAcquire"]
            if near:
                drop = near[0]
                break
        if not ck(drop is not None, "귀속 장비가 발밑으로 넘침"):
            return ck.result()
        ck(rp.sys_msgs(a, SYS_LOOT_OVERFLOW, t0), "LootOverflow 알림")
        oid, it = drop
        born = it["at"]
        ck(rp.walk_many([b, c], it["x"], it["y"], bots), "파티원 · 남 도착")
        while time.time() - born < 5.0:   # 시험 사본: 주인 2초 · 파티 4초 — 둘 다 지났다
            rp.pump(bots, 0.2)
        ck(not try_pick(b, oid), "파티 단계 뒤에도 파티원 못 줍는다")
        ck(not try_pick(c, oid), "누구나 단계 뒤에도 남 못 줍는다")
        while time.time() - born < 34.0:
            rp.pump(bots, 0.5)
        ck(oid in b.items, "30초가 지나도 바닥에 남아 있다(귀속 넘침 2분)")
        # 경제 로그 대조(economy 시나리오): 이 시나리오의 필드 장비는 모두 넘쳤다
        DONE["field"] += len([i for i, _n in rp.sys_msgs(a, SYS_LOOT_OVERFLOW, t0) if ITEMS.get(i, {}).get("type") == "eEquipment"])
        return ck.result()
    finally:
        for x in bots:
            x.close()


def scenario_bound(port: int):
    """장비 파편(획득 귀속 재료) — 경매 등록 · 버리기 거부, 철검을 사고 받으면 거래 이력 bit 9"""
    ck = Checks()
    seller = Bot(port, f"lv20_xy729_757_CR{TAG}")   # 경매인(NPC 21) 집 (729, 756) 곁 — 걸을 수 있는 칸
    buyer = Bot(port, f"lv20_xy730_756_CR{TAG}")
    bots = [seller, buyer]
    try:
        rp.pump(bots, 1.0)
        inv = seller.slots()
        ck(inv.get(2, (0, 0))[0] == 66 and inv.get(5, (0, 0))[0] == 3, f"꾸러미(2칸 파편 · 5칸 철검) {inv.get(2)} {inv.get(5)}")
        t0 = time.time()
        ack = seller.auction_ack(seller.auction(AUC_REGISTER, slot=2, count=1, price=100, item=66))
        ck(ack is not None and ack["result"] == AR_BOUND, f"파편 경매 등록 → Bound {ack and ack['result']}")
        ck((66, 2) in seller.bound_msgs(t0), "ItemBound 알림(아이템 · 칸)")
        t1 = time.time()
        seller.discard(2, 1)
        seller.wait(lambda: seller.packets(T_DISCARD_ACK, t1), 3.0)
        dis = seller.packets(T_DISCARD_ACK, t1)
        ck(dis and dis[-1][3] == 0, "파편 버리기 실패")
        ck((66, 2) in seller.bound_msgs(t1), "버리기 ItemBound 알림")
        rp.pump(bots, 0.3)
        ck(seller.slots().get(2) == (66, 50), "파편 그대로 50")
        # 철검(거래 이력 없음)을 올리고 다른 사람이 산다 → 받으면 bit 9
        reg = seller.auction_ack(seller.auction(AUC_REGISTER, slot=5, count=1, price=500, item=3))
        if not ck(reg is not None and reg["result"] == AR_OK, f"철검 등록 {reg}"):
            return ck.result()
        buy = buyer.auction_ack(buyer.auction(AUC_BUY, price=500, id_=reg["id"]))
        ck(buy is not None and buy["result"] == AR_OK, f"구매 {buy}")
        t2 = time.time()
        claim = buyer.auction_ack(buyer.auction(AUC_CLAIM, id_=0))
        ck(claim is not None and claim["result"] == AR_OK and claim["item"] == 3, f"받기 {claim}")
        buyer.wait(lambda: [x for x in buyer.details(t2) if x[1] == 3], 3.0)
        det = [x for x in buyer.details(t2) if x[1] == 3]
        ck(det and det[-1][2] & TRADED_BIT, f"산 철검 extra bit 9(거래 이력) — {det and hex(det[-1][2])}")
        ck(det and not det[-1][2] & BOUND_BIT, "귀속은 아니다")
        return ck.result()
    finally:
        for x in bots:
            x.close()


def scenario_economy(port: int, work: Path):
    """/economy(지금 값) + economy.log(지난 하루들) = 이 시험이 받은 것"""
    ck = Checks()
    a = Bot(port, nm(1, 749, 760, "e"))
    try:
        rp.pump([a], 0.5)
        t0 = time.time()
        a.say("/economy")
        a.wait(lambda: [p for p in a.packets(rp.T_SC_CHAT, t0) if rp.cstr(p[20:40]) == "경제"], 3.0)
        rp.pump([a], 0.8)
        parts = [rp.cstr(p[40:168]) for p in a.packets(rp.T_SC_CHAT, t0) if rp.cstr(p[20:40]) == "경제"]
        snap = None
        try:
            snap = json.loads("".join(parts))
        except ValueError:
            pass
        if not ck(snap is not None, f"/economy JSON({len(parts)}줄)"):
            return ck.result()
        log = work / "economy.log"
        lines = [json.loads(x) for x in log.read_text(encoding="utf-8").splitlines() if x.strip()] if log.exists() else []

        def total(*path):
            s = 0
            for ln in [*lines, snap]:
                v = ln
                for k in path:
                    v = v.get(k, {}) if isinstance(v, dict) else {}
                if isinstance(v, dict):
                    s += sum(x for x in v.values() if isinstance(x, int))
                elif isinstance(v, int):
                    s += v
            return s
        got = {"field": total("gear", "in", "Field"), "elite_gear": total("gear", "in", "Elite"), "boss_gear": total("gear", "in", "Boss"),
               "m61_kill": total("material", "in", "Kill", "61"), "m64_kill": total("material", "in", "Kill", "64"),
               "m62_elite": total("material", "in", "Elite", "62"), "m64_elite": total("material", "in", "Elite", "64")}
        for k, want in DONE.items():
            ck(got[k] == want, f"{k} {got[k]}(받은 것 {want})")
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
            r.update({"max_hp": BOSS_HP, "attack": 1, "attack_interval_ms": 5000, "aggressive": False,
                      "adena_chance": 100, "adena_min": ADENA, "adena_max": ADENA})
        elif r.get("boss"):
            r.update({"attack": 1, "aggressive": False})
        else:
            r.update({"max_hp": HP, "level": LEVEL, "attack": 1, "attack_interval_ms": 5000, "move_interval_ms": 60000, "aggressive": False,
                      "exp": EXP, "adena_chance": 100, "adena_min": ADENA, "adena_max": ADENA})

    def loot_const(r):
        r.update({"rare_pity": RARE_PITY, "heroic_pity": 20, "class_swap_percent": 100, "level_gap": 10, "elite_permille": 1000,
                  "elite_min_tier": 3, "elite_hp_percent": ELITE_HP_PCT, "elite_exp_percent": ELITE_EXP_PCT,
                  "elite_adena_percent": ELITE_ADENA_PCT, "elite_share_percent": 10, "boss_share_percent": 3})

    def tier_loot(r):
        if r["tier"] == 1:
            r.update({"common_bp": 9999, "uncommon_bp": 0, "rare_bp": 1, "essence_permille": 0, "crystal_item": 0, "crystal_permille": 0})
        elif r["tier"] == 2:
            r.update({"common_bp": 4000, "uncommon_bp": 3000, "rare_bp": 3000, "essence_item": ESSENCE_T2, "essence_permille": 1000,
                      "crystal_item": CRYSTAL_T2, "crystal_permille": 1000})
        elif r["tier"] == 3:
            r.update({"elite_gear_percent": 100})

    def item(r):
        # boundloot: 일반 필드 장비를 획득 귀속으로(넘친 귀속 장비 줍기 규칙 시험) — 다른 시나리오는 귀속 여부를 보지 않는다
        if r["type"] == "eEquipment" and r["grade"] == "eNone" and "Field" in r["route"]:
            r["bind"] = "eAcquire"

    def party_const(r):
        r.update({"loot_owner_ms": 2000, "loot_party_ms": 4000})

    def boss_loot(r):
        if r["species"] == BOSS and r["item_id"] == 0:
            r["weight"] = 0

    def boss_ai(r):
        if r["monster_id"] == BOSS:
            r.update({"phase2_hp": 0, "phase3_hp": 0, "phase2_skill": 0, "phase3_skill": 0})
    patch(dst / "Monster.json", monster)
    patch(dst / "LootConst.json", loot_const)
    patch(dst / "TierLoot.json", tier_loot)
    patch(dst / "BossLoot.json", boss_loot)
    patch(dst / "Item.json", item)
    patch(dst / "PartyConst.json", party_const)
    patch(dst / "MonsterAI.json", boss_ai)
    # 모로즈의 기술 칸을 모두 지운다(MonsterAISkill — 넉백 · 장판이 봇을 밀어내지 않게)
    p = dst / "MonsterAISkill.json"
    lines = p.read_text(encoding="utf-8").split("\n")
    keep = [ln for ln in lines if not ln.startswith('{"monster_id": %d,' % BOSS)]
    body = [ln.rstrip(",") for ln in keep if ln.startswith("{") and not ln.startswith('{"table"')]
    p.write_text(keep[0] + "\n" + ",\n".join(body) + "\n]}\n", encoding="utf-8", newline="\n")
    for row in json.loads((dst / "Item.json").read_text(encoding="utf-8"))["rows"]:
        ITEMS[row["id"]] = row
        rp.ITEM_TYPE[row["id"]] = row["type"]
        rp.ITEM_NAME[row["id"]] = row.get("display_name") or row["name"]
    (dst / "manifest.json").write_text(export.manifest_text(dst), encoding="utf-8", newline="\n")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--server", default=str(REPO / "build" / "linux-memory" / "GameServer"))
    ap.add_argument("--port", type=int, default=4350)
    ap.add_argument("--keep", action="store_true", help="시험용 GameData 사본 · 서버 로그를 남긴다")
    ap.add_argument("--only", default="", help="쉼표로(field,gap,pity,elite,boss,boundloot,bound,economy)")
    args = ap.parse_args()

    work = Path(tempfile.mkdtemp(prefix="harven_econ_"))
    make_fixture(REPO / "Bin" / "GameData", work / "GameData")
    log = open(work / "server.log", "w")
    env = dict(os.environ, HARVEN_ECONOMY_LOG=str(work / "economy.log"))
    srv = subprocess.Popen([str(Path(args.server).resolve()), "--data-dir", str(work / "GameData"), "--port", str(args.port), "--dev"],
                           stdout=log, stderr=subprocess.STDOUT, cwd=work, env=env)
    results = []
    try:
        if not rp.wait_port(args.port, 90):
            print("FAIL: 서버가 뜨지 않았다")
            return 1
        time.sleep(2.0)
        scenarios = [("field", scenario_field), ("gap", scenario_gap), ("pity", scenario_pity), ("elite", scenario_elite),
                     ("boss", scenario_boss), ("boundloot", scenario_boundloot), ("bound", scenario_bound), ("economy", lambda p: scenario_economy(p, work))]
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
