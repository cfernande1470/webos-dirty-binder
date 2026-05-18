#!/usr/bin/env bash
set -euo pipefail

TV_IP="${TV_IP:-192.168.2.121}"
SIDE_DIR="${SIDE_DIR:-/tmp/android-usb/android-sidecar}"
SERVICE="${SERVICE:-test.android.service}"
ROUNDS="${ROUNDS:-10}"

ssh root@"$TV_IP" "SIDE_DIR='$SIDE_DIR' SERVICE='$SERVICE' ROUNDS='$ROUNDS' sh -s" <<'TVSH'
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

rm -f logs/android_like_lifecycle_*.log run/android_like_lifecycle_*.pid

cleanup() {
  [ -f run/android_like_lifecycle_service.pid ] && kill "$(cat run/android_like_lifecycle_service.pid)" 2>/dev/null || true
  [ -f run/android_like_lifecycle_sm.pid ] && kill "$(cat run/android_like_lifecycle_sm.pid)" 2>/dev/null || true
}
trap cleanup EXIT

echo "== start mini_servicemgr =="
bin/mini_servicemgr > logs/android_like_lifecycle_sm.log 2>&1 &
echo "$!" > run/android_like_lifecycle_sm.pid
sleep 2

echo "== start Android-like service $SERVICE =="
bin/android_like_echo_service "$SERVICE" > logs/android_like_lifecycle_service.log 2>&1 &
echo "$!" > run/android_like_lifecycle_service.pid
sleep 3

echo "== run Android-like lifecycle client rounds=$ROUNDS =="
set +e
bin/android_like_lifecycle_client "$SERVICE" "$ROUNDS" > logs/android_like_lifecycle_client.log 2>&1
client_rc="$?"
set -e

cat logs/android_like_lifecycle_client.log

if [ "$client_rc" -ne 0 ]; then
  echo "FAIL: android_like_lifecycle_client rc=$client_rc"
  echo "== mini_servicemgr log =="
  cat logs/android_like_lifecycle_sm.log || true
  echo "== service log =="
  cat logs/android_like_lifecycle_service.log || true
  exit "$client_rc"
fi

grep -q 'ANDROID_LIKE_HANDLE_ACQUIRE_OK' logs/android_like_lifecycle_client.log
grep -q 'ANDROID_LIKE_HANDLE_RELEASE_OK' logs/android_like_lifecycle_client.log
grep -q 'ANDROID_LIKE_LIFECYCLE_CLIENT_OK' logs/android_like_lifecycle_client.log

echo "== service markers =="
cat logs/android_like_lifecycle_service.log || true
grep -q 'ANDROID_LIKE_BN_ECHO_TRANSACTION_OK' logs/android_like_lifecycle_service.log

echo "ANDROID_LIKE_REFCOUNT_SMOKE_OK"
TVSH
