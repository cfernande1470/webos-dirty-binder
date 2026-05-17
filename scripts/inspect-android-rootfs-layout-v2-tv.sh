#!/usr/bin/env bash
set -euo pipefail

TV_IP="${TV_IP:-192.168.2.121}"
ROOTFS="${ROOTFS:-/media/internal/android-rootfs}"
IMG_DIR="${IMG_DIR:-/media/internal/android-images}"
MNT_DIR="${MNT_DIR:-/media/internal/android-mounts}"

ssh root@"$TV_IP" "ROOTFS='$ROOTFS' IMG_DIR='$IMG_DIR' MNT_DIR='$MNT_DIR' sh -s" <<'TVSH'
set -eu

echo "== Android rootfs layout inspect v2 =="
date || true
uname -a || true

SYSTEM_IMG="$IMG_DIR/system.img"
VENDOR_IMG="$IMG_DIR/vendor.img"
SYS_RAW="$MNT_DIR/system_raw"
VENDOR_RAW="$MNT_DIR/vendor_raw"

mkdir -p "$SYS_RAW" "$VENDOR_RAW"

echo
echo "== images =="
ls -lh "$SYSTEM_IMG" "$VENDOR_IMG" 2>/dev/null || true

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

echo
echo "== unmount stale raw mounts =="
for p in "$VENDOR_RAW" "$SYS_RAW"; do
  if is_mounted "$p"; then
    echo "umount $p"
    umount "$p" 2>/dev/null || true
  fi
done

echo
echo "== mount raw images read-only =="
mount -o loop,ro "$SYSTEM_IMG" "$SYS_RAW"
echo "ANDROID_ROOTFS_LAYOUT_V2_SYSTEM_RAW_MOUNT_OK"

mount -o loop,ro "$VENDOR_IMG" "$VENDOR_RAW"
echo "ANDROID_ROOTFS_LAYOUT_V2_VENDOR_RAW_MOUNT_OK"

echo
echo "== mounted raw images =="
grep "$MNT_DIR" /proc/mounts 2>/dev/null || true
df -h "$SYS_RAW" "$VENDOR_RAW" 2>/dev/null || true

echo
echo "== system raw top-level ls -la =="
ls -la "$SYS_RAW" | sed -n '1,160p'

echo
echo "== vendor raw top-level ls -la =="
ls -la "$VENDOR_RAW" | sed -n '1,160p'

echo
echo "== system raw symlinks top-level =="
find "$SYS_RAW" -maxdepth 2 -type l -exec ls -la {} \; 2>/dev/null | sed -n '1,220p' || true

echo
echo "== candidate bin dirs =="
for d in \
  "$SYS_RAW/bin" \
  "$SYS_RAW/system/bin" \
  "$SYS_RAW/system/system/bin" \
  "$SYS_RAW/product/bin" \
  "$SYS_RAW/apex" \
  "$VENDOR_RAW/bin" \
  "$VENDOR_RAW/vendor/bin"
do
  echo
  echo "--- $d"
  if [ -e "$d" ] || [ -L "$d" ]; then
    ls -la "$d" 2>/dev/null | sed -n '1,80p' || true
  else
    echo "missing"
  fi
done

echo
echo "== candidate Android executables =="
for f in \
  "$SYS_RAW/bin/toybox" \
  "$SYS_RAW/bin/sh" \
  "$SYS_RAW/bin/linker64" \
  "$SYS_RAW/system/bin/toybox" \
  "$SYS_RAW/system/bin/sh" \
  "$SYS_RAW/system/bin/linker64" \
  "$SYS_RAW/system/bin/getprop" \
  "$SYS_RAW/system/bin/app_process64" \
  "$SYS_RAW/system/bin/servicemanager" \
  "$SYS_RAW/system/bin/surfaceflinger" \
  "$SYS_RAW/system/system/bin/toybox" \
  "$SYS_RAW/system/system/bin/sh" \
  "$SYS_RAW/system/system/bin/linker64"
do
  echo
  echo "--- $f"
  ls -la "$f" 2>/dev/null || echo "missing"
done

echo
echo "== candidate library dirs =="
for d in \
  "$SYS_RAW/lib64" \
  "$SYS_RAW/system/lib64" \
  "$SYS_RAW/system/system/lib64" \
  "$VENDOR_RAW/lib64" \
  "$VENDOR_RAW/vendor/lib64"
do
  echo
  echo "--- $d"
  if [ -e "$d" ] || [ -L "$d" ]; then
    ls -la "$d" 2>/dev/null | sed -n '1,80p' || true
  else
    echo "missing"
  fi
done

echo
echo "== chroot layout probes =="
set +e

echo
echo "--- probe A: chroot SYS_RAW /bin/toybox true"
chroot "$SYS_RAW" /bin/toybox true
rc_a="$?"
echo "probe_a_rc=$rc_a"

echo
echo "--- probe B: chroot SYS_RAW /system/bin/toybox true"
chroot "$SYS_RAW" /system/bin/toybox true
rc_b="$?"
echo "probe_b_rc=$rc_b"

echo
echo "--- probe C: chroot SYS_RAW/system /bin/toybox true"
if [ -d "$SYS_RAW/system" ]; then
  chroot "$SYS_RAW/system" /bin/toybox true
  rc_c="$?"
else
  rc_c=127
fi
echo "probe_c_rc=$rc_c"

echo
echo "--- probe D: chroot SYS_RAW /system/bin/sh -c echo"
chroot "$SYS_RAW" /system/bin/sh -c 'echo ANDROID_ROOTFS_LAYOUT_V2_SH_OK'
rc_d="$?"
echo "probe_d_rc=$rc_d"

echo
echo "--- probe E: chroot SYS_RAW /bin/sh -c echo"
chroot "$SYS_RAW" /bin/sh -c 'echo ANDROID_ROOTFS_LAYOUT_V2_BIN_SH_OK'
rc_e="$?"
echo "probe_e_rc=$rc_e"

echo
echo "--- probe F: direct execute with chroot SYS_RAW /system/bin/getprop"
chroot "$SYS_RAW" /system/bin/getprop ro.build.version.release
rc_f="$?"
echo "probe_f_rc=$rc_f"

set -e

echo
echo "== probe summary =="
echo "probe_a_sysraw_bin_toybox=$rc_a"
echo "probe_b_sysraw_system_bin_toybox=$rc_b"
echo "probe_c_sysraw_system_bin_toybox=$rc_c"
echo "probe_d_sysraw_system_sh=$rc_d"
echo "probe_e_sysraw_bin_sh=$rc_e"
echo "probe_f_sysraw_getprop=$rc_f"

if [ "$rc_a" -eq 0 ] || [ "$rc_b" -eq 0 ] || [ "$rc_c" -eq 0 ] || [ "$rc_d" -eq 0 ] || [ "$rc_e" -eq 0 ]; then
  echo "ANDROID_ROOTFS_LAYOUT_V2_CHROOT_BASIC_OK"
else
  echo "ANDROID_ROOTFS_LAYOUT_V2_CHROOT_BASIC_FAIL"
fi

echo
echo "== recommended interpretation hints =="
echo "If probe A or E works: system.img is probably a root-style Android filesystem."
echo "If probe B or D works: system.img works as root with /system paths."
echo "If probe C works: system.img contains nested /system and should be chrooted differently."
echo "If all fail with symlink loop: need synthetic root or bind layout."

echo
echo "== leave raw mounts active for inspection =="
grep "$MNT_DIR" /proc/mounts 2>/dev/null || true

echo
echo "ANDROID_ROOTFS_LAYOUT_V2_DONE"
TVSH
