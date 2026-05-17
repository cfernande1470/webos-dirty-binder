#!/usr/bin/env bash
set -euo pipefail

TV_IP="${TV_IP:-192.168.2.121}"
SIDE_DIR="${SIDE_DIR:-/media/internal/android-sidecar}"
SERVICE="${SERVICE:-test.android.aidl.factory.stress}"
CLIENTS="${CLIENTS:-16}"

ssh root@"$TV_IP" "SIDE_DIR='$SIDE_DIR' SERVICE='$SERVICE' CLIENTS='$CLIENTS' sh -s" <<'TVSH'
set -eu

cd "$SIDE_DIR"

for f in \
  bin/mini_servicemgr \
  bin/android_like_aidl_binder_return_service \
  bin/android_like_aidl_binder_return_client \
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
  bin/android_like_aidl_binder_return_service \
  bin/android_like_aidl_binder_return_client \
  load-binder-tv.sh

mkdir -p logs run

if [ ! -e /dev/binder ]; then
  ./load-binder-tv.sh "$SIDE_DIR/modules/binder.ko"
fi

killall mini_servicemgr 2>/dev/null || true
killall android_like_aidl_binder_return_service 2>/dev/null || true
killall android_like_aidl_binder_return_client 2>/dev/null || true

rm -f logs/android_like_binder_return_stress_*.log run/android_like_binder_return_stress_*.pid

cleanup() {
  for pidfile in run/android_like_binder_return_stress_client_*.pid; do
    [ -e "$pidfile" ] || continue
    kill "$(cat "$pidfile")" 2>/dev/null || true
  done

  [ -f run/android_like_binder_return_stress_service.pid ] && kill "$(cat run/android_like_binder_return_stress_service.pid)" 2>/dev/null || true
  [ -f run/android_like_binder_return_stress_sm.pid ] && kill "$(cat run/android_like_binder_return_stress_sm.pid)" 2>/dev/null || true
}

trap cleanup EXIT

echo "== binder return stress config =="
echo "SERVICE=$SERVICE"
echo "CLIENTS=$CLIENTS"

echo "== start mini_servicemgr =="
bin/mini_servicemgr > logs/android_like_binder_return_stress_sm.log 2>&1 &
echo "$!" > run/android_like_binder_return_stress_sm.pid

sleep 2

echo "== start binder-return service =="
bin/android_like_aidl_binder_return_service "$SERVICE" > logs/android_like_binder_return_stress_service.log 2>&1 &
echo "$!" > run/android_like_binder_return_stress_service.pid

sleep 3

echo "== launch $CLIENTS binder-return clients =="
i=1
while [ "$i" -le "$CLIENTS" ]; do
  bin/android_like_aidl_binder_return_client "$SERVICE" > "logs/android_like_binder_return_stress_client_$i.log" 2>&1 &
  echo "$!" > "run/android_like_binder_return_stress_client_$i.pid"
  i=$((i + 1))
done

fail=0

echo "== wait clients =="
i=1
while [ "$i" -le "$CLIENTS" ]; do
  pid="$(cat "run/android_like_binder_return_stress_client_$i.pid")"

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
  log="logs/android_like_binder_return_stress_client_$i.log"

  echo "--- client $i markers ---"
  grep 'AIDL_LIKE_BINDER_RETURN' "$log" || true

  grep -q 'AIDL_LIKE_BINDER_RETURN_HANDLE_OK' "$log" || fail=1
  grep -q 'AIDL_LIKE_BINDER_RETURN_CHILD_CALL_OK' "$log" || fail=1
  grep -q 'AIDL_LIKE_BINDER_RETURN_CLIENT_SMOKE_OK' "$log" || fail=1

  i=$((i + 1))
done

if [ "$fail" -ne 0 ]; then
  echo "FAIL: one or more binder-return clients failed"
  echo "== service log =="
  cat logs/android_like_binder_return_stress_service.log || true
  echo "== servicemgr log =="
  cat logs/android_like_binder_return_stress_sm.log || true
  exit 1
fi

echo "AIDL_LIKE_BINDER_RETURN_STRESS_CLIENTS_OK"

object_sent_count="$(grep -c 'AIDL_LIKE_BINDER_RETURN_CHILD_OBJECT_SENT' logs/android_like_binder_return_stress_service.log || true)"
child_call_count="$(grep -c 'AIDL_LIKE_BINDER_RETURN_CHILD_CALL_OK' logs/android_like_binder_return_stress_service.log || true)"
service_registered_count="$(grep -c 'AIDL_LIKE_BINDER_RETURN_SERVICE_REGISTERED' logs/android_like_binder_return_stress_service.log || true)"

client_handle_count=0
client_child_count=0
client_smoke_count=0

i=1
while [ "$i" -le "$CLIENTS" ]; do
  log="logs/android_like_binder_return_stress_client_$i.log"

  c="$(grep -c 'AIDL_LIKE_BINDER_RETURN_HANDLE_OK' "$log" || true)"
  client_handle_count="$((client_handle_count + c))"

  c="$(grep -c 'AIDL_LIKE_BINDER_RETURN_CHILD_CALL_OK' "$log" || true)"
  client_child_count="$((client_child_count + c))"

  c="$(grep -c 'AIDL_LIKE_BINDER_RETURN_CLIENT_SMOKE_OK' "$log" || true)"
  client_smoke_count="$((client_smoke_count + c))"

  i=$((i + 1))
done

echo "== binder return stress counts =="
echo "service_registered_count=$service_registered_count"
echo "object_sent_count=$object_sent_count"
echo "child_call_count=$child_call_count"
echo "client_handle_count=$client_handle_count"
echo "client_child_count=$client_child_count"
echo "client_smoke_count=$client_smoke_count"
echo "expected=$CLIENTS"

if [ "$service_registered_count" -lt 1 ]; then
  echo "FAIL: service did not register"
  exit 1
fi

if [ "$object_sent_count" -lt "$CLIENTS" ]; then
  echo "FAIL: object_sent_count too low"
  cat logs/android_like_binder_return_stress_service.log || true
  exit 1
fi

if [ "$child_call_count" -lt "$CLIENTS" ]; then
  echo "FAIL: child_call_count too low"
  cat logs/android_like_binder_return_stress_service.log || true
  exit 1
fi

if [ "$client_handle_count" -lt "$CLIENTS" ]; then
  echo "FAIL: client_handle_count too low"
  exit 1
fi

if [ "$client_child_count" -lt "$CLIENTS" ]; then
  echo "FAIL: client_child_count too low"
  exit 1
fi

if [ "$client_smoke_count" -lt "$CLIENTS" ]; then
  echo "FAIL: client_smoke_count too low"
  exit 1
fi

echo "AIDL_LIKE_BINDER_RETURN_STRESS_SERVICE_COUNTS_OK"

echo "== service markers =="
grep 'AIDL_LIKE_BINDER_RETURN' logs/android_like_binder_return_stress_service.log || true

echo "AIDL_LIKE_BINDER_RETURN_STRESS_SMOKE_TV_OK"
TVSH
