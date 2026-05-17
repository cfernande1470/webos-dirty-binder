#!/usr/bin/env bash
set -euo pipefail

TV_IP="${TV_IP:-192.168.2.121}"
SIDE_DIR="${SIDE_DIR:-/media/internal/android-sidecar}"
SERVICE="${SERVICE:-test.android.aidl.factory}"

ssh root@"$TV_IP" "SIDE_DIR='$SIDE_DIR' SERVICE='$SERVICE' sh -s" <<'TVSH'
set -eu

cd "$SIDE_DIR"

for f in \
  bin/mini_servicemgr \
  bin/android_like_aidl_binder_return_service \
  bin/android_like_aidl_binder_return_client \
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
  bin/android_like_aidl_binder_return_service \
  bin/android_like_aidl_binder_return_client \
  load-binder-tv.sh

mkdir -p logs run

if [ ! -e /dev/binder ]; then
  ./load-binder-tv.sh "$SIDE_DIR/modules/binder.ko"
fi

killall mini_servicemgr 2>/dev/null || true
killall android_like_aidl_binder_return_service 2>/dev/null || true
killall android_like_aidl_binder_return_client 2>/dev/null || true

rm -f logs/android_like_binder_return_*.log run/android_like_binder_return_*.pid

cleanup() {
  [ -f run/android_like_binder_return_service.pid ] && kill "$(cat run/android_like_binder_return_service.pid)" 2>/dev/null || true
  [ -f run/android_like_binder_return_sm.pid ] && kill "$(cat run/android_like_binder_return_sm.pid)" 2>/dev/null || true
}

trap cleanup EXIT

echo "== binder return config =="
echo "SERVICE=$SERVICE"

echo "== start mini_servicemgr =="
bin/mini_servicemgr > logs/android_like_binder_return_sm.log 2>&1 &
echo "$!" > run/android_like_binder_return_sm.pid

sleep 2

echo "== start binder-return service =="
bin/android_like_aidl_binder_return_service "$SERVICE" > logs/android_like_binder_return_service.log 2>&1 &
echo "$!" > run/android_like_binder_return_service.pid

sleep 3

echo "== run binder-return client =="
set +e
bin/android_like_aidl_binder_return_client "$SERVICE" > logs/android_like_binder_return_client.log 2>&1
client_rc="$?"
set -e

echo "== client markers =="
grep 'AIDL_LIKE_BINDER_RETURN' logs/android_like_binder_return_client.log || true

echo "== service markers =="
grep 'AIDL_LIKE_BINDER_RETURN' logs/android_like_binder_return_service.log || true

if [ "$client_rc" -ne 0 ]; then
  echo "FAIL: binder-return client rc=$client_rc"
  echo "== client log =="
  cat logs/android_like_binder_return_client.log || true
  echo "== service log =="
  cat logs/android_like_binder_return_service.log || true
  echo "== servicemgr log =="
  cat logs/android_like_binder_return_sm.log || true
  exit "$client_rc"
fi

grep -q 'AIDL_LIKE_BINDER_RETURN_SERVICE_REGISTERED' logs/android_like_binder_return_service.log
grep -q 'AIDL_LIKE_BINDER_RETURN_CHILD_OBJECT_SENT' logs/android_like_binder_return_service.log
grep -q 'AIDL_LIKE_BINDER_RETURN_HANDLE_OK' logs/android_like_binder_return_client.log
grep -q 'AIDL_LIKE_BINDER_RETURN_CHILD_CALL_OK' logs/android_like_binder_return_client.log
grep -q 'AIDL_LIKE_BINDER_RETURN_CLIENT_SMOKE_OK' logs/android_like_binder_return_client.log

echo "AIDL_LIKE_BINDER_RETURN_SMOKE_TV_OK"
TVSH
