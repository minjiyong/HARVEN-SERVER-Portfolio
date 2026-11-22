"""세션 · 로그인 회귀 시험 — HARVEN_IN_MEMORY_DB=ON 서버를 띄운 뒤 실행한다.

    python3 Tools/Test/regress_session.py [host] [port]

SERVER_BUG_AUDIT 1 · 2 · 6번(로그인 직후 패킷 크래시 · 끊긴 로그인의 유령 User · 막힌 저장 위치),
3번(같은 계정 중복 접속 — 먼저 있던 세션을 끊고, 그 저장 뒤에 새 세션이 읽는다),
4 · 9 · 40번(없앤 옛 패킷 · 크기가 다른 패킷)을 다시 확인한다.
"""
import socket
import struct
import sys
import time
import select

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 4000

T_LOGIN, T_MOVE = 35, 53                       # USER_LOGIN_REQ · USER_MOVE_REQ
T_LOGIN_ACK, T_ADD, T_SYSTEM, T_MOVE_ACK = 11, 21, 32, 54
PROTOCOL_VERSION = 3
STAMP = int(time.time()) % 100000


def login_packet(name):
    return struct.pack("<HB20s20sHB", 46, T_LOGIN, name.encode(), b"pw", PROTOCOL_VERSION, 0)


_seq = [0]


def move_packet(direction):
    _seq[0] = (_seq[0] + 1) & 0xFFFF
    return struct.pack("<HBBH", 6, T_MOVE, direction, _seq[0])


def move_ack_position(p):
    return struct.unpack_from("<hh", p, 5)


def connect():
    c = socket.create_connection((HOST, PORT), timeout=5)
    c.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    return c


def read_packets(c, seconds):
    buf, out, end = b"", [], time.time() + seconds
    while time.time() < end:
        r, _, _ = select.select([c], [], [], 0.05)
        if not r:
            continue
        try:
            data = c.recv(65536)
        except OSError:
            break
        if not data:
            break
        buf += data
        while len(buf) >= 3:
            size = struct.unpack_from("<H", buf)[0]
            if size < 3 or len(buf) < size:
                break
            out.append((buf[2], buf[:size]))
            buf = buf[size:]
    return out


def login(name, wait=1.5):
    c = connect()
    c.sendall(login_packet(name))
    packets = read_packets(c, wait)
    return c, packets


def ack_position(packets):
    acks = [p for t, p in packets if t == T_LOGIN_ACK]
    return struct.unpack_from("<hh", acks[0], 19) if acks else None


def visible_names(packets):
    return [p[24:44].split(b"\0")[0].decode("utf-8", "replace") for t, p in packets if t == T_ADD]


def server_alive():
    c, packets = login(f"alive{STAMP}_{int(time.time() * 1000) % 100000}", 1.0)
    c.close()
    return ack_position(packets) is not None


failures = []


def check(label, ok, detail=""):
    print(f"{'PASS' if ok else 'FAIL'}: {label}{(' — ' + detail) if detail else ''}")
    if not ok:
        failures.append(label)


def test_login_then_move_flood():
    rounds = 200
    for i in range(rounds):
        try:
            c = connect()
            c.sendall(login_packet(f"fl{STAMP}_{i}") + move_packet(3) * 60)
            if i % 2 == 0:
                read_packets(c, 0.02)
            c.close()
        except OSError as e:
            check("로그인 직후 이동 패킷 폭주", False, f"{i}번째 접속에서 {e}")
            return
        time.sleep(0.005)
    time.sleep(1.0)
    check("로그인 직후 이동 패킷 폭주(200회) 뒤에도 서버가 산다", server_alive())


def test_no_ghost_after_hangup():
    ghosts = 0
    for i in range(10):
        name = f"gh{STAMP}_{i}"
        c, packets = login(name, 1.0)          # 계정 만들고 저장 위치를 남긴다
        c.close()
        time.sleep(0.5)
        g = connect()
        g.sendall(login_packet(name))
        if i % 2:
            time.sleep(0.001 * i)
        g.close()                              # 로그인 결과가 오기 전에 끊는다
        time.sleep(1.0)
        c, packets = login(name, 1.5)
        ghosts += visible_names(packets).count(name)
        c.close()
        time.sleep(0.3)
    check("로그인 중 끊은 캐릭터가 월드에 남지 않는다(10회)", ghosts == 0, f"남은 유령 {ghosts}")


def test_second_login_packet_ignored():
    c = connect()
    name = f"dl{STAMP}"
    c.sendall(login_packet(name) + login_packet(name))
    packets = read_packets(c, 1.5)
    acks = sum(1 for t, _ in packets if t == T_LOGIN_ACK)
    c.close()
    check("한 세션의 두 번째 로그인 패킷은 버린다", acks == 1, f"LOGIN_ACK {acks}개")


def test_blocked_saved_position(label, blocked_xy):
    # 인메모리 DB(샌드박스)는 "xyX_Y_…" 이름의 저장 위치를 (X, Y)로 돌려준다 — 걸을 수 없는 칸에 저장된 캐릭터를 만든다
    name = f"xy{blocked_xy[0]}_{blocked_xy[1]}_{STAMP % 1000}"
    c, packets = login(name, 1.0)      # 처음 접속(새 캐릭터)
    c.close()
    time.sleep(1.0)
    c, packets = login(name, 1.5)      # 두 번째 접속 — 저장 위치가 막힌 칸
    pos = ack_position(packets)
    # Zone에 제대로 등록됐으면 이동마다 USER_MOVE_ACK가 온다(반쯤 등록되면 아무 답이 없거나 서버가 죽는다)
    acks = 0
    for direction in range(4):
        c.sendall(move_packet(direction))
        acks += sum(1 for t, _ in read_packets(c, 0.6) if t == T_MOVE_ACK)
    c.close()
    time.sleep(0.5)
    ok = pos is not None and tuple(pos) != tuple(blocked_xy) and acks > 0 and server_alive()
    check(f"막힌 저장 위치({label})에서 다시 로그인", ok, f"로그인 위치 {pos}, 이동 응답 {acks}")


def test_legacy_packets_rejected():
    """없앤 옛 패킷(순간이동 · 옛 이동)과 크기가 다른 패킷은 처리하지 않는다."""
    c, packets = login(f"lg{STAMP}", 1.0)
    start = ack_position(packets)
    my_id = [p for t, p in packets if t == T_LOGIN_ACK][0][3:19] if start is not None else b""
    c.sendall(struct.pack("<HBhh", 7, 4, 1500, 1500))              # 옛 USER_TELEPORT_REQ
    c.sendall(struct.pack("<HBBI", 8, 1, 3, 0) * 3)                # 옛 USER_MOVE_REQ
    c.sendall(struct.pack("<HBB", 4, T_MOVE, 3))                   # 짧은 USER_MOVE_REQ(크기 다름)
    later = read_packets(c, 1.0)
    # 내 이동 결과만 센다(SUBJECT_MOVE_NFY는 주변 몬스터 · 다른 플레이어 것도 온다)
    moved = [t for t, p in later if t == T_MOVE_ACK or (t == 22 and p[3:19] == my_id)]
    c.close()
    check("없앤 옛 패킷 · 크기가 다른 패킷은 처리하지 않는다", start is not None and not moved and server_alive(),
          f"이동 관련 응답 {len(moved)}")


def closed_by_server(c, seconds):
    """서버가 이 연결을 닫았는가(받은 패킷 목록도 돌려준다)."""
    buf, packets, end = b"", [], time.time() + seconds
    while time.time() < end:
        r, _, _ = select.select([c], [], [], 0.05)
        if not r:
            continue
        try:
            data = c.recv(65536)
        except OSError:
            return True, packets
        if not data:
            return True, packets
        buf += data
        while len(buf) >= 3:
            size = struct.unpack_from("<H", buf)[0]
            if size < 3 or len(buf) < size:
                break
            packets.append((buf[2], buf[:size]))
            buf = buf[size:]
    return False, packets


def test_relogin_kicks_old_session():
    name = f"kk{STAMP}"
    a, packets = login(name, 1.0)
    start = ack_position(packets)
    last = start
    for direction in (3, 3, 1, 1, 3):           # 몇 칸 움직여 저장할 위치를 바꾼다
        a.sendall(move_packet(direction))
        for t, p in read_packets(a, 0.6):
            if t == T_MOVE_ACK:
                last = move_ack_position(p)
    b, b_packets = login(name, 2.0)
    kicked, a_packets = closed_by_server(a, 2.0)
    pos_b = ack_position(b_packets)
    told = any(t == T_SYSTEM for t, _ in a_packets)
    copies = visible_names(b_packets).count(name)
    a.close()
    b.close()
    check("같은 계정으로 다시 로그인하면 먼저 있던 세션을 끊는다", kicked and pos_b is not None,
          f"끊김 {kicked} · 알림 {told} · 새 세션 로그인 {pos_b is not None}")
    check("새 세션은 끊긴 세션이 저장한 위치에서 시작한다", pos_b is not None and tuple(pos_b) == tuple(last),
          f"처음 {start} → 마지막 {last} → 새 세션 {pos_b}")
    check("끊긴 세션이 월드에 남지 않는다", copies == 0, f"보이는 같은 이름 {copies}")


def test_concurrent_logins_same_account():
    name = f"cc{STAMP}"
    c, _ = login(name, 1.0)       # 계정을 만들어 둔다
    c.close()
    time.sleep(0.5)
    conns = [connect() for _ in range(5)]
    for c in conns:
        c.sendall(login_packet(name))
    results = []
    for c in conns:
        closed, packets = closed_by_server(c, 0.8)
        results.append((closed, packets))
    time.sleep(2.0)
    in_game = 0
    for c, (closed, packets) in zip(conns, results):
        later_closed, later = closed_by_server(c, 0.2)
        got_ack = any(t == T_LOGIN_ACK for t, _ in packets + later)
        if got_ack and not (closed or later_closed):
            in_game += 1
        c.close()
    time.sleep(0.5)
    check("같은 계정 동시 로그인 5개 중 게임에 남는 건 하나", in_game == 1 and server_alive(), f"게임 중 {in_game}")

if __name__ == "__main__":
    test_second_login_packet_ignored()
    test_no_ghost_after_hangup()
    test_blocked_saved_position("마을 밖 Zone 0 벽", (6, 0))
    test_blocked_saved_position("마을 담장", (710, 714))
    test_blocked_saved_position("월드 밖", (2500, 10))
    test_legacy_packets_rejected()
    test_relogin_kicks_old_session()
    test_concurrent_logins_same_account()
    test_login_then_move_flood()
    print("\nOK" if not failures else f"\nFAILED: {failures}")
    sys.exit(1 if failures else 0)
