#!/usr/bin/env bash
set -euo pipefail

TV_IP="${TV_IP:-192.168.2.121}"
SIDE_DIR="${SIDE_DIR:-/media/internal/android-sidecar}"
SERVICE="${SERVICE:-test.android.aidl.oneway}"
ROUNDS="${ROUNDS:-100}"

ssh root@"$TV_IP" "SIDE_DIR='$SIDE_DIR' SERVICE='$SERVICE' ROUNDS='$ROUNDS' sh -s" <<'TVSH'
set -eu

cd "$SIDE_DIR"

for f in \
  bin/mini_servicemgr \
  bin/android_like_aidl_oneway_service \
  bin/android_like_aidl_oneway_client \
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
  bin/android_like_aidl_oneway_service \
  bin/android_like_aidl_oneway_client \
  load-binder-tv.sh

mkdir -p logs run

if [ ! -e /dev/binder ]; then
  ./load-binder-tv.sh "$SIDE_DIR/modules/binder.ko"
fi

killall mini_servicemgr 2>/dev/null || true
killall android_like_aidl_oneway_service 2>/dev/null || true
killall android_like_aidl_oneway_client 2>/dev/null || true

rm -f logs/android_like_oneway_*.log run/android_like_oneway_*.pid

cleanup() {
  [ -f run/android_like_oneway_service.pid ] && kill "$(cat run/android_like_oneway_service.pid)" 2>/dev/null || true
  [ -f run/android_like_oneway_sm.pid ] && kill "$(cat run/android_like_oneway_sm.pid)" 2>/dev/null || true
}

trap cleanup EXIT

echo "== aidl oneway config =="
echo "SERVICE=$SERVICE"
echo "ROUNDS=$ROUNDS"

echo "== start mini_servicemgr =="
bin/mini_servicemgr > logs/android_like_oneway_sm.log 2>&1 &
echo "$!" > run/android_like_oneway_sm.pid

sleep 2

echo "== start oneway service =="
bin/android_like_aidl_oneway_service "$SERVICE" > logs/android_like_oneway_service.log 2>&1 &
echo "$!" > run/android_like_oneway_service.pid

sleep 3

echo "== run oneway client =="
set +e
bin/android_like_aidl_oneway_client "$SERVICE" "$ROUNDS" > logs/android_like_oneway_client.log 2>&1
client_rc="$?"
set -e

echo "== oneway client markers =="
grep 'AIDL_LIKE_ONEWAY' logs/android_like_oneway_client.log || true

echo "== oneway service markers =="
grep 'AIDL_LIKE_ONEWAY' logs/android_like_oneway_service.log || true

if [ "$client_rc" -ne 0 ]; then
  echo "FAIL: oneway client rc=$client_rc"
  echo "== client log =="
  cat logs/android_like_oneway_client.log || true
  echo "== service log =="
  cat logs/android_like_oneway_service.log || true
  echo "== servicemgr log =="
  cat logs/android_like_oneway_sm.log || true
  exit "$client_rc"
fi

grep -q 'AIDL_LIKE_ONEWAY_SERVICE_REGISTERED' logs/android_like_oneway_service.log
grep -q 'AIDL_LIKE_ONEWAY_NOTIFY_SENT' logs/android_like_oneway_client.log
grep -q 'AIDL_LIKE_ONEWAY_NOTIFY_OK' logs/android_like_oneway_service.log
grep -q 'AIDL_LIKE_ONEWAY_GET_COUNT_OK' logs/android_like_oneway_client.log
grep -q 'AIDL_LIKE_ONEWAY_CLIENT_SMOKE_OK' logs/android_like_oneway_client.log

sent_count="$(grep -c 'AIDL_LIKE_ONEWAY_NOTIFY_SENT' logs/android_like_oneway_client.log || true)"
notify_count="$(grep -c 'AIDL_LIKE_ONEWAY_NOTIFY_OK' logs/android_like_oneway_service.log || true)"

echo "== oneway counts =="
echo "sent_count=$sent_count"
echo "notify_count=$notify_count"
echo "expected=$ROUNDS"

if [ "$sent_count" -lt "$ROUNDS" ]; then
  echo "FAIL: sent_count too low"
  exit 1
fi

if [ "$notify_count" -lt "$ROUNDS" ]; then
  echo "FAIL: notify_count too low"
  exit 1
fi

echo "AIDL_LIKE_ONEWAY_SMOKE_TV_OK"
TVSH
