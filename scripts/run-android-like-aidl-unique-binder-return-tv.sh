#!/usr/bin/env bash
set -euo pipefail

TV_IP="${TV_IP:-192.168.2.121}"
SIDE_DIR="${SIDE_DIR:-/media/internal/android-sidecar}"
SERVICE="${SERVICE:-test.android.aidl.factory.unique}"
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

rm -f logs/android_like_unique_binder_return_*.log run/android_like_unique_binder_return_*.pid

cleanup() {
  for pidfile in run/android_like_unique_binder_return_client_*.pid; do
    [ -e "$pidfile" ] || continue
    kill "$(cat "$pidfile")" 2>/dev/null || true
  done

  [ -f run/android_like_unique_binder_return_service.pid ] && kill "$(cat run/android_like_unique_binder_return_service.pid)" 2>/dev/null || true
  [ -f run/android_like_unique_binder_return_sm.pid ] && kill "$(cat run/android_like_unique_binder_return_sm.pid)" 2>/dev/null || true
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

echo "== unique binder return config =="
echo "SERVICE=$SERVICE"
echo "CLIENTS=$CLIENTS"

echo "== start mini_servicemgr =="
bin/mini_servicemgr > logs/android_like_unique_binder_return_sm.log 2>&1 &
echo "$!" > run/android_like_unique_binder_return_sm.pid

sleep 2

echo "== start binder-return service unique mode =="
bin/android_like_aidl_binder_return_service "$SERVICE" "$CLIENTS" --unique-children > logs/android_like_unique_binder_return_service.log 2>&1 &
echo "$!" > run/android_like_unique_binder_return_service.pid

sleep 3

echo "== launch $CLIENTS binder-return clients =="
i=1
while [ "$i" -le "$CLIENTS" ]; do
  bin/android_like_aidl_binder_return_client "$SERVICE" > "logs/android_like_unique_binder_return_client_$i.log" 2>&1 &
  echo "$!" > "run/android_like_unique_binder_return_client_$i.pid"
  i=$((i + 1))
done

fail=0

echo "== wait clients =="
i=1
while [ "$i" -le "$CLIENTS" ]; do
  pid="$(cat "run/android_like_unique_binder_return_client_$i.pid")"

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

echo "== wait unique lifecycle exact marker =="
if ! wait_for_marker 'AIDL_LIKE_BINDER_RETURN_UNIQUE_CHILD_LIFECYCLE_EXACT_OK' logs/android_like_unique_binder_return_service.log 60; then
  echo "FAIL: service did not observe exact lifecycle cleanup"
  echo "== service log =="
  cat logs/android_like_unique_binder_return_service.log || true
  exit 1
fi

echo "== client marker summary =="
i=1
while [ "$i" -le "$CLIENTS" ]; do
  log="logs/android_like_unique_binder_return_client_$i.log"

  echo "--- client $i markers ---"
  grep 'AIDL_LIKE_BINDER_RETURN' "$log" || true

  grep -q 'AIDL_LIKE_BINDER_RETURN_HANDLE_OK' "$log" || fail=1
  grep -q 'AIDL_LIKE_BINDER_RETURN_CHILD_CALL_OK' "$log" || fail=1
  grep -q 'AIDL_LIKE_BINDER_RETURN_CLIENT_SMOKE_OK' "$log" || fail=1

  i=$((i + 1))
done

if [ "$fail" -ne 0 ]; then
  echo "FAIL: one or more unique binder-return clients failed"
  echo "== service log =="
  cat logs/android_like_unique_binder_return_service.log || true
  echo "== servicemgr log =="
  cat logs/android_like_unique_binder_return_sm.log || true
  exit 1
fi

object_sent_count="$(grep -c 'AIDL_LIKE_BINDER_RETURN_CHILD_OBJECT_SENT' logs/android_like_unique_binder_return_service.log || true)"
child_call_count="$(grep -c 'AIDL_LIKE_BINDER_RETURN_CHILD_CALL_OK' logs/android_like_unique_binder_return_service.log || true)"
release_count="$(grep -c 'AIDL_LIKE_BINDER_RETURN_CHILD_RELEASE' logs/android_like_unique_binder_return_service.log || true)"
decrefs_count="$(grep -c 'AIDL_LIKE_BINDER_RETURN_CHILD_DECREFS' logs/android_like_unique_binder_return_service.log || true)"
unique_ok_count="$(grep -c 'AIDL_LIKE_BINDER_RETURN_UNIQUE_CHILD_LIFECYCLE_EXACT_OK' logs/android_like_unique_binder_return_service.log || true)"

client_handle_count=0
client_child_count=0
client_smoke_count=0

i=1
while [ "$i" -le "$CLIENTS" ]; do
  log="logs/android_like_unique_binder_return_client_$i.log"

  c="$(grep -c 'AIDL_LIKE_BINDER_RETURN_HANDLE_OK' "$log" || true)"
  client_handle_count="$((client_handle_count + c))"

  c="$(grep -c 'AIDL_LIKE_BINDER_RETURN_CHILD_CALL_OK' "$log" || true)"
  client_child_count="$((client_child_count + c))"

  c="$(grep -c 'AIDL_LIKE_BINDER_RETURN_CLIENT_SMOKE_OK' "$log" || true)"
  client_smoke_count="$((client_smoke_count + c))"

  i=$((i + 1))
done

echo "== unique binder return counts =="
echo "object_sent_count=$object_sent_count"
echo "child_call_count=$child_call_count"
echo "release_count=$release_count"
echo "decrefs_count=$decrefs_count"
echo "unique_ok_count=$unique_ok_count"
echo "client_handle_count=$client_handle_count"
echo "client_child_count=$client_child_count"
echo "client_smoke_count=$client_smoke_count"
echo "expected=$CLIENTS"

if [ "$object_sent_count" -lt "$CLIENTS" ]; then
  echo "FAIL: object_sent_count too low"
  exit 1
fi

if [ "$child_call_count" -lt "$CLIENTS" ]; then
  echo "FAIL: child_call_count too low"
  exit 1
fi

if [ "$release_count" -lt "$CLIENTS" ]; then
  echo "FAIL: release_count too low"
  exit 1
fi

if [ "$decrefs_count" -lt "$CLIENTS" ]; then
  echo "FAIL: decrefs_count too low"
  exit 1
fi

if [ "$unique_ok_count" -lt 1 ]; then
  echo "FAIL: unique lifecycle exact marker missing"
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

echo "== service unique lifecycle markers =="
grep 'AIDL_LIKE_BINDER_RETURN' logs/android_like_unique_binder_return_service.log || true

echo "AIDL_LIKE_BINDER_RETURN_UNIQUE_LIFECYCLE_SMOKE_TV_OK"
TVSH
