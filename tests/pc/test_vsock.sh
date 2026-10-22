#!/bin/bash
# runtime/pc/vsock.c between two processes on one relay (tests/pc/test_vsock.c), under Wine on Linux.
#   tests/pc/test_vsock.sh BUILD_DIR      (BUILD_DIR: with hiraku-eamuse and pc/test_vsock.exe built:
#                                          ninja hiraku-eamuse hiraku-pc-test-vsock)
# Needs port 5740 free (the relay's). WINEPREFIX as you have it (a scratch one is made otherwise).
set -u
B=$(realpath "${1:-build/cmake}")
T=$(mktemp -d)
trap 'kill $E 2>/dev/null; rm -rf "$T"' EXIT
export WINEPREFIX=${WINEPREFIX:-$T/wine} WINEDEBUG=-all
"$B/hiraku-eamuse" --port 0 --data "$T/data" --quiet > "$T/server.log" 2>&1 &
E=$!
sleep 1
export HK_PC_RELAY=127.0.0.1:5740
wine "$B/pc/test_vsock.exe" host > "$T/host.log" 2>&1 &
H=$!
sleep 3
wine "$B/pc/test_vsock.exe" client > "$T/client.log" 2>&1
C=$?
wait $H
HR=$?
echo "-- host"; grep -E "^(ok|FAIL)" "$T/host.log"
echo "-- client"; grep -E "^(ok|FAIL)" "$T/client.log"
[ $C -eq 0 ] && [ $HR -eq 0 ] && echo "test_vsock: all passed" || { echo "test_vsock: FAILED"; cat "$T/server.log"; exit 1; }
