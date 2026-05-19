#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
[ -f "$ROOT/configs/android-usb.env" ] && . "$ROOT/configs/android-usb.env"

TV_IP="${TV_IP:-192.168.2.121}"
ANDROID_USB_MOUNT="${ANDROID_USB_MOUNT:-/tmp/android-usb}"
ANDROID_ROOTFS_DIR="${ANDROID_ROOTFS_DIR:-$ANDROID_USB_MOUNT/android-rootfs}"
ANDROID_SIDE_DIR="${ANDROID_SIDE_DIR:-$ANDROID_USB_MOUNT/android-sidecar}"

ssh root@"$TV_IP" "export R='$ANDROID_ROOTFS_DIR'; export S='$ANDROID_SIDE_DIR'; sh -s" <<'TVSH'
set -u

echo "== kernel =="
uname -a
cat /proc/version 2>/dev/null || true

echo
echo "== android usb mounts =="
grep -E 'android-usb|android-rootfs|system_raw|vendor_raw|binder' /proc/mounts || true

echo
echo "== binder devices =="
grep -i binder /proc/misc 2>/dev/null || true
ls -l /dev/binder /dev/hwbinder /dev/vndbinder 2>/dev/null || true
ls -l "$R/dev/binder" "$R/dev/hwbinder" "$R/dev/vndbinder" 2>/dev/null || true

echo
echo "== binder modules =="
lsmod 2>/dev/null | grep -i binder || true
for f in /sys/module/*binder*/parameters/*; do
  [ -e "$f" ] || continue
  echo "--- $f"
  cat "$f" 2>/dev/null || true
done

echo
echo "== binder FD prerequisite symbols =="
fd_ok=1
for f in \
  /sys/module/binder/parameters/sym___alloc_fd \
  /sys/module/binder/parameters/sym___fd_install \
  /sys/module/binder/parameters/sym___close_fd \
  /sys/module/binder/parameters/sym_get_files_struct \
  /sys/module/binder/parameters/sym_put_files_struct
do
  name="$(basename "$f")"
  val="$(cat "$f" 2>/dev/null || echo 0)"
  echo "$name=$val"
  if [ "$val" = "0" ]; then
    fd_ok=0
  fi
done

if [ "$fd_ok" = "1" ]; then
  echo "BINDER_FD_SYMBOLS_READY=YES"
else
  echo "BINDER_FD_SYMBOLS_READY=NO"
fi

echo
echo "== android build props on disk =="
grep -h '^ro.build.version.release=' "$R/system/build.prop" "$R/system/system/build.prop" "$R/vendor/build.prop" 2>/dev/null || true

echo
echo "== linkerconfig/apex =="
ls -ld "$R/linkerconfig" "$R/apex" "$R/system/apex" 2>/dev/null || true
ls -l "$R/system/bin/linkerconfig" "$R/apex/com.android.runtime/bin/linkerconfig" 2>/dev/null || true

echo
echo "== key android binaries =="
for b in /system/bin/servicemanager /system/bin/hwservicemanager /system/bin/vndservicemanager /system/bin/getprop /system/bin/toybox; do
  if [ -e "$R$b" ]; then
    echo "--- $b"
    ls -l "$R$b"
  else
    echo "MISSING $b"
  fi
done

echo
echo "== chroot getprop =="
chroot "$R" /system/bin/getprop ro.build.version.release 2>&1 || true

echo
echo "== running service managers =="
ps | grep -E 'servicemanager|hwservicemanager' | grep -v grep || true

echo
echo "== servicemanager test =="
if ps | grep '[s]ervicemanager' >/dev/null 2>&1; then
  echo "SERVICEMANAGER_ALREADY_RUNNING=YES"
else
  rm -f /tmp/android-servicemanager-test.log
  ( chroot "$R" /system/bin/servicemanager; echo "SERVICEMANAGER_EXIT=$?" ) > /tmp/android-servicemanager-test.log 2>&1 &
  pid=$!
  sleep 3
  if kill -0 "$pid" 2>/dev/null; then
    echo "SERVICEMANAGER_STILL_RUNNING_AFTER_3S=YES"
    kill "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
  else
    wait "$pid" 2>/dev/null || true
    echo "SERVICEMANAGER_EXITED_QUICKLY=YES"
  fi
  cat /tmp/android-servicemanager-test.log 2>/dev/null || true
fi

echo
echo "== hwservicemanager one-shot test =="
if [ -x "$R/system/bin/hwservicemanager" ]; then
  rm -f /tmp/android-hwservicemanager-test.log
  ( chroot "$R" /system/bin/hwservicemanager; echo "HWSERVICEMANAGER_EXIT=$?" ) > /tmp/android-hwservicemanager-test.log 2>&1 &
  pid=$!
  sleep 3
  if kill -0 "$pid" 2>/dev/null; then
    echo "HWSERVICEMANAGER_STILL_RUNNING_AFTER_3S=YES"
    kill "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
  else
    wait "$pid" 2>/dev/null || true
    echo "HWSERVICEMANAGER_EXITED_QUICKLY=YES"
  fi
  cat /tmp/android-hwservicemanager-test.log 2>/dev/null || true
fi

echo
echo "== install log tail =="
tail -n 80 "$S/logs/android-usb-install.log" 2>/dev/null || true

echo
echo "== binder dmesg tail =="
dmesg 2>/dev/null | grep -i binder | tail -n 160 || true
TVSH
