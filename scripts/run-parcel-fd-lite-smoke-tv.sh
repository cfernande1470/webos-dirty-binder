#!/usr/bin/env bash
set -euo pipefail

TV_IP="${TV_IP:-192.168.2.121}"
SIDE_DIR="${SIDE_DIR:-/tmp/android-usb/android-sidecar}"
SERVICE="${SERVICE:-test.android.parcelfd}"
SOCKET_PATH="${SOCKET_PATH:-$SIDE_DIR/run/parcel_fd_lite.sock}"

ssh root@"$TV_IP" "SIDE_DIR='$SIDE_DIR' SERVICE='$SERVICE' SOCKET_PATH='$SOCKET_PATH' sh -s" <<'TVSH'
set -eu

cd "$SIDE_DIR"

for f in \
  bin/mini_servicemgr \
  bin/parcel_fd_lite_service \
  bin/parcel_fd_lite_client \
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
  bin/parcel_fd_lite_service \
  bin/parcel_fd_lite_client \
  load-binder-tv.sh

mkdir -p logs run

if [ ! -e /dev/binder ]; then
  ./load-binder-tv.sh "$SIDE_DIR/modules/binder.ko"
fi

killall mini_servicemgr 2>/dev/null || true
killall parcel_fd_lite_service 2>/dev/null || true
killall parcel_fd_lite_client 2>/dev/null || true

rm -f "$SOCKET_PATH"
rm -f logs/parcel_fd_lite_*.log run/parcel_fd_lite_*.pid

cleanup() {
  [ -f run/parcel_fd_lite_service.pid ] && kill "$(cat run/parcel_fd_lite_service.pid)" 2>/dev/null || true
  [ -f run/parcel_fd_lite_sm.pid ] && kill "$(cat run/parcel_fd_lite_sm.pid)" 2>/dev/null || true
  rm -f "$SOCKET_PATH"
}

trap cleanup EXIT

echo "== start mini_servicemgr =="
bin/mini_servicemgr > logs/parcel_fd_lite_sm.log 2>&1 &
echo "$!" > run/parcel_fd_lite_sm.pid

sleep 2

echo "== start parcel fd lite service =="
bin/parcel_fd_lite_service "$SERVICE" "$SOCKET_PATH" > logs/parcel_fd_lite_service.log 2>&1 &
echo "$!" > run/parcel_fd_lite_service.pid

sleep 3

echo "== run parcel fd lite client =="
set +e
bin/parcel_fd_lite_client "$SERVICE" "$SOCKET_PATH" > logs/parcel_fd_lite_client.log 2>&1
client_rc="$?"
set -e

echo "== parcel fd lite client log =="
cat logs/parcel_fd_lite_client.log || true

echo "== parcel fd lite service log =="
cat logs/parcel_fd_lite_service.log || true

echo "== mini_servicemgr log tail =="
tail -160 logs/parcel_fd_lite_sm.log || true

if [ "$client_rc" -ne 0 ]; then
  echo "FAIL: parcel_fd_lite_client rc=$client_rc"
  exit "$client_rc"
fi

grep -q 'PARCELFD_LITE_SERVICE_SOCKET_READY' logs/parcel_fd_lite_service.log
grep -q 'PARCELFD_LITE_SERVICE_REGISTERED' logs/parcel_fd_lite_service.log
grep -q 'PARCELFD_LITE_WRITE_FD_OK' logs/parcel_fd_lite_client.log
grep -q 'PARCELFD_LITE_TOKEN_ENCODE_OK' logs/parcel_fd_lite_client.log
grep -q 'PARCELFD_LITE_SOCKET_SEND_OK' logs/parcel_fd_lite_client.log
grep -q 'PARCELFD_LITE_BINDER_CONTROL_OK' logs/parcel_fd_lite_service.log
grep -q 'PARCELFD_LITE_READ_FD_OK' logs/parcel_fd_lite_service.log
grep -q 'PARCELFD_LITE_PAYLOAD_READ_OK' logs/parcel_fd_lite_service.log
grep -q 'PARCELFD_LITE_CLIENT_BINDER_REPLY_OK' logs/parcel_fd_lite_client.log
grep -q 'PARCELFD_LITE_SMOKE_OK' logs/parcel_fd_lite_client.log

echo "PARCELFD_LITE_SMOKE_TV_OK"
TVSH
