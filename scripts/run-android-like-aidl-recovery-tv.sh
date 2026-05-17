#!/usr/bin/env bash
set -euo pipefail

TV_IP="${TV_IP:-192.168.2.121}"
SIDE_DIR="${SIDE_DIR:-/media/internal/android-sidecar}"
SERVICE="${SERVICE:-test.android.aidl.recovery}"
CYCLES="${CYCLES:-5}"
ROUNDS="${ROUNDS:-10}"

ssh root@"$TV_IP" "SIDE_DIR='$SIDE_DIR' SERVICE='$SERVICE' CYCLES='$CYCLES' ROUNDS='$ROUNDS' sh -s" <<'TVSH'
set -eu

cd "$SIDE_DIR"

for f in \
  bin/mini_servicemgr \
  bin/android_like_aidl_service \
  bin/android_like_aidl_client \
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
  bin/android_like_aidl_service \
  bin/android_like_aidl_client \
  load-binder-tv.sh

mkdir -p logs run

if [ ! -e /dev/binder ]; then
  ./load-binder-tv.sh "$SIDE_DIR/modules/binder.ko"
fi

killall mini_servicemgr 2>/dev/null || true
killall android_like_aidl_service 2>/dev/null || true
killall android_like_aidl_client 2>/dev/null || true

rm -f logs/android_like_aidl_recovery_*.log run/android_like_aidl_recovery_*.pid

cleanup() {
  [ -f run/android_like_aidl_recovery_service.pid ] && kill "$(cat run/android_like_aidl_recovery_service.pid)" 2>/dev/null || true
  [ -f run/android_like_aidl_recovery_sm.pid ] && kill "$(cat run/android_like_aidl_recovery_sm.pid)" 2>/dev/null || true
}

trap cleanup EXIT

start_service() {
  cycle="$1"

  echo "== start aidl-like service cycle=$cycle service=$SERVICE =="
  bin/android_like_aidl_service "$SERVICE" >> logs/android_like_aidl_recovery_service.log 2>&1 &
  echo "$!" > run/android_like_aidl_recovery_service.pid

  sleep 3
}

run_client() {
  cycle="$1"
  log="logs/android_like_aidl_recovery_client_${cycle}.log"

  echo "== run aidl-like client cycle=$cycle rounds=$ROUNDS =="

  set +e
  bin/android_like_aidl_client "$SERVICE" "$ROUNDS" > "$log" 2>&1
  rc="$?"
  set -e

  echo "--- client cycle=$cycle markers ---"
  grep 'AIDL_LIKE_' "$log" || true

  if [ "$rc" -ne 0 ]; then
    echo "FAIL: client cycle=$cycle rc=$rc"
    echo "== client log =="
    cat "$log" || true
    echo "== service log =="
    cat logs/android_like_aidl_recovery_service.log || true
    echo "== servicemgr log =="
    cat logs/android_like_aidl_recovery_sm.log || true
    exit "$rc"
  fi

  grep -q 'AIDL_LIKE_EXCEPTION_CODE_OK' "$log"
  grep -q 'AIDL_LIKE_ECHO_OK' "$log"
  grep -q 'AIDL_LIKE_ADD_OK' "$log"
  grep -q 'AIDL_LIKE_CLIENT_SMOKE_OK' "$log"

  echo "AIDL_LIKE_RECOVERY_CLIENT_OK cycle=$cycle"
}

kill_service_and_wait_for_death() {
  cycle="$1"

  old_death_count="$(grep -c "service died name=$SERVICE" logs/android_like_aidl_recovery_sm.log || true)"

  svc_pid="$(cat run/android_like_aidl_recovery_service.pid)"
  echo "== kill aidl-like service cycle=$cycle pid=$svc_pid =="

  kill "$svc_pid" 2>/dev/null || true

  set +e
  wait "$svc_pid"
  set -e

  sleep 1

  detected=0
  i=0
  while [ "$i" -lt 30 ]; do
    new_death_count="$(grep -c "service died name=$SERVICE" logs/android_like_aidl_recovery_sm.log || true)"

    if [ "$new_death_count" -gt "$old_death_count" ]; then
      detected=1
      break
    fi

    sleep 1
    i=$((i + 1))
  done

  if [ "$detected" -ne 1 ]; then
    echo "FAIL: ServiceManager did not report service death for $SERVICE"
    echo "== servicemgr log =="
    cat logs/android_like_aidl_recovery_sm.log || true
    exit 1
  fi

  echo "AIDL_LIKE_RECOVERY_DEATH_DETECTED cycle=$cycle"
}

echo "== aidl recovery config =="
echo "SERVICE=$SERVICE"
echo "CYCLES=$CYCLES"
echo "ROUNDS=$ROUNDS"

echo "== start mini_servicemgr =="
bin/mini_servicemgr > logs/android_like_aidl_recovery_sm.log 2>&1 &
echo "$!" > run/android_like_aidl_recovery_sm.pid

sleep 2

start_service 0
run_client 0

echo "AIDL_LIKE_RECOVERY_INITIAL_OK"

cycle=1
while [ "$cycle" -le "$CYCLES" ]; do
  kill_service_and_wait_for_death "$cycle"

  start_service "$cycle"

  registered_count="$(grep -c 'AIDL_LIKE_SERVICE_REGISTERED' logs/android_like_aidl_recovery_service.log || true)"

  if [ "$registered_count" -lt "$((cycle + 1))" ]; then
    echo "FAIL: service did not re-register enough times"
    echo "registered_count=$registered_count expected_at_least=$((cycle + 1))"
    cat logs/android_like_aidl_recovery_service.log || true
    exit 1
  fi

  echo "AIDL_LIKE_RECOVERY_REREGISTER_OK cycle=$cycle"

  run_client "$cycle"

  cycle=$((cycle + 1))
done

expected_clients="$((CYCLES + 1))"
client_smoke_count=0

for log in logs/android_like_aidl_recovery_client_*.log; do
  [ -e "$log" ] || continue
  c="$(grep -c 'AIDL_LIKE_CLIENT_SMOKE_OK' "$log" || true)"
  client_smoke_count="$((client_smoke_count + c))"
done

death_count="$(grep -c "service died name=$SERVICE" logs/android_like_aidl_recovery_sm.log || true)"
registered_count="$(grep -c 'AIDL_LIKE_SERVICE_REGISTERED' logs/android_like_aidl_recovery_service.log || true)"

echo "== recovery counts =="
echo "expected_clients=$expected_clients"
echo "client_smoke_count=$client_smoke_count"
echo "death_count=$death_count"
echo "registered_count=$registered_count"

if [ "$client_smoke_count" -lt "$expected_clients" ]; then
  echo "FAIL: client smoke count too low"
  exit 1
fi

if [ "$death_count" -lt "$CYCLES" ]; then
  echo "FAIL: death count too low"
  cat logs/android_like_aidl_recovery_sm.log || true
  exit 1
fi

if [ "$registered_count" -lt "$expected_clients" ]; then
  echo "FAIL: service registration count too low"
  cat logs/android_like_aidl_recovery_service.log || true
  exit 1
fi

echo "== final recovery markers =="
grep 'AIDL_LIKE_RECOVERY_' logs/android_like_aidl_recovery_client_*.log 2>/dev/null || true
grep 'AIDL_LIKE_SERVICE_REGISTERED' logs/android_like_aidl_recovery_service.log || true
grep "service died name=$SERVICE" logs/android_like_aidl_recovery_sm.log || true

echo "AIDL_LIKE_RECOVERY_SMOKE_TV_OK"
TVSH
