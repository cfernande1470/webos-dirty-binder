#!/usr/bin/env bash
set -euo pipefail

TV_IP="${TV_IP:-192.168.2.121}"
MOUNTPOINT="${MOUNTPOINT:-/media/internal/android-usb}"

ssh root@"$TV_IP" "MOUNTPOINT='$MOUNTPOINT' sh -s" <<'TVSH'
set -eu

echo "== ensure Android USB mounted =="
date || true

mkdir -p "$MOUNTPOINT"

echo
echo "== current sd devices =="
ls -l /dev/sd* 2>/dev/null || true
cat /proc/partitions 2>/dev/null | grep -E 'sd[a-z]' || true

echo
echo "== current android mounts =="
grep -E 'android-usb|android-rootfs|android-images|android-mounts|android-downloads' /proc/mounts 2>/dev/null || true

if grep -q " $MOUNTPOINT " /proc/mounts 2>/dev/null; then
  echo "ANDROID_USB_ALREADY_MOUNTED"
else
  USB_PART="${USB_PART:-}"

  if [ -z "$USB_PART" ]; then
    for p in /dev/sd[a-z]1 /dev/sd[a-z][0-9]; do
      [ -b "$p" ] || continue
      USB_PART="$p"
      break
    done
  fi

  if [ -z "$USB_PART" ]; then
    echo "ERROR: no USB partition found"
    exit 1
  fi

  echo "Mounting USB_PART=$USB_PART at $MOUNTPOINT"
  mount "$USB_PART" "$MOUNTPOINT"
  echo "ANDROID_USB_MOUNTED"
fi

echo
echo "== ensure symlinks =="
for name in android-rootfs android-downloads android-images android-mounts; do
  dst="$MOUNTPOINT/$name"
  src="/media/internal/$name"

  mkdir -p "$dst"

  if [ -L "$src" ]; then
    echo "$src -> $(readlink "$src")"
  elif [ -e "$src" ]; then
    echo "WARN: $src exists and is not symlink; leaving it alone"
  else
    ln -s "$dst" "$src"
    echo "linked $src -> $dst"
  fi
done

echo
echo "== verify Android USB layout =="
ls -ld "$MOUNTPOINT" "$MOUNTPOINT"/android-* 2>/dev/null || true
ls -ld /media/internal/android-rootfs /media/internal/android-downloads /media/internal/android-images /media/internal/android-mounts 2>/dev/null || true
ls -lh /media/internal/android-images/system.img /media/internal/android-images/vendor.img 2>/dev/null || true
df -h /media/internal "$MOUNTPOINT" /media/internal/android-images 2>/dev/null || true

if [ ! -f /media/internal/android-images/system.img ]; then
  echo "ERROR: system.img missing after USB mount"
  exit 1
fi

if [ ! -f /media/internal/android-images/vendor.img ]; then
  echo "ERROR: vendor.img missing after USB mount"
  exit 1
fi

echo "ANDROID_USB_READY_AFTER_REBOOT"
TVSH
