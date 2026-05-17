#!/usr/bin/env bash
set -euo pipefail

TV_IP="${TV_IP:-192.168.2.121}"
OUT="logs/fd-autopsy-$(date +%Y%m%d-%H%M%S).log"

mkdir -p logs

ssh root@"$TV_IP" 'sh -s' <<'TVSH' | tee "$OUT"
set -eu

echo "== FD AUTOPSY TV =="
date || true
uptime || true
uname -a || true
id || true

echo
echo "== binder device =="
ls -l /dev/binder /dev/hwbinder /dev/vndbinder 2>/dev/null || true

echo
echo "== binder module =="
lsmod | grep -E '(^binder|binder)' || true
cat /proc/modules | grep -E '(^binder|binder)' || true
find /lib/modules /media/internal/android-sidecar -name 'binder.ko' -type f 2>/dev/null | while read f; do
  echo "--- $f"
  ls -lh "$f" || true
  strings "$f" | grep -Ei 'binder|transfer|fd|accept_fds|security_binder' | head -80 || true
done

echo
echo "== kernel config binder/security =="
zcat /proc/config.gz 2>/dev/null | grep -Ei 'ANDROID|BINDER|SECURITY|SELINUX|SMACK|APPARMOR|LSM' || true
cat /boot/config-$(uname -r) 2>/dev/null | grep -Ei 'ANDROID|BINDER|SECURITY|SELINUX|SMACK|APPARMOR|LSM' || true

echo
echo "== debugfs binder =="
mount | grep debugfs || true
mount -t debugfs debugfs /sys/kernel/debug 2>/dev/null || true

find /sys/kernel/debug -maxdepth 3 -type f 2>/dev/null | grep -E '/binder/' || true

for f in \
  /sys/kernel/debug/binder/state \
  /sys/kernel/debug/binder/stats \
  /sys/kernel/debug/binder/transactions \
  /sys/kernel/debug/binder/transaction_log \
  /sys/kernel/debug/binder/failed_transaction_log
do
  if [ -r "$f" ]; then
    echo
    echo "--- $f"
    cat "$f" 2>/dev/null | tail -200 || true
  fi
done

echo
echo "== pstore after reboot/crash =="
find /sys/fs/pstore -maxdepth 1 -type f 2>/dev/null | sort | while read f; do
  echo "--- $f"
  ls -lh "$f" || true
  head -200 "$f" 2>/dev/null || true
done

echo
echo "== dmesg binder/crash tail =="
dmesg 2>/dev/null | grep -Ei 'binder|fd|failed|panic|oops|bug|segfault|watchdog|reboot|fatal|exception|denied|security' | tail -250 || true

echo
echo "== android-sidecar deployed fd binaries =="
ls -lh /media/internal/android-sidecar/bin 2>/dev/null | grep -Ei 'fd|binder|service|client' || true

echo
echo "FD_AUTOPSY_TV_DONE"
TVSH

echo
echo "Saved: $OUT"
