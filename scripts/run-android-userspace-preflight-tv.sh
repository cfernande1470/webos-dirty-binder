#!/usr/bin/env bash
set -euo pipefail

TV_IP="${TV_IP:-192.168.2.121}"
SIDE_DIR="${SIDE_DIR:-/tmp/android-usb/android-sidecar}"
ANDROID_ROOTFS="${ANDROID_ROOTFS:-/tmp/android-usb/android-rootfs}"

ssh root@"$TV_IP" "SIDE_DIR='$SIDE_DIR' ANDROID_ROOTFS='$ANDROID_ROOTFS' sh -s" <<'TVSH'
set -eu

cd "$SIDE_DIR"

for f in \
  bin/android_userspace_preflight \
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
  bin/android_userspace_preflight \
  load-binder-tv.sh

mkdir -p logs run "$ANDROID_ROOTFS"

if [ ! -e /dev/binder ]; then
  ./load-binder-tv.sh "$SIDE_DIR/modules/binder.ko"
fi

rm -f logs/android_userspace_preflight.log

{
  echo "== android userspace preflight config =="
  echo "SIDE_DIR=$SIDE_DIR"
  echo "ANDROID_ROOTFS=$ANDROID_ROOTFS"

  echo
  echo "== date / uptime =="
  date || true
  uptime || true
  cat /proc/uptime 2>/dev/null || true

  echo
  echo "== uname =="
  uname -a || true

  echo
  echo "== id =="
  id || true

  echo
  echo "== binder devices =="
  ls -l /dev/binder /dev/hwbinder /dev/vndbinder 2>/dev/null || true

  echo
  echo "== mounts relevant =="
  mount | grep -E ' / |/media/internal|binder|tmpfs|devpts|proc|sysfs' || true

  echo
  echo "== filesystems =="
  cat /proc/filesystems 2>/dev/null || true

  echo
  echo "== cgroups =="
  cat /proc/cgroups 2>/dev/null || true

  echo
  echo "== namespaces of self =="
  ls -l /proc/self/ns 2>/dev/null || true

  echo
  echo "== android rootfs dir =="
  ls -lah "$ANDROID_ROOTFS" 2>/dev/null || true

  echo
  echo "== run compiled preflight =="
  bin/android_userspace_preflight "$SIDE_DIR/preflight-mounts"
} | tee logs/android_userspace_preflight.log

grep -q 'ANDROID_PREFLIGHT_BINDER_DEVICE_OK' logs/android_userspace_preflight.log
grep -q 'ANDROID_PREFLIGHT_BINDER_VERSION_OK' logs/android_userspace_preflight.log
grep -q 'ANDROID_PREFLIGHT_MEMFD_OK' logs/android_userspace_preflight.log
grep -q 'ANDROID_PREFLIGHT_EVENTFD_OK' logs/android_userspace_preflight.log
grep -q 'ANDROID_PREFLIGHT_SIGNALFD_OK' logs/android_userspace_preflight.log
grep -q 'ANDROID_PREFLIGHT_EPOLL_OK' logs/android_userspace_preflight.log
grep -q 'ANDROID_PREFLIGHT_TMPFS_MOUNT_OK' logs/android_userspace_preflight.log
grep -q 'ANDROID_PREFLIGHT_PROC_MOUNT_OK' logs/android_userspace_preflight.log
grep -q 'ANDROID_PREFLIGHT_DEVPTS_MOUNT_OK' logs/android_userspace_preflight.log
grep -q 'ANDROID_PREFLIGHT_MOUNT_NS_OK' logs/android_userspace_preflight.log
grep -q 'ANDROID_PREFLIGHT_OK' logs/android_userspace_preflight.log

echo "ANDROID_USERSPACE_PREFLIGHT_SMOKE_TV_OK"
TVSH
