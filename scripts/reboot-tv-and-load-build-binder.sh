#!/usr/bin/env bash
set -euo pipefail

cd /home/pi/disk/webos-dirty-binder

TV_IP="${TV_IP:-192.168.2.121}"
FD_DEBUG_STAGE="${FD_DEBUG_STAGE:-2}"
KO="build/linux-4.4.84/drivers/android/binder.ko"

[ -f "$KO" ] || { echo "ERROR: missing $KO"; exit 1; }

modinfo -p "$KO" | grep -q '^fd_debug_stage:' || {
  echo "ERROR: $KO lacks fd_debug_stage"
  exit 1
}

readelf -sW "$KO" | grep -q ' cleanup_module$' || {
  echo "ERROR: $KO lacks cleanup_module; refusing to load permanent module"
  exit 1
}

echo "== using ONLY build KO: $KO =="
ls -lh "$KO"

echo "== reboot TV =="
ssh -o ConnectTimeout=5 root@"$TV_IP" 'sync; reboot' || true

echo "== wait SSH down =="
for i in $(seq 1 40); do
  if ! ssh -o ConnectTimeout=2 -o BatchMode=yes root@"$TV_IP" true 2>/dev/null; then
    echo "SSH down"
    break
  fi
  sleep 1
done

echo "== wait SSH up =="
for i in $(seq 1 180); do
  if ssh -o ConnectTimeout=2 -o BatchMode=yes root@"$TV_IP" true 2>/dev/null; then
    echo "SSH up"
    break
  fi
  sleep 1
  [ "$i" = 180 ] && { echo "ERROR: SSH did not return"; exit 1; }
done

scp "$KO" root@"$TV_IP":/tmp/binder-build-unloadable.ko >/dev/null

ssh root@"$TV_IP" "FD_DEBUG_STAGE='$FD_DEBUG_STAGE' sh -s" <<'TVSH'
set -eu

echo "== current binder after reboot =="
grep '^binder ' /proc/modules 2>/dev/null || echo "binder not loaded"

if grep -q '^binder ' /proc/modules 2>/dev/null; then
  echo "ERROR: binder auto-loaded before our test KO"
  grep '^binder ' /proc/modules || true
  exit 23
fi

addr() {
  awk -v s="$1" '$3 == s { print "0x"$1; exit }' /proc/kallsyms
}
need() {
  v="$(addr "$1")"
  [ -n "$v" ] || { echo "ERROR missing symbol $1"; exit 1; }
  echo "$v"
}

insmod /tmp/binder-build-unloadable.ko \
  sym_zap_page_range="$(need zap_page_range)" \
  sym_put_files_struct="$(need put_files_struct)" \
  sym_get_vm_area="$(need get_vm_area)" \
  sym___fd_install="$(need __fd_install)" \
  sym___close_fd="$(need __close_fd)" \
  sym_map_kernel_range_noflush="$(need map_kernel_range_noflush)" \
  sym___lock_task_sighand="$(need __lock_task_sighand)" \
  sym_get_files_struct="$(need get_files_struct)" \
  sym___alloc_fd="$(need __alloc_fd)" \
  fd_debug_stage="$FD_DEBUG_STAGE"

minor="$(awk '$2 == "binder" { print $1; exit }' /proc/misc)"
[ -n "$minor" ] && [ ! -e /dev/binder ] && mknod /dev/binder c 10 "$minor"
chmod 666 /dev/binder 2>/dev/null || true

echo "== loaded binder line =="
grep '^binder ' /proc/modules || true

if grep '^binder ' /proc/modules | grep -q '\[permanent\]'; then
  echo "ERROR: loaded binder is still permanent"
  exit 24
fi

echo "fd_debug_stage=$(cat /sys/module/binder/parameters/fd_debug_stage)"
echo "LOAD_BUILD_UNLOADABLE_BINDER_OK"
TVSH
