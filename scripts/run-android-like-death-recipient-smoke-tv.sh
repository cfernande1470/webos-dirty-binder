#!/usr/bin/env bash
set -euo pipefail

TV_IP="${TV_IP:-192.168.2.121}"
SIDE_DIR="${SIDE_DIR:-/media/internal/android-sidecar}"
SERVICE="${SERVICE:-test.android.service}"

ssh root@"$TV_IP" "SIDE_DIR='$SIDE_DIR' SERVICE='$SERVICE' sh -s" <<'TVSH'
set -eu

cd "$SIDE_DIR"

for f in \
  bin/mini_servicemgr \
  bin/android_like_echo_service \
  bin/android_like_death_recipient_client \
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
  bin/android_like_death_recipient_client \
  bin/android_like_lifecycle_client \
  load-binder-tv.sh

mkdir -p logs run

if [ ! -e /dev/binder ]; then
  ./load-binder-tv.sh "$SIDE_DIR/modules/binder.ko"
fi

killall mini_servicemgr 2>/dev/null || true
killall android_like_echo_service 2>/dev/null || true
killall android_like_death_recipient_client 2>/dev/null || true
killall android_like_lifecycle_client 2>/dev/null || true

rm -f logs/android_like_death_*.log run/android_like_death_*.pid

cleanup() {
  [ -f run/android_like_death_service2.pid ] && kill "$(cat run/android_like_death_service2.pid)" 2>/dev/null || true
  [ -f run/android_like_death_service1.pid ] && kill "$(cat run/android_like_death_service1.pid)" 2>/dev/null || true
  [ -f run/android_like_death_sm.pid ] && kill "$(cat run/android_like_death_sm.pid)" 2>/dev/null || true
}
trap cleanup EXIT

echo "== start mini_servicemgr =="
bin/mini_servicemgr > logs/android_like_death_sm.log 2>&1 &
echo "$!" > run/android_like_death_sm.pid
sleep 2

echo "== start Android-like service #1 $SERVICE =="
bin/android_like_echo_service "$SERVICE" > logs/android_like_death_service1.log 2>&1 &
echo "$!" > run/android_like_death_service1.pid
sleep 3

echo "== run death-recipient client =="
set +e
bin/android_like_death_recipient_client "$SERVICE" "$(cat run/android_like_death_service1.pid)" \
  > logs/android_like_death_client.log 2>&1
death_rc="$?"
set -e

cat logs/android_like_death_client.log

if [ "$death_rc" -ne 0 ]; then
  echo "FAIL: android_like_death_recipient_client rc=$death_rc"
  echo "== mini_servicemgr log =="
  cat logs/android_like_death_sm.log || true
  echo "== service #1 log =="
  cat logs/android_like_death_service1.log || true
  exit "$death_rc"
fi

grep -q 'ANDROID_LIKE_LINK_TO_DEATH_OK' logs/android_like_death_client.log
grep -q 'ANDROID_LIKE_DEATH_NOTIFICATION_RECEIVED_OK' logs/android_like_death_client.log
grep -q 'ANDROID_LIKE_DEAD_BINDER_DONE_OK' logs/android_like_death_client.log
grep -q 'ANDROID_LIKE_DEATH_RECIPIENT_CLIENT_OK' logs/android_like_death_client.log

echo "== start Android-like service #2 $SERVICE =="
bin/android_like_echo_service "$SERVICE" > logs/android_like_death_service2.log 2>&1 &
echo "$!" > run/android_like_death_service2.pid
sleep 3

echo "== recovery lifecycle client =="
bin/android_like_lifecycle_client "$SERVICE" 1 \
  > logs/android_like_death_recovery_client.log 2>&1

cat logs/android_like_death_recovery_client.log

grep -q 'ANDROID_LIKE_LIFECYCLE_CLIENT_OK' logs/android_like_death_recovery_client.log
grep -q 'ANDROID_LIKE_BN_ECHO_TRANSACTION_OK' logs/android_like_death_service2.log

echo "ANDROID_LIKE_DEATH_RECIPIENT_SMOKE_OK"
TVSH
