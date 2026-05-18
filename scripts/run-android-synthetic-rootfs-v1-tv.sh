#!/usr/bin/env bash
set -euo pipefail

TV_IP="${TV_IP:-192.168.2.121}"
ROOTFS="${ROOTFS:-/tmp/android-usb/android-rootfs}"
IMG_DIR="${IMG_DIR:-/tmp/android-usb/android-images}"
MNT_DIR="${MNT_DIR:-/tmp/android-usb/android-mounts}"
USB_DIR="${USB_DIR:-/tmp/android-usb}"

ssh root@"$TV_IP" "ROOTFS='$ROOTFS' IMG_DIR='$IMG_DIR' MNT_DIR='$MNT_DIR' USB_DIR='$USB_DIR' sh -s" <<'TVSH'
set -eu

echo "== Android synthetic rootfs v1 =="
date || true
uname -a || true
id || true

# Resolve symlinks so /proc/mounts checks match the actual mount paths.
ROOTFS="$(readlink -f "$ROOTFS" 2>/dev/null || echo "$ROOTFS")"
IMG_DIR="$(readlink -f "$IMG_DIR" 2>/dev/null || echo "$IMG_DIR")"
MNT_DIR="$(readlink -f "$MNT_DIR" 2>/dev/null || echo "$MNT_DIR")"
USB_DIR="$(readlink -f "$USB_DIR" 2>/dev/null || echo "$USB_DIR")"

SYSTEM_IMG="$IMG_DIR/system.img"
VENDOR_IMG="$IMG_DIR/vendor.img"
SYS_RAW="$MNT_DIR/system_raw"
VENDOR_RAW="$MNT_DIR/vendor_raw"

if [ ! -f "$SYSTEM_IMG" ]; then
  echo "ERROR: missing $SYSTEM_IMG"
  exit 1
fi

if [ ! -f "$VENDOR_IMG" ]; then
  echo "ERROR: missing $VENDOR_IMG"
  exit 1
fi

is_mounted() {
  grep -q " $1 " /proc/mounts 2>/dev/null
}

try_umount() {
  p="$1"
  if is_mounted "$p"; then
    echo "umount $p"
    umount "$p" 2>/dev/null || true
  fi
}

echo
echo "== paths =="
echo "ROOTFS=$ROOTFS -> $(readlink -f "$ROOTFS" 2>/dev/null || echo "$ROOTFS")"
echo "IMG_DIR=$IMG_DIR -> $(readlink -f "$IMG_DIR" 2>/dev/null || echo "$IMG_DIR")"
echo "MNT_DIR=$MNT_DIR -> $(readlink -f "$MNT_DIR" 2>/dev/null || echo "$MNT_DIR")"
echo "USB_DIR=$USB_DIR"

echo
echo "== storage =="
df -h "$ROOTFS" "$IMG_DIR" "$USB_DIR" 2>/dev/null || true
du -sh "$IMG_DIR" "$ROOTFS" "$USB_DIR/android-data" 2>/dev/null || true

echo
echo "== prepare dirs =="
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

echo
echo "== unmount stale synthetic mounts =="
# Deepest first.
for p in \
  "$ROOTFS/proc" \
  "$ROOTFS/sys" \
  "$ROOTFS/apex" \
  "$ROOTFS/vendor" \
  "$ROOTFS/system" \
  "$VENDOR_RAW" \
  "$SYS_RAW"
do
  try_umount "$p"
done

echo
echo "== mount raw images =="
mount -o loop,ro "$SYSTEM_IMG" "$SYS_RAW"
echo "ANDROID_SYNTH_ROOTFS_SYSTEM_RAW_MOUNT_OK"

mount -o loop,ro "$VENDOR_IMG" "$VENDOR_RAW"
echo "ANDROID_SYNTH_ROOTFS_VENDOR_RAW_MOUNT_OK"

if [ ! -d "$SYS_RAW/system/bin" ]; then
  echo "ERROR: expected $SYS_RAW/system/bin"
  find "$SYS_RAW" -maxdepth 3 | sort | sed -n '1,200p'
  exit 1
fi

if [ ! -d "$SYS_RAW/system/apex" ]; then
  echo "ERROR: expected $SYS_RAW/system/apex"
  find "$SYS_RAW/system" -maxdepth 2 | sort | sed -n '1,200p'
  exit 1
fi

echo
echo "== bind Android layout into synthetic root =="
mount -o bind "$SYS_RAW/system" "$ROOTFS/system"
echo "ANDROID_SYNTH_ROOTFS_BIND_SYSTEM_OK"

mount -o bind "$VENDOR_RAW" "$ROOTFS/vendor"
echo "ANDROID_SYNTH_ROOTFS_BIND_VENDOR_OK"

mount -o bind "$SYS_RAW/system/apex" "$ROOTFS/apex"
echo "ANDROID_SYNTH_ROOTFS_BIND_APEX_OK"

# Writable areas.
mount -o bind "$USB_DIR/android-data" "$ROOTFS/data" 2>/dev/null || echo "WARN: bind /data failed"
mount -o bind "$USB_DIR/android-cache" "$ROOTFS/cache" 2>/dev/null || echo "WARN: bind /cache failed"

echo
echo "== minimal dev/proc/sys =="
rm -f "$ROOTFS/dev/binder" "$ROOTFS/dev/null" "$ROOTFS/dev/zero" "$ROOTFS/dev/random" "$ROOTFS/dev/urandom" 2>/dev/null || true

if [ -e /dev/binder ]; then
  mknod "$ROOTFS/dev/binder" c 10 53 2>/dev/null || cp -a /dev/binder "$ROOTFS/dev/binder" 2>/dev/null || true
  chmod 0600 "$ROOTFS/dev/binder" 2>/dev/null || true
fi

mknod "$ROOTFS/dev/null" c 1 3 2>/dev/null || true
mknod "$ROOTFS/dev/zero" c 1 5 2>/dev/null || true
mknod "$ROOTFS/dev/random" c 1 8 2>/dev/null || true
mknod "$ROOTFS/dev/urandom" c 1 9 2>/dev/null || true
chmod 0666 "$ROOTFS/dev/null" "$ROOTFS/dev/zero" "$ROOTFS/dev/random" "$ROOTFS/dev/urandom" 2>/dev/null || true

mount -t proc proc "$ROOTFS/proc" 2>/dev/null || echo "WARN: mount proc failed"
mount -t sysfs sysfs "$ROOTFS/sys" 2>/dev/null || echo "WARN: mount sysfs failed"

echo
echo "== mounted layout =="
grep " $ROOTFS" /proc/mounts 2>/dev/null || true
ls -la "$ROOTFS" | sed -n '1,120p'
ls -la "$ROOTFS/system/bin" | sed -n '1,80p'
ls -la "$ROOTFS/apex/com.android.runtime/bin" | sed -n '1,80p' || true

echo
echo "== linker sanity =="
ls -la "$ROOTFS/system/bin/linker64" || true
ls -la "$ROOTFS/apex/com.android.runtime/bin/linker64" || true
ls -la "$ROOTFS/system/bin/toybox" || true
ls -la "$ROOTFS/system/bin/sh" || true
ls -la "$ROOTFS/system/bin/getprop" || true

echo
echo "== chroot basic probes =="
set +e

echo "--- toybox true"
chroot "$ROOTFS" /system/bin/toybox true
rc_toybox_true="$?"
echo "toybox_true_rc=$rc_toybox_true"

echo "--- toybox uname"
chroot "$ROOTFS" /system/bin/toybox uname -a
rc_toybox_uname="$?"
echo "toybox_uname_rc=$rc_toybox_uname"

echo "--- sh echo"
chroot "$ROOTFS" /system/bin/sh -c 'echo ANDROID_SYNTH_ROOTFS_SH_OK'
rc_sh="$?"
echo "sh_rc=$rc_sh"

echo "--- getprop"
chroot "$ROOTFS" /system/bin/getprop ro.build.version.release
rc_getprop="$?"
echo "getprop_rc=$rc_getprop"
echo "ANDROID_SYNTH_ROOTFS_GETPROP_ATTEMPTED"

echo "--- list /system/bin"
chroot "$ROOTFS" /system/bin/toybox ls /system/bin >/tmp/android_synth_ls.out 2>/tmp/android_synth_ls.err
rc_ls="$?"
echo "ls_rc=$rc_ls"
sed -n '1,80p' /tmp/android_synth_ls.out 2>/dev/null || true
sed -n '1,80p' /tmp/android_synth_ls.err 2>/dev/null || true

echo "--- linker direct version-ish probe"
chroot "$ROOTFS" /apex/com.android.runtime/bin/linker64 2>/tmp/android_synth_linker.err
rc_linker="$?"
echo "linker_rc=$rc_linker"
sed -n '1,80p' /tmp/android_synth_linker.err 2>/dev/null || true

set -e

echo
echo "== result summary =="
echo "toybox_true_rc=$rc_toybox_true"
echo "toybox_uname_rc=$rc_toybox_uname"
echo "sh_rc=$rc_sh"
echo "getprop_rc=$rc_getprop"
echo "ls_rc=$rc_ls"
echo "linker_rc=$rc_linker"

if [ "$rc_toybox_true" -eq 0 ]; then
  echo "ANDROID_SYNTH_ROOTFS_TOYBOX_OK"
fi

if [ "$rc_sh" -eq 0 ]; then
  echo "ANDROID_SYNTH_ROOTFS_SH_OK_MARKER"
fi

if [ "$rc_toybox_true" -eq 0 ] || [ "$rc_sh" -eq 0 ]; then
  echo "ANDROID_SYNTH_ROOTFS_V1_OK"
else
  echo "ANDROID_SYNTH_ROOTFS_V1_FAIL"
  exit 1
fi

echo
echo "== leave mounts active for next milestone =="
grep " $ROOTFS" /proc/mounts 2>/dev/null || true

echo
echo "ANDROID_SYNTHETIC_ROOTFS_V1_DONE"
TVSH
