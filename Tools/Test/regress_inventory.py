"""소지품 회귀 시험 — 착용 칸 목록(CInventory::m_Equipped)이 칸 상태와 늘 같은가.
HARVEN_IN_MEMORY_DB=ON 서버(되도록 Debug 빌드 — 착용 칸을 읽을 때마다 칸 전체 훑기와 대조하는 assert가 켜진다)를 띄운 뒤 실행한다.

    python3 Tools/Test/regress_inventory.py [host] [port]

A가 장착 · 해제 · 자리 바꿈 · 정리 · 버림 · 줍기 · 물약을 섞어 하는 동안, 옆에 선 B가 받는 A의 외형(PLAYER_LOOK_NFY)이
A의 소지품(ITEM_LIST_ACK 두 쪽 + ITEM_SLOT_INF로 따라 그린 것)에서 착용 중인 물건들과 늘 같은지 본다.
가방은 50칸(MAX_INVENTORY_SLOTS) — 소지품 목록은 30칸씩 두 쪽(0~29 · 30~49, 빈 칸은 itemId 0)으로 온다.
마지막에 같은 요청을 몰아 넣고(자리 바꿈 · 정리 · 해제 · 장착) 서버가 살아 있는지 · 외형이 맞는지 다시 본다.
"""
import select
import socket
import struct
import sys
import time

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 4000
STAMP = int(time.time()) % 100000

T_LOGIN = 35
T_EQUIP, T_UNEQUIP, T_SWAP, T_DISCARD, T_PICKUP, T_USE, T_SORT = 5, 6, 7, 8, 9, 10, 73
T_LOGIN_ACK, T_ITEM_LIST, T_SLOT_INF, T_LOOK = 11, 44, 45, 72
ID_SIZE = 16                     # ObjID
LOOK_SLOTS = 8                   # kLookSlots
BAG_SLOTS = 50                   # MAX_INVENTORY_SLOTS
PAGE_SLOTS = 30                  # kItemListPageSlots
PROTOCOL_VERSION = 3


def connect():
    c = socket.create_connection((HOST, PORT), timeout=5)
    c.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    return c


class Client:
    def __init__(self, name):
        self.c, self.buf = connect(), b""
        self.c.sendall(struct.pack("<HB20s20sHB", 46, T_LOGIN, name.encode(), b"pw", PROTOCOL_VERSION, 0))
        self.id = None
        self.pos = None
        self.slots = {}           # slot → (itemId, count, equipped)
        self.listed = set()       # 소지품 목록(두 쪽)으로 받은 칸 번호
        self.looks = {}           # 남의 id(bytes) → 외형 물건 id 목록

    def poll(self, seconds):
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

    def on_packet(self, t, p):
        if t == T_LOGIN_ACK:
            self.id = bytes(p[3:3 + ID_SIZE])
            self.pos = struct.unpack_from("<hh", p, 3 + ID_SIZE)
        elif t == T_ITEM_LIST:
            # 한 쪽 = 그 범위의 칸들(빈 칸은 itemId 0) — 항목마다 그 칸을 덮어쓴다(쪽 범위 밖은 그대로)
            for i in range(min(p[3], PAGE_SLOTS)):
                slot, item, count, equipped, _ = struct.unpack_from("<HHHBB", p, 4 + i * 8)
                self.listed.add(slot)
                if item:
                    self.slots[slot] = (item, count, equipped)
                else:
                    self.slots.pop(slot, None)
        elif t == T_SLOT_INF:
            slot, item, count, equipped, _ = struct.unpack_from("<HHHBB", p, 3)
            if item:
                self.slots[slot] = (item, count, equipped)
            else:
                self.slots.pop(slot, None)
        elif t == T_LOOK:
            who = bytes(p[3:3 + ID_SIZE])
            items = struct.unpack_from(f"<{LOOK_SLOTS}H", p, 3 + ID_SIZE)
            self.looks[who] = sorted(i for i in items if i)

    def send(self, data):
        self.c.sendall(data)

    def equipped(self):
        return sorted(item for item, _, eq in self.slots.values() if eq)

    def slot_of(self, item_id, equipped=None):
        for slot, (item, _, eq) in sorted(self.slots.items()):
            if item == item_id and (equipped is None or bool(eq) == equipped):
                return slot
        return None

    def free_slot(self):
        return next(i for i in range(BAG_SLOTS - 1, -1, -1) if i not in self.slots)


def equip(slot):   return struct.pack("<HBH", 5, T_EQUIP, slot)
def unequip(slot): return struct.pack("<HBH", 5, T_UNEQUIP, slot)
def swap(a, b):    return struct.pack("<HBHH", 7, T_SWAP, a, b)
def discard(s, n): return struct.pack("<HBHH", 7, T_DISCARD, s, n)
def pickup():      return struct.pack("<HB", 3, T_PICKUP)
def use(slot):     return struct.pack("<HBH", 5, T_USE, slot)
def sort():        return struct.pack("<HBB", 4, T_SORT, 0)


def place_together():
    """새 캐릭터는 마을 둘레 아무 데나 선다 — 인메모리 DB(샌드박스)는 "xyX_Y_…" 이름의 저장 위치를 (X, Y)로
    돌려주므로, 처음 들어온 자리 하나를 골라 두 캐릭터를 만들고(첫 접속) 다시 들어와 그 칸에 함께 세운다."""
    probe = Client(f"invp{STAMP}")
    probe.poll(1.0)
    probe.c.close()
    if probe.pos is None:
        return None, None
    names = [f"xy{probe.pos[0]}_{probe.pos[1]}_{tag}{STAMP}" for tag in ("a", "b")]
    for name in names:
        first = Client(name)
        first.poll(1.0)
        first.c.close()
    time.sleep(1.0)
    a = Client(names[0])
    a.poll(1.5)
    b = Client(names[1])
    b.poll(1.5)
    a.poll(0.2)
    return a, b


def main():
    a, b = place_together()
    if a is None or a.id is None or b.id is None or a.pos != b.pos:
        print("FAIL: 두 캐릭터를 한 칸에 세우지 못함")
        return 1

    fails = []
    if a.listed != set(range(BAG_SLOTS)):
        fails.append(f"소지품 목록: 두 쪽으로 받은 칸 {len(a.listed)}개 ≠ {BAG_SLOTS}칸(0~{BAG_SLOTS - 1})")

    def step(label, data, expect_look=True, wait=0.5):
        a.send(data)
        a.poll(wait)
        b.poll(0.05)
        if expect_look:
            want, got = a.equipped(), b.looks.get(a.id)
            if want != got:
                fails.append(f"{label}: B가 본 A의 외형 {got} ≠ A의 착용 {want}")
        print(f"  · {label:<28} 착용 {a.equipped()}")

    start = a.equipped()
    if len(start) < 2:
        print(f"FAIL: 시작 장비가 입혀져 있지 않다 — {start}")
        return 1
    if b.looks.get(a.id) != start:
        fails.append(f"처음 시야: B가 본 A의 외형 {b.looks.get(a.id)} ≠ A의 착용 {start}")
    # 시작 장비는 무기 → 갑옷 → 장화 순으로 앞 칸부터 들어간다(GiveStarterKit)
    weapon, armor = [item for _, (item, _, eq) in sorted(a.slots.items()) if eq][:2]
    try:
        # 1) 착용 중인 칸을 옮긴 뒤 해제 · 장착 — 칸이 옮겨진 걸 목록이 모르면 외형이 틀린다(Debug는 assert)
        w = a.slot_of(weapon, True)
        step("착용 무기 칸 옮기기", swap(w, a.free_slot()), expect_look=False)
        step("옮긴 무기 해제", unequip(a.slot_of(weapon, True)))
        step("다시 장착", equip(a.slot_of(weapon, False)))
        # 2) 정리 — 착용 중인 것들이 앞 칸으로 몰린다
        step("소지품 정리", sort(), expect_look=False)
        step("방어구 해제", unequip(a.slot_of(armor, True)))
        step("방어구 · 다른 칸 맞바꿈", swap(a.slot_of(armor, False), min(s for s, v in a.slots.items() if v[2])), expect_look=False)
        step("방어구 다시 장착", equip(a.slot_of(armor, False)))
        # 3) 착용 무기를 버렸다 줍기(꺼내기 · 넣기)
        step("착용 무기 버리기", discard(a.slot_of(weapon, True), 1))
        step("다시 줍기", pickup(), expect_look=False)
        step("주운 무기 장착", equip(a.slot_of(weapon, False)))
        # 4) 물약(소비 아이템 경로)
        potion = next((s for s, (item, count, eq) in sorted(a.slots.items()) if not eq and count > 1), None)
        if potion is not None:
            step("물약 사용", use(potion), expect_look=False)
        # 5) 몰아 넣기 — 응답을 기다리지 않고 자리 바꿈 · 정리 · 해제 · 장착을 뒤섞는다
        burst = b""
        for i in range(40):
            burst += swap(i % 6, (i * 7 + 3) % BAG_SLOTS) + sort()
            if i % 5 == 0:
                burst += unequip(i % 4) + equip(i % 4)
        a.send(burst)
        a.poll(2.5)
        # 몰아 넣은 뒤의 착용 상태를 외형으로 한 번 더 확인(해제 → 장착이 외형을 다시 보낸다)
        w = a.slot_of(weapon, True)
        if w is None:
            step("무기 장착(몰아 넣은 뒤)", equip(a.slot_of(weapon, False)))
        else:
            step("무기 해제(몰아 넣은 뒤)", unequip(w))
            step("무기 장착(몰아 넣은 뒤)", equip(a.slot_of(weapon, False)))
    except (ConnectionError, OSError) as e:
        print(f"FAIL: 연결이 끊김 — {e}")
        return 1
    finally:
        a.c.close()
        b.c.close()

    try:
        z = Client(f"invz{STAMP}")
        z.poll(1.0)
        alive = z.id is not None
        z.c.close()
    except OSError:
        alive = False
    if not alive:
        fails.append("서버 응답 없음")
    for f in fails:
        print(f"FAIL: {f}")
    print(f"{'PASS' if not fails else 'FAIL'}: 소지품 착용 칸 — 외형 확인 {'모두 맞음' if not fails else f'{len(fails)}건 틀림'}, "
          f"서버 {'살아 있음' if alive else '응답 없음'}")
    return 0 if not fails else 1


if __name__ == "__main__":
    sys.exit(main())
