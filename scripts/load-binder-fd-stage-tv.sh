#!/usr/bin/env bash
set -euo pipefail

TV_IP="${TV_IP:-192.168.2.121}"
FD_DEBUG_STAGE="${FD_DEBUG_STAGE:-0}"
FORCE_RMMOD="${FORCE_RMMOD:-0}"

KO="${KO:-}"
if [ -z "$KO" ]; then
  for c in \
    build/linux-4.4.84/drivers/android/binder.ko \
    build/linux-4.4.84/drivers/staging/android/binder.ko \
    artifacts/binder.ko \
    artifacts/binder-dirty-lgc1-o20-4.4.84-229.1.kavir.2.ko
  do
    [ -f "$c" ] && KO="$c" && break
  done
fi

[ -n "$KO" ] && [ -f "$KO" ] || {
  echo "ERROR: binder.ko not found"
  exit 1
}

echo "== selected KO: $KO =="
modinfo -p "$KO" 2>/dev/null | grep -q '^fd_debug_stage:' || {
  echo "ERROR: selected KO has no fd_debug_stage param; refusing to copy stale module"
  echo "modinfo -p $KO:"
  modinfo -p "$KO" 2>/dev/null || true
  exit 1
}

scp "$KO" root@"$TV_IP":/tmp/binder-fd-stage.ko >/dev/null

ssh root@"$TV_IP" "FD_DEBUG_STAGE='$FD_DEBUG_STAGE' FORCE_RMMOD='$FORCE_RMMOD' sh -s" <<'TVSH'
set -u

make_binder_dev() {
  minor="$(awk '$2 == "binder" { print $1; exit }' /proc/misc 2>/dev/null || true)"
  if [ -n "$minor" ] && [ ! -e /dev/binder ]; then
    rm -f /dev/binder 2>/dev/null || true
    mknod /dev/binder c 10 "$minor" 2>/dev/null || true
  fi
  chmod 666 /dev/binder 2>/dev/null || true
}

show_binder_state() {
  echo "== /proc/modules binder =="
  grep '^binder ' /proc/modules 2>/dev/null || echo "binder not in /proc/modules"

  echo "== /sys/module/binder params =="
  if [ -d /sys/module/binder/parameters ]; then
    ls -l /sys/module/binder/parameters 2>/dev/null || true
    for x in /sys/module/binder/parameters/*; do
      [ -e "$x" ] || continue
      echo "$(basename "$x")=$(cat "$x" 2>/dev/null || true)"
    done
  else
    echo "no /sys/module/binder/parameters"
  fi
}

list_binder_holders() {
  echo "== processes holding binder/android sidecar =="
  found=0

  for p in /proc/[0-9]*; do
    pid="${p##*/}"
    comm="$(cat "$p/comm" 2>/dev/null || true)"
    cmd="$(tr '\000' ' ' < "$p/cmdline" 2>/dev/null || true)"
    hit=0

    for fd in "$p"/fd/*; do
      t="$(readlink "$fd" 2>/dev/null || true)"
      case "$t" in
        *"/dev/binder"*|*"anon_inode:binder"*|*"binder"*)
          hit=1
          ;;
      esac
      [ "$hit" = 1 ] && break
    done

    case "$cmd" in
      *android-usb*|*android-rootfs*|*servicemanager*|*hwservicemanager*|*vndservicemanager*|*mini_servicemgr*|*fd_passing*)
        hit=1
        ;;
    esac

    if [ "$hit" = 1 ]; then
      found=1
      echo "pid=$pid comm=$comm cmd=$cmd"
      for fd in "$p"/fd/*; do
        t="$(readlink "$fd" 2>/dev/null || true)"
        case "$t" in
          *binder*) echo "  fd ${fd##*/} -> $t" ;;
        esac
      done
    fi
  done

  [ "$found" = 1 ] || echo "no obvious binder holders found"
}

kill_binder_world() {
  sig="$1"
  echo "== kill binder/android holders: SIG$sig =="

  killall servicemanager 2>/dev/null || true
  killall hwservicemanager 2>/dev/null || true
  killall vndservicemanager 2>/dev/null || true
  killall mini_servicemgr 2>/dev/null || true
  killall android_like_fd_passing_service 2>/dev/null || true
  killall android_like_fd_passing_client 2>/dev/null || true

  for p in /proc/[0-9]*; do
    pid="${p##*/}"
    [ "$pid" = "$$" ] && continue

    comm="$(cat "$p/comm" 2>/dev/null || true)"
    cmd="$(tr '\000' ' ' < "$p/cmdline" 2>/dev/null || true)"
    hit=0

    for fd in "$p"/fd/*; do
      t="$(readlink "$fd" 2>/dev/null || true)"
      case "$t" in
        *"/dev/binder"*|*"anon_inode:binder"*|*"binder"*)
          hit=1
          ;;
      esac
      [ "$hit" = 1 ] && break
    done

    case "$cmd" in
      *android-usb*|*android-rootfs*|*servicemanager*|*hwservicemanager*|*vndservicemanager*|*mini_servicemgr*|*fd_passing*)
        hit=1
        ;;
    esac

    if [ "$hit" = 1 ]; then
      echo "kill -$sig pid=$pid comm=$comm"
      kill -"$sig" "$pid" 2>/dev/null || true
    fi
  done
}

addr() {
  awk -v s="$1" '$3 == s { print "0x"$1; exit }' /proc/kallsyms
}

need() {
  v="$(addr "$1")"
  if [ -z "$v" ]; then
    echo "ERROR missing symbol $1"
    exit 1
  fi
  echo "$v"
}

echo "== requested fd_debug_stage=$FD_DEBUG_STAGE =="
show_binder_state

# Best case: binder is already the staged module. Do NOT unload/reinsmod.
if [ -e /sys/module/binder/parameters/fd_debug_stage ]; then
  echo "== binder already loaded with fd_debug_stage; reusing it =="
  echo "$FD_DEBUG_STAGE" > /sys/module/binder/parameters/fd_debug_stage 2>/dev/null || true
  now="$(cat /sys/module/binder/parameters/fd_debug_stage 2>/dev/null || true)"
  echo "fd_debug_stage now=$now"
  if [ "$now" = "$FD_DEBUG_STAGE" ]; then
    make_binder_dev
    echo "LOAD_BINDER_FD_STAGE_REUSED_OK stage=$FD_DEBUG_STAGE"
    exit 0
  fi
  echo "WARN: fd_debug_stage param exists but could not be changed; will try reload"
fi

# If old binder is loaded, remove it aggressively.
if grep -q '^binder ' /proc/modules 2>/dev/null; then
  echo "== old/non-staged binder loaded; unloading =="

  list_binder_holders

  i=1
  while [ "$i" -le 6 ]; do
    echo "== unload attempt $i =="
    kill_binder_world TERM
    sleep 1
    kill_binder_world KILL
    sleep 1

    rmmod binder 2>/tmp/rmmod-binder.err && break

    echo "rmmod failed:"
    cat /tmp/rmmod-binder.err 2>/dev/null || true
    show_binder_state
    list_binder_holders

    i=$((i + 1))
  done

  if grep -q '^binder ' /proc/modules 2>/dev/null; then
    if [ "$FORCE_RMMOD" = 1 ]; then
      echo "== FORCE_RMMOD=1: trying rmmod -f binder =="
      rmmod -f binder 2>/tmp/rmmod-binder-force.err || true
      cat /tmp/rmmod-binder-force.err 2>/dev/null || true
    fi
  fi

  if grep -q '^binder ' /proc/modules 2>/dev/null; then
    echo "ERROR: binder still loaded; refusing insmod because it would return File exists"
    show_binder_state
    list_binder_holders
    exit 17
  fi
fi

rm -f /dev/binder 2>/dev/null || true

echo "== insmod staged binder stage=$FD_DEBUG_STAGE =="
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

make_binder_dev
show_binder_state

echo "LOAD_BINDER_FD_STAGE_INSERTED_OK stage=$FD_DEBUG_STAGE"
TVSH
