#!/usr/bin/env bash
set -euo pipefail

TV_IP="${TV_IP:-192.168.2.121}"
SIDE_DIR="${SIDE_DIR:-/tmp/android-usb/android-sidecar}"
SERVICE="${SERVICE:-test.ctxrestart}"

ssh root@"$TV_IP" "SIDE_DIR='$SIDE_DIR' SERVICE='$SERVICE' sh -s" <<'TVSH'
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

rm -f logs/ctx_*.log run/ctx_*.pid

cleanup() {
  [ -f run/ctx_service1.pid ] && kill "$(cat run/ctx_service1.pid)" 2>/dev/null || true
  [ -f run/ctx_service2.pid ] && kill "$(cat run/ctx_service2.pid)" 2>/dev/null || true
  [ -f run/ctx_sm1.pid ] && kill "$(cat run/ctx_sm1.pid)" 2>/dev/null || true
  [ -f run/ctx_sm2.pid ] && kill "$(cat run/ctx_sm2.pid)" 2>/dev/null || true
}
trap cleanup EXIT

run_client_limited() {
  label="$1"
  log="$2"

  bin/echo_client "$SERVICE" "$label" > "$log" 2>&1 &
  pid="$!"

  i=0
  while kill -0 "$pid" 2>/dev/null; do
    i=$((i + 1))
    if [ "$i" -ge 10 ]; then
      echo "client timeout, killing pid=$pid" >> "$log"
      kill "$pid" 2>/dev/null || true
      wait "$pid" 2>/dev/null || true
      return 124
    fi
    sleep 1
  done

  wait "$pid"
}

wait_list_contains() {
  name="$1"
  tries=20

  while [ "$tries" -gt 0 ]; do
    bin/list_services > logs/ctx_list_probe.log 2>&1 || true
    if grep -q "^$name$" logs/ctx_list_probe.log; then
      return 0
    fi
    tries=$((tries - 1))
    sleep 1
  done

  echo "FAIL: service did not appear in listServices: $name"
  cat logs/ctx_list_probe.log || true
  return 1
}

echo "== start first mini_servicemgr =="
bin/mini_servicemgr > logs/ctx_sm1.log 2>&1 &
echo "$!" > run/ctx_sm1.pid
sleep 2

echo "== start first service =="
bin/echo_service "$SERVICE" > logs/ctx_service1.log 2>&1 &
echo "$!" > run/ctx_service1.pid
sleep 3

wait_list_contains "$SERVICE"

echo "== first client should succeed =="
run_client_limited "before context manager death" logs/ctx_client_before.log
cat logs/ctx_client_before.log
grep -q 'echo-client reply status=0' logs/ctx_client_before.log

echo "== kill first mini_servicemgr =="
kill "$(cat run/ctx_sm1.pid)" 2>/dev/null || true
rm -f run/ctx_sm1.pid
sleep 3

echo "== client should fail while context manager is gone =="
set +e
run_client_limited "while context manager is gone" logs/ctx_client_without_sm.log
without_sm_rc="$?"
set -e
cat logs/ctx_client_without_sm.log || true

if [ "$without_sm_rc" -eq 0 ]; then
  echo "FAIL: client unexpectedly succeeded without context manager"
  exit 1
fi

echo "client_without_sm_rc=$without_sm_rc"

echo "== kill old service =="
kill "$(cat run/ctx_service1.pid)" 2>/dev/null || true
rm -f run/ctx_service1.pid
sleep 2

echo "== start second mini_servicemgr =="
bin/mini_servicemgr > logs/ctx_sm2.log 2>&1 &
echo "$!" > run/ctx_sm2.pid
sleep 2

echo "== start second service =="
bin/echo_service "$SERVICE" > logs/ctx_service2.log 2>&1 &
echo "$!" > run/ctx_service2.pid
sleep 3

wait_list_contains "$SERVICE"

echo "== second client should succeed =="
run_client_limited "after context manager restart" logs/ctx_client_after.log
cat logs/ctx_client_after.log
grep -q 'echo-client reply status=0' logs/ctx_client_after.log

echo "== list after restart =="
bin/list_services > logs/ctx_list_after.log 2>&1
cat logs/ctx_list_after.log
grep -q "^$SERVICE$" logs/ctx_list_after.log

echo "== first mini_servicemgr log tail =="
tail -n 120 logs/ctx_sm1.log || true

echo "== second mini_servicemgr log tail =="
tail -n 120 logs/ctx_sm2.log || true

echo "== relevant dmesg tail =="
dmesg | tail -n 180 | grep -i -E 'binder|oops|panic|fault|unable|segv|dead' || true

echo "CONTEXT_RESTART_SMOKE_OK"
TVSH
