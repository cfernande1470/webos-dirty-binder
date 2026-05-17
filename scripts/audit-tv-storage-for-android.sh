#!/usr/bin/env bash
set -euo pipefail

TV_IP="${TV_IP:-192.168.2.121}"
OUT="logs/tv-storage-audit-$(date +%Y%m%d-%H%M%S).log"

ssh root@"$TV_IP" 'sh -s' <<'TVSH' | tee "$OUT"
set -eu

echo "== TV STORAGE AUDIT FOR ANDROID =="
date || true
uname -a || true
id || true

echo
echo "== df all =="
df -h 2>/dev/null || true
echo
df -hP 2>/dev/null || true

echo
echo "== mounts =="
mount 2>/dev/null || true

echo
echo "== proc mounts interesting =="
grep -E 'mmc|ubi|ext|squash|tmpfs|overlay|media|mnt|lg|appstore|internal' /proc/mounts 2>/dev/null || true

echo
echo "== block devices =="
cat /proc/partitions 2>/dev/null || true
ls -l /dev/mmcblk* /dev/loop* /dev/mapper/* 2>/dev/null || true

echo
echo "== blkid if available =="
blkid 2>/dev/null || true

echo
echo "== path identity =="
for p in / /media /media/internal /mnt /mnt/lg /mnt/lg/appstore /tmp /var /home /home/root /opt /usr /var/log /var/lib; do
  echo "--- $p"
  ls -ld "$p" 2>/dev/null || true
  readlink -f "$p" 2>/dev/null || true
  df -hP "$p" 2>/dev/null || true
done

echo
echo "== top-level sizes, same filesystem where possible =="
for base in /media/internal /mnt/lg/appstore /mnt/lg /var /home/root /tmp /opt /usr/local; do
  if [ -d "$base" ]; then
    echo
    echo "--- top entries in $base"
    du -sk "$base"/* "$base"/.[!.]* 2>/dev/null | sort -nr | head -40 | awk '
      function human(k) {
        if (k >= 1048576) return sprintf("%.1fG", k/1048576);
        if (k >= 1024) return sprintf("%.1fM", k/1024);
        return sprintf("%dK", k);
      }
      {printf "%8s  %s\n", human($1), $2}
    '
  fi
done

echo
echo "== android-related sizes =="
for p in \
  /media/internal/android-rootfs \
  /media/internal/android-sidecar \
  /media/internal/android-downloads \
  /media/internal/android-images \
  /media/internal/android-mounts \
  /mnt/lg/appstore/android-rootfs \
  /mnt/lg/appstore/android-sidecar \
  /mnt/lg/appstore/android-downloads \
  /mnt/lg/appstore/android-images \
  /tmp/android-rootfs \
  /tmp/android-downloads \
  /tmp/android-images
do
  [ -e "$p" ] && du -sh "$p" 2>/dev/null || true
done

echo
echo "== largest files in likely safe/user areas =="
for base in /media/internal /mnt/lg/appstore /home/root /tmp; do
  if [ -d "$base" ]; then
    echo
    echo "--- largest files under $base"
    find "$base" -xdev -type f -size +20M -exec ls -lh {} \; 2>/dev/null \
      | awk '{print $5 "  " $9}' \
      | sort -hr \
      | head -80 || true
  fi
done

echo
echo "== likely cache/log/download dirs =="
for base in /media/internal /mnt/lg/appstore /var /home/root; do
  if [ -d "$base" ]; then
    echo
    echo "--- cache/log/download candidates under $base"
    find "$base" -xdev -type d \( \
      -iname '*cache*' -o \
      -iname '*log*' -o \
      -iname '*tmp*' -o \
      -iname '*download*' -o \
      -iname '*crash*' -o \
      -iname '*dump*' \
    \) -maxdepth 5 2>/dev/null | sort | head -200 || true
  fi
done

echo
echo "== opkg info =="
opkg list-installed 2>/dev/null | head -80 || true
opkg info 2>/dev/null | grep -E '^(Package|Installed-Size|Size):' | head -160 || true

echo
echo "== our known safe cleanup estimate =="
echo "--- android-sidecar logs"
du -sh /media/internal/android-sidecar/logs 2>/dev/null || true
echo "--- android downloads"
du -sh /media/internal/android-downloads 2>/dev/null || true
echo "--- android images"
du -sh /media/internal/android-images 2>/dev/null || true
echo "--- android mounts"
du -sh /media/internal/android-mounts 2>/dev/null || true

echo
echo "TV_STORAGE_AUDIT_DONE"
TVSH

echo
echo "Saved: $OUT"

echo
echo "================ SUMMARY ================"
echo
grep -A20 '== df all ==' "$OUT" | sed -n '1,25p'
echo
grep -A20 '== android-related sizes ==' "$OUT" | sed -n '1,40p'
echo
grep -A100 '== largest files in likely safe/user areas ==' "$OUT" | sed -n '1,120p'
echo
echo "TV_STORAGE_AUDIT_SUMMARY_DONE"
