#!/usr/bin/env bash
set -euo pipefail

TV_IP="${TV_IP:-192.168.2.121}"
SIDE_DIR="${SIDE_DIR:-/media/internal/android-sidecar}"
SERVICE="${SERVICE:-test.android.fddevnull}"

ssh root@"$TV_IP" "SIDE_DIR='$SIDE_DIR' SERVICE='$SERVICE' sh -s" <<'TVSH'
set -eu

cd "$SIDE_DIR"

for f in \
  bin/mini_servicemgr \
  bin/android_like_fd_devnull_service \
  bin/android_like_fd_devnull_client \
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
  bin/android_like_fd_devnull_service \
  bin/android_like_fd_devnull_client \
  load-binder-tv.sh

mkdir -p logs run

echo "== pre dmesg binder tail =="
dmesg 2>/dev/null | grep -Ei 'binder|panic|oops|watchdog|failed|fatal|exception' | tail -80 || true

if [ ! -e /dev/binder ]; then
  ./load-binder-tv.sh "$SIDE_DIR/modules/binder.ko"
fi

killall mini_servicemgr 2>/dev/null || true
killall android_like_fd_devnull_service 2>/dev/null || true
killall android_like_fd_devnull_client 2>/dev/null || true
killall android_like_fd_object_service 2>/dev/null || true
killall android_like_fd_object_client 2>/dev/null || true

rm -f logs/android_like_fd_devnull_*.log run/android_like_fd_devnull_*.pid

cleanup() {
  [ -f run/android_like_fd_devnull_service.pid ] && kill "$(cat run/android_like_fd_devnull_service.pid)" 2>/dev/null || true
  [ -f run/android_like_fd_devnull_sm.pid ] && kill "$(cat run/android_like_fd_devnull_sm.pid)" 2>/dev/null || true
}

trap cleanup EXIT

echo "== start mini_servicemgr =="
bin/mini_servicemgr > logs/android_like_fd_devnull_sm.log 2>&1 &
echo "$!" > run/android_like_fd_devnull_sm.pid

sleep 2

echo "== start fd devnull service $SERVICE =="
bin/android_like_fd_devnull_service "$SERVICE" > logs/android_like_fd_devnull_service.log 2>&1 &
echo "$!" > run/android_like_fd_devnull_service.pid

sleep 3

echo "== run fd devnull client =="
set +e
bin/android_like_fd_devnull_client "$SERVICE" > logs/android_like_fd_devnull_client.log 2>&1
client_rc="$?"
set -e

echo "== fd devnull client log =="
cat logs/android_like_fd_devnull_client.log || true

echo "== fd devnull service log =="
cat logs/android_like_fd_devnull_service.log || true

echo "== mini_servicemgr log tail =="
tail -160 logs/android_like_fd_devnull_sm.log || true

echo "== post dmesg binder tail =="
dmesg 2>/dev/null | grep -Ei 'binder|panic|oops|watchdog|failed|fatal|exception' | tail -120 || true

if [ "$client_rc" -ne 0 ]; then
  echo "FAIL: android_like_fd_devnull_client rc=$client_rc"
  exit "$client_rc"
fi

grep -q 'ANDROID_LIKE_FD_DEVNULL_SERVICE_REGISTERED' logs/android_like_fd_devnull_service.log
grep -q 'ANDROID_LIKE_FD_DEVNULL_SERVICE_GOT_FD' logs/android_like_fd_devnull_service.log
grep -q 'ANDROID_LIKE_FD_DEVNULL_SERVICE_FSTAT_OK' logs/android_like_fd_devnull_service.log
grep -q 'ANDROID_LIKE_FD_DEVNULL_CLIENT_REPLY_OK' logs/android_like_fd_devnull_client.log
grep -q 'ANDROID_LIKE_FD_DEVNULL_SMOKE_OK' logs/android_like_fd_devnull_client.log

echo "ANDROID_LIKE_FD_DEVNULL_SMOKE_TV_OK"
TVSH
