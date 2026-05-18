#!/usr/bin/env bash
set -euo pipefail

TV_IP="${TV_IP:-192.168.2.121}"
SIDE_DIR="${SIDE_DIR:-/tmp/android-usb/android-sidecar}"
SERVICE="${SERVICE:-test.rebind}"

ssh root@"$TV_IP" "SIDE_DIR='$SIDE_DIR' SERVICE='$SERVICE' sh -s" <<'TVSH'
set -eu

cd "$SIDE_DIR"

for f in \
  bin/mini_servicemgr \
  bin/echo_service \
  bin/echo_client \
  bin/list_services \
  modules/binder.ko \
  load-binder-tv.sh
do
  if [ ! -e "$f" ]; then
    echo "ERROR: missing $SIDE_DIR/$f"
    find . -maxdepth 3 -type f -o -type d | sort
    exit 1
  fi
done

chmod +x bin/mini_servicemgr bin/echo_service bin/echo_client bin/list_services load-binder-tv.sh

mkdir -p logs run

if [ ! -e /dev/binder ]; then
  ./load-binder-tv.sh "$SIDE_DIR/modules/binder.ko"
fi

killall mini_servicemgr 2>/dev/null || true
killall echo_service 2>/dev/null || true
killall echo_client 2>/dev/null || true
killall list_services 2>/dev/null || true

rm -f logs/rebind_*.log run/rebind_*.pid

cleanup() {
  [ -f run/rebind_service.pid ] && kill "$(cat run/rebind_service.pid)" 2>/dev/null || true
  [ -f run/rebind_sm.pid ] && kill "$(cat run/rebind_sm.pid)" 2>/dev/null || true
}
trap cleanup EXIT

wait_list_contains() {
  name="$1"
  tries=20

  while [ "$tries" -gt 0 ]; do
    bin/list_services > logs/rebind_list_probe.log 2>&1 || true
    if grep -q "^$name$" logs/rebind_list_probe.log; then
      return 0
    fi
    tries=$((tries - 1))
    sleep 1
  done

  echo "FAIL: service did not appear in listServices: $name"
  cat logs/rebind_list_probe.log || true
  return 1
}

wait_list_absent() {
  name="$1"
  tries=20

  while [ "$tries" -gt 0 ]; do
    bin/list_services > logs/rebind_list_probe.log 2>&1 || true
    if ! grep -q "^$name$" logs/rebind_list_probe.log; then
      return 0
    fi
    tries=$((tries - 1))
    sleep 1
  done

  echo "FAIL: service did not disappear from listServices: $name"
  cat logs/rebind_list_probe.log || true
  return 1
}

start_service() {
  label="$1"

  echo "== start $SERVICE $label =="
  bin/echo_service "$SERVICE" > "logs/rebind_service_${label}.log" 2>&1 &
  echo "$!" > run/rebind_service.pid
  sleep 2

  wait_list_contains "$SERVICE"
}

call_service() {
  label="$1"

  echo "== call $SERVICE $label =="
  bin/echo_client "$SERVICE" "hello $label" > "logs/rebind_client_${label}.log" 2>&1
  cat "logs/rebind_client_${label}.log"

  grep -q 'echo-client reply status=0' "logs/rebind_client_${label}.log"
}

kill_service_and_wait_absent() {
  label="$1"

  echo "== kill $SERVICE $label =="
  kill "$(cat run/rebind_service.pid)" 2>/dev/null || true
  rm -f run/rebind_service.pid

  wait_list_absent "$SERVICE"

  echo "== list after kill $label =="
  bin/list_services > "logs/rebind_list_after_kill_${label}.log" 2>&1
  cat "logs/rebind_list_after_kill_${label}.log"

  if grep -q "^$SERVICE$" "logs/rebind_list_after_kill_${label}.log"; then
    echo "FAIL: $SERVICE still listed after kill $label"
    exit 1
  fi
}

echo "== start mini_servicemgr =="
bin/mini_servicemgr > logs/rebind_mini_servicemgr.log 2>&1 &
echo "$!" > run/rebind_sm.pid
sleep 2

echo "== initial list =="
bin/list_services > logs/rebind_initial_list.log 2>&1
cat logs/rebind_initial_list.log

start_service first
call_service first
kill_service_and_wait_absent first

start_service second
call_service second
kill_service_and_wait_absent second

echo "== final getService should fail =="
set +e
bin/echo_client "$SERVICE" "should be gone" > logs/rebind_final_client.log 2>&1
final_rc="$?"
set -e
cat logs/rebind_final_client.log

if [ "$final_rc" -eq 0 ]; then
  echo "FAIL: final client unexpectedly succeeded"
  exit 1
fi

grep -q -E 'NOT FOUND|getService failed' logs/rebind_final_client.log

echo "== mini_servicemgr tail =="
tail -n 180 logs/rebind_mini_servicemgr.log || true

echo "== relevant dmesg tail =="
dmesg | tail -n 180 | grep -i -E 'binder|oops|panic|fault|unable|segv|dead' || true

echo "REBIND_SMOKE_OK"
TVSH
