#!/usr/bin/env bash
set -euo pipefail

TV_IP="${TV_IP:-192.168.2.121}"
SIDE_DIR="${SIDE_DIR:-/media/internal/android-sidecar}"
SERVICE="${SERVICE:-test.android.aidl.stress}"
CLIENTS="${CLIENTS:-16}"
ROUNDS="${ROUNDS:-50}"

ssh root@"$TV_IP" "SIDE_DIR='$SIDE_DIR' SERVICE='$SERVICE' CLIENTS='$CLIENTS' ROUNDS='$ROUNDS' sh -s" <<'TVSH'
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

rm -f logs/android_like_aidl_stress_*.log run/android_like_aidl_stress_*.pid

cleanup() {
  for pidfile in run/android_like_aidl_stress_client_*.pid; do
    [ -e "$pidfile" ] || continue
    kill "$(cat "$pidfile")" 2>/dev/null || true
  done

  [ -f run/android_like_aidl_stress_service.pid ] && kill "$(cat run/android_like_aidl_stress_service.pid)" 2>/dev/null || true
  [ -f run/android_like_aidl_stress_sm.pid ] && kill "$(cat run/android_like_aidl_stress_sm.pid)" 2>/dev/null || true
}

trap cleanup EXIT

echo "== aidl stress config =="
echo "SERVICE=$SERVICE"
echo "CLIENTS=$CLIENTS"
echo "ROUNDS=$ROUNDS"

echo "== start mini_servicemgr =="
bin/mini_servicemgr > logs/android_like_aidl_stress_sm.log 2>&1 &
echo "$!" > run/android_like_aidl_stress_sm.pid

sleep 2

echo "== start aidl-like service $SERVICE =="
bin/android_like_aidl_service "$SERVICE" > logs/android_like_aidl_stress_service.log 2>&1 &
echo "$!" > run/android_like_aidl_stress_service.pid

sleep 3

echo "== launch $CLIENTS concurrent aidl-like clients, rounds=$ROUNDS =="
i=1
while [ "$i" -le "$CLIENTS" ]; do
  bin/android_like_aidl_client "$SERVICE" "$ROUNDS" > "logs/android_like_aidl_stress_client_$i.log" 2>&1 &
  echo "$!" > "run/android_like_aidl_stress_client_$i.pid"
  i=$((i + 1))
done

fail=0

echo "== wait clients =="
i=1
while [ "$i" -le "$CLIENTS" ]; do
  pid="$(cat "run/android_like_aidl_stress_client_$i.pid")"

  set +e
  wait "$pid"
  rc="$?"
  set -e

  if [ "$rc" -eq 0 ]; then
    echo "client $i OK"
  else
    echo "client $i FAIL rc=$rc"
    fail=1
  fi

  i=$((i + 1))
done

echo "== client marker summary =="
i=1
while [ "$i" -le "$CLIENTS" ]; do
  log="logs/android_like_aidl_stress_client_$i.log"

  echo "--- client $i markers ---"
  grep 'AIDL_LIKE_' "$log" | tail -n 8 || true

  grep -q 'AIDL_LIKE_EXCEPTION_CODE_OK' "$log" || fail=1
  grep -q 'AIDL_LIKE_ECHO_OK' "$log" || fail=1
  grep -q 'AIDL_LIKE_ADD_OK' "$log" || fail=1
  grep -q 'AIDL_LIKE_CLIENT_SMOKE_OK' "$log" || fail=1

  i=$((i + 1))
done

if [ "$fail" -ne 0 ]; then
  echo "FAIL: one or more AIDL-like clients failed"
  echo "== service log =="
  cat logs/android_like_aidl_stress_service.log || true
  echo "== servicemgr log =="
  cat logs/android_like_aidl_stress_sm.log || true
  exit 1
fi

echo "AIDL_LIKE_STRESS_CLIENTS_OK"

expected_each="$((CLIENTS * ROUNDS))"
expected_exception="$((CLIENTS * ROUNDS * 2))"

echo_count="$(grep -c 'AIDL_LIKE_ECHO_SERVICE_OK' logs/android_like_aidl_stress_service.log || true)"
add_count="$(grep -c 'AIDL_LIKE_ADD_SERVICE_OK' logs/android_like_aidl_stress_service.log || true)"
client_exception_count=0
client_echo_count=0
client_add_count=0

i=1
while [ "$i" -le "$CLIENTS" ]; do
  log="logs/android_like_aidl_stress_client_$i.log"

  c="$(grep -c 'AIDL_LIKE_EXCEPTION_CODE_OK' "$log" || true)"
  client_exception_count="$((client_exception_count + c))"

  c="$(grep -c 'AIDL_LIKE_ECHO_OK' "$log" || true)"
  client_echo_count="$((client_echo_count + c))"

  c="$(grep -c 'AIDL_LIKE_ADD_OK' "$log" || true)"
  client_add_count="$((client_add_count + c))"

  i=$((i + 1))
done

echo "== stress counts =="
echo "expected_each=$expected_each"
echo "expected_exception=$expected_exception"
echo "service_echo_count=$echo_count"
echo "service_add_count=$add_count"
echo "client_exception_count=$client_exception_count"
echo "client_echo_count=$client_echo_count"
echo "client_add_count=$client_add_count"

if [ "$echo_count" -lt "$expected_each" ]; then
  echo "FAIL: service echo count too low"
  cat logs/android_like_aidl_stress_service.log || true
  exit 1
fi

if [ "$add_count" -lt "$expected_each" ]; then
  echo "FAIL: service add count too low"
  cat logs/android_like_aidl_stress_service.log || true
  exit 1
fi

if [ "$client_exception_count" -lt "$expected_exception" ]; then
  echo "FAIL: client exception count too low"
  exit 1
fi

if [ "$client_echo_count" -lt "$expected_each" ]; then
  echo "FAIL: client echo count too low"
  exit 1
fi

if [ "$client_add_count" -lt "$expected_each" ]; then
  echo "FAIL: client add count too low"
  exit 1
fi

echo "AIDL_LIKE_STRESS_SERVICE_COUNTS_OK"

echo "== final service markers =="
grep 'AIDL_LIKE_' logs/android_like_aidl_stress_service.log | tail -n 20 || true

echo "AIDL_LIKE_STRESS_SMOKE_TV_OK"
TVSH
