#!/usr/bin/env bash
set -euo pipefail

TV_IP="${TV_IP:-192.168.2.121}"
SIDE_DIR="${SIDE_DIR:-/media/internal/android-sidecar}"

ssh root@"$TV_IP" "SIDE_DIR='$SIDE_DIR' sh -s" <<'TVSH'
set -eu

cd "$SIDE_DIR"

for f in \
  bin/mini_servicemgr \
  bin/echo_service \
  bin/libbinder_lite_client \
  modules/binder.ko \
  load-binder-tv.sh
do
  if [ ! -e "$f" ]; then
    echo "ERROR: missing $SIDE_DIR/$f"
    find . -maxdepth 3 -type f -o -type d | sort
    exit 1
  fi
done

chmod +x bin/mini_servicemgr bin/echo_service bin/libbinder_lite_client load-binder-tv.sh

mkdir -p logs run

if [ ! -e /dev/binder ]; then
  ./load-binder-tv.sh "$SIDE_DIR/modules/binder.ko"
fi

killall mini_servicemgr 2>/dev/null || true
killall echo_service 2>/dev/null || true
killall libbinder_lite_client 2>/dev/null || true

rm -f logs/libbinder_lite_*.log run/libbinder_lite_*.pid

cleanup() {
  [ -f run/libbinder_lite_service.pid ] && kill "$(cat run/libbinder_lite_service.pid)" 2>/dev/null || true
  [ -f run/libbinder_lite_sm.pid ] && kill "$(cat run/libbinder_lite_sm.pid)" 2>/dev/null || true
}
trap cleanup EXIT

echo "== start mini_servicemgr =="
bin/mini_servicemgr > logs/libbinder_lite_sm.log 2>&1 &
echo "$!" > run/libbinder_lite_sm.pid
sleep 2

echo "== start echo_service test.aosp =="
bin/echo_service test.aosp > logs/libbinder_lite_echo_service.log 2>&1 &
echo "$!" > run/libbinder_lite_service.pid
sleep 3

echo "== run libbinder-lite client =="
set +e
bin/libbinder_lite_client test.aosp > logs/libbinder_lite_client.log 2>&1
client_rc="$?"
set -e

cat logs/libbinder_lite_client.log

if [ "$client_rc" -ne 0 ]; then
  echo "FAIL: libbinder_lite_client rc=$client_rc"
  echo "== mini_servicemgr log =="
  cat logs/libbinder_lite_sm.log || true
  echo "== echo_service log =="
  cat logs/libbinder_lite_echo_service.log || true
  exit "$client_rc"
fi

grep -q 'LIBBINDER_LITE_API_CLIENT_OK' logs/libbinder_lite_client.log
grep -q 'LIBBINDER_LITE_CLIENT_OK' logs/libbinder_lite_client.log

echo "LIBBINDER_LITE_CLIENT_SMOKE_OK"
TVSH
