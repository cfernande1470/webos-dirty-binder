#!/usr/bin/env bash
set -euo pipefail

TV_IP="${TV_IP:-192.168.2.121}"
SIDE_DIR="${SIDE_DIR:-/media/internal/android-sidecar}"
SERVICE="${SERVICE:-test.android.fdbridge}"
SOCKET_PATH="${SOCKET_PATH:-$SIDE_DIR/run/fd_bridge.sock}"

ssh root@"$TV_IP" "SIDE_DIR='$SIDE_DIR' SERVICE='$SERVICE' SOCKET_PATH='$SOCKET_PATH' sh -s" <<'TVSH'
set -eu

cd "$SIDE_DIR"

for f in \
  bin/mini_servicemgr \
  bin/fd_bridge_service \
  bin/fd_bridge_client \
  modules/binder.ko \
  load-binder-tv.sh
do
  if [ ! -e "$f" ]; then
    echo "ERROR: missing $SIDE_DIR/$f"
    find . -maxdepth 3 \( -type f -o -type d \) | sort
    exit 1
  fi
done

chmod +x \
  bin/mini_servicemgr \
  bin/fd_bridge_service \
  bin/fd_bridge_client \
  load-binder-tv.sh

mkdir -p logs run

if [ ! -e /dev/binder ]; then
  ./load-binder-tv.sh "$SIDE_DIR/modules/binder.ko"
fi

killall mini_servicemgr 2>/dev/null || true
killall fd_bridge_service 2>/dev/null || true
killall fd_bridge_client 2>/dev/null || true

rm -f "$SOCKET_PATH"
rm -f logs/fd_bridge_*.log run/fd_bridge_*.pid

cleanup() {
  [ -f run/fd_bridge_service.pid ] && kill "$(cat run/fd_bridge_service.pid)" 2>/dev/null || true
  [ -f run/fd_bridge_sm.pid ] && kill "$(cat run/fd_bridge_sm.pid)" 2>/dev/null || true
  rm -f "$SOCKET_PATH"
}

trap cleanup EXIT

echo "== start mini_servicemgr =="
bin/mini_servicemgr > logs/fd_bridge_sm.log 2>&1 &
echo "$!" > run/fd_bridge_sm.pid

sleep 2

echo "== start fd bridge service =="
bin/fd_bridge_service "$SERVICE" "$SOCKET_PATH" > logs/fd_bridge_service.log 2>&1 &
echo "$!" > run/fd_bridge_service.pid

sleep 3

echo "== run fd bridge client =="
set +e
bin/fd_bridge_client "$SERVICE" "$SOCKET_PATH" > logs/fd_bridge_client.log 2>&1
client_rc="$?"
set -e

echo "== fd bridge client log =="
cat logs/fd_bridge_client.log || true

echo "== fd bridge service log =="
cat logs/fd_bridge_service.log || true

echo "== mini_servicemgr log tail =="
tail -160 logs/fd_bridge_sm.log || true

if [ "$client_rc" -ne 0 ]; then
  echo "FAIL: fd_bridge_client rc=$client_rc"
  exit "$client_rc"
fi

grep -q 'FD_BRIDGE_SERVICE_SOCKET_READY' logs/fd_bridge_service.log
grep -q 'FD_BRIDGE_SERVICE_REGISTERED' logs/fd_bridge_service.log
grep -q 'FD_BRIDGE_CLIENT_SOCKET_SEND_OK' logs/fd_bridge_client.log
grep -q 'FD_BRIDGE_BINDER_CONTROL_OK' logs/fd_bridge_service.log
grep -q 'FD_BRIDGE_SERVICE_GOT_FD' logs/fd_bridge_service.log
grep -q 'FD_BRIDGE_SERVICE_READ_OK' logs/fd_bridge_service.log
grep -q 'FD_BRIDGE_CLIENT_BINDER_REPLY_OK' logs/fd_bridge_client.log
grep -q 'FD_BRIDGE_SMOKE_OK' logs/fd_bridge_client.log

echo "FD_BRIDGE_SMOKE_TV_OK"
TVSH
