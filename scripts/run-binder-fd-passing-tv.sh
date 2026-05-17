#!/usr/bin/env bash
set -euo pipefail

TV_IP="${TV_IP:-192.168.2.121}"
SIDE_DIR="${SIDE_DIR:-/media/internal/android-sidecar}"
SERVICE="${SERVICE:-test.android.fd}"
ROUNDS="${ROUNDS:-16}"

if [ "${BINDER_FD_PASSING_UNSAFE:-0}" != "1" ]; then
  echo "BINDER_FD_PASSING_QUARANTINED"
  echo "Refusing to send BINDER_TYPE_FD by default because this rebooted the TV / returned BR_FAILED_REPLY."
  echo "Set BINDER_FD_PASSING_UNSAFE=1 to run the dangerous probe explicitly."
  exit 0
fi

ssh root@"$TV_IP" "SIDE_DIR='$SIDE_DIR' SERVICE='$SERVICE' ROUNDS='$ROUNDS' sh -s" <<'TVSH'
set -eu

cd "$SIDE_DIR"

for f in \
  bin/mini_servicemgr \
  bin/android_like_fd_passing_service \
  bin/android_like_fd_passing_client \
  modules/binder.ko \
  load-binder-tv.sh
do
  if [ ! -e "$f" ]; then
    echo "ERROR: missing $SIDE_DIR/$f"
    find . -maxdepth 3 -type f -o -type d | sort
    exit 1
  fi
done

chmod +x \
  bin/mini_servicemgr \
  bin/android_like_fd_passing_service \
  bin/android_like_fd_passing_client \
  load-binder-tv.sh

mkdir -p logs run

if [ ! -e /dev/binder ]; then
  ./load-binder-tv.sh "$SIDE_DIR/modules/binder.ko"
fi

killall mini_servicemgr 2>/dev/null || true
killall android_like_fd_passing_service 2>/dev/null || true
killall android_like_fd_passing_client 2>/dev/null || true

rm -f logs/binder_fd_*.log run/binder_fd_*.pid

cleanup() {
  [ -f run/binder_fd_service.pid ] && kill "$(cat run/binder_fd_service.pid)" 2>/dev/null || true
  [ -f run/binder_fd_sm.pid ] && kill "$(cat run/binder_fd_sm.pid)" 2>/dev/null || true
}

trap cleanup EXIT

echo "== binder fd config =="
echo "SERVICE=$SERVICE"
echo "ROUNDS=$ROUNDS"

echo "== start mini_servicemgr =="
bin/mini_servicemgr > logs/binder_fd_sm.log 2>&1 &
echo "$!" > run/binder_fd_sm.pid

sleep 2

echo "== start fd-passing service =="
bin/android_like_fd_passing_service "$SERVICE" > logs/binder_fd_service.log 2>&1 &
echo "$!" > run/binder_fd_service.pid

sleep 3

echo "== run fd-passing client =="
set +e
bin/android_like_fd_passing_client "$SERVICE" "$ROUNDS" > logs/binder_fd_client.log 2>&1
client_rc="$?"
set -e

echo "== fd client markers =="
grep 'BINDER_FD' logs/binder_fd_client.log || true

echo "== fd service markers =="
grep 'BINDER_FD' logs/binder_fd_service.log || true

if [ "$client_rc" -ne 0 ]; then
  echo "FAIL: fd-passing client rc=$client_rc"
  echo "== client log =="
  cat logs/binder_fd_client.log || true
  echo "== service log =="
  cat logs/binder_fd_service.log || true
  echo "== servicemgr log =="
  cat logs/binder_fd_sm.log || true
  exit "$client_rc"
fi

grep -q 'BINDER_FD_SERVICE_REGISTERED' logs/binder_fd_service.log
grep -q 'BINDER_FD_OBJECT_SENT' logs/binder_fd_client.log
grep -q 'BINDER_FD_RECEIVED_OK' logs/binder_fd_service.log
grep -q 'BINDER_FD_READ_OK' logs/binder_fd_service.log
grep -q 'BINDER_FD_CLIENT_ROUND_OK' logs/binder_fd_client.log
grep -q 'BINDER_FD_CLIENT_SMOKE_OK' logs/binder_fd_client.log

sent_count="$(grep -c 'BINDER_FD_OBJECT_SENT' logs/binder_fd_client.log || true)"
received_count="$(grep -c 'BINDER_FD_RECEIVED_OK' logs/binder_fd_service.log || true)"
read_count="$(grep -c 'BINDER_FD_READ_OK' logs/binder_fd_service.log || true)"
round_count="$(grep -c 'BINDER_FD_CLIENT_ROUND_OK' logs/binder_fd_client.log || true)"

echo "== fd passing counts =="
echo "sent_count=$sent_count"
echo "received_count=$received_count"
echo "read_count=$read_count"
echo "round_count=$round_count"
echo "expected=$ROUNDS"

if [ "$sent_count" -lt "$ROUNDS" ]; then
  echo "FAIL: sent_count too low"
  exit 1
fi

if [ "$received_count" -lt "$ROUNDS" ]; then
  echo "FAIL: received_count too low"
  exit 1
fi

if [ "$read_count" -lt "$ROUNDS" ]; then
  echo "FAIL: read_count too low"
  exit 1
fi

if [ "$round_count" -lt "$ROUNDS" ]; then
  echo "FAIL: round_count too low"
  exit 1
fi

echo "BINDER_FD_SMOKE_TV_OK"
TVSH
