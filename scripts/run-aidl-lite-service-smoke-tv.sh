#!/usr/bin/env bash
set -euo pipefail

TV_IP="${TV_IP:-192.168.2.121}"
SIDE_DIR="${SIDE_DIR:-/tmp/android-usb/android-sidecar}"
SERVICE="${SERVICE:-test.aidl.service}"

ssh root@"$TV_IP" "SIDE_DIR='$SIDE_DIR' SERVICE='$SERVICE' sh -s" <<'TVSH'
set -eu

cd "$SIDE_DIR"

for f in \
  bin/mini_servicemgr \
  bin/aidl_lite_echo_service \
  bin/aidl_lite_echo_client \
  modules/binder.ko \
  load-binder-tv.sh
do
  if [ ! -e "$f" ]; then
    echo "ERROR: missing $SIDE_DIR/$f"
    find . -maxdepth 3 -type f -o -type d | sort
    exit 1
  fi
done

chmod +x bin/mini_servicemgr bin/aidl_lite_echo_service bin/aidl_lite_echo_client load-binder-tv.sh

mkdir -p logs run

if [ ! -e /dev/binder ]; then
  ./load-binder-tv.sh "$SIDE_DIR/modules/binder.ko"
fi

killall mini_servicemgr 2>/dev/null || true
killall aidl_lite_echo_service 2>/dev/null || true
killall aidl_lite_echo_client 2>/dev/null || true

rm -f logs/aidl_service_*.log run/aidl_service_*.pid

cleanup() {
  [ -f run/aidl_service_service.pid ] && kill "$(cat run/aidl_service_service.pid)" 2>/dev/null || true
  [ -f run/aidl_service_sm.pid ] && kill "$(cat run/aidl_service_sm.pid)" 2>/dev/null || true
}
trap cleanup EXIT

echo "== start mini_servicemgr =="
bin/mini_servicemgr > logs/aidl_service_sm.log 2>&1 &
echo "$!" > run/aidl_service_sm.pid
sleep 2

echo "== start AIDL-lite echo service $SERVICE =="
bin/aidl_lite_echo_service "$SERVICE" > logs/aidl_service_service.log 2>&1 &
echo "$!" > run/aidl_service_service.pid
sleep 3

echo "== run AIDL-lite echo client against C++ service =="
set +e
bin/aidl_lite_echo_client "$SERVICE" "hello C++ BnEchoService" > logs/aidl_service_client.log 2>&1
client_rc="$?"
set -e

cat logs/aidl_service_client.log

if [ "$client_rc" -ne 0 ]; then
  echo "FAIL: aidl_lite_echo_client rc=$client_rc"
  echo "== mini_servicemgr log =="
  cat logs/aidl_service_sm.log || true
  echo "== aidl_lite_echo_service log =="
  cat logs/aidl_service_service.log || true
  exit "$client_rc"
fi

grep -q 'AIDL_LITE_ECHO_CLIENT_OK' logs/aidl_service_client.log

echo "AIDL_LITE_SERVICE_SMOKE_OK"
TVSH
