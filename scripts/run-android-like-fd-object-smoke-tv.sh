#!/usr/bin/env bash
set -euo pipefail

echo "ERROR: Binder FD object smoke is quarantined because it can freeze/reboot the TV."
echo "Use SCM_RIGHTS FD bridge instead."
exit 99

#!/usr/bin/env bash
set -euo pipefail

TV_IP="${TV_IP:-192.168.2.121}"
SIDE_DIR="${SIDE_DIR:-/media/internal/android-sidecar}"
SERVICE="${SERVICE:-test.android.fdobject}"

ssh root@"$TV_IP" "SIDE_DIR='$SIDE_DIR' SERVICE='$SERVICE' sh -s" <<'TVSH'
set -eu

cd "$SIDE_DIR"

for f in \
  bin/mini_servicemgr \
  bin/android_like_fd_object_service \
  bin/android_like_fd_object_client \
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
  bin/android_like_fd_object_service \
  bin/android_like_fd_object_client \
  load-binder-tv.sh

mkdir -p logs run

if [ ! -e /dev/binder ]; then
  ./load-binder-tv.sh "$SIDE_DIR/modules/binder.ko"
fi

killall mini_servicemgr 2>/dev/null || true
killall android_like_fd_object_service 2>/dev/null || true
killall android_like_fd_object_client 2>/dev/null || true

rm -f logs/android_like_fd_object_*.log run/android_like_fd_object_*.pid

cleanup() {
  [ -f run/android_like_fd_object_service.pid ] && kill "$(cat run/android_like_fd_object_service.pid)" 2>/dev/null || true
  [ -f run/android_like_fd_object_sm.pid ] && kill "$(cat run/android_like_fd_object_sm.pid)" 2>/dev/null || true
}

trap cleanup EXIT

echo "== start mini_servicemgr =="
bin/mini_servicemgr > logs/android_like_fd_object_sm.log 2>&1 &
echo "$!" > run/android_like_fd_object_sm.pid

sleep 2

echo "== start fd object service $SERVICE =="
bin/android_like_fd_object_service "$SERVICE" > logs/android_like_fd_object_service.log 2>&1 &
echo "$!" > run/android_like_fd_object_service.pid

sleep 3

echo "== run fd object client =="
set +e
bin/android_like_fd_object_client "$SERVICE" > logs/android_like_fd_object_client.log 2>&1
client_rc="$?"
set -e

echo "== fd object client log =="
cat logs/android_like_fd_object_client.log || true

echo "== fd object service log =="
cat logs/android_like_fd_object_service.log || true

if [ "$client_rc" -ne 0 ]; then
  echo "FAIL: android_like_fd_object_client rc=$client_rc"
  echo "== mini_servicemgr log =="
  cat logs/android_like_fd_object_sm.log || true
  exit "$client_rc"
fi

grep -q 'ANDROID_LIKE_FD_OBJECT_SERVICE_REGISTERED' logs/android_like_fd_object_service.log
grep -q 'ANDROID_LIKE_FD_OBJECT_SERVICE_GOT_FD' logs/android_like_fd_object_service.log
grep -q 'ANDROID_LIKE_FD_OBJECT_SERVICE_READ_OK' logs/android_like_fd_object_service.log
grep -q 'ANDROID_LIKE_FD_OBJECT_CLIENT_REPLY_OK' logs/android_like_fd_object_client.log
grep -q 'ANDROID_LIKE_FD_OBJECT_SMOKE_OK' logs/android_like_fd_object_client.log

echo "ANDROID_LIKE_FD_OBJECT_SMOKE_TV_OK"
TVSH
