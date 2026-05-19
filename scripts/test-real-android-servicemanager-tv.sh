#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

TV_IP="${TV_IP:-192.168.2.121}"
SIDE_DIR="${SIDE_DIR:-/tmp/android-usb/android-sidecar}"
SERVICE="${SERVICE:-test.android.real.sm.fd}"
ROUNDS="${ROUNDS:-1}"
WAIT_SECS="${WAIT_SECS:-90}"

echo "== load final Binder fd_debug_stage=7 =="
TV_IP="$TV_IP" FD_DEBUG_STAGE=7 scripts/reload-build-binder-tv.sh

echo "== deploy sidecar binaries =="
TV_IP="$TV_IP" SIDE_DIR="$SIDE_DIR" scripts/deploy-fd-stage-sidecar-tv.sh

echo "== start Android USB with REAL /system/bin/servicemanager =="
TV_IP="$TV_IP" \
FD_DEBUG_STAGE=7 \
START_SERVICEMANAGER=1 \
NO_BUILD=1 \
scripts/install-android-usb.sh

echo "== wait for real Android servicemanager =="
ssh root@"$TV_IP" "SIDE_DIR='$SIDE_DIR' WAIT_SECS='$WAIT_SECS' sh -s" <<'TVWAIT'
set -eu

find_real_sm() {
  for p in /proc/[0-9]*; do
    [ -d "$p" ] || continue
    pid="${p##*/}"

    comm="$(cat "$p/comm" 2>/dev/null || true)"
    cmd=""
    exe=""

    if [ -r "$p/cmdline" ]; then
      cmd="$(tr '\000' ' ' < "$p/cmdline" 2>/dev/null || true)"
    fi
    exe="$(readlink "$p/exe" 2>/dev/null || true)"

    case "$comm|$cmd|$exe" in
      *mini_servicemgr*) ;;
      *"/system/bin/servicemanager"*|*"android-rootfs"*"/servicemanager"*|*"servicemanager"*)
        if [ "$comm" = "servicemanager" ] || echo "$cmd $exe" | grep -q 'system/bin/servicemanager\|android-rootfs.*/servicemanager'; then
          echo "$pid|$comm|$cmd|$exe"
          return 0
        fi
        ;;
    esac
  done

  return 1
}

i=0
while [ "$i" -lt "$WAIT_SECS" ]; do
  if sm="$(find_real_sm)"; then
    pid="${sm%%|*}"
    rest="${sm#*|}"
    comm="${rest%%|*}"
    rest="${rest#*|}"
    cmd="${rest%%|*}"
    exe="${rest#*|}"

    echo "REAL_ANDROID_SERVICEMANAGER_PID=$pid"
    echo "REAL_ANDROID_SERVICEMANAGER_COMM=$comm"
    echo "REAL_ANDROID_SERVICEMANAGER_CMD=$cmd"
    echo "REAL_ANDROID_SERVICEMANAGER_EXE=$exe"
    exit 0
  fi

  sleep 1
  i=$((i + 1))
done

echo "FAIL: real Android servicemanager did not appear"
echo "== binder =="
grep '^binder ' /proc/modules 2>/dev/null || true
cat /sys/module/binder/parameters/fd_debug_stage 2>/dev/null || true
ls -l /dev/binder 2>/dev/null || true

echo "== processes matching service =="
ps w 2>/dev/null | grep -i service | grep -v grep || true

echo "== android usb install log tail =="
tail -200 "$SIDE_DIR/logs/android-usb-install.log" 2>/dev/null || true

exit 10
TVWAIT

echo "== real servicemanager smoke =="
ssh root@"$TV_IP" "SIDE_DIR='$SIDE_DIR' SERVICE='$SERVICE' ROUNDS='$ROUNDS' sh -s" <<'TVSH'
set -eu

echo "== binder state =="
grep '^binder ' /proc/modules || true
cat /sys/module/binder/parameters/fd_debug_stage 2>/dev/null || true
ls -l /dev/binder 2>/dev/null || true

echo "== ensure mini_servicemgr is not running =="
if ps w 2>/dev/null | grep -v grep | grep -q 'mini_servicemgr'; then
  echo "FAIL: mini_servicemgr is running; not a real servicemanager test"
  ps w | grep mini_servicemgr | grep -v grep || true
  exit 11
fi

echo "== verify context manager already occupied =="
rm -f /tmp/mini-cmgr-probe.log
if [ -x "$SIDE_DIR/bin/mini_servicemgr" ]; then
  timeout 3 "$SIDE_DIR/bin/mini_servicemgr" >/tmp/mini-cmgr-probe.log 2>&1 || true
  cat /tmp/mini-cmgr-probe.log || true

  if grep -Eiq 'already set|Device or resource busy|errno=16|EBUSY|context manager' /tmp/mini-cmgr-probe.log; then
    echo "REAL_SM_CONTEXT_MANAGER_OK"
  else
    echo "WARN: context-manager probe did not show EBUSY; continuing"
  fi
fi

echo "== run FD service/client against real Android servicemanager =="
killall android_like_fd_passing_service android_like_fd_passing_client 2>/dev/null || true
rm -f /tmp/real-sm-fd-service.log /tmp/real-sm-fd-client.log

SERVICE="$SERVICE" ROUNDS="$ROUNDS" "$SIDE_DIR/bin/android_like_fd_passing_service" \
  >/tmp/real-sm-fd-service.log 2>&1 &
svc_pid="$!"

sleep 2

set +e
SERVICE="$SERVICE" ROUNDS="$ROUNDS" "$SIDE_DIR/bin/android_like_fd_passing_client" \
  >/tmp/real-sm-fd-client.log 2>&1
rc="$?"
set -e

sleep 1
kill "$svc_pid" 2>/dev/null || true

echo "== client log =="
cat /tmp/real-sm-fd-client.log || true

echo "== service log =="
cat /tmp/real-sm-fd-service.log || true

sent="$(grep -c 'BINDER_FD_OBJECT_SENT' /tmp/real-sm-fd-client.log 2>/dev/null || true)"
recv="$(grep -c 'BINDER_FD_OBJECT_RECEIVED' /tmp/real-sm-fd-service.log 2>/dev/null || true)"
readok="$(grep -c 'BINDER_FD_READ_OK' /tmp/real-sm-fd-service.log 2>/dev/null || true)"

echo "sent_count=$sent"
echo "received_count=$recv"
echo "read_count=$readok"
echo "expected=$ROUNDS"
echo "client_rc=$rc"

if [ "$rc" = 0 ] && [ "$sent" = "$ROUNDS" ] && [ "$recv" = "$ROUNDS" ] && [ "$readok" = "$ROUNDS" ]; then
  echo "REAL_ANDROID_SERVICEMANAGER_FD_SMOKE_OK"
  exit 0
fi

echo "FAIL: real Android servicemanager FD smoke failed"
exit 12
TVSH
