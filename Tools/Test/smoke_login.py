"""TCP framing and reconnect checks against HARVEN_IN_MEMORY_DB=ON.

Run after starting GameServer: python3 Tools/Test/smoke_login.py [host] [port]
"""

import socket
import struct
import sys
import time


def read_exact(connection, count):
    chunks = bytearray()
    while len(chunks) < count:
        part = connection.recv(count - len(chunks))
        if not part:
            raise RuntimeError("connection closed before the login response")
        chunks.extend(part)
    return bytes(chunks)


def login_request(name):
    # USER_LOGIN_REQ(35): 이름 · 비밀번호 · 프로토콜 판(2) · 새 캐릭터 직업(0 = 기사)
    return struct.pack("<HB20s20sHB", 46, 35, name, b"test-only", 3, 0)


def expect_login_ack(connection):
    (size,) = struct.unpack("<H", read_exact(connection, 2))
    if size < 3 or size > 4096:
        raise RuntimeError(f"invalid response size: {size}")
    response = read_exact(connection, size - 2)
    if response[0] != 11:  # PacketType::USER_LOGIN_ACK
        raise RuntimeError(f"expected USER_LOGIN_ACK (11), got type {response[0]}")


def main():
    host = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 4000
    stamp = int(time.time()) % 100000000

    with socket.create_connection((host, port), timeout=10) as connection:
        connection.settimeout(10)
        request = login_request(f"smoke{stamp}".encode("ascii"))
        connection.sendall(request[:1])
        time.sleep(0.05)
        connection.sendall(request[1:])
        expect_login_ack(connection)
    print("PASS: fragmented login request")

    with socket.create_connection((host, port), timeout=10) as connection:
        connection.settimeout(10)
        # Unknown type is safely ignored; the following complete packet must be routed.
        connection.sendall(struct.pack("<HB", 3, 255) + login_request(f"batch{stamp}".encode("ascii")))
        expect_login_ack(connection)
    print("PASS: two packets in one TCP write")

    with socket.create_connection((host, port), timeout=10) as connection:
        connection.settimeout(3)
        connection.sendall(b"\x02\x00")  # Too short to contain the packet type.
        if connection.recv(1) != b"":
            raise RuntimeError("server accepted an invalid two-byte frame")
    print("PASS: malformed frame disconnected")

    with socket.create_connection((host, port), timeout=10) as connection:
        connection.settimeout(10)
        connection.sendall(login_request(f"again{stamp}".encode("ascii")))
        expect_login_ack(connection)
    print("PASS: reconnect after disconnect")


if __name__ == "__main__":
    main()
