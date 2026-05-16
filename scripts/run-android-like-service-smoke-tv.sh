#!/usr/bin/env bash
set -euo pipefail

TV_IP="${TV_IP:-192.168.2.121}"
SIDE_DIR="${SIDE_DIR:-/media/internal/android-sidecar}"
SERVICE="${SERVICE:-test.android.service}"

ssh root@"$TV_IP" "SIDE_DIR='$SIDE_DIR' SERVICE='$SERVICE' sh -s" <<'TVSH'
set -eu

cd "$SIDE_DIR"

for f in \
  bin/mini_servicemgr \
  bin/android_like_echo_service \
  bin/android_like_echo_client \
  modules/binder.ko \
  load-binder-tv.sh
do
  if [ ! -e "$f" ]; then
    echo "ERROR: missing $SIDE_DIR/$f"
    find . -maxdepth 3 -type f -o -type d | sort
    exit 1
  fi
done

chmod +x bin/mini_servicemgr bin/android_like_echo_service bin/android_like_echo_client load-binder-tv.sh

mkdir -p logs run

if [ ! -e /dev/binder ]; then
  ./load-binder-tv.sh "$SIDE_DIR/modules/binder.ko"
fi

killall mini_servicemgr 2>/dev/null || true
killall android_like_echo_service 2>/dev/null || true
killall android_like_echo_client 2>/dev/null || true

rm -f logs/android_like_service_*.log run/android_like_service_*.pid

cleanup() {
  [ -f run/android_like_service.pid ] && kill "$(cat run/android_like_service.pid)" 2>/dev/null || true
  [ -f run/android_like_sm.pid ] && kill "$(cat run/android_like_sm.pid)" 2>/dev/null || true
}
trap cleanup EXIT

echo "== start mini_servicemgr =="
bin/mini_servicemgr > logs/android_like_service_sm.log 2>&1 &
echo "$!" > run/android_like_sm.pid
sleep 2

echo "== start Android-like service $SERVICE =="
bin/android_like_echo_service "$SERVICE" > logs/android_like_service_server.log 2>&1 &
echo "$!" > run/android_like_service.pid
sleep 3

echo "== run Android-like API client against Android-like service =="
set +e
bin/android_like_echo_client "$SERVICE" "hello Android-like service" > logs/android_like_service_client.log 2>&1
client_rc="$?"
set -e

cat logs/android_like_service_client.log

if [ "$client_rc" -ne 0 ]; then
  echo "FAIL: android_like_echo_client rc=$client_rc"
  echo "== mini_servicemgr log =="
  cat logs/android_like_service_sm.log || true
  echo "== android_like_echo_service log =="
  cat logs/android_like_service_server.log || true
  exit "$client_rc"
fi

grep -q 'ANDROID_LIKE_INTERFACE_CONTRACT_OK' logs/android_like_service_client.log
grep -q 'ANDROID_LIKE_AIDL_WIRE_OK' logs/android_like_client.log
grep -q 'ANDROID_LIKE_API_CLIENT_OK' logs/android_like_service_client.log

echo "== Android-like service log markers =="
cat logs/android_like_service_server.log || true

grep -q 'ANDROID_LIKE_SERVICE_REGISTERED' logs/android_like_service_server.log
grep -q 'ANDROID_LIKE_SERVICE_OK' logs/android_like_service_server.log

echo "ANDROID_LIKE_SERVICE_SMOKE_OK"
TVSH
