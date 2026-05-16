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

rm -f logs/multi_*.log run/multi_*.pid

cleanup() {
  [ -f run/multi_a.pid ] && kill "$(cat run/multi_a.pid)" 2>/dev/null || true
  [ -f run/multi_b.pid ] && kill "$(cat run/multi_b.pid)" 2>/dev/null || true
  [ -f run/multi_sm.pid ] && kill "$(cat run/multi_sm.pid)" 2>/dev/null || true
}
trap cleanup EXIT

echo "== start mini_servicemgr =="
bin/mini_servicemgr > logs/multi_mini_servicemgr.log 2>&1 &
echo "$!" > run/multi_sm.pid
sleep 2

echo "== start service A =="
bin/echo_service test.echo.a > logs/multi_echo_a.log 2>&1 &
echo "$!" > run/multi_a.pid
sleep 2

echo "== start service B =="
bin/echo_service test.echo.b > logs/multi_echo_b.log 2>&1 &
echo "$!" > run/multi_b.pid
sleep 2

echo "== list both services =="
bin/list_services > logs/multi_list_both.log 2>&1
cat logs/multi_list_both.log

grep -q '^test.echo.a$' logs/multi_list_both.log
grep -q '^test.echo.b$' logs/multi_list_both.log

echo "== call service A =="
bin/echo_client test.echo.a "hello A" > logs/multi_client_a.log 2>&1
cat logs/multi_client_a.log
grep -q 'echo-client reply status=0' logs/multi_client_a.log

echo "== call service B =="
bin/echo_client test.echo.b "hello B" > logs/multi_client_b.log 2>&1
cat logs/multi_client_b.log
grep -q 'echo-client reply status=0' logs/multi_client_b.log

echo "== kill service A =="
kill "$(cat run/multi_a.pid)" 2>/dev/null || true
rm -f run/multi_a.pid
sleep 3

echo "== list after killing A =="
bin/list_services > logs/multi_list_after_kill_a.log 2>&1
cat logs/multi_list_after_kill_a.log

if grep -q '^test.echo.a$' logs/multi_list_after_kill_a.log; then
  echo "FAIL: dead service A is still listed"
  exit 1
fi

grep -q '^test.echo.b$' logs/multi_list_after_kill_a.log

echo "== service B should still work =="
bin/echo_client test.echo.b "B still alive" > logs/multi_client_b_after.log 2>&1
cat logs/multi_client_b_after.log
grep -q 'echo-client reply status=0' logs/multi_client_b_after.log

echo "== service A should fail cleanly =="
set +e
bin/echo_client test.echo.a "A should be gone" > logs/multi_client_a_after.log 2>&1
rc_a="$?"
set -e
cat logs/multi_client_a_after.log

if [ "$rc_a" -eq 0 ]; then
  echo "FAIL: dead service A unexpectedly succeeded"
  exit 1
fi

grep -q -E 'NOT FOUND|getService failed' logs/multi_client_a_after.log

echo "== mini_servicemgr tail =="
tail -n 160 logs/multi_mini_servicemgr.log

echo "MULTISERVICE_SMOKE_OK"
TVSH
