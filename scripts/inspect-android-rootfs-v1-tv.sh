#!/usr/bin/env bash
set -euo pipefail

TV_IP="${TV_IP:-192.168.2.121}"
ROOTFS="${ROOTFS:-/tmp/android-usb/android-rootfs}"
IMG_DIR="${IMG_DIR:-/tmp/android-usb/android-images}"
MNT_DIR="${MNT_DIR:-/tmp/android-usb/android-mounts}"

ssh root@"$TV_IP" "ROOTFS='$ROOTFS' IMG_DIR='$IMG_DIR' MNT_DIR='$MNT_DIR' sh -s" <<'TVSH'
set -eu

echo "== Android rootfs inspect v1 =="
date || true
uname -a || true
id || true

echo
echo "== paths =="
echo "ROOTFS=$ROOTFS -> $(readlink -f "$ROOTFS" 2>/dev/null || echo "$ROOTFS")"
echo "IMG_DIR=$IMG_DIR -> $(readlink -f "$IMG_DIR" 2>/dev/null || echo "$IMG_DIR")"
echo "MNT_DIR=$MNT_DIR -> $(readlink -f "$MNT_DIR" 2>/dev/null || echo "$MNT_DIR")"

SYSTEM_IMG="$IMG_DIR/system.img"
VENDOR_IMG="$IMG_DIR/vendor.img"

echo
echo "== image presence =="
ls -lh "$SYSTEM_IMG" "$VENDOR_IMG" 2>/dev/null || true

if [ ! -f "$SYSTEM_IMG" ]; then
  echo "ERROR: missing $SYSTEM_IMG"
  exit 1
fi

if [ ! -f "$VENDOR_IMG" ]; then
  echo "ERROR: missing $VENDOR_IMG"
  exit 1
fi

echo
echo "== storage =="
df -h /media/internal /tmp/android-usb "$ROOTFS" "$IMG_DIR" 2>/dev/null || true
du -sh "$IMG_DIR" "$ROOTFS" 2>/dev/null || true

echo
echo "== create rootfs skeleton =="
mkdir -p \
  "$ROOTFS/system" \
  "$ROOTFS/vendor" \
  "$ROOTFS/dev" \
  "$ROOTFS/proc" \
  "$ROOTFS/sys" \
  "$ROOTFS/run" \
  "$ROOTFS/tmp" \
  "$ROOTFS/data" \
  "$ROOTFS/cache" \
  "$ROOTFS/apex" \
  "$ROOTFS/mnt" \
  "$ROOTFS/storage"

chmod 0755 "$ROOTFS" "$ROOTFS/system" "$ROOTFS/vendor" "$ROOTFS/dev" "$ROOTFS/proc" "$ROOTFS/sys" "$ROOTFS/run" "$ROOTFS/tmp" "$ROOTFS/data" "$ROOTFS/cache"

is_mounted() {
  path="$1"
  grep -q " $path " /proc/mounts 2>/dev/null
}

echo
echo "== unmount stale rootfs mounts if any =="
for p in "$ROOTFS/proc" "$ROOTFS/system" "$ROOTFS/vendor"; do
  if is_mounted "$p"; then
    echo "umount $p"
    umount "$p" 2>/dev/null || true
  fi
done

echo
echo "== mount Android images read-only =="
if mount -o loop,ro "$SYSTEM_IMG" "$ROOTFS/system"; then
  echo "ANDROID_ROOTFS_INSPECT_V1_SYSTEM_MOUNT_OK"
else
  echo "ERROR: failed to mount system.img"
  exit 1
fi

if mount -o loop,ro "$VENDOR_IMG" "$ROOTFS/vendor"; then
  echo "ANDROID_ROOTFS_INSPECT_V1_VENDOR_MOUNT_OK"
else
  echo "ERROR: failed to mount vendor.img"
  exit 1
fi

echo
echo "== mounted rootfs filesystems =="
grep " $ROOTFS" /proc/mounts 2>/dev/null || true
df -h "$ROOTFS/system" "$ROOTFS/vendor" 2>/dev/null || true

echo
echo "== create minimal dev nodes =="
rm -f "$ROOTFS/dev/binder" "$ROOTFS/dev/null" "$ROOTFS/dev/zero" "$ROOTFS/dev/random" "$ROOTFS/dev/urandom" "$ROOTFS/dev/ashmem" 2>/dev/null || true

if [ -e /dev/binder ]; then
  mknod "$ROOTFS/dev/binder" c 10 53 2>/dev/null || cp -a /dev/binder "$ROOTFS/dev/binder" 2>/dev/null || true
  chmod 0600 "$ROOTFS/dev/binder" 2>/dev/null || true
fi

mknod "$ROOTFS/dev/null" c 1 3 2>/dev/null || true
mknod "$ROOTFS/dev/zero" c 1 5 2>/dev/null || true
mknod "$ROOTFS/dev/random" c 1 8 2>/dev/null || true
mknod "$ROOTFS/dev/urandom" c 1 9 2>/dev/null || true
chmod 0666 "$ROOTFS/dev/null" "$ROOTFS/dev/zero" "$ROOTFS/dev/random" "$ROOTFS/dev/urandom" 2>/dev/null || true

ls -l "$ROOTFS/dev" 2>/dev/null || true

echo
echo "== mount proc inside rootfs =="
if ! is_mounted "$ROOTFS/proc"; then
  mount -t proc proc "$ROOTFS/proc" 2>/dev/null || echo "WARN: proc mount failed"
fi

echo
echo "== top-level system/vendor =="
find "$ROOTFS/system" -maxdepth 2 | sort | sed -n '1,160p'
echo
find "$ROOTFS/vendor" -maxdepth 2 | sort | sed -n '1,160p'

echo
echo "== key Android binaries =="
for f in \
  "$ROOTFS/system/bin/linker64" \
  "$ROOTFS/system/bin/sh" \
  "$ROOTFS/system/bin/toybox" \
  "$ROOTFS/system/bin/getprop" \
  "$ROOTFS/system/bin/app_process64" \
  "$ROOTFS/system/bin/servicemanager" \
  "$ROOTFS/system/bin/hwservicemanager" \
  "$ROOTFS/system/bin/surfaceflinger" \
  "$ROOTFS/system/bin/zygote64" \
  "$ROOTFS/system/bin/bootanimation"
do
  if [ -e "$f" ]; then
    ls -lh "$f"
  else
    echo "missing $f"
  fi
done

echo
echo "== key Android libraries =="
for f in \
  "$ROOTFS/system/lib64/libc.so" \
  "$ROOTFS/system/lib64/libbinder.so" \
  "$ROOTFS/system/lib64/libutils.so" \
  "$ROOTFS/system/lib64/liblog.so" \
  "$ROOTFS/system/lib64/libandroid_runtime.so" \
  "$ROOTFS/system/lib64/libgui.so" \
  "$ROOTFS/system/lib64/libui.so" \
  "$ROOTFS/vendor/lib64"
do
  if [ -e "$f" ]; then
    ls -ldh "$f"
  else
    echo "missing $f"
  fi
done

echo
echo "== system/bin sample =="
find "$ROOTFS/system/bin" -maxdepth 1 -type f 2>/dev/null | sort | sed -n '1,180p' || true

echo
echo "== chroot smoke probes =="
set +e

echo "--- chroot /system/bin/toybox true"
chroot "$ROOTFS" /system/bin/toybox true
rc_toybox_true="$?"
echo "toybox_true_rc=$rc_toybox_true"

echo "--- chroot /system/bin/toybox uname -a"
chroot "$ROOTFS" /system/bin/toybox uname -a
rc_toybox_uname="$?"
echo "toybox_uname_rc=$rc_toybox_uname"

echo "--- chroot /system/bin/sh -c echo"
chroot "$ROOTFS" /system/bin/sh -c 'echo ANDROID_ROOTFS_CHROOT_SH_OK'
rc_sh="$?"
echo "sh_rc=$rc_sh"

echo "--- chroot /system/bin/getprop ro.build.version.release"
chroot "$ROOTFS" /system/bin/getprop ro.build.version.release
rc_getprop="$?"
echo "getprop_rc=$rc_getprop"

echo "--- chroot /system/bin/ls /system/bin"
chroot "$ROOTFS" /system/bin/toybox ls /system/bin >/tmp/android_rootfs_ls.out 2>/tmp/android_rootfs_ls.err
rc_ls="$?"
echo "toybox_ls_rc=$rc_ls"
sed -n '1,80p' /tmp/android_rootfs_ls.out 2>/dev/null || true
sed -n '1,80p' /tmp/android_rootfs_ls.err 2>/dev/null || true

set -e

echo
echo "== chroot result summary =="
echo "toybox_true_rc=$rc_toybox_true"
echo "toybox_uname_rc=$rc_toybox_uname"
echo "sh_rc=$rc_sh"
echo "getprop_rc=$rc_getprop"
echo "toybox_ls_rc=$rc_ls"

if [ "$rc_toybox_true" -eq 0 ] || [ "$rc_sh" -eq 0 ]; then
  echo "ANDROID_ROOTFS_INSPECT_V1_CHROOT_BASIC_OK"
else
  echo "ANDROID_ROOTFS_INSPECT_V1_CHROOT_BASIC_FAIL"
fi

echo
echo "== binder visibility from rootfs =="
ls -l "$ROOTFS/dev/binder" 2>/dev/null || true

if [ -e "$ROOTFS/dev/binder" ]; then
  echo "ANDROID_ROOTFS_INSPECT_V1_BINDER_NODE_OK"
else
  echo "ANDROID_ROOTFS_INSPECT_V1_BINDER_NODE_MISSING"
fi

echo
echo "== final mounts left active =="
grep " $ROOTFS" /proc/mounts 2>/dev/null || true

echo
echo "ANDROID_ROOTFS_INSPECT_V1_DONE"
TVSH
