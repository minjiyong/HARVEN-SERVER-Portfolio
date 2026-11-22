#!/usr/bin/env bash
set -euo pipefail

server=$1
shift
port=4000
previous=''
check_only=false
for argument in "$@"; do
    if [[ "$previous" == '--port' ]]; then port=$argument; fi
    if [[ "$argument" == '--check-data' || "$argument" == '--dump-data' ]]; then check_only=true; fi
    previous=$argument
done
if [[ ! "$port" =~ ^[0-9]+$ ]] || (( port < 1 || port > 65535 )); then
    echo '[ERROR] --port must be between 1 and 65535.'
    exit 1
fi
if ! $check_only; then
    listener=$(ss -H -ltnp "sport = :$port")
    if [[ -n "$listener" ]]; then
        echo "[HARVEN] Port $port is already in use. A second server cannot start."
        echo "$listener"
        echo '[HARVEN] If GameServer is already running, connect to that server.'
        echo '[HARVEN] To restart it, press Ctrl+C in its original server window first.'
        echo '[HARVEN] For a separate server, use: run_server.bat --port 4001'
        exit 10
    fi
fi
# Flush each log line even when launched through a Windows terminal or a pipe.
exec stdbuf -oL -eL "$server" "$@"
