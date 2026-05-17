#!/usr/bin/env bash
set -euo pipefail

TV_IP="${TV_IP:-192.168.2.121}"

ssh root@"$TV_IP" 'sh -s' <<'TVSH'
set -eu

echo "== DEEP TV STORAGE MAP =="
date || true

echo
echo "== mounted filesystems =="
df -hP 2>/dev/null || true

echo
echo "== partitions =="
cat /proc/partitions 2>/dev/null || true

echo
echo "== block device nodes =="
ls -lh /dev/mmcblk* /dev/sd* /dev/loop* 2>/dev/null || true

echo
echo "== mount table =="
cat /proc/mounts 2>/dev/null | grep -E 'mmc|sd|loop|overlay|tmpfs|media|mnt|var|appstore|internal' || true

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
du -sk /mnt/lg/appstore/* /mnt/lg/appstore/.[!.]* 2>/dev/null \
  | sort -nr \
  | head -80 \
  | awk '
      function human(k) {
        if (k >= 1048576) return sprintf("%.2fG", k/1048576);
        if (k >= 1024) return sprintf("%.1fM", k/1024);
        return sprintf("%dK", k);
      }
      {printf "%8s  %s\n", human($1), $2}
    '

echo
echo "== biggest directories under /media/internal =="
du -sk /media/internal/* /media/internal/.[!.]* 2>/dev/null \
  | sort -nr \
  | head -80 \
  | awk '
      function human(k) {
        if (k >= 1048576) return sprintf("%.2fG", k/1048576);
        if (k >= 1024) return sprintf("%.1fM", k/1024);
        return sprintf("%dK", k);
      }
      {printf "%8s  %s\n", human($1), $2}
    '

echo
echo "== biggest directories under /var =="
du -sk /var/* /var/.[!.]* 2>/dev/null \
  | sort -nr \
  | head -80 \
  | awk '
      function human(k) {
        if (k >= 1048576) return sprintf("%.2fG", k/1048576);
        if (k >= 1024) return sprintf("%.1fM", k/1024);
        return sprintf("%dK", k);
      }
      {printf "%8s  %s\n", human($1), $2}
    '

echo
echo "== candidate cleanup dirs sizes =="
for p in \
  /media/internal/android-downloads \
  /media/internal/android-images \
  /media/internal/android-sidecar/logs \
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
  /var/lib/wam/Default/Code\ Cache \
  /var/lib/wam/Default/GPUCache \
  /var/lib/wam/Default/Application\ Cache \
  /var/lib/wam/Default/Service\ Worker/CacheStorage
do
  if [ -e "$p" ]; then
    du -sh "$p" 2>/dev/null || true
  fi
done

echo
echo "== files bigger than 5MB under /media/internal =="
find /media/internal -xdev -type f -size +5M -exec ls -lh {} \; 2>/dev/null \
  | awk '{print $5 "  " $9}' \
  | sort -hr \
  | head -100 || true

echo
echo "== files bigger than 5MB under /mnt/lg/appstore =="
find /mnt/lg/appstore -xdev -type f -size +5M -exec ls -lh {} \; 2>/dev/null \
  | awk '{print $5 "  " $9}' \
  | sort -hr \
  | head -120 || true

echo
echo "DEEP_TV_STORAGE_MAP_DONE"
TVSH
