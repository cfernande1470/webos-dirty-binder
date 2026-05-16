#!/usr/bin/env bash
set -euo pipefail

TV_IP="${TV_IP:-192.168.2.121}"
SIDE_DIR="${SIDE_DIR:-/media/internal/android-sidecar}"

ssh root@"$TV_IP" "SIDE_DIR='$SIDE_DIR' sh -s" <<'TVSH'
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

if [ ! -e /dev/binder ]; then
  ./load-binder-tv.sh "$SIDE_DIR/modules/binder.ko"
fi

mkdir -p logs run

killall mini_servicemgr 2>/dev/null || true
killall echo_service 2>/dev/null || true
killall echo_client 2>/dev/null || true
killall list_services 2>/dev/null || true
killall aosp_sm_probe 2>/dev/null || true

rm -f logs/aosp_sm_*.log run/aosp_sm_*.pid

cleanup() {
  [ -f run/aosp_sm_service.pid ] && kill "$(cat run/aosp_sm_service.pid)" 2>/dev/null || true
  [ -f run/aosp_sm_mgr.pid ] && kill "$(cat run/aosp_sm_mgr.pid)" 2>/dev/null || true
}
trap cleanup EXIT

echo "== start mini_servicemgr =="
bin/mini_servicemgr > logs/aosp_sm_mgr.log 2>&1 &
echo "$!" > run/aosp_sm_mgr.pid
sleep 2

echo "== start echo_service test.aosp =="
bin/echo_service test.aosp > logs/aosp_sm_echo_service.log 2>&1 &
echo "$!" > run/aosp_sm_service.pid
sleep 3

echo "== baseline custom protocol still works =="
bin/echo_client test.aosp "baseline before AOSP probe" > logs/aosp_sm_baseline_client.log 2>&1
cat logs/aosp_sm_baseline_client.log
grep -q 'echo-client reply status=0' logs/aosp_sm_baseline_client.log

echo "== AOSP probe =="
if [ ! -x bin/aosp_sm_probe ]; then
  echo "ERROR: bin/aosp_sm_probe missing"
  echo "Next step: build and install aosp_sm_probe"
  exit 2
fi

set +e
bin/aosp_sm_probe test.aosp > logs/aosp_sm_probe.log 2>&1
probe_rc="$?"
set -e

cat logs/aosp_sm_probe.log

if [ "$probe_rc" -ne 0 ]; then
  echo "FAIL: aosp_sm_probe rc=$probe_rc"
  echo "== mini_servicemgr log =="
  cat logs/aosp_sm_mgr.log || true
  echo "== echo_service log =="
  cat logs/aosp_sm_echo_service.log || true
  exit "$probe_rc"
fi

grep -q 'AOSP_LIST_SERVICES_OK' logs/aosp_sm_probe.log
grep -q 'AOSP_SM_COMPAT_OK' logs/aosp_sm_probe.log

echo "AOSP_SM_COMPAT_SMOKE_OK"
TVSH
