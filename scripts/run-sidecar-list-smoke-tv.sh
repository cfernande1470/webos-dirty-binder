#!/usr/bin/env bash
set -euo pipefail

TV_IP="${TV_IP:-192.168.2.121}"
SIDE_DIR="${SIDE_DIR:-/tmp/android-usb/android-sidecar}"

ssh root@"$TV_IP" "SIDE_DIR='$SIDE_DIR' sh -s" <<'TVSH'
set -eu

cd "$SIDE_DIR"

for f in \
  bin/mini_servicemgr \
  bin/echo_service \
  bin/echo_client \
  bin/list_services \
  modules/binder.ko \
  load-binder-tv.sh
do
  if [ ! -e "$f" ]; then
    echo "ERROR: missing $SIDE_DIR/$f"
    find . -maxdepth 3 -type f -o -type d | sort
    exit 1
  fi
done

chmod +x bin/mini_servicemgr bin/echo_service bin/echo_client bin/list_services load-binder-tv.sh

mkdir -p logs run

if [ ! -e /dev/binder ]; then
  ./load-binder-tv.sh "$SIDE_DIR/modules/binder.ko"
fi

killall mini_servicemgr 2>/dev/null || true
killall echo_service 2>/dev/null || true
killall echo_client 2>/dev/null || true
killall list_services 2>/dev/null || true

rm -f logs/list_*.log run/list_*.pid

cleanup() {
  [ -f run/list_echo.pid ] && kill "$(cat run/list_echo.pid)" 2>/dev/null || true
  [ -f run/list_sm.pid ] && kill "$(cat run/list_sm.pid)" 2>/dev/null || true
}
trap cleanup EXIT

echo "== start mini_servicemgr =="
bin/mini_servicemgr > logs/list_mini_servicemgr.log 2>&1 &
echo "$!" > run/list_sm.pid
sleep 2

echo "== list before services =="
bin/list_services > logs/list_before.log 2>&1 || true
cat logs/list_before.log

echo "== start echo_service test.echo =="
bin/echo_service test.echo > logs/list_echo_service.log 2>&1 &
echo "$!" > run/list_echo.pid
sleep 3

echo "== list after service =="
bin/list_services > logs/list_after.log 2>&1
cat logs/list_after.log

echo "== verify echo still works =="
bin/echo_client test.echo "hello after listServices" > logs/list_echo_client.log 2>&1
cat logs/list_echo_client.log

echo "== mini_servicemgr log =="
cat logs/list_mini_servicemgr.log

if ! grep -q '^test.echo$' logs/list_after.log; then
  echo "FAIL: listServices did not print test.echo"
  exit 1
fi

if ! grep -q 'echo-client reply status=0' logs/list_echo_client.log; then
  echo "FAIL: echo client did not succeed after listServices"
  exit 1
fi

echo "LIST_SMOKE_OK"
TVSH
