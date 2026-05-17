#!/usr/bin/env bash
set -euo pipefail

TV_IP="${TV_IP:-192.168.2.121}"
SIDE_DIR="${SIDE_DIR:-/media/internal/android-sidecar}"
SERVICE="${SERVICE:-test.android.aidl.ping}"

ssh root@"$TV_IP" "SIDE_DIR='$SIDE_DIR' SERVICE='$SERVICE' sh -s" <<'TVSH'
set -eu

cd "$SIDE_DIR"

for f in \
  bin/mini_servicemgr \
  bin/android_like_aidl_service \
  bin/android_like_aidl_client \
  bin/android_like_binder_ping_client \
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
  bin/android_like_aidl_service \
  bin/android_like_aidl_client \
  bin/android_like_binder_ping_client \
  load-binder-tv.sh

mkdir -p logs run

if [ ! -e /dev/binder ]; then
  ./load-binder-tv.sh "$SIDE_DIR/modules/binder.ko"
fi

killall mini_servicemgr 2>/dev/null || true
killall android_like_aidl_service 2>/dev/null || true
killall android_like_aidl_client 2>/dev/null || true
killall android_like_binder_ping_client 2>/dev/null || true

rm -f logs/binder_ping_*.log run/binder_ping_*.pid

cleanup() {
  [ -f run/binder_ping_service.pid ] && kill "$(cat run/binder_ping_service.pid)" 2>/dev/null || true
  [ -f run/binder_ping_sm.pid ] && kill "$(cat run/binder_ping_sm.pid)" 2>/dev/null || true
}

trap cleanup EXIT

echo "== binder ping config =="
echo "SERVICE=$SERVICE"

echo "== start mini_servicemgr =="
bin/mini_servicemgr > logs/binder_ping_sm.log 2>&1 &
echo "$!" > run/binder_ping_sm.pid

sleep 2

echo "== start aidl-like service =="
bin/android_like_aidl_service "$SERVICE" > logs/binder_ping_service.log 2>&1 &
echo "$!" > run/binder_ping_service.pid

sleep 3

echo "== run binder ping client =="
set +e
bin/android_like_binder_ping_client "$SERVICE" > logs/binder_ping_client.log 2>&1
ping_rc="$?"
set -e

echo "== binder ping client markers =="
grep 'BINDER_PING' logs/binder_ping_client.log || true

echo "== binder ping service markers =="
grep 'BINDER_PING' logs/binder_ping_service.log || true

if [ "$ping_rc" -ne 0 ]; then
  echo "FAIL: binder ping client rc=$ping_rc"
  echo "== client log =="
  cat logs/binder_ping_client.log || true
  echo "== service log =="
  cat logs/binder_ping_service.log || true
  echo "== servicemgr log =="
  cat logs/binder_ping_sm.log || true
  exit "$ping_rc"
fi

grep -q 'BINDER_PING_SERVICE_REGISTERED' logs/binder_ping_service.log
grep -q 'BINDER_PING_TRANSACTION_OK' logs/binder_ping_service.log
grep -q 'BINDER_PING_CLIENT_OK' logs/binder_ping_client.log

echo "== verify normal AIDL call still works =="
set +e
bin/android_like_aidl_client "$SERVICE" 1 > logs/binder_ping_aidl_client.log 2>&1
aidl_rc="$?"
set -e

grep 'AIDL_LIKE_' logs/binder_ping_aidl_client.log || true

if [ "$aidl_rc" -ne 0 ]; then
  echo "FAIL: aidl client after ping rc=$aidl_rc"
  cat logs/binder_ping_aidl_client.log || true
  exit "$aidl_rc"
fi

grep -q 'AIDL_LIKE_CLIENT_SMOKE_OK' logs/binder_ping_aidl_client.log

echo "BINDER_PING_AIDL_RECOVERY_OK"
echo "BINDER_PING_SMOKE_TV_OK"
TVSH
