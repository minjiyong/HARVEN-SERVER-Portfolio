"""Zone 경계 회귀 시험 — HARVEN_IN_MEMORY_DB=ON 서버(되도록 -D_DEBUG 빌드: 소유 Zone 검사 assert가 켜진다)를 띄운 뒤 실행한다.

    python3 Tools/Test/regress_zone.py [host] [port] [seconds]

SERVER_BUG_AUDIT 5번: Zone 경계를 넘는 순간 옛 Zone 큐에 남은 패킷이 옛 Zone 스레드에서 처리되면 두 Zone 스레드가 같은 User의
인벤토리 · HP를 동시에 만진다(디버그 빌드는 Inventory의 소유 Zone assert로 바로 죽는다). 경계 칸에서 좌우로 오가며 소지품 요청을
쏟아 넣고, 서버가 살아 있는지 · 요청마다 응답이 오는지 본다.
"""
import select
import socket
import struct
import sys
import time

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 4000
SECONDS = float(sys.argv[3]) if len(sys.argv) > 3 else 20.0
STAMP = int(time.time()) % 1000

T_LOGIN, T_MOVE, T_SWAP, T_SORT = 35, 53, 7, 73
T_LOGIN_ACK, T_SWAP_ACK, T_MOVE_ACK = 11, 17, 54
BORDER = (499, 6)       # Zone 0 | Zone 1 경계(x 499 → 500), 두 칸 모두 걸을 수 있다


def connect():
    c = socket.create_connection((HOST, PORT), timeout=5)
    c.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    return c


class Reader:
    def __init__(self, c):
        self.c, self.buf = c, b""

    def poll(self, seconds):
        out, end = [], time.time() + seconds
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
                    out.append((self.buf[2], self.buf[:size]))
                    self.buf = self.buf[size:]
            if time.time() >= end:
                return out


def login(name):
    c = connect()
    c.sendall(struct.pack("<HB20s20sHB", 46, T_LOGIN, name.encode(), b"pw", 3, 0))
    reader = Reader(c)
    packets = reader.poll(1.5)
    acks = [p for t, p in packets if t == T_LOGIN_ACK]
    return c, reader, (struct.unpack_from("<hh", acks[0], 19) if acks else None)


def main():
    # 인메모리 DB(샌드박스)는 "xyX_Y_…" 이름의 저장 위치를 (X, Y)로 돌려준다 — 처음 접속으로 계정을 만들고 다시 들어가 경계에 선다
    name = f"xy{BORDER[0]}_{BORDER[1]}_{STAMP}"
    c, _, _ = login(name)
    c.close()
    time.sleep(1.0)
    c, reader, pos = login(name)
    if pos is None or tuple(pos) != BORDER:
        print(f"FAIL: 경계 칸에 서지 못함 — 로그인 위치 {pos}")
        return 1

    seq, swaps_sent, swap_acks, move_acks, crossings = 0, 0, 0, 0, 0
    x = BORDER[0]
    end = time.time() + SECONDS
    try:
        while time.time() < end:
            direction = 3 if x <= BORDER[0] else 2          # 오른쪽(Zone 1)으로, 다시 왼쪽(Zone 0)으로
            seq = (seq + 1) & 0xFFFF
            burst = struct.pack("<HBBH", 6, T_MOVE, direction, seq)
            for _ in range(6):                               # 경계를 넘는 걸음 바로 뒤에 소지품 요청을 붙인다
                burst += struct.pack("<HBHH", 7, T_SWAP, 0, 1) + struct.pack("<HBB", 4, T_SORT, 0)
                swaps_sent += 1
            c.sendall(burst)
            for t, p in reader.poll(0.45):
                if t == T_SWAP_ACK:
                    swap_acks += 1
                elif t == T_MOVE_ACK:
                    move_acks += 1
                    nx = struct.unpack_from("<hh", p, 5)[0]
                    if (nx <= BORDER[0]) != (x <= BORDER[0]):
                        crossings += 1
                    x = nx
        swap_acks += sum(1 for t, _ in reader.poll(1.5) if t == T_SWAP_ACK)
    except (ConnectionError, OSError) as e:
        print(f"FAIL: 연결이 끊김 — {e}")
        return 1
    finally:
        c.close()

    try:
        a, _, alive = login(f"zz{STAMP}")
        a.close()
    except OSError:
        alive = None
    ok = alive is not None and crossings >= 4 and swap_acks == swaps_sent
    print(f"{'PASS' if ok else 'FAIL'}: Zone 경계 {crossings}번 넘나들며 소지품 요청 {swaps_sent}개 — 응답 {swap_acks}개, "
          f"이동 응답 {move_acks}개, 서버 {'살아 있음' if alive else '응답 없음'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
