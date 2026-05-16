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
  modules/binder.ko \
  load-binder-tv.sh
do
  if [ ! -e "$f" ]; then
    echo "ERROR: missing $SIDE_DIR/$f"
    echo "Directory tree:"
    find . -maxdepth 3 -type f -o -type d | sort
    exit 1
  fi
done

chmod +x bin/mini_servicemgr bin/echo_service bin/echo_client load-binder-tv.sh

mkdir -p logs run

if [ ! -e /dev/binder ]; then
  echo "== load binder =="
  ./load-binder-tv.sh "$SIDE_DIR/modules/binder.ko"
else
  echo "== binder already loaded =="
  ls -l /dev/binder
fi

killall mini_servicemgr 2>/dev/null || true
killall echo_service 2>/dev/null || true
killall echo_client 2>/dev/null || true

rm -f logs/death_*.log run/death_*.pid

cleanup() {
  [ -f run/death_service.pid ] && kill "$(cat run/death_service.pid)" 2>/dev/null || true
  [ -f run/death_sm.pid ] && kill "$(cat run/death_sm.pid)" 2>/dev/null || true
}
trap cleanup EXIT

run_client_limited() {
  name="$1"
  msg="$2"
  log="$3"

  bin/echo_client "$name" "$msg" > "$log" 2>&1 &
  pid="$!"

  i=0
  while kill -0 "$pid" 2>/dev/null; do
    i=$((i + 1))
    if [ "$i" -ge 10 ]; then
      echo "client timeout, killing pid=$pid" >> "$log"
      kill "$pid" 2>/dev/null || true
      wait "$pid" 2>/dev/null || true
      return 124
    fi
    sleep 1
  done

  wait "$pid"
}

echo "== start mini_servicemgr =="
bin/mini_servicemgr > logs/death_mini_servicemgr.log 2>&1 &
echo "$!" > run/death_sm.pid
sleep 2

echo "== start echo_service test.death =="
bin/echo_service test.death > logs/death_echo_service.log 2>&1 &
echo "$!" > run/death_service.pid
sleep 3

echo "== before death: client should succeed =="
set +e
run_client_limited test.death "before death" logs/death_client_before.log
before_rc="$?"
set -e

cat logs/death_client_before.log

if [ "$before_rc" -ne 0 ]; then
  echo "FAIL: client before death returned $before_rc"
  echo "== mini_servicemgr log =="
  cat logs/death_mini_servicemgr.log || true
  echo "== echo_service log =="
  cat logs/death_echo_service.log || true
  exit 1
fi

echo "== kill echo_service =="
kill "$(cat run/death_service.pid)" 2>/dev/null || true
rm -f run/death_service.pid
sleep 2

echo "== after death: client should fail cleanly =="
set +e
run_client_limited test.death "after death" logs/death_client_after.log
after_rc="$?"
set -e

cat logs/death_client_after.log || true

echo "== mini_servicemgr log =="
cat logs/death_mini_servicemgr.log || true

echo "== echo_service log =="
cat logs/death_echo_service.log || true

echo "== relevant dmesg =="
dmesg | tail -n 180 | grep -i -E 'binder|oops|panic|fault|unable|segv|dead' || true

if [ "$after_rc" -eq 0 ]; then
  echo "FAIL: client unexpectedly succeeded after service death"
  exit 1
fi

if grep -q -E 'BR_DEAD_BINDER|BR_DEAD_REPLY|BR_FAILED_REPLY|NOT FOUND|getService failed|service died|death|dead' \
  logs/death_mini_servicemgr.log \
  logs/death_client_after.log
then
  echo "DEATH_SMOKE_OK after_rc=$after_rc"
  exit 0
fi

echo "FAIL: after-death client failed, but expected death/failure marker was not found"
exit 1
TVSH
