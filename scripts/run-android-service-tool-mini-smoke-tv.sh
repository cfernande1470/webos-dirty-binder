#!/usr/bin/env bash
set -euo pipefail

TV_IP="${TV_IP:-192.168.2.121}"
SIDE_DIR="${SIDE_DIR:-/tmp/android-usb/android-sidecar}"
ROOTFS="${ROOTFS:-/tmp/android-usb/android-rootfs}"
SERVICE="${SERVICE:-test.android.parcelfd}"

ssh root@"$TV_IP" "SIDE_DIR='$SIDE_DIR' ROOTFS='$ROOTFS' SERVICE='$SERVICE' sh -s" <<'TVSH'
set -eu

echo "== Android service tool against mini_servicemgr smoke =="
date || true
uname -a || true

ROOTFS="$(readlink -f "$ROOTFS" 2>/dev/null || echo "$ROOTFS")"

cd "$SIDE_DIR"

for f in \
  bin/mini_servicemgr \
  bin/parcel_fd_lite_service \
  bin/parcel_fd_lite_client \
  load-binder-tv.sh \
  modules/binder.ko
do
  if [ ! -e "$f" ]; then
    echo "ERROR: missing $SIDE_DIR/$f"
    find "$SIDE_DIR" -maxdepth 3 \( -type f -o -type d \) | sort
    exit 1
  fi
done

if [ ! -x "$ROOTFS/system/bin/service" ]; then
  echo "ERROR: missing Android service tool at $ROOTFS/system/bin/service"
  ls -lh "$ROOTFS/system/bin/service" 2>/dev/null || true
  exit 1
fi

if [ ! -e "$ROOTFS/dev/binder" ]; then
  echo "ERROR: missing $ROOTFS/dev/binder"
  exit 1
fi

mkdir -p logs run "$ROOTFS/run"

echo
echo "== load binder if needed =="
if ! grep -q '^binder ' /proc/modules; then
  ./load-binder-tv.sh "$SIDE_DIR/modules/binder.ko"
else
  echo "binder already loaded"
fi

echo
echo "== clean old processes =="
killall mini_servicemgr 2>/dev/null || true
killall servicemanager 2>/dev/null || true
killall parcel_fd_lite_service 2>/dev/null || true
killall parcel_fd_lite_client 2>/dev/null || true

rm -f logs/android_service_tool_mini_*.log run/android_service_tool_mini_*.pid
rm -f "$ROOTFS/run/parcel_fd_lite.sock"

cleanup() {
  [ -f run/android_service_tool_mini_pfd.pid ] && kill "$(cat run/android_service_tool_mini_pfd.pid)" 2>/dev/null || true
  [ -f run/android_service_tool_mini_sm.pid ] && kill "$(cat run/android_service_tool_mini_sm.pid)" 2>/dev/null || true
  rm -f "$ROOTFS/run/parcel_fd_lite.sock"
}
trap cleanup EXIT

echo
echo "== start mini_servicemgr =="
bin/mini_servicemgr > logs/android_service_tool_mini_sm.log 2>&1 &
echo "$!" > run/android_service_tool_mini_sm.pid

sleep 2

echo
echo "== start ParcelFD-lite service =="
bin/parcel_fd_lite_service "$SERVICE" "$ROOTFS/run/parcel_fd_lite.sock" > logs/android_service_tool_mini_pfd_service.log 2>&1 &
echo "$!" > run/android_service_tool_mini_pfd.pid

sleep 3

echo
echo "== Android /system/bin/service list =="
set +e
chroot "$ROOTFS" /system/bin/service list > logs/android_service_tool_mini_service_list.log 2>&1
SERVICE_LIST_RC="$?"
set -e
echo "service_list_rc=$SERVICE_LIST_RC"
cat logs/android_service_tool_mini_service_list.log || true

echo
echo "== Android /system/bin/service check $SERVICE =="
set +e
chroot "$ROOTFS" /system/bin/service check "$SERVICE" > logs/android_service_tool_mini_service_check.log 2>&1
SERVICE_CHECK_RC="$?"
set -e
echo "service_check_rc=$SERVICE_CHECK_RC"
cat logs/android_service_tool_mini_service_check.log || true

echo
echo "== native ParcelFD-lite client sanity =="
set +e
bin/parcel_fd_lite_client "$SERVICE" "$ROOTFS/run/parcel_fd_lite.sock" > logs/android_service_tool_mini_pfd_client.log 2>&1
CLIENT_RC="$?"
set -e
echo "client_rc=$CLIENT_RC"
cat logs/android_service_tool_mini_pfd_client.log || true

echo
echo "== mini_servicemgr log =="
cat logs/android_service_tool_mini_sm.log || true

echo
echo "== ParcelFD-lite service log =="
cat logs/android_service_tool_mini_pfd_service.log || true

if [ "$CLIENT_RC" -eq 0 ] &&
   grep -q 'PARCELFD_LITE_SMOKE_OK' logs/android_service_tool_mini_pfd_client.log; then
  echo "ANDROID_SERVICE_TOOL_MINI_NATIVE_PFD_OK"
else
  echo "ANDROID_SERVICE_TOOL_MINI_NATIVE_PFD_FAIL"
  exit 1
fi

if grep -q "$SERVICE" logs/android_service_tool_mini_service_list.log || \
   grep -qi 'found' logs/android_service_tool_mini_service_check.log || \
   grep -qi "$SERVICE" logs/android_service_tool_mini_service_check.log; then
  echo "ANDROID_SERVICE_TOOL_MINI_ANDROID_CLIENT_SEES_SERVICE"
else
  echo "ANDROID_SERVICE_TOOL_MINI_ANDROID_CLIENT_NO_SERVICE_YET"
fi

echo
echo "ANDROID_SERVICE_TOOL_MINI_SMOKE_DONE"
TVSH
