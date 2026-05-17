#!/usr/bin/env bash
set -euo pipefail

TV_IP="${TV_IP:-192.168.2.121}"
SIDE_DIR="${SIDE_DIR:-/media/internal/android-sidecar}"
SERVICE="${SERVICE:-test.android.aidl.oneway.stress}"
CLIENTS="${CLIENTS:-8}"
ROUNDS="${ROUNDS:-250}"

ssh root@"$TV_IP" "SIDE_DIR='$SIDE_DIR' SERVICE='$SERVICE' CLIENTS='$CLIENTS' ROUNDS='$ROUNDS' sh -s" <<'TVSH'
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

rm -f logs/android_like_oneway_stress_*.log run/android_like_oneway_stress_*.pid

cleanup() {
  for pidfile in run/android_like_oneway_stress_client_*.pid; do
    [ -e "$pidfile" ] || continue
    kill "$(cat "$pidfile")" 2>/dev/null || true
  done

  [ -f run/android_like_oneway_stress_service.pid ] && kill "$(cat run/android_like_oneway_stress_service.pid)" 2>/dev/null || true
  [ -f run/android_like_oneway_stress_sm.pid ] && kill "$(cat run/android_like_oneway_stress_sm.pid)" 2>/dev/null || true
}

trap cleanup EXIT

echo "== aidl oneway stress config =="
echo "SERVICE=$SERVICE"
echo "CLIENTS=$CLIENTS"
echo "ROUNDS=$ROUNDS"

echo "== start mini_servicemgr =="
bin/mini_servicemgr > logs/android_like_oneway_stress_sm.log 2>&1 &
echo "$!" > run/android_like_oneway_stress_sm.pid

sleep 2

echo "== start oneway service =="
bin/android_like_aidl_oneway_service "$SERVICE" > logs/android_like_oneway_stress_service.log 2>&1 &
echo "$!" > run/android_like_oneway_stress_service.pid

sleep 3

echo "== launch $CLIENTS oneway clients, rounds=$ROUNDS =="
i=1
while [ "$i" -le "$CLIENTS" ]; do
  bin/android_like_aidl_oneway_client "$SERVICE" "$ROUNDS" --expect-at-least > "logs/android_like_oneway_stress_client_$i.log" 2>&1 &
  echo "$!" > "run/android_like_oneway_stress_client_$i.pid"
  i=$((i + 1))
done

fail=0

echo "== wait clients =="
i=1
while [ "$i" -le "$CLIENTS" ]; do
  pid="$(cat "run/android_like_oneway_stress_client_$i.pid")"

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
  log="logs/android_like_oneway_stress_client_$i.log"

  echo "--- client $i markers tail ---"
  grep 'AIDL_LIKE_ONEWAY' "$log" | tail -n 8 || true

  grep -q 'AIDL_LIKE_ONEWAY_NOTIFY_SENT' "$log" || fail=1
  grep -q 'AIDL_LIKE_ONEWAY_GET_COUNT_OK' "$log" || fail=1
  grep -q 'AIDL_LIKE_ONEWAY_CLIENT_SMOKE_OK' "$log" || fail=1

  if grep -q 'unexpected BR_REPLY for one-way notify' "$log"; then
    fail=1
  fi

  i=$((i + 1))
done

if [ "$fail" -ne 0 ]; then
  echo "FAIL: one or more oneway stress clients failed"
  echo "== service log tail =="
  tail -n 200 logs/android_like_oneway_stress_service.log || true
  echo "== servicemgr log =="
  cat logs/android_like_oneway_stress_sm.log || true
  exit 1
fi

echo "AIDL_LIKE_ONEWAY_STRESS_CLIENTS_OK"

expected="$((CLIENTS * ROUNDS))"

service_registered_count="$(grep -c 'AIDL_LIKE_ONEWAY_SERVICE_REGISTERED' logs/android_like_oneway_stress_service.log || true)"
notify_count="$(grep -c 'AIDL_LIKE_ONEWAY_NOTIFY_OK' logs/android_like_oneway_stress_service.log || true)"

client_sent_count=0
client_get_count=0
client_smoke_count=0

i=1
while [ "$i" -le "$CLIENTS" ]; do
  log="logs/android_like_oneway_stress_client_$i.log"

  c="$(grep -c 'AIDL_LIKE_ONEWAY_NOTIFY_SENT' "$log" || true)"
  client_sent_count="$((client_sent_count + c))"

  c="$(grep -c 'AIDL_LIKE_ONEWAY_GET_COUNT_OK' "$log" || true)"
  client_get_count="$((client_get_count + c))"

  c="$(grep -c 'AIDL_LIKE_ONEWAY_CLIENT_SMOKE_OK' "$log" || true)"
  client_smoke_count="$((client_smoke_count + c))"

  i=$((i + 1))
done

echo "== oneway stress counts =="
echo "service_registered_count=$service_registered_count"
echo "notify_count=$notify_count"
echo "client_sent_count=$client_sent_count"
echo "client_get_count=$client_get_count"
echo "client_smoke_count=$client_smoke_count"
echo "expected=$expected"

if [ "$service_registered_count" -lt 1 ]; then
  echo "FAIL: service did not register"
  exit 1
fi

if [ "$notify_count" -lt "$expected" ]; then
  echo "FAIL: notify_count too low"
  tail -n 200 logs/android_like_oneway_stress_service.log || true
  exit 1
fi

if [ "$client_sent_count" -lt "$expected" ]; then
  echo "FAIL: client_sent_count too low"
  exit 1
fi

if [ "$client_get_count" -lt "$CLIENTS" ]; then
  echo "FAIL: client_get_count too low"
  exit 1
fi

if [ "$client_smoke_count" -lt "$CLIENTS" ]; then
  echo "FAIL: client_smoke_count too low"
  exit 1
fi

echo "AIDL_LIKE_ONEWAY_STRESS_SERVICE_COUNTS_OK"

echo "== service markers tail =="
grep 'AIDL_LIKE_ONEWAY' logs/android_like_oneway_stress_service.log | tail -n 40 || true

echo "AIDL_LIKE_ONEWAY_STRESS_SMOKE_TV_OK"
TVSH
