#!/usr/bin/env bash
set -euo pipefail

TV_IP="${TV_IP:-192.168.2.121}"

ssh root@"$TV_IP" 'sh -s' <<'TVSH'
set -eu

echo "== Android rootfs readiness audit =="
date || true
uptime || true
uname -a || true
id || true

echo
echo "== storage =="
df -h / /media/internal /tmp 2>/dev/null || true
du -sh /media/internal/android-sidecar /media/internal/android-rootfs 2>/dev/null || true

echo
echo "== network =="
ip addr 2>/dev/null || ifconfig 2>/dev/null || true
ip route 2>/dev/null || route -n 2>/dev/null || true
cat /etc/resolv.conf 2>/dev/null || true

echo
echo "== internet checks =="
ping -c 1 -W 3 1.1.1.1 2>/dev/null && echo "PING_IP_OK" || echo "PING_IP_FAIL"
ping -c 1 -W 3 google.com 2>/dev/null && echo "DNS_PING_OK" || echo "DNS_PING_FAIL"

echo
echo "== download tools =="
for t in wget curl busybox aria2c openssl sha256sum md5sum; do
  printf "%-16s " "$t"
  command -v "$t" 2>/dev/null || echo "missing"
done

echo
echo "== archive tools =="
for t in tar gzip gunzip xz unxz bzip2 bunzip2 unzip cpio dd file readelf ldd; do
  printf "%-16s " "$t"
  command -v "$t" 2>/dev/null || echo "missing"
done

echo
echo "== rootfs/chroot/mount tools =="
for t in chroot mount umount mknod losetup nsenter unshare proot toybox; do
  printf "%-16s " "$t"
  command -v "$t" 2>/dev/null || echo "missing"
done

echo
echo "== package managers, if any =="
for t in opkg apk apt apt-get dnf yum pacman rpm dpkg luna-send; do
  printf "%-16s " "$t"
  command -v "$t" 2>/dev/null || echo "missing"
done

echo
echo "== binder devices =="
ls -l /dev/binder /dev/hwbinder /dev/vndbinder 2>/dev/null || true
grep '^binder ' /proc/modules 2>/dev/null || true

echo
echo "== kernel features hints =="
zcat /proc/config.gz 2>/dev/null | grep -E 'CONFIG_ANDROID|CONFIG_NAMESPACES|CONFIG_OVERLAY_FS|CONFIG_TMPFS|CONFIG_DEVTMPFS|CONFIG_CGROUP|CONFIG_SECCOMP|CONFIG_BINFMT_MISC' || true

echo
echo "== libc / dynamic loaders =="
find /lib /usr/lib -maxdepth 3 \( -name 'ld-*' -o -name 'ld-linux*' -o -name 'libc.so*' \) 2>/dev/null | sort | head -80 || true

echo
echo "== existing android dirs =="
find /media/internal -maxdepth 3 \( -name '*android*' -o -name '*rootfs*' -o -name '*aosp*' \) 2>/dev/null | sort || true

echo
echo "ANDROID_ROOTFS_READINESS_AUDIT_DONE"
TVSH
