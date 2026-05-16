#!/usr/bin/env bash
set -euo pipefail

TV_IP="${TV_IP:-192.168.2.121}"
SIDE_DIR="${SIDE_DIR:-/media/internal/android-sidecar}"
SERVICE="${SERVICE:-test.duplicate}"

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

rm -f logs/dup_*.log run/dup_*.pid

cleanup() {
  [ -f run/dup_service_a.pid ] && kill "$(cat run/dup_service_a.pid)" 2>/dev/null || true
  [ -f run/dup_service_b.pid ] && kill "$(cat run/dup_service_b.pid)" 2>/dev/null || true
  [ -f run/dup_sm.pid ] && kill "$(cat run/dup_sm.pid)" 2>/dev/null || true
}
trap cleanup EXIT

count_service() {
  log="$1"
  grep -x "$SERVICE" "$log" 2>/dev/null | wc -l | tr -d ' '
}

list_and_count() {
  label="$1"
  log="logs/dup_list_${label}.log"

  bin/list_services > "$log" 2>&1
  cat "$log" >&2

  count_service "$log"
}

wait_count() {
  expected="$1"
  label="$2"
  tries=20

  while [ "$tries" -gt 0 ]; do
    n="$(list_and_count "$label")"
    if [ "$n" = "$expected" ]; then
      return 0
    fi
    tries=$((tries - 1))
    sleep 1
  done

  echo "FAIL: expected $expected entries for $SERVICE, got $n"
  return 1
}

echo "== start mini_servicemgr =="
bin/mini_servicemgr > logs/dup_mini_servicemgr.log 2>&1 &
echo "$!" > run/dup_sm.pid
sleep 2

echo "== start first service =="
bin/echo_service "$SERVICE" > logs/dup_service_a.log 2>&1 &
echo "$!" > run/dup_service_a.pid
sleep 3

echo "== list after first registration =="
n="$(list_and_count after_first)"
if [ "$n" != "1" ]; then
  echo "FAIL: expected exactly one service after first registration, got $n"
  exit 1
fi

echo "== first client should succeed =="
bin/echo_client "$SERVICE" "first registration" > logs/dup_client_first.log 2>&1
cat logs/dup_client_first.log
grep -q 'echo-client reply status=0' logs/dup_client_first.log

echo "== start second service with same name =="
bin/echo_service "$SERVICE" > logs/dup_service_b.log 2>&1 &
echo "$!" > run/dup_service_b.pid
sleep 3

echo "== list after duplicate registration =="
n="$(list_and_count after_duplicate)"
if [ "$n" != "1" ]; then
  echo "FAIL: duplicate service name produced $n registry entries"
  echo "== mini_servicemgr log =="
  cat logs/dup_mini_servicemgr.log || true
  echo "== first service log =="
  cat logs/dup_service_a.log || true
  echo "== second service log =="
  cat logs/dup_service_b.log || true
  exit 1
fi

echo "== client after duplicate should still succeed =="
bin/echo_client "$SERVICE" "after duplicate registration" > logs/dup_client_after_duplicate.log 2>&1
cat logs/dup_client_after_duplicate.log
grep -q 'echo-client reply status=0' logs/dup_client_after_duplicate.log

echo "== kill first service =="
kill "$(cat run/dup_service_a.pid)" 2>/dev/null || true
rm -f run/dup_service_a.pid
sleep 3

echo "== list after killing first service: replacement should remain =="
bin/list_services > logs/dup_list_after_kill_a.log 2>&1
cat logs/dup_list_after_kill_a.log

n="$(count_service logs/dup_list_after_kill_a.log)"
if [ "$n" != "1" ]; then
  echo "FAIL: replacement service should remain after old service death, got count=$n"
  echo "== mini_servicemgr log =="
  cat logs/dup_mini_servicemgr.log || true
  exit 1
fi

echo "== client after killing first service should still succeed =="
bin/echo_client "$SERVICE" "after killing first service" > logs/dup_client_after_kill_a.log 2>&1
cat logs/dup_client_after_kill_a.log
grep -q 'echo-client reply status=0' logs/dup_client_after_kill_a.log

echo "== kill second service =="
kill "$(cat run/dup_service_b.pid)" 2>/dev/null || true
rm -f run/dup_service_b.pid
sleep 3

echo "== final list should not contain service =="
bin/list_services > logs/dup_list_final.log 2>&1
cat logs/dup_list_final.log

if grep -q "^$SERVICE$" logs/dup_list_final.log; then
  echo "FAIL: service still listed after both service processes died"
  exit 1
fi

echo "== final client should fail cleanly =="
set +e
bin/echo_client "$SERVICE" "final should fail" > logs/dup_client_final.log 2>&1
final_rc="$?"
set -e
cat logs/dup_client_final.log

if [ "$final_rc" -eq 0 ]; then
  echo "FAIL: final client unexpectedly succeeded"
  exit 1
fi

grep -q -E 'NOT FOUND|getService failed|BR_DEAD_REPLY|BR_FAILED_REPLY' logs/dup_client_final.log

echo "== mini_servicemgr tail =="
tail -n 200 logs/dup_mini_servicemgr.log || true

echo "== relevant dmesg tail =="
dmesg | tail -n 180 | grep -i -E 'binder|oops|panic|fault|unable|segv|dead' || true

echo "DUPLICATE_SMOKE_OK"
TVSH
