#!/usr/bin/env bash
set -euo pipefail

TV_IP="${TV_IP:-192.168.2.121}"
SIDE_DIR="${SIDE_DIR:-/tmp/android-usb/android-sidecar}"
SERVICE="${SERVICE:-test.android.service}"
CLIENTS="${CLIENTS:-4}"
ROUNDS="${ROUNDS:-10}"

ssh root@"$TV_IP" "SIDE_DIR='$SIDE_DIR' SERVICE='$SERVICE' CLIENTS='$CLIENTS' ROUNDS='$ROUNDS' sh -s" <<'TVSH'
set -eu

cd "$SIDE_DIR"

for f in \
  bin/mini_servicemgr \
  bin/android_like_echo_service \
  bin/android_like_lifecycle_client \
  modules/binder.ko \
  load-binder-tv.sh
do
  if [ ! -e "$f" ]; then
    echo "ERROR: missing $SIDE_DIR/$f"
    find . -maxdepth 3 \( -type f -o -type d \) | sort
    exit 1
  fi
done

chmod +x \
  bin/mini_servicemgr \
  bin/android_like_echo_service \
  bin/android_like_lifecycle_client \
  load-binder-tv.sh

mkdir -p logs run

if [ ! -e /dev/binder ]; then
  ./load-binder-tv.sh "$SIDE_DIR/modules/binder.ko"
fi

killall mini_servicemgr 2>/dev/null || true
killall android_like_echo_service 2>/dev/null || true
killall android_like_lifecycle_client 2>/dev/null || true

rm -f logs/android_like_concurrent_*.log run/android_like_concurrent_*.pid

cleanup() {
  for pidfile in run/android_like_concurrent_client_*.pid; do
    [ -f "$pidfile" ] && kill "$(cat "$pidfile")" 2>/dev/null || true
  done
  [ -f run/android_like_concurrent_service.pid ] && kill "$(cat run/android_like_concurrent_service.pid)" 2>/dev/null || true
  [ -f run/android_like_concurrent_sm.pid ] && kill "$(cat run/android_like_concurrent_sm.pid)" 2>/dev/null || true
}
trap cleanup EXIT

echo "== start mini_servicemgr =="
bin/mini_servicemgr > logs/android_like_concurrent_sm.log 2>&1 &
echo "$!" > run/android_like_concurrent_sm.pid
sleep 2

echo "== start Android-like service $SERVICE =="
bin/android_like_echo_service "$SERVICE" > logs/android_like_concurrent_service.log 2>&1 &
echo "$!" > run/android_like_concurrent_service.pid
sleep 3

echo "== run concurrent lifecycle clients clients=$CLIENTS rounds=$ROUNDS =="
i=0
while [ "$i" -lt "$CLIENTS" ]; do
  (
    bin/android_like_lifecycle_client "$SERVICE" "$ROUNDS" \
      > "logs/android_like_concurrent_client_${i}.log" 2>&1
  ) &
  echo "$!" > "run/android_like_concurrent_client_${i}.pid"
  i=$((i + 1))
done

fail=0
i=0
while [ "$i" -lt "$CLIENTS" ]; do
  pid="$(cat "run/android_like_concurrent_client_${i}.pid")"
  if ! wait "$pid"; then
    echo "FAIL: concurrent lifecycle client $i pid=$pid"
    fail=1
  fi
  i=$((i + 1))
done

echo "== client logs =="
i=0
while [ "$i" -lt "$CLIENTS" ]; do
  echo "== client $i =="
  cat "logs/android_like_concurrent_client_${i}.log" || true
  i=$((i + 1))
done

if [ "$fail" -ne 0 ]; then
  echo "== mini_servicemgr log =="
  cat logs/android_like_concurrent_sm.log || true
  echo "== service log =="
  cat logs/android_like_concurrent_service.log || true
  exit 1
fi

i=0
while [ "$i" -lt "$CLIENTS" ]; do
  grep -q 'ANDROID_LIKE_HANDLE_ACQUIRE_OK' "logs/android_like_concurrent_client_${i}.log"
  grep -q 'ANDROID_LIKE_HANDLE_RELEASE_OK' "logs/android_like_concurrent_client_${i}.log"
  grep -q 'ANDROID_LIKE_LIFECYCLE_CLIENT_OK' "logs/android_like_concurrent_client_${i}.log"
  i=$((i + 1))
done

expected=$((CLIENTS * ROUNDS))
actual="$(grep -c 'ANDROID_LIKE_BN_ECHO_TRANSACTION_OK' logs/android_like_concurrent_service.log || true)"

echo "Android-like concurrent expected_transactions=$expected actual_transactions=$actual"

if [ "$actual" -lt "$expected" ]; then
  echo "FAIL: service saw fewer echo transactions than expected"
  echo "== service log =="
  cat logs/android_like_concurrent_service.log || true
  exit 1
fi

echo "ANDROID_LIKE_CONCURRENT_LIFECYCLE_CLIENT_OK clients=$CLIENTS rounds=$ROUNDS"
echo "ANDROID_LIKE_CONCURRENT_LIFECYCLE_STRESS_OK"
TVSH
