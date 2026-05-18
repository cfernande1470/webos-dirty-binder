#!/usr/bin/env bash
set -euo pipefail

TV_IP="${TV_IP:-192.168.2.121}"
SIDE_DIR="${SIDE_DIR:-/tmp/android-usb/android-sidecar}"
SERVICE="${SERVICE:-test.android.aidl.registry}"
CLIENTS="${CLIENTS:-8}"

ssh root@"$TV_IP" "SIDE_DIR='$SIDE_DIR' SERVICE='$SERVICE' CLIENTS='$CLIENTS' sh -s" <<'TVSH'
set -eu

cd "$SIDE_DIR"

for f in \
  bin/mini_servicemgr \
  bin/android_like_aidl_listener_registry_service \
  bin/android_like_aidl_listener_registry_client \
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
  bin/android_like_aidl_listener_registry_service \
  bin/android_like_aidl_listener_registry_client \
  load-binder-tv.sh

mkdir -p logs run

if [ ! -e /dev/binder ]; then
  ./load-binder-tv.sh "$SIDE_DIR/modules/binder.ko"
fi

killall mini_servicemgr 2>/dev/null || true
killall android_like_aidl_listener_registry_service 2>/dev/null || true
killall android_like_aidl_listener_registry_client 2>/dev/null || true

rm -f logs/android_like_aidl_registry_*.log run/android_like_aidl_registry_*.pid

cleanup() {
  for pidfile in run/android_like_aidl_registry_client_*.pid; do
    [ -e "$pidfile" ] || continue
    kill "$(cat "$pidfile")" 2>/dev/null || true
  done

  [ -f run/android_like_aidl_registry_service.pid ] && kill "$(cat run/android_like_aidl_registry_service.pid)" 2>/dev/null || true
  [ -f run/android_like_aidl_registry_sm.pid ] && kill "$(cat run/android_like_aidl_registry_sm.pid)" 2>/dev/null || true
}

trap cleanup EXIT

wait_for_marker() {
  marker="$1"
  file="$2"
  tries="${3:-60}"

  i=0
  while [ "$i" -lt "$tries" ]; do
    if grep -q "$marker" "$file" 2>/dev/null; then
      return 0
    fi

    sleep 1
    i=$((i + 1))
  done

  return 1
}

echo "== listener registry config =="
echo "SERVICE=$SERVICE"
echo "CLIENTS=$CLIENTS"

echo "== start mini_servicemgr =="
bin/mini_servicemgr > logs/android_like_aidl_registry_sm.log 2>&1 &
echo "$!" > run/android_like_aidl_registry_sm.pid

sleep 2

echo "== start listener registry service =="
bin/android_like_aidl_listener_registry_service "$SERVICE" "$CLIENTS" > logs/android_like_aidl_registry_service.log 2>&1 &
echo "$!" > run/android_like_aidl_registry_service.pid

sleep 3

echo "== launch $CLIENTS registry clients =="
i=1
while [ "$i" -le "$CLIENTS" ]; do
  bin/android_like_aidl_listener_registry_client "$SERVICE" "$i" > "logs/android_like_aidl_registry_client_$i.log" 2>&1 &
  echo "$!" > "run/android_like_aidl_registry_client_$i.pid"
  i=$((i + 1))
done

fail=0

echo "== wait clients =="
i=1
while [ "$i" -le "$CLIENTS" ]; do
  pid="$(cat "run/android_like_aidl_registry_client_$i.pid")"

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
  log="logs/android_like_aidl_registry_client_$i.log"

  echo "--- client $i markers ---"
  grep 'AIDL_LIKE_LISTENER_REGISTRY' "$log" || true

  grep -q 'AIDL_LIKE_LISTENER_REGISTRY_LOOPER_READY' "$log" || fail=1
  grep -q 'AIDL_LIKE_LISTENER_REGISTRY_REGISTER_SENT' "$log" || fail=1
  grep -q 'AIDL_LIKE_LISTENER_REGISTRY_THREAD_OK' "$log" || fail=1
  grep -q 'AIDL_LIKE_LISTENER_REGISTRY_CLIENT_BROADCAST_OK' "$log" || fail=1
  grep -q 'AIDL_LIKE_LISTENER_REGISTRY_CLIENT_SMOKE_OK' "$log" || fail=1

  if grep -q 'AIDL_LIKE_LISTENER_REGISTRY_MAIN_GOT_CALLBACK_FAIL' "$log"; then
    fail=1
  fi

  i=$((i + 1))
done

if [ "$fail" -ne 0 ]; then
  echo "FAIL: one or more registry clients failed"
  echo "== service log =="
  cat logs/android_like_aidl_registry_service.log || true
  echo "== servicemgr log =="
  cat logs/android_like_aidl_registry_sm.log || true
  exit 1
fi

echo "== wait service death cleanup =="
if ! wait_for_marker 'AIDL_LIKE_LISTENER_REGISTRY_DEATH_CLEANUP_OK' logs/android_like_aidl_registry_service.log 60; then
  echo "FAIL: service did not clean up all listener deaths"
  echo "== service log =="
  cat logs/android_like_aidl_registry_service.log || true
  exit 1
fi

registered_count="$(grep -c 'AIDL_LIKE_LISTENER_REGISTRY_REGISTER_OK' logs/android_like_aidl_registry_service.log || true)"
death_requested_count="$(grep -c 'AIDL_LIKE_LISTENER_REGISTRY_DEATH_REQUESTED' logs/android_like_aidl_registry_service.log || true)"
broadcast_count="$(grep -c 'AIDL_LIKE_LISTENER_REGISTRY_BROADCAST_OK' logs/android_like_aidl_registry_service.log || true)"
death_cleanup_count="$(grep -c 'AIDL_LIKE_LISTENER_REGISTRY_DEATH_CLEANUP_OK' logs/android_like_aidl_registry_service.log || true)"
client_smoke_count=0
thread_ok_count=0

i=1
while [ "$i" -le "$CLIENTS" ]; do
  log="logs/android_like_aidl_registry_client_$i.log"

  c="$(grep -c 'AIDL_LIKE_LISTENER_REGISTRY_CLIENT_SMOKE_OK' "$log" || true)"
  client_smoke_count="$((client_smoke_count + c))"

  c="$(grep -c 'AIDL_LIKE_LISTENER_REGISTRY_THREAD_OK' "$log" || true)"
  thread_ok_count="$((thread_ok_count + c))"

  i=$((i + 1))
done

echo "== registry counts =="
echo "registered_count=$registered_count"
echo "death_requested_count=$death_requested_count"
echo "broadcast_count=$broadcast_count"
echo "death_cleanup_count=$death_cleanup_count"
echo "thread_ok_count=$thread_ok_count"
echo "client_smoke_count=$client_smoke_count"
echo "expected=$CLIENTS"

if [ "$registered_count" -lt "$CLIENTS" ]; then
  echo "FAIL: registered_count too low"
  exit 1
fi

if [ "$death_requested_count" -lt "$CLIENTS" ]; then
  echo "FAIL: death_requested_count too low"
  exit 1
fi

if [ "$broadcast_count" -lt 1 ]; then
  echo "FAIL: broadcast_count too low"
  exit 1
fi

if [ "$death_cleanup_count" -lt 1 ]; then
  echo "FAIL: death_cleanup_count too low"
  exit 1
fi

if [ "$thread_ok_count" -lt "$CLIENTS" ]; then
  echo "FAIL: thread_ok_count too low"
  exit 1
fi

if [ "$client_smoke_count" -lt "$CLIENTS" ]; then
  echo "FAIL: client_smoke_count too low"
  exit 1
fi

echo "== service markers =="
grep 'AIDL_LIKE_LISTENER_REGISTRY' logs/android_like_aidl_registry_service.log || true

echo "AIDL_LIKE_LISTENER_REGISTRY_SMOKE_TV_OK"
TVSH
