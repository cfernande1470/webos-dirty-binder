#!/usr/bin/env bash
set -euo pipefail

TV_IP="${TV_IP:-192.168.2.121}"

ssh root@"$TV_IP" 'sh -s' <<'TVSH'
set -eu

human_kb() {
  awk '
    function human(k) {
      if (k >= 1048576) return sprintf("%.2fG", k/1048576);
      if (k >= 1024) return sprintf("%.1fM", k/1024);
      return sprintf("%dK", k);
    }
    {printf "%8s  %s\n", human($1), $2}
  '
}

human_bytes() {
  awk '
    function human(b) {
      if (b >= 1073741824) return sprintf("%.2fG", b/1073741824);
      if (b >= 1048576) return sprintf("%.1fM", b/1048576);
      if (b >= 1024) return sprintf("%.1fK", b/1024);
      return sprintf("%dB", b);
    }
    {printf "%8s  %s\n", human($1), $2}
  '
}

echo "== DEEP TV STORAGE MAP BUSYBOX =="
date || true

echo
echo "== mounted filesystems =="
df -hP 2>/dev/null || true

echo
echo "== partitions =="
cat /proc/partitions 2>/dev/null || true

echo
echo "== mounted mmc partitions with mountpoints =="
for p in /dev/mmcblk0p*; do
  [ -e "$p" ] || continue
  mp="$(grep "^$p " /proc/mounts 2>/dev/null | awk '{print $2}' | tr '\n' ' ')"
  size="$(cat /sys/class/block/$(basename "$p")/size 2>/dev/null || echo 0)"
  kb="$((size / 2))"
  printf "%-20s %12s KB  %s\n" "$p" "$kb" "$mp"
done

echo
echo "== biggest directories under /mnt/lg/appstore =="
if [ -d /mnt/lg/appstore ]; then
  du -sk /mnt/lg/appstore/* /mnt/lg/appstore/.[!.]* 2>/dev/null | sort -nr | head -80 | human_kb
fi

echo
echo "== biggest directories under /media/internal =="
if [ -d /media/internal ]; then
  du -sk /media/internal/* /media/internal/.[!.]* 2>/dev/null | sort -nr | head -80 | human_kb
fi

echo
echo "== biggest directories under /var =="
if [ -d /var ]; then
  du -sk /var/* /var/.[!.]* 2>/dev/null | sort -nr | head -80 | human_kb
fi

echo
echo "== candidate cleanup dirs sizes =="
for p in \
  /tmp/android-usb/android-downloads \
  /tmp/android-usb/android-images \
  /tmp/android-usb/android-sidecar/logs \
  /media/internal/downloads \
  /mnt/lg/appstore/cryptofs/apps/var/cache \
  /mnt/lg/appstore/cryptofs/tmp \
  /mnt/lg/appstore/developer/apps/var/cache \
  /mnt/lg/appstore/developer/tmp \
  /mnt/lg/appstore/preload/igallery/files/download \
  /mnt/lg/appstore/system/apps/var/cache \
  /mnt/lg/appstore/system/tmp \
  /var/cache \
  /var/file-cache \
  /var/lib/systemd/coredump \
  /var/lib/wam/Default/Cache \
  "/var/lib/wam/Default/Code Cache" \
  /var/lib/wam/Default/GPUCache \
  "/var/lib/wam/Default/Application Cache" \
  "/var/lib/wam/Default/Service Worker/CacheStorage"
do
  if [ -e "$p" ]; then
    du -sk "$p" 2>/dev/null | human_kb || true
  fi
done

echo
echo "== files bigger than 5MB under /media/internal =="
if [ -d /media/internal ]; then
  find /media/internal -xdev -type f -size +5M -exec ls -ln {} \; 2>/dev/null \
    | awk '{print $5, $9}' \
    | sort -nr \
    | head -120 \
    | human_bytes || true
fi

echo
echo "== files bigger than 5MB under /mnt/lg/appstore =="
if [ -d /mnt/lg/appstore ]; then
  find /mnt/lg/appstore -xdev -type f -size +5M -exec ls -ln {} \; 2>/dev/null \
    | awk '{print $5, $9}' \
    | sort -nr \
    | head -160 \
    | human_bytes || true
fi

echo
echo "== removable android staging estimate =="
du -sk \
  /tmp/android-usb/android-downloads \
  /tmp/android-usb/android-images \
  /tmp/android-usb/android-mounts \
  /tmp/android-usb/android-sidecar/logs \
  2>/dev/null | sort -nr | human_kb || true

echo
echo "DEEP_TV_STORAGE_MAP_BUSYBOX_DONE"
TVSH
