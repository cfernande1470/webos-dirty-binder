#!/usr/bin/env bash
set -euo pipefail

TV_IP="${TV_IP:-192.168.2.121}"
SIDE_DIR="${SIDE_DIR:-/media/internal/android-sidecar}"
CLIENTS="${CLIENTS:-8}"
ROUNDS="${ROUNDS:-10}"

ssh root@"$TV_IP" "SIDE_DIR='$SIDE_DIR' CLIENTS='$CLIENTS' ROUNDS='$ROUNDS' sh -s" <<'TVSH'
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

rm -f logs/stress_*.log run/stress_*.pid run/stress_pids.*

cleanup() {
  [ -f run/stress_service.pid ] && kill "$(cat run/stress_service.pid)" 2>/dev/null || true
  [ -f run/stress_sm.pid ] && kill "$(cat run/stress_sm.pid)" 2>/dev/null || true
}
trap cleanup EXIT

echo "== start mini_servicemgr =="
bin/mini_servicemgr > logs/stress_mini_servicemgr.log 2>&1 &
echo "$!" > run/stress_sm.pid
sleep 2

echo "== start echo_service test.stress =="
bin/echo_service test.stress > logs/stress_echo_service.log 2>&1 &
echo "$!" > run/stress_service.pid
sleep 3

echo "== warmup list =="
bin/list_services > logs/stress_list_before.log 2>&1
cat logs/stress_list_before.log
grep -q '^test.stress$' logs/stress_list_before.log

echo "== warmup client =="
bin/echo_client test.stress "warmup" > logs/stress_warmup_client.log 2>&1
cat logs/stress_warmup_client.log
grep -q 'echo-client reply status=0' logs/stress_warmup_client.log

echo "== stress: CLIENTS=$CLIENTS ROUNDS=$ROUNDS =="
round=1
fail=0
total=0

while [ "$round" -le "$ROUNDS" ]; do
  echo "-- round $round --"
  rm -f "run/stress_pids.$round"

  client=1
  while [ "$client" -le "$CLIENTS" ]; do
    log="logs/stress_r${round}_c${client}.log"
    bin/echo_client test.stress "round=$round client=$client" > "$log" 2>&1 &
    pid="$!"
    echo "$pid $log" >> "run/stress_pids.$round"
    total=$((total + 1))
    client=$((client + 1))
  done

  while read pid log; do
    if ! wait "$pid"; then
      echo "FAIL: pid=$pid log=$log exited non-zero"
      fail=$((fail + 1))
      cat "$log" || true
      continue
    fi

    if ! grep -q 'echo-client reply status=0' "$log"; then
      echo "FAIL: missing successful reply in $log"
      fail=$((fail + 1))
      cat "$log" || true
    fi
  done < "run/stress_pids.$round"

  round=$((round + 1))
done

echo "== post-stress list =="
bin/list_services > logs/stress_list_after.log 2>&1
cat logs/stress_list_after.log
grep -q '^test.stress$' logs/stress_list_after.log

echo "== final client =="
bin/echo_client test.stress "final" > logs/stress_final_client.log 2>&1
cat logs/stress_final_client.log
grep -q 'echo-client reply status=0' logs/stress_final_client.log

echo "== mini_servicemgr tail =="
tail -n 120 logs/stress_mini_servicemgr.log || true

echo "== echo_service tail =="
tail -n 80 logs/stress_echo_service.log || true

echo "== relevant dmesg tail =="
dmesg | tail -n 180 | grep -i -E 'binder|oops|panic|fault|unable|segv|dead' || true

echo "TOTAL_CALLS=$total"
echo "FAILURES=$fail"

if [ "$fail" -ne 0 ]; then
  echo "STRESS_SMOKE_FAIL"
  exit 1
fi

echo "STRESS_SMOKE_OK"
TVSH
