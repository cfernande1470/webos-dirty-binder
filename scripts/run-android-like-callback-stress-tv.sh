#!/usr/bin/env bash
set -euo pipefail

TV_IP="${TV_IP:-192.168.2.121}"
SIDE_DIR="${SIDE_DIR:-/media/internal/android-sidecar}"
SERVICE="${SERVICE:-test.android.callback.stress}"
CLIENTS="${CLIENTS:-8}"

ssh root@"$TV_IP" "SIDE_DIR='$SIDE_DIR' SERVICE='$SERVICE' CLIENTS='$CLIENTS' sh -s" <<'TVSH'
set -eu

cd "$SIDE_DIR"

for f in \
  bin/mini_servicemgr \
  bin/android_like_callback_service \
  bin/android_like_callback_threadpool_client \
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
  bin/android_like_callback_service \
  bin/android_like_callback_threadpool_client \
  load-binder-tv.sh

mkdir -p logs run

if [ ! -e /dev/binder ]; then
  ./load-binder-tv.sh "$SIDE_DIR/modules/binder.ko"
fi

killall mini_servicemgr 2>/dev/null || true
killall android_like_callback_service 2>/dev/null || true
killall android_like_callback_client 2>/dev/null || true
killall android_like_callback_threadpool_client 2>/dev/null || true

rm -f logs/android_like_stress_*.log run/android_like_stress_*.pid

cleanup() {
  for pidfile in run/android_like_stress_client_*.pid; do
    [ -e "$pidfile" ] || continue
    kill "$(cat "$pidfile")" 2>/dev/null || true
  done

  [ -f run/android_like_stress_service.pid ] && kill "$(cat run/android_like_stress_service.pid)" 2>/dev/null || true
  [ -f run/android_like_stress_sm.pid ] && kill "$(cat run/android_like_stress_sm.pid)" 2>/dev/null || true
}

trap cleanup EXIT

echo "== stress config =="
echo "SERVICE=$SERVICE"
echo "CLIENTS=$CLIENTS"

echo "== start mini_servicemgr =="
bin/mini_servicemgr > logs/android_like_stress_sm.log 2>&1 &
echo "$!" > run/android_like_stress_sm.pid

sleep 2

echo "== start callback service $SERVICE =="
bin/android_like_callback_service "$SERVICE" > logs/android_like_stress_service.log 2>&1 &
echo "$!" > run/android_like_stress_service.pid

sleep 3

echo "== launch $CLIENTS concurrent threadpool clients =="
i=1
while [ "$i" -le "$CLIENTS" ]; do
  bin/android_like_callback_threadpool_client "$SERVICE" > "logs/android_like_stress_client_$i.log" 2>&1 &
  echo "$!" > "run/android_like_stress_client_$i.pid"
  i=$((i + 1))
done

fail=0

echo "== wait clients =="
i=1
while [ "$i" -le "$CLIENTS" ]; do
  pid="$(cat "run/android_like_stress_client_$i.pid")"

  if wait "$pid"; then
    echo "client $i OK"
  else
    rc="$?"
    echo "client $i FAIL rc=$rc"
    fail=1
  fi

  i=$((i + 1))
done

echo "== client summary =="
i=1
while [ "$i" -le "$CLIENTS" ]; do
  log="logs/android_like_stress_client_$i.log"

  echo "--- client $i markers ---"
  grep 'ANDROID_LIKE_' "$log" || true

  grep -q 'ANDROID_LIKE_THREADPOOL_CLIENT_LOOPER_READY' "$log" || fail=1
  grep -q 'ANDROID_LIKE_THREADPOOL_ONEWAY_REGISTER_SENT' "$log" || fail=1
  grep -q 'ANDROID_LIKE_THREADPOOL_CALLBACK_THREAD_OK' "$log" || fail=1
  grep -q 'ANDROID_LIKE_THREADPOOL_MAIN_OBSERVED_CALLBACK_OK' "$log" || fail=1
  grep -q 'ANDROID_LIKE_THREADPOOL_SMOKE_OK' "$log" || fail=1

  if grep -q 'ANDROID_LIKE_THREADPOOL_MAIN_GOT_CALLBACK_FAIL' "$log"; then
    echo "client $i got unexpected main-thread callback"
    fail=1
  fi

  i=$((i + 1))
done

if [ "$fail" -ne 0 ]; then
  echo "FAIL: one or more clients failed"
  echo "== service log =="
  cat logs/android_like_stress_service.log || true
  echo "== servicemgr log =="
  cat logs/android_like_stress_sm.log || true
  exit 1
fi

echo "ANDROID_LIKE_STRESS_CLIENTS_OK"

handle_count="$(grep -c 'ANDROID_LIKE_CALLBACK_HANDLE_OK' logs/android_like_stress_service.log || true)"
reply_count="$(grep -c 'ANDROID_LIKE_CALLBACK_REPLY_OK' logs/android_like_stress_service.log || true)"
service_ok_count="$(grep -c 'ANDROID_LIKE_CALLBACK_SERVICE_OK' logs/android_like_stress_service.log || true)"

echo "== service counts =="
echo "handle_count=$handle_count"
echo "reply_count=$reply_count"
echo "service_ok_count=$service_ok_count"
echo "expected=$CLIENTS"

if [ "$handle_count" -lt "$CLIENTS" ]; then
  echo "FAIL: service handle_count too low"
  cat logs/android_like_stress_service.log || true
  exit 1
fi

if [ "$reply_count" -lt "$CLIENTS" ]; then
  echo "FAIL: service reply_count too low"
  cat logs/android_like_stress_service.log || true
  exit 1
fi

if [ "$service_ok_count" -lt "$CLIENTS" ]; then
  echo "FAIL: service_ok_count too low"
  cat logs/android_like_stress_service.log || true
  exit 1
fi

echo "ANDROID_LIKE_STRESS_SERVICE_COUNTS_OK"

echo "== final service markers =="
grep 'ANDROID_LIKE_' logs/android_like_stress_service.log || true

echo "ANDROID_LIKE_STRESS_SMOKE_TV_OK"
TVSH
