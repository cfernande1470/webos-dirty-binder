#!/usr/bin/env bash
set -euo pipefail

TV_IP="${TV_IP:-192.168.2.121}"
SIDE_DIR="${SIDE_DIR:-/tmp/android-usb/android-sidecar}"

pick_ko() {
  for c in \
    build/linux-4.4.84/drivers/android/binder.ko \
    build/linux-4.4.84/drivers/staging/android/binder.ko \
    artifacts/binder.ko \
    artifacts/binder-dirty-lgc1-o20-4.4.84-229.1.kavir.2.ko
  do
    [ -f "$c" ] && echo "$c" && return 0
  done
  find build artifacts -name 'binder*.ko' -type f 2>/dev/null | sort | head -n 1
}

ensure_sidecar_binaries() {
  if [ -f build/mini_servicemgr_static ] &&
     [ -f build/android_like_fd_passing_service_static ] &&
     [ -f build/android_like_fd_passing_client_static ]; then
    return 0
  fi

  echo "== sidecar FD binaries missing; building sidecar =="
  chmod +x scripts/build-sidecar.sh

  # En la NanoPi aarch64, gcc/g++ nativos valen para la TV aarch64.
  CC="${CC:-gcc}" CXX="${CXX:-g++}" scripts/build-sidecar.sh 2>&1 | tee /tmp/build-sidecar-fd-stage.log
}

KO="$(pick_ko || true)"
[ -n "$KO" ] && [ -f "$KO" ] || {
  echo "ERROR: binder.ko not found"
  exit 1
}

modinfo -p "$KO" 2>/dev/null | grep -q '^fd_debug_stage:' || {
  echo "ERROR: selected KO has no fd_debug_stage: $KO"
  modinfo -p "$KO" 2>/dev/null || true
  exit 1
}

ensure_sidecar_binaries

MINI="build/mini_servicemgr_static"
SVC="build/android_like_fd_passing_service_static"
CLI="build/android_like_fd_passing_client_static"

missing=0
for f in "$MINI" "$SVC" "$CLI"; do
  if [ ! -f "$f" ]; then
    echo "ERROR: still missing $f"
    missing=1
  fi
done

if [ "$missing" = 1 ]; then
  echo
  echo "== build-sidecar tail =="
  tail -120 /tmp/build-sidecar-fd-stage.log 2>/dev/null || true
  exit 1
fi

echo "== selected files =="
echo "KO=$KO"
echo "MINI=$MINI"
echo "SVC=$SVC"
echo "CLI=$CLI"

TMPD="$(mktemp -d)"
trap 'rm -rf "$TMPD"' EXIT

mkdir -p "$TMPD/bin" "$TMPD/modules"

# Renombramos los *_static a los nombres exactos que espera run-binder-fd-passing-tv.sh.
cp "$MINI" "$TMPD/bin/mini_servicemgr"
cp "$SVC" "$TMPD/bin/android_like_fd_passing_service"
cp "$CLI" "$TMPD/bin/android_like_fd_passing_client"
cp "$KO" "$TMPD/modules/binder.ko"

cat >"$TMPD/load-binder-tv.sh" <<'LOADTV'
#!/bin/sh
set -eu

KO="${1:-modules/binder.ko}"

make_binder_dev() {
  minor="$(awk '$2 == "binder" { print $1; exit }' /proc/misc 2>/dev/null || true)"
  if [ -n "$minor" ] && [ ! -e /dev/binder ]; then
    rm -f /dev/binder 2>/dev/null || true
    mknod /dev/binder c 10 "$minor" 2>/dev/null || true
  fi
  chmod 666 /dev/binder 2>/dev/null || true
}

if grep -q '^binder ' /proc/modules 2>/dev/null; then
  make_binder_dev
  echo "load-binder-tv.sh: binder already loaded"
  exit 0
fi

addr() {
  awk -v s="$1" '$3 == s { print "0x"$1; exit }' /proc/kallsyms
}

need() {
  v="$(addr "$1")"
  [ -n "$v" ] || { echo "ERROR missing symbol $1"; exit 1; }
  echo "$v"
}

insmod "$KO" \
  sym_zap_page_range="$(need zap_page_range)" \
  sym_put_files_struct="$(need put_files_struct)" \
  sym_get_vm_area="$(need get_vm_area)" \
  sym___fd_install="$(need __fd_install)" \
  sym___close_fd="$(need __close_fd)" \
  sym_map_kernel_range_noflush="$(need map_kernel_range_noflush)" \
  sym___lock_task_sighand="$(need __lock_task_sighand)" \
  sym_get_files_struct="$(need get_files_struct)" \
  sym___alloc_fd="$(need __alloc_fd)" \
  fd_debug_stage="${FD_DEBUG_STAGE:-5}"

make_binder_dev
LOADTV

chmod +x "$TMPD/bin/"* "$TMPD/load-binder-tv.sh"

echo "== create remote sidecar: $SIDE_DIR =="
ssh root@"$TV_IP" "rm -rf '$SIDE_DIR'; mkdir -p '$SIDE_DIR/bin' '$SIDE_DIR/modules' '$SIDE_DIR/logs' '$SIDE_DIR/run'"

scp "$TMPD/bin/mini_servicemgr" root@"$TV_IP":"$SIDE_DIR/bin/mini_servicemgr" >/dev/null
scp "$TMPD/bin/android_like_fd_passing_service" root@"$TV_IP":"$SIDE_DIR/bin/android_like_fd_passing_service" >/dev/null
scp "$TMPD/bin/android_like_fd_passing_client" root@"$TV_IP":"$SIDE_DIR/bin/android_like_fd_passing_client" >/dev/null
scp "$TMPD/modules/binder.ko" root@"$TV_IP":"$SIDE_DIR/modules/binder.ko" >/dev/null
scp "$TMPD/load-binder-tv.sh" root@"$TV_IP":"$SIDE_DIR/load-binder-tv.sh" >/dev/null

ssh root@"$TV_IP" "
  chmod +x '$SIDE_DIR'/bin/* '$SIDE_DIR/load-binder-tv.sh'
  echo '== remote sidecar files =='
  find '$SIDE_DIR' -maxdepth 3 \( -type f -o -type d \) | sort
  echo '== remote binary file types =='
  file '$SIDE_DIR'/bin/* 2>/dev/null || true
"

echo "DEPLOY_FD_STAGE_SIDECAR_OK"
