#!/usr/bin/env bash
set -euo pipefail

TV_IP="${TV_IP:-192.168.2.121}"
SIDE_DIR="${SIDE_DIR:-/tmp/android-usb/android-sidecar}"
SERVICE="${SERVICE:-test.android.aidl.stale}"
CLIENT_SLEEP="${CLIENT_SLEEP:-8}"

ssh root@"$TV_IP" "SIDE_DIR='$SIDE_DIR' SERVICE='$SERVICE' CLIENT_SLEEP='$CLIENT_SLEEP' sh -s" <<'TVSH'
set -eu

cd "$SIDE_DIR"

for f in \
  bin/mini_servicemgr \
  bin/android_like_aidl_service \
  bin/android_like_aidl_stale_handle_client \
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
  bin/android_like_aidl_stale_handle_client \
  load-binder-tv.sh

mkdir -p logs run

if [ ! -e /dev/binder ]; then
  ./load-binder-tv.sh "$SIDE_DIR/modules/binder.ko"
fi

killall mini_servicemgr 2>/dev/null || true
killall android_like_aidl_service 2>/dev/null || true
killall android_like_aidl_client 2>/dev/null || true
killall android_like_aidl_stale_handle_client 2>/dev/null || true

rm -f logs/android_like_aidl_stale_*.log run/android_like_aidl_stale_*.pid

cleanup() {
  [ -f run/android_like_aidl_stale_client.pid ] && kill "$(cat run/android_like_aidl_stale_client.pid)" 2>/dev/null || true
  [ -f run/android_like_aidl_stale_service.pid ] && kill "$(cat run/android_like_aidl_stale_service.pid)" 2>/dev/null || true
  [ -f run/android_like_aidl_stale_sm.pid ] && kill "$(cat run/android_like_aidl_stale_sm.pid)" 2>/dev/null || true
}

trap cleanup EXIT

start_service() {
  tag="$1"

  echo "== start aidl-like service tag=$tag service=$SERVICE =="
  bin/android_like_aidl_service "$SERVICE" >> logs/android_like_aidl_stale_service.log 2>&1 &
  echo "$!" > run/android_like_aidl_stale_service.pid

  sleep 3
}

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

echo "== stale handle config =="
echo "SERVICE=$SERVICE"
echo "CLIENT_SLEEP=$CLIENT_SLEEP"

echo "== start mini_servicemgr =="
bin/mini_servicemgr > logs/android_like_aidl_stale_sm.log 2>&1 &
echo "$!" > run/android_like_aidl_stale_sm.pid

sleep 2

start_service initial

echo "== start stale-handle client =="
bin/android_like_aidl_stale_handle_client "$SERVICE" "$CLIENT_SLEEP" > logs/android_like_aidl_stale_client.log 2>&1 &
echo "$!" > run/android_like_aidl_stale_client.pid

if ! wait_for_marker 'AIDL_LIKE_STALE_READY_TO_KILL' logs/android_like_aidl_stale_client.log 30; then
  echo "FAIL: stale client never became ready"
  echo "== client log =="
  cat logs/android_like_aidl_stale_client.log || true
  echo "== service log =="
  cat logs/android_like_aidl_stale_service.log || true
  echo "== servicemgr log =="
  cat logs/android_like_aidl_stale_sm.log || true
  exit 1
fi

old_death_count="$(grep -c "service died name=$SERVICE" logs/android_like_aidl_stale_sm.log || true)"
svc_pid="$(cat run/android_like_aidl_stale_service.pid)"

echo "== kill service pid=$svc_pid =="
kill "$svc_pid" 2>/dev/null || true

set +e
wait "$svc_pid"
set -e

detected=0
i=0
while [ "$i" -lt 30 ]; do
  new_death_count="$(grep -c "service died name=$SERVICE" logs/android_like_aidl_stale_sm.log || true)"

  if [ "$new_death_count" -gt "$old_death_count" ]; then
    detected=1
    break
  fi

  sleep 1
  i=$((i + 1))
done

if [ "$detected" -ne 1 ]; then
  echo "FAIL: ServiceManager did not report service death"
  echo "== servicemgr log =="
  cat logs/android_like_aidl_stale_sm.log || true
  exit 1
fi

echo "== death detected by ServiceManager =="

start_service recovered

echo "== wait stale-handle client exit =="
client_pid="$(cat run/android_like_aidl_stale_client.pid)"

set +e
wait "$client_pid"
client_rc="$?"
set -e

echo "== stale client markers =="
grep 'AIDL_LIKE_STALE_' logs/android_like_aidl_stale_client.log || true

echo "== servicemgr death markers =="
grep "service died name=$SERVICE" logs/android_like_aidl_stale_sm.log || true

echo "== service registration markers =="
grep 'AIDL_LIKE_SERVICE_REGISTERED' logs/android_like_aidl_stale_service.log || true

if [ "$client_rc" -ne 0 ]; then
  echo "FAIL: stale-handle client rc=$client_rc"
  echo "== client log =="
  cat logs/android_like_aidl_stale_client.log || true
  echo "== service log =="
  cat logs/android_like_aidl_stale_service.log || true
  echo "== servicemgr log =="
  cat logs/android_like_aidl_stale_sm.log || true
  exit "$client_rc"
fi

grep -q 'AIDL_LIKE_STALE_INITIAL_OK' logs/android_like_aidl_stale_client.log
grep -q 'AIDL_LIKE_STALE_READY_TO_KILL' logs/android_like_aidl_stale_client.log
grep -q 'AIDL_LIKE_STALE_HANDLE_DEAD_REPLY_OK' logs/android_like_aidl_stale_client.log
grep -q 'AIDL_LIKE_STALE_RERESOLVE_OK' logs/android_like_aidl_stale_client.log
grep -q 'AIDL_LIKE_STALE_AFTER_RECOVERY_OK' logs/android_like_aidl_stale_client.log
grep -q 'AIDL_LIKE_STALE_SMOKE_OK' logs/android_like_aidl_stale_client.log

reg_count="$(grep -c 'AIDL_LIKE_SERVICE_REGISTERED' logs/android_like_aidl_stale_service.log || true)"
death_count="$(grep -c "service died name=$SERVICE" logs/android_like_aidl_stale_sm.log || true)"

echo "== stale handle counts =="
echo "reg_count=$reg_count"
echo "death_count=$death_count"

if [ "$reg_count" -lt 2 ]; then
  echo "FAIL: expected at least 2 service registrations"
  exit 1
fi

if [ "$death_count" -lt 1 ]; then
  echo "FAIL: expected at least 1 service death"
  exit 1
fi

echo "AIDL_LIKE_STALE_SMOKE_TV_OK"
TVSH
