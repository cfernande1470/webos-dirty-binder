#!/usr/bin/env bash
set -euo pipefail

TV_IP="${TV_IP:-192.168.2.121}"
SIDE_DIR="${SIDE_DIR:-/media/internal/android-sidecar}"
ROOTFS="${ROOTFS:-/media/internal/android-rootfs}"

OUT="logs/android-real-sm-v0-failure-$(date +%Y%m%d-%H%M%S).log"

ssh root@"$TV_IP" "SIDE_DIR='$SIDE_DIR' ROOTFS='$ROOTFS' sh -s" <<'TVSH' | tee "$OUT"
set +e

echo "== Android real servicemanager v0 failure collection =="
date || true
uptime || true
uname -a || true

echo
echo "== processes =="
ps | grep -E 'servicemanager|parcel_fd_lite|mini_servicemgr|binder' | grep -v grep || true

echo
echo "== binder module/device =="
grep '^binder ' /proc/modules || true
ls -l /dev/binder /dev/hwbinder /dev/vndbinder 2>/dev/null || true

echo
echo "== rootfs mounts =="
grep -E 'android-rootfs|android-mounts|android-usb' /proc/mounts || true

echo
echo "== sidecar logs list =="
ls -lh "$SIDE_DIR/logs"/android_real_sm_*.log 2>/dev/null || true

echo
echo "================ servicemanager log ================"
cat "$SIDE_DIR/logs/android_real_sm_servicemanager.log" 2>/dev/null || true

echo
echo "================ ParcelFD-lite service log ================"
cat "$SIDE_DIR/logs/android_real_sm_parcelfd_service.log" 2>/dev/null || true

echo
echo "================ ParcelFD-lite client log ================"
cat "$SIDE_DIR/logs/android_real_sm_parcelfd_client.log" 2>/dev/null || true

echo
echo "================ dmesg Binder/Android tail ================"
dmesg 2>/dev/null | grep -Ei 'binder|servicemanager|service_manager|transaction failed|SELinux|avc|denied|linker|apex|android' | tail -200 || true

echo
echo "================ rootfs Android binaries ================"
for f in \
  "$ROOTFS/system/bin/servicemanager" \
  "$ROOTFS/system/bin/hwservicemanager" \
  "$ROOTFS/system/bin/vndservicemanager" \
  "$ROOTFS/system/bin/toybox" \
  "$ROOTFS/system/bin/sh" \
  "$ROOTFS/apex/com.android.runtime/bin/linker64"
do
  echo "--- $f"
  ls -lh "$f" 2>/dev/null || true
done

echo
echo "================ quick chroot sanity ================"
chroot "$ROOTFS" /system/bin/toybox true
echo "toybox_true_rc=$?"
chroot "$ROOTFS" /system/bin/sh -c 'echo CHROOT_STILL_OK'
echo "sh_rc=$?"

echo
echo "ANDROID_REAL_SM_V0_FAILURE_COLLECTION_DONE"
TVSH

echo
echo "Saved: $OUT"
echo
echo "== compact diagnosis hints =="
grep -Ei 'cannot|failed|denied|avc|SELinux|BR_FAILED|transaction failed|binder|linker|service manager|servicemanager|permission|not found|No such' "$OUT" | tail -120 || true
