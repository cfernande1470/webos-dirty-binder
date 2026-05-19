#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

TV_IP="${TV_IP:-192.168.2.121}"
FD_DEBUG_STAGE="${FD_DEBUG_STAGE:-7}"
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

scp "$KO" root@"$TV_IP":/tmp/binder-build-final.ko >/dev/null

ssh root@"$TV_IP" "FD_DEBUG_STAGE='$FD_DEBUG_STAGE' sh -s" <<'TVSH'
set -eu

killall servicemanager hwservicemanager vndservicemanager mini_servicemgr android_like_fd_passing_service android_like_fd_passing_client 2>/dev/null || true
sleep 1

if grep -q '^binder ' /proc/modules 2>/dev/null; then
  echo "== rmmod old binder =="
  if ! rmmod binder; then
    echo "ERROR: binder is still in use; refusing to reuse old/stuck module"
    grep '^binder ' /proc/modules || true
    echo "Reboot required before loading the new build KO."
    exit 42
  fi
fi

rm -f /dev/binder /dev/hwbinder /dev/vndbinder 2>/dev/null || true

addr() {
  awk -v s="$1" '$3 == s { print "0x"$1; exit }' /proc/kallsyms
}

need() {
  v="$(addr "$1")"
  [ -n "$v" ] || { echo "ERROR missing symbol $1"; exit 1; }
  echo "$v"
}

insmod /tmp/binder-build-final.ko \
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

for dev in binder hwbinder vndbinder; do
  minor="$(awk -v d="$dev" '$2 == d { print $1; exit }' /proc/misc 2>/dev/null || true)"
  if [ -n "$minor" ] && [ ! -e "/dev/$dev" ]; then
    mknod "/dev/$dev" c 10 "$minor" 2>/dev/null || true
  fi
  chmod 666 "/dev/$dev" 2>/dev/null || true
done

grep '^binder ' /proc/modules
if grep '^binder ' /proc/modules | grep -q '\[permanent\]'; then
  echo "ERROR: binder is permanent"
  exit 24
fi

echo "fd_debug_stage=$(cat /sys/module/binder/parameters/fd_debug_stage)"
echo "RELOAD_BUILD_FINAL_BINDER_OK"
TVSH
