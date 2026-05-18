#!/usr/bin/env bash
set -euo pipefail

TV_IP="${TV_IP:-192.168.2.121}"
SIDE_DIR="${SIDE_DIR:-/tmp/android-usb/android-sidecar}"
SERVICE="${SERVICE:-test.android.aidl.callback}"

ssh root@"$TV_IP" "SIDE_DIR='$SIDE_DIR' SERVICE='$SERVICE' sh -s" <<'TVSH'
set -eu

cd "$SIDE_DIR"

for f in \
  bin/mini_servicemgr \
  bin/android_like_aidl_callback_service \
  bin/android_like_aidl_callback_threadpool_client \
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
  bin/android_like_aidl_callback_service \
  bin/android_like_aidl_callback_threadpool_client \
  load-binder-tv.sh

mkdir -p logs run

if [ ! -e /dev/binder ]; then
  ./load-binder-tv.sh "$SIDE_DIR/modules/binder.ko"
fi

killall mini_servicemgr 2>/dev/null || true
killall android_like_aidl_callback_service 2>/dev/null || true
killall android_like_aidl_callback_threadpool_client 2>/dev/null || true

rm -f logs/android_like_aidl_callback_*.log run/android_like_aidl_callback_*.pid

cleanup() {
  [ -f run/android_like_aidl_callback_service.pid ] && kill "$(cat run/android_like_aidl_callback_service.pid)" 2>/dev/null || true
  [ -f run/android_like_aidl_callback_sm.pid ] && kill "$(cat run/android_like_aidl_callback_sm.pid)" 2>/dev/null || true
}

trap cleanup EXIT

echo "== aidl callback listener config =="
echo "SERVICE=$SERVICE"

echo "== start mini_servicemgr =="
bin/mini_servicemgr > logs/android_like_aidl_callback_sm.log 2>&1 &
echo "$!" > run/android_like_aidl_callback_sm.pid

sleep 2

echo "== start aidl callback service =="
bin/android_like_aidl_callback_service "$SERVICE" > logs/android_like_aidl_callback_service.log 2>&1 &
echo "$!" > run/android_like_aidl_callback_service.pid

sleep 3

echo "== run aidl callback threadpool client =="
set +e
bin/android_like_aidl_callback_threadpool_client "$SERVICE" > logs/android_like_aidl_callback_client.log 2>&1
client_rc="$?"
set -e

echo "== client markers =="
grep 'AIDL_LIKE_CALLBACK_LISTENER' logs/android_like_aidl_callback_client.log || true

echo "== service markers =="
grep 'AIDL_LIKE_CALLBACK_LISTENER' logs/android_like_aidl_callback_service.log || true

if [ "$client_rc" -ne 0 ]; then
  echo "FAIL: android_like_aidl_callback_threadpool_client rc=$client_rc"
  echo "== client log =="
  cat logs/android_like_aidl_callback_client.log || true
  echo "== service log =="
  cat logs/android_like_aidl_callback_service.log || true
  echo "== servicemgr log =="
  cat logs/android_like_aidl_callback_sm.log || true
  exit "$client_rc"
fi

grep -q 'AIDL_LIKE_CALLBACK_LISTENER_SERVICE_REGISTERED' logs/android_like_aidl_callback_service.log
grep -q 'AIDL_LIKE_CALLBACK_LISTENER_HANDLE_OK' logs/android_like_aidl_callback_service.log
grep -q 'AIDL_LIKE_CALLBACK_LISTENER_REPLY_OK' logs/android_like_aidl_callback_service.log
grep -q 'AIDL_LIKE_CALLBACK_LISTENER_LOOPER_READY' logs/android_like_aidl_callback_client.log
grep -q 'AIDL_LIKE_CALLBACK_LISTENER_THREAD_OK' logs/android_like_aidl_callback_client.log
grep -q 'AIDL_LIKE_CALLBACK_LISTENER_ONEWAY_REGISTER_SENT' logs/android_like_aidl_callback_client.log
grep -q 'AIDL_LIKE_CALLBACK_LISTENER_MAIN_OBSERVED_OK' logs/android_like_aidl_callback_client.log
grep -q 'AIDL_LIKE_CALLBACK_LISTENER_SMOKE_OK' logs/android_like_aidl_callback_client.log

if grep -q 'AIDL_LIKE_CALLBACK_LISTENER_MAIN_GOT_CALLBACK_FAIL' logs/android_like_aidl_callback_client.log; then
  echo "FAIL: callback landed on main thread"
  cat logs/android_like_aidl_callback_client.log || true
  exit 1
fi

echo "AIDL_LIKE_CALLBACK_LISTENER_SMOKE_TV_OK"
TVSH
