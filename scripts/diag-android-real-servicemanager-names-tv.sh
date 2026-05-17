#!/usr/bin/env bash
set -euo pipefail

TV_IP="${TV_IP:-192.168.2.121}"
SIDE_DIR="${SIDE_DIR:-/media/internal/android-sidecar}"
ROOTFS="${ROOTFS:-/media/internal/android-rootfs}"
IMG_DIR="${IMG_DIR:-/media/internal/android-images}"
MNT_DIR="${MNT_DIR:-/media/internal/android-mounts}"
USB_DIR="${USB_DIR:-/media/internal/android-usb}"

ssh root@"$TV_IP" \
  "SIDE_DIR='$SIDE_DIR' ROOTFS='$ROOTFS' IMG_DIR='$IMG_DIR' MNT_DIR='$MNT_DIR' USB_DIR='$USB_DIR' sh -s" <<'TVSH'
set -eu

echo "== Android real servicemanager name/policy diag =="
date || true
uname -a || true

ROOTFS="$(readlink -f "$ROOTFS" 2>/dev/null || echo "$ROOTFS")"
IMG_DIR="$(readlink -f "$IMG_DIR" 2>/dev/null || echo "$IMG_DIR")"
MNT_DIR="$(readlink -f "$MNT_DIR" 2>/dev/null || echo "$MNT_DIR")"
USB_DIR="$(readlink -f "$USB_DIR" 2>/dev/null || echo "$USB_DIR")"

SYSTEM_IMG="$IMG_DIR/system.img"
VENDOR_IMG="$IMG_DIR/vendor.img"
SYS_RAW="$MNT_DIR/system_raw"
VENDOR_RAW="$MNT_DIR/vendor_raw"

cd "$SIDE_DIR"

mkdir -p logs run

echo
echo "== ensure binder loaded =="
if ! grep -q '^binder ' /proc/modules; then
  ./load-binder-tv.sh "$SIDE_DIR/modules/binder.ko"
else
  echo "binder already loaded"
fi

echo
echo "== ensure mounts/rootfs =="
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

mount_loop_ro_if_needed() {
  img="$1"
  dst="$2"
  if ! is_mounted "$dst"; then
    mount -o loop,ro "$img" "$dst"
  fi
}

bind_if_needed() {
  src="$1"
  dst="$2"
  if ! is_mounted "$dst"; then
    mount -o bind "$src" "$dst"
  fi
}

mount_loop_ro_if_needed "$SYSTEM_IMG" "$SYS_RAW"
mount_loop_ro_if_needed "$VENDOR_IMG" "$VENDOR_RAW"
bind_if_needed "$SYS_RAW/system" "$ROOTFS/system"
bind_if_needed "$VENDOR_RAW" "$ROOTFS/vendor"
bind_if_needed "$SYS_RAW/system/apex" "$ROOTFS/apex"
bind_if_needed "$USB_DIR/android-data" "$ROOTFS/data"
bind_if_needed "$USB_DIR/android-cache" "$ROOTFS/cache"

if ! is_mounted "$ROOTFS/proc"; then
  mount -t proc proc "$ROOTFS/proc" 2>/dev/null || true
fi

if ! is_mounted "$ROOTFS/sys"; then
  mount -t sysfs sysfs "$ROOTFS/sys" 2>/dev/null || true
fi

rm -f "$ROOTFS/dev/binder" "$ROOTFS/dev/null" "$ROOTFS/dev/zero" "$ROOTFS/dev/random" "$ROOTFS/dev/urandom" 2>/dev/null || true
mknod "$ROOTFS/dev/binder" c 10 53 2>/dev/null || cp -a /dev/binder "$ROOTFS/dev/binder" 2>/dev/null || true
chmod 0600 "$ROOTFS/dev/binder" 2>/dev/null || true
mknod "$ROOTFS/dev/null" c 1 3 2>/dev/null || true
mknod "$ROOTFS/dev/zero" c 1 5 2>/dev/null || true
mknod "$ROOTFS/dev/random" c 1 8 2>/dev/null || true
mknod "$ROOTFS/dev/urandom" c 1 9 2>/dev/null || true
chmod 0666 "$ROOTFS/dev/null" "$ROOTFS/dev/zero" "$ROOTFS/dev/random" "$ROOTFS/dev/urandom" 2>/dev/null || true
mkdir -p "$ROOTFS/linkerconfig" "$ROOTFS/metadata" "$ROOTFS/mnt" "$ROOTFS/storage"
touch "$ROOTFS/default.prop" 2>/dev/null || true

echo
echo "== service_contexts hints =="
for f in \
  "$ROOTFS/system/etc/selinux/plat_service_contexts" \
  "$ROOTFS/system/system_ext/etc/selinux/system_ext_service_contexts" \
  "$ROOTFS/system/product/etc/selinux/product_service_contexts" \
  "$ROOTFS/vendor/etc/selinux/vendor_service_contexts"
do
  echo "--- $f"
  if [ -f "$f" ]; then
    grep -E '^(activity|package|surfaceflinger|media|gpu|test|default_android_service|servicemanager|manager)' "$f" | head -80 || true
  else
    echo "missing"
  fi
done

echo
echo "== clean old processes =="
killall mini_servicemgr 2>/dev/null || true
killall servicemanager 2>/dev/null || true
killall parcel_fd_lite_service 2>/dev/null || true
killall parcel_fd_lite_client 2>/dev/null || true

rm -f "$SIDE_DIR/logs/diag_real_sm_"*.log "$SIDE_DIR/run/diag_real_sm_"*.pid 2>/dev/null || true
rm -f "$ROOTFS/run/parcel_fd_lite.sock" 2>/dev/null || true

cleanup() {
  killall parcel_fd_lite_service 2>/dev/null || true
  killall parcel_fd_lite_client 2>/dev/null || true
  killall servicemanager 2>/dev/null || true
  rm -f "$ROOTFS/run/parcel_fd_lite.sock" 2>/dev/null || true
}
trap cleanup EXIT

echo
echo "== start real Android servicemanager =="
chroot "$ROOTFS" /system/bin/servicemanager > "$SIDE_DIR/logs/diag_real_sm_servicemanager.log" 2>&1 &
SM_PID="$!"
echo "$SM_PID" > "$SIDE_DIR/run/diag_real_sm_servicemanager.pid"

sleep 3

if kill -0 "$SM_PID" 2>/dev/null; then
  echo "DIAG_REAL_SM_PROCESS_ALIVE"
else
  echo "DIAG_REAL_SM_PROCESS_EXITED"
  cat "$SIDE_DIR/logs/diag_real_sm_servicemanager.log" || true
  exit 1
fi

echo
echo "== run service-name matrix =="
NAMES="test.android.parcelfd activity package media.metrics gpu surfaceflinger"

for name in $NAMES; do
  safe="$(echo "$name" | tr '.-' '__')"
  sock="$ROOTFS/run/parcel_fd_lite_$safe.sock"

  echo
  echo "================ SERVICE NAME: $name ================"

  killall parcel_fd_lite_service 2>/dev/null || true
  killall parcel_fd_lite_client 2>/dev/null || true
  rm -f "$sock"

  bin/parcel_fd_lite_service "$name" "$sock" > "$SIDE_DIR/logs/diag_real_sm_service_$safe.log" 2>&1 &
  SVC_PID="$!"

  sleep 3

  set +e
  bin/parcel_fd_lite_client "$name" "$sock" > "$SIDE_DIR/logs/diag_real_sm_client_$safe.log" 2>&1
  CLIENT_RC="$?"
  set -e

  echo "client_rc=$CLIENT_RC"

  echo "--- service log"
  cat "$SIDE_DIR/logs/diag_real_sm_service_$safe.log" || true

  echo "--- client log"
  cat "$SIDE_DIR/logs/diag_real_sm_client_$safe.log" || true

  if [ "$CLIENT_RC" -eq 0 ] &&
     grep -q 'PARCELFD_LITE_CLIENT_BINDER_REPLY_OK' "$SIDE_DIR/logs/diag_real_sm_client_$safe.log"; then
    echo "DIAG_REAL_SM_NAME_OK name=$name"
  else
    echo "DIAG_REAL_SM_NAME_FAIL name=$name"
  fi

  kill "$SVC_PID" 2>/dev/null || true
  sleep 1
done

echo
echo "== servicemanager log =="
cat "$SIDE_DIR/logs/diag_real_sm_servicemanager.log" || true

echo
echo "== optional Android service list command =="
set +e
chroot "$ROOTFS" /system/bin/service list > "$SIDE_DIR/logs/diag_real_sm_android_service_list.log" 2>&1
SERVICE_LIST_RC="$?"
set -e
echo "service_list_rc=$SERVICE_LIST_RC"
cat "$SIDE_DIR/logs/diag_real_sm_android_service_list.log" || true

echo
echo "== dmesg relevant tail =="
dmesg 2>/dev/null | grep -Ei 'DIRTY_BINDER_IOCTL_COMPAT_V0|ioctl 40046210|ioctl 4018620d|binder|servicemanager|service_manager|avc|denied|selinux' | tail -180 || true

echo
echo "DIAG_ANDROID_REAL_SM_NAMES_DONE"
TVSH
