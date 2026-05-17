#!/usr/bin/env bash
set -euo pipefail

TV_IP="${TV_IP:-192.168.2.121}"
SIDE_DIR="${SIDE_DIR:-/media/internal/android-sidecar}"
ROOTFS="${ROOTFS:-/media/internal/android-rootfs}"
IMG_DIR="${IMG_DIR:-/media/internal/android-images}"
MNT_DIR="${MNT_DIR:-/media/internal/android-mounts}"
USB_DIR="${USB_DIR:-/media/internal/android-usb}"
SERVICE="${SERVICE:-test.android.parcelfd}"

ssh root@"$TV_IP" \
  "SIDE_DIR='$SIDE_DIR' ROOTFS='$ROOTFS' IMG_DIR='$IMG_DIR' MNT_DIR='$MNT_DIR' USB_DIR='$USB_DIR' SERVICE='$SERVICE' sh -s" <<'TVSH'
set -eu

echo "== Android real servicemanager v0 =="
date || true
uname -a || true
id || true

ROOTFS="$(readlink -f "$ROOTFS" 2>/dev/null || echo "$ROOTFS")"
IMG_DIR="$(readlink -f "$IMG_DIR" 2>/dev/null || echo "$IMG_DIR")"
MNT_DIR="$(readlink -f "$MNT_DIR" 2>/dev/null || echo "$MNT_DIR")"
USB_DIR="$(readlink -f "$USB_DIR" 2>/dev/null || echo "$USB_DIR")"

SYSTEM_IMG="$IMG_DIR/system.img"
VENDOR_IMG="$IMG_DIR/vendor.img"
SYS_RAW="$MNT_DIR/system_raw"
VENDOR_RAW="$MNT_DIR/vendor_raw"

echo
echo "== paths =="
echo "SIDE_DIR=$SIDE_DIR"
echo "ROOTFS=$ROOTFS"
echo "IMG_DIR=$IMG_DIR"
echo "MNT_DIR=$MNT_DIR"
echo "USB_DIR=$USB_DIR"
echo "SERVICE=$SERVICE"

cd "$SIDE_DIR"

for f in \
  bin/parcel_fd_lite_service \
  bin/parcel_fd_lite_client \
  modules/binder.ko \
  load-binder-tv.sh
do
  if [ ! -e "$f" ]; then
    echo "ERROR: missing $SIDE_DIR/$f"
    find "$SIDE_DIR" -maxdepth 3 \( -type f -o -type d \) | sort
    exit 1
  fi
done

chmod +x \
  bin/parcel_fd_lite_service \
  bin/parcel_fd_lite_client \
  load-binder-tv.sh

echo
echo "== load binder if needed =="
if ! grep -q '^binder ' /proc/modules; then
  ./load-binder-tv.sh "$SIDE_DIR/modules/binder.ko"
else
  echo "binder already loaded"
  grep '^binder ' /proc/modules || true
fi

if [ ! -e /dev/binder ]; then
  echo "ERROR: /dev/binder missing"
  exit 1
fi

echo
echo "== ensure synthetic rootfs mounts =="
mkdir -p \
  "$SYS_RAW" \
  "$VENDOR_RAW" \
  "$ROOTFS/system" \
  "$ROOTFS/vendor" \
  "$ROOTFS/apex" \
  "$ROOTFS/dev" \
  "$ROOTFS/proc" \
  "$ROOTFS/sys" \
  "$ROOTFS/run" \
  "$ROOTFS/tmp" \
  "$ROOTFS/data" \
  "$ROOTFS/cache" \
  "$USB_DIR/android-data" \
  "$USB_DIR/android-cache"

is_mounted() {
  grep -q " $1 " /proc/mounts 2>/dev/null
}

mount_if_needed_loop_ro() {
  img="$1"
  dst="$2"

  if is_mounted "$dst"; then
    echo "already mounted: $dst"
    return 0
  fi

  mount -o loop,ro "$img" "$dst"
}

bind_if_needed() {
  src="$1"
  dst="$2"

  if is_mounted "$dst"; then
    echo "already mounted: $dst"
    return 0
  fi

  mount -o bind "$src" "$dst"
}

mount_if_needed_loop_ro "$SYSTEM_IMG" "$SYS_RAW"
mount_if_needed_loop_ro "$VENDOR_IMG" "$VENDOR_RAW"

bind_if_needed "$SYS_RAW/system" "$ROOTFS/system"
bind_if_needed "$VENDOR_RAW" "$ROOTFS/vendor"
bind_if_needed "$SYS_RAW/system/apex" "$ROOTFS/apex"
bind_if_needed "$USB_DIR/android-data" "$ROOTFS/data"
bind_if_needed "$USB_DIR/android-cache" "$ROOTFS/cache"

if ! is_mounted "$ROOTFS/proc"; then
  mount -t proc proc "$ROOTFS/proc" 2>/dev/null || echo "WARN: proc mount failed"
fi

if ! is_mounted "$ROOTFS/sys"; then
  mount -t sysfs sysfs "$ROOTFS/sys" 2>/dev/null || echo "WARN: sysfs mount failed"
fi

rm -f "$ROOTFS/dev/binder" "$ROOTFS/dev/null" "$ROOTFS/dev/zero" "$ROOTFS/dev/random" "$ROOTFS/dev/urandom" 2>/dev/null || true

mknod "$ROOTFS/dev/binder" c 10 53 2>/dev/null || cp -a /dev/binder "$ROOTFS/dev/binder" 2>/dev/null || true
chmod 0600 "$ROOTFS/dev/binder" 2>/dev/null || true

mknod "$ROOTFS/dev/null" c 1 3 2>/dev/null || true
mknod "$ROOTFS/dev/zero" c 1 5 2>/dev/null || true
mknod "$ROOTFS/dev/random" c 1 8 2>/dev/null || true
mknod "$ROOTFS/dev/urandom" c 1 9 2>/dev/null || true
chmod 0666 "$ROOTFS/dev/null" "$ROOTFS/dev/zero" "$ROOTFS/dev/random" "$ROOTFS/dev/urandom" 2>/dev/null || true

# Basic dirs/properties expected by Android tools.
mkdir -p "$ROOTFS/linkerconfig" "$ROOTFS/metadata" "$ROOTFS/mnt" "$ROOTFS/storage"
touch "$ROOTFS/default.prop" 2>/dev/null || true

echo
echo "== mounted layout =="
grep -E " $ROOTFS| $MNT_DIR" /proc/mounts 2>/dev/null || true

echo
echo "== Android binary sanity =="
ls -lh "$ROOTFS/system/bin/servicemanager" || true
ls -lh "$ROOTFS/system/bin/toybox" || true
ls -lh "$ROOTFS/apex/com.android.runtime/bin/linker64" || true

echo
echo "== clean old Binder users =="
killall mini_servicemgr 2>/dev/null || true
killall servicemanager 2>/dev/null || true
killall parcel_fd_lite_service 2>/dev/null || true
killall parcel_fd_lite_client 2>/dev/null || true
killall android_userspace_preflight_v1 2>/dev/null || true

mkdir -p "$SIDE_DIR/logs" "$SIDE_DIR/run"
rm -f "$SIDE_DIR/logs/android_real_sm_"*.log "$SIDE_DIR/run/android_real_sm_"*.pid 2>/dev/null || true
rm -f "$ROOTFS/run/parcel_fd_lite.sock" 2>/dev/null || true

cleanup() {
  [ -f "$SIDE_DIR/run/android_real_sm_parcelfd.pid" ] && kill "$(cat "$SIDE_DIR/run/android_real_sm_parcelfd.pid")" 2>/dev/null || true
  [ -f "$SIDE_DIR/run/android_real_sm_servicemanager.pid" ] && kill "$(cat "$SIDE_DIR/run/android_real_sm_servicemanager.pid")" 2>/dev/null || true
  rm -f "$ROOTFS/run/parcel_fd_lite.sock" 2>/dev/null || true
}

trap cleanup EXIT

echo
echo "== start Android real servicemanager =="
set +e
chroot "$ROOTFS" /system/bin/servicemanager > "$SIDE_DIR/logs/android_real_sm_servicemanager.log" 2>&1 &
SM_PID="$!"
set -e

echo "$SM_PID" > "$SIDE_DIR/run/android_real_sm_servicemanager.pid"
echo "ANDROID_REAL_SM_STARTED pid=$SM_PID"

sleep 3

if kill -0 "$SM_PID" 2>/dev/null; then
  echo "ANDROID_REAL_SM_PROCESS_ALIVE"
else
  echo "ANDROID_REAL_SM_PROCESS_EXITED"
  echo "== servicemanager log =="
  cat "$SIDE_DIR/logs/android_real_sm_servicemanager.log" || true
  echo "FAIL: Android real servicemanager exited"
  exit 1
fi

echo
echo "== servicemanager log initial =="
cat "$SIDE_DIR/logs/android_real_sm_servicemanager.log" || true

echo
echo "== start ParcelFD-lite sidecar service against real servicemanager =="
echo "ANDROID_REAL_SM_PARCELFD_SERVICE_REGISTER_ATTEMPT"

set +e
bin/parcel_fd_lite_service "$SERVICE" "$ROOTFS/run/parcel_fd_lite.sock" > "$SIDE_DIR/logs/android_real_sm_parcelfd_service.log" 2>&1 &
PFD_PID="$!"
set -e

echo "$PFD_PID" > "$SIDE_DIR/run/android_real_sm_parcelfd.pid"

sleep 4

echo
echo "== ParcelFD-lite service log =="
cat "$SIDE_DIR/logs/android_real_sm_parcelfd_service.log" || true

if ! kill -0 "$PFD_PID" 2>/dev/null; then
  echo "WARN: ParcelFD-lite service exited"
fi

echo
echo "== run ParcelFD-lite client against real servicemanager =="
echo "ANDROID_REAL_SM_PARCELFD_CLIENT_ATTEMPT"

set +e
bin/parcel_fd_lite_client "$SERVICE" "$ROOTFS/run/parcel_fd_lite.sock" > "$SIDE_DIR/logs/android_real_sm_parcelfd_client.log" 2>&1
CLIENT_RC="$?"
set -e

echo "client_rc=$CLIENT_RC"

echo
echo "== ParcelFD-lite client log =="
cat "$SIDE_DIR/logs/android_real_sm_parcelfd_client.log" || true

echo
echo "== servicemanager log final =="
cat "$SIDE_DIR/logs/android_real_sm_servicemanager.log" || true

echo
echo "== dmesg Binder tail =="
dmesg 2>/dev/null | grep -Ei 'binder|servicemanager|transaction failed|SELinux|avc|denied' | tail -120 || true

if [ "$CLIENT_RC" -eq 0 ] &&
   grep -q 'PARCELFD_LITE_CLIENT_BINDER_REPLY_OK' "$SIDE_DIR/logs/android_real_sm_parcelfd_client.log" &&
   grep -q 'PARCELFD_LITE_PAYLOAD_READ_OK' "$SIDE_DIR/logs/android_real_sm_parcelfd_service.log"; then
  echo "ANDROID_REAL_SM_SMOKE_OK"
  echo "ANDROID_REAL_SERVICEMANAGER_V0_DONE"
  exit 0
fi

echo
echo "ANDROID_REAL_SM_SMOKE_FAIL"
echo "Interpretation:"
echo "  If servicemanager stayed alive but addService/getService failed,"
echo "  keep mini_servicemgr as our compatibility shim for now."
echo "  Next step would be protocol/SELinux/linkerconfig diagnostics."
exit 1
TVSH
