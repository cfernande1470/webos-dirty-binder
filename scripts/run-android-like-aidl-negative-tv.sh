#!/usr/bin/env bash
set -euo pipefail

TV_IP="${TV_IP:-192.168.2.121}"
SIDE_DIR="${SIDE_DIR:-/tmp/android-usb/android-sidecar}"
SERVICE="${SERVICE:-test.android.aidl.negative}"

ssh root@"$TV_IP" "SIDE_DIR='$SIDE_DIR' SERVICE='$SERVICE' sh -s" <<'TVSH'
set -eu

cd "$SIDE_DIR"

for f in \
  bin/mini_servicemgr \
  bin/android_like_aidl_service \
  bin/android_like_aidl_negative_client \
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
  bin/android_like_aidl_negative_client \
  load-binder-tv.sh

mkdir -p logs run

if [ ! -e /dev/binder ]; then
  ./load-binder-tv.sh "$SIDE_DIR/modules/binder.ko"
fi

killall mini_servicemgr 2>/dev/null || true
killall android_like_aidl_service 2>/dev/null || true
killall android_like_aidl_client 2>/dev/null || true
killall android_like_aidl_negative_client 2>/dev/null || true

rm -f logs/android_like_aidl_negative_*.log run/android_like_aidl_negative_*.pid

cleanup() {
  [ -f run/android_like_aidl_negative_service.pid ] && kill "$(cat run/android_like_aidl_negative_service.pid)" 2>/dev/null || true
  [ -f run/android_like_aidl_negative_sm.pid ] && kill "$(cat run/android_like_aidl_negative_sm.pid)" 2>/dev/null || true
}

trap cleanup EXIT

echo "== aidl negative config =="
echo "SERVICE=$SERVICE"

echo "== start mini_servicemgr =="
bin/mini_servicemgr > logs/android_like_aidl_negative_sm.log 2>&1 &
echo "$!" > run/android_like_aidl_negative_sm.pid

sleep 2

echo "== start aidl-like service =="
bin/android_like_aidl_service "$SERVICE" > logs/android_like_aidl_negative_service.log 2>&1 &
echo "$!" > run/android_like_aidl_negative_service.pid

sleep 3

echo "== run negative client =="
set +e
bin/android_like_aidl_negative_client "$SERVICE" > logs/android_like_aidl_negative_client.log 2>&1
client_rc="$?"
set -e

echo "== negative client markers =="
grep 'AIDL_LIKE_NEGATIVE' logs/android_like_aidl_negative_client.log || true

echo "== aidl service markers =="
grep 'AIDL_LIKE_' logs/android_like_aidl_negative_service.log || true

if [ "$client_rc" -ne 0 ]; then
  echo "FAIL: negative client rc=$client_rc"
  echo "== client log =="
  cat logs/android_like_aidl_negative_client.log || true
  echo "== service log =="
  cat logs/android_like_aidl_negative_service.log || true
  echo "== servicemgr log =="
  cat logs/android_like_aidl_negative_sm.log || true
  exit "$client_rc"
fi

grep -q 'AIDL_LIKE_SERVICE_REGISTERED' logs/android_like_aidl_negative_service.log
grep -q 'AIDL_LIKE_NEGATIVE_BAD_DESCRIPTOR_OK' logs/android_like_aidl_negative_client.log
grep -q 'AIDL_LIKE_NEGATIVE_UNKNOWN_CODE_OK' logs/android_like_aidl_negative_client.log
grep -q 'AIDL_LIKE_NEGATIVE_TRUNCATED_PARCEL_OK' logs/android_like_aidl_negative_client.log
grep -q 'AIDL_LIKE_NEGATIVE_BAD_ARGS_OK' logs/android_like_aidl_negative_client.log
grep -q 'AIDL_LIKE_NEGATIVE_RECOVERY_VALID_CALL_OK' logs/android_like_aidl_negative_client.log
grep -q 'AIDL_LIKE_NEGATIVE_CLIENT_SMOKE_OK' logs/android_like_aidl_negative_client.log

echo "AIDL_LIKE_NEGATIVE_SMOKE_TV_OK"
TVSH
