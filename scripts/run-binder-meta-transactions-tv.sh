#!/usr/bin/env bash
set -euo pipefail

TV_IP="${TV_IP:-192.168.2.121}"
SIDE_DIR="${SIDE_DIR:-/media/internal/android-sidecar}"
SERVICE="${SERVICE:-test.android.aidl.meta}"

ssh root@"$TV_IP" "SIDE_DIR='$SIDE_DIR' SERVICE='$SERVICE' sh -s" <<'TVSH'
set -eu

cd "$SIDE_DIR"

for f in \
  bin/mini_servicemgr \
  bin/android_like_aidl_service \
  bin/android_like_aidl_client \
  bin/android_like_binder_meta_client \
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
  bin/android_like_binder_meta_client \
  load-binder-tv.sh

mkdir -p logs run

if [ ! -e /dev/binder ]; then
  ./load-binder-tv.sh "$SIDE_DIR/modules/binder.ko"
fi

killall mini_servicemgr 2>/dev/null || true
killall android_like_aidl_service 2>/dev/null || true
killall android_like_aidl_client 2>/dev/null || true
killall android_like_binder_meta_client 2>/dev/null || true

rm -f logs/binder_meta_*.log run/binder_meta_*.pid

cleanup() {
  [ -f run/binder_meta_service.pid ] && kill "$(cat run/binder_meta_service.pid)" 2>/dev/null || true
  [ -f run/binder_meta_sm.pid ] && kill "$(cat run/binder_meta_sm.pid)" 2>/dev/null || true
}

trap cleanup EXIT

echo "== binder meta config =="
echo "SERVICE=$SERVICE"

echo "== start mini_servicemgr =="
bin/mini_servicemgr > logs/binder_meta_sm.log 2>&1 &
echo "$!" > run/binder_meta_sm.pid

sleep 2

echo "== start aidl-like service =="
bin/android_like_aidl_service "$SERVICE" > logs/binder_meta_service.log 2>&1 &
echo "$!" > run/binder_meta_service.pid

sleep 3

echo "== run binder meta client =="
set +e
bin/android_like_binder_meta_client "$SERVICE" > logs/binder_meta_client.log 2>&1
meta_rc="$?"
set -e

echo "== binder meta client markers =="
grep 'BINDER_META' logs/binder_meta_client.log || true

echo "== binder meta service markers =="
grep -E 'BINDER_META|AIDL_LIKE_SERVICE_REGISTERED' logs/binder_meta_service.log || true

if [ "$meta_rc" -ne 0 ]; then
  echo "FAIL: binder meta client rc=$meta_rc"
  echo "== client log =="
  cat logs/binder_meta_client.log || true
  echo "== service log =="
  cat logs/binder_meta_service.log || true
  echo "== servicemgr log =="
  cat logs/binder_meta_sm.log || true
  exit "$meta_rc"
fi

grep -q 'BINDER_META_SERVICE_REGISTERED' logs/binder_meta_service.log
grep -q 'BINDER_META_INTERFACE_TRANSACTION_OK' logs/binder_meta_service.log
grep -q 'BINDER_META_DESCRIPTOR_OK' logs/binder_meta_client.log
grep -q 'BINDER_META_INTERFACE_TRANSACTION_OK' logs/binder_meta_client.log

echo "== verify normal AIDL call still works =="
set +e
bin/android_like_aidl_client "$SERVICE" 1 > logs/binder_meta_aidl_client.log 2>&1
aidl_rc="$?"
set -e

grep 'AIDL_LIKE_' logs/binder_meta_aidl_client.log || true

if [ "$aidl_rc" -ne 0 ]; then
  echo "FAIL: aidl client after meta rc=$aidl_rc"
  cat logs/binder_meta_aidl_client.log || true
  exit "$aidl_rc"
fi

grep -q 'AIDL_LIKE_CLIENT_SMOKE_OK' logs/binder_meta_aidl_client.log

echo "BINDER_META_AIDL_RECOVERY_OK"
echo "BINDER_META_SMOKE_TV_OK"
TVSH
