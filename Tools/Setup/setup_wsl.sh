#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# HARVEN-SERVER — WSL(Ubuntu 24.04) 개발 환경 준비. 다시 돌려도 된다(된 것은 건너뛴다)
#
#   Windows PowerShell에서 저장소 루트로 가서:   wsl -d Ubuntu-24.04 -- bash Tools/Setup/setup_wsl.sh
#   WSL 터미널이면 저장소 폴더에서:            bash Tools/Setup/setup_wsl.sh
#
#   1. 빌드 · 디버그 도구(g++ · cmake · ninja · gdb · rsync) · Boost · unixODBC   — Visual Studio(WSL 대상)가 쓴다
#   2. MySQL Connector/ODBC Unicode 드라이버(repo.mysql.com의 Ubuntu 24.04 최신판) — 클라우드 DB(RDS) 접속용
#      MariaDB ODBC는 SSLMODE=VERIFY_IDENTITY를 모르므로 쓰지 않는다(Shared/Database/mysql/README.md)
#   3. AWS RDS CA 묶음 → Shared/Database/mysql/global-bundle.pem(Git 무시)
#   4. harven.env가 없으면 harven.env.example로 만들고 드라이버 이름을 채운다 — 계정 · 비밀번호는 직접 넣는다
#   5. RDS 3306 포트에 닿는지 본다(안 닿으면 보안 그룹에 내 공인 IP /32를 추가해야 한다)
# sudo 비밀번호를 한 번 묻는다(apt).
# ─────────────────────────────────────────────────────────────────────────────
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"
[[ -f harven.env.example ]] || { echo "저장소 루트를 찾지 못했다: $ROOT"; exit 1; }

find_driver() {
    odbcinst -q -d 2>/dev/null | tr -d '[]' | grep -E '^MySQL ODBC [0-9.]+ Unicode Driver$' | sort -V | tail -n1 || true
}

echo "== 1/5 빌드 도구 · Boost · unixODBC"
sudo apt-get update -qq
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
    g++ cmake ninja-build gdb rsync zip libboost-dev unixodbc unixodbc-dev odbcinst curl ca-certificates >/dev/null

echo "== 2/5 MySQL Connector/ODBC"
driver="$(find_driver)"
if [[ -z "$driver" ]]; then
    arch="$(dpkg --print-architecture)"
    base="https://repo.mysql.com/apt/ubuntu"
    deb_path="$(curl -fsSL "$base/dists/noble/mysql-tools/binary-$arch/Packages" \
        | awk '/^Package: /{p=($2=="mysql-connector-odbc")} p && /^Filename: /{print $2}' | sort -V | tail -n1)"
    [[ -n "$deb_path" ]] || { echo "MySQL APT 저장소에서 mysql-connector-odbc($arch)를 찾지 못했다"; exit 1; }
    tmp="$(mktemp -d)"
    trap 'rm -rf "$tmp"' EXIT
    echo "   내려받기: ${deb_path##*/}"
    curl -fsSL -o "$tmp/${deb_path##*/}" "$base/$deb_path"
    sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq "$tmp/${deb_path##*/}" >/dev/null
    driver="$(find_driver)"
    [[ -n "$driver" ]] || { echo "드라이버를 설치했지만 Unicode 드라이버가 등록되지 않았다 — odbcinst -q -d로 확인"; exit 1; }
fi
echo "   드라이버: $driver"

echo "== 3/5 RDS CA 묶음"
ca="Shared/Database/mysql/global-bundle.pem"
if [[ -s "$ca" ]]; then
    echo "   있음: $ca"
else
    curl -fsSL -o "$ca" https://db.example.com/global/global-bundle.pem
    echo "   받음: $ca"
fi

echo "== 4/5 harven.env"
if [[ -f harven.env ]]; then
    current="$(sed -n 's/\r$//; s/^HARVEN_DB_DRIVER=//p' harven.env | tail -n1)"
    if [[ "$current" != "$driver" ]]; then
        sed -i "s|^HARVEN_DB_DRIVER=.*|HARVEN_DB_DRIVER=$driver|" harven.env
        echo "   고침: HARVEN_DB_DRIVER '$current' → '$driver'(설치된 드라이버)"
    else
        echo "   있음: harven.env"
    fi
else
    sed "s|^HARVEN_DB_DRIVER=.*|HARVEN_DB_DRIVER=$driver|" harven.env.example > harven.env
    echo "   만듦: harven.env — HARVEN_DB_USER · HARVEN_DB_PASSWORD를 채운다"
fi

echo "== 5/5 RDS 연결 경로"
host="$(sed -n 's/\r$//; s/^HARVEN_DB_HOST=//p' harven.env | tail -n1)"
port="$(sed -n 's/\r$//; s/^HARVEN_DB_PORT=//p' harven.env | tail -n1)"
port="${port:-3306}"
myip="$(curl -fsS --max-time 5 https://checkip.amazonaws.com 2>/dev/null | tr -d '[:space:]' || true)"
if [[ -z "$host" ]]; then
    echo "   HARVEN_DB_HOST가 비어 있다 — 인메모리 프리셋만 쓸 수 있다"
elif timeout 5 bash -c "exec 3<>/dev/tcp/$host/$port" 2>/dev/null; then
    echo "   $host:$port 에 닿는다"
else
    echo "   $host:$port 에 닿지 않는다 — RDS 보안 그룹 인바운드에 내 공인 IP ${myip:-?}/32 (TCP $port)를 추가해 달라고 한다"
fi

if ! grep -qE '^HARVEN_DB_PASSWORD=.+' harven.env; then
    echo
    echo "남은 일: harven.env의 HARVEN_DB_PASSWORD(와 계정)를 채운 뒤 VS에서 프리셋 linux-debug로 F5"
fi
echo "끝."
