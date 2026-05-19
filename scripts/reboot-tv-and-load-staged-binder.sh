#!/usr/bin/env bash
set -euo pipefail

TV_IP="${TV_IP:-192.168.2.121}"
FD_DEBUG_STAGE="${FD_DEBUG_STAGE:-0}"

KO=""
for c in \
  build/linux-4.4.84/drivers/android/binder.ko \
  build/linux-4.4.84/drivers/staging/android/binder.ko \
  artifacts/binder.ko \
  artifacts/binder-dirty-lgc1-o20-4.4.84-229.1.kavir.2.ko
do
  [ -f "$c" ] && KO="$c" && break
done

[ -n "$KO" ] && [ -f "$KO" ] || {
  echo "ERROR: binder.ko not found"
  exit 1
}

echo "== selected KO: $KO =="
modinfo -p "$KO" | grep -q '^fd_debug_stage:' || {
  echo "ERROR: selected binder.ko has no fd_debug_stage; refusing"
  modinfo -p "$KO" || true
  exit 1
}

echo "== reboot TV =="
ssh -o ConnectTimeout=5 root@"$TV_IP" 'sync; reboot' || true

echo "== wait for SSH to go down =="
for i in $(seq 1 40); do
  if ! ssh -o ConnectTimeout=2 -o BatchMode=yes root@"$TV_IP" true 2>/dev/null; then
    echo "SSH is down"
    break
  fi
  sleep 1
done

echo "== wait for SSH to come back =="
for i in $(seq 1 180); do
  if ssh -o ConnectTimeout=2 -o BatchMode=yes root@"$TV_IP" true 2>/dev/null; then
    echo "SSH is back"
    break
  fi
  sleep 1
  if [ "$i" = 180 ]; then
    echo "ERROR: TV did not come back over SSH"
    exit 1
  fi
done

echo "== copy staged binder immediately =="
scp "$KO" root@"$TV_IP":/tmp/binder-fd-stage.ko >/dev/null

echo "== install staged binder if binder is not already loaded =="
ssh root@"$TV_IP" "FD_DEBUG_STAGE='$FD_DEBUG_STAGE' sh -s" <<'TVSH'
set -eu

echo "== current binder state after reboot =="
grep '^binder ' /proc/modules 2>/dev/null || echo "binder not loaded yet"

if grep -q '^binder ' /proc/modules 2>/dev/null; then
  echo "ERROR: binder was already loaded before we could insert staged module"
  echo "This means some boot/startup script auto-loaded the old permanent binder."
  echo
  echo "== loaded module =="
  grep '^binder ' /proc/modules || true
  echo
  echo "== params =="
  ls -l /sys/module/binder/parameters 2>/dev/null || true
  echo
  echo "== likely startup files mentioning binder =="
  grep -R "binder\.ko\|insmod.*binder\|modprobe.*binder" \
    /etc /var /mnt/lg /media /tmp 2>/dev/null | head -80 || true
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

echo "== insmod staged binder fd_debug_stage=$FD_DEBUG_STAGE =="
insmod /tmp/binder-fd-stage.ko \
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

echo "== staged binder loaded =="
grep '^binder ' /proc/modules || true
ls -l /sys/module/binder/parameters || true
cat /sys/module/binder/parameters/fd_debug_stage || true

echo "REBOOT_LOAD_STAGED_BINDER_OK stage=$FD_DEBUG_STAGE"
TVSH
