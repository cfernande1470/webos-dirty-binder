#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
[ -f "$ROOT/configs/android-usb.env" ] && . "$ROOT/configs/android-usb.env"

TV_IP="${TV_IP:-192.168.2.121}"
ANDROID_USB_MOUNT="${ANDROID_USB_MOUNT:-/tmp/android-usb}"
ANDROID_SIDE_DIR="${ANDROID_SIDE_DIR:-$ANDROID_USB_MOUNT/android-sidecar}"
ANDROID_ROOTFS_DIR="${ANDROID_ROOTFS_DIR:-$ANDROID_USB_MOUNT/android-rootfs}"
FD_TEST_TIMEOUT="${FD_TEST_TIMEOUT:-30}"

SRC="$ROOT/tools/binder_fd_selftest.c"
OUT="$ROOT/build/binder_fd_selftest_static"

echo "== Build binder_fd_selftest v6 =="
mkdir -p "$ROOT/build"
if command -v aarch64-linux-gnu-gcc >/dev/null 2>&1; then
  aarch64-linux-gnu-gcc -O2 -Wall -Wextra -static -o "$OUT" "$SRC"
elif command -v gcc >/dev/null 2>&1; then
  gcc -O2 -Wall -Wextra -static -o "$OUT" "$SRC" || gcc -O2 -Wall -Wextra -o "$OUT" "$SRC"
else
  echo "ERROR: no gcc found on NanoPi" >&2
  exit 1
fi

file "$OUT" || true
ls -lh "$OUT"

echo
echo "== Hard-clean Binder users on TV =="
ssh root@"$TV_IP" "S='$ANDROID_SIDE_DIR' sh -s" <<'TVCLEAN'
set -u

kill_comm_like() {
  pattern="$1"
  for p in /proc/[0-9]*; do
    [ -r "$p/comm" ] || continue
    comm="$(cat "$p/comm" 2>/dev/null || true)"
    case "$comm" in
      *"$pattern"*)
        pid="${p#/proc/}"
        echo "kill pid=$pid comm=$comm"
        kill "$pid" 2>/dev/null || true
        ;;
    esac
  done
}

kill_binder_holders() {
  for fd in /proc/[0-9]*/fd/*; do
    target="$(readlink "$fd" 2>/dev/null || true)"
    case "$target" in
      /dev/binder|*/binder)
        pid="${fd#/proc/}"
        pid="${pid%%/*}"
        comm="$(cat /proc/"$pid"/comm 2>/dev/null || true)"
        echo "kill binder holder pid=$pid comm=$comm fd=$fd"
        kill "$pid" 2>/dev/null || true
        ;;
    esac
  done
}

kill_comm_like binder_fd
kill_comm_like servicemanager
kill_comm_like hwservicemanager
sleep 1
kill_binder_holders
sleep 1
kill_comm_like binder_fd
kill_comm_like servicemanager
kill_comm_like hwservicemanager
sleep 1
kill_binder_holders
sleep 1

if grep -q '^binder ' /proc/modules 2>/dev/null; then
  echo "trying rmmod binder"
  rmmod binder 2>/tmp/test-fd-rmmod.err || {
    echo "WARN: rmmod binder failed"
    cat /tmp/test-fd-rmmod.err 2>/dev/null || true
  }
fi

mkdir -p "$S/logs" "$S/run" 2>/dev/null || true
rm -f "$S/logs/android-usb-install.log" "$S/run/android-usb-install.pid" 2>/dev/null || true

echo "remaining binder holders:"
for fd in /proc/[0-9]*/fd/*; do
  target="$(readlink "$fd" 2>/dev/null || true)"
  case "$target" in
    /dev/binder|*/binder)
      pid="${fd#/proc/}"
      pid="${pid%%/*}"
      comm="$(cat /proc/"$pid"/comm 2>/dev/null || true)"
      echo "pid=$pid comm=$comm fd=$fd target=$target"
      ;;
  esac
done
TVCLEAN

echo
echo "== Install Android USB without starting servicemanager =="
START_SERVICEMANAGER=0 TV_IP="$TV_IP" "$ROOT/scripts/install-android-usb.sh"

echo
echo "== Wait for installer PID =="
ssh root@"$TV_IP" "SIDE='$ANDROID_SIDE_DIR' sh -s" <<'TVWAIT'
set -u
PIDFILE="$SIDE/run/android-usb-install.pid"
LOG="$SIDE/logs/android-usb-install.log"

i=0
while [ "$i" -lt 30 ]; do
  [ -f "$PIDFILE" ] && break
  i=$((i + 1))
  sleep 1
done

[ -f "$PIDFILE" ] || {
  echo "ERROR: no pid file"
  [ -f "$LOG" ] && tail -n 160 "$LOG"
  exit 1
}

pid="$(cat "$PIDFILE" 2>/dev/null || true)"
echo "REMOTE_INSTALL_PID=$pid"

i=0
while kill -0 "$pid" 2>/dev/null; do
  [ "$i" -lt 900 ] || {
    echo "ERROR: installer timeout"
    [ -f "$LOG" ] && tail -n 160 "$LOG"
    exit 1
  }
  sleep 1
  i=$((i + 1))
done

grep -q 'ANDROID_USB_INSTALL_DONE' "$LOG" || {
  echo "ERROR: install did not finish cleanly"
  tail -n 160 "$LOG"
  exit 1
}

echo "ANDROID_USB_INSTALL_DONE_SEEN=YES"
tail -n 60 "$LOG"
TVWAIT

echo
echo "== Copy FD selftest to TV =="
REMOTE_TEST_BIN="$ANDROID_SIDE_DIR/bin/binder_fd_selftest.v6.$(date +%Y%m%d-%H%M%S).$$"
REMOTE_TMP="/tmp/binder_fd_selftest.$$"
ssh root@"$TV_IP" "mkdir -p '$ANDROID_SIDE_DIR/bin' '$ANDROID_SIDE_DIR/logs' '$ANDROID_SIDE_DIR/run'"
scp "$OUT" root@"$TV_IP":"$REMOTE_TMP"
ssh root@"$TV_IP" "tmp='$REMOTE_TMP'; dest='$REMOTE_TEST_BIN'; cp \"\$tmp\" \"\$dest\" && chmod +x \"\$dest\" && rm -f \"\$tmp\" && ls -lh \"\$dest\""
echo "REMOTE_TEST_BIN=$REMOTE_TEST_BIN"

echo
echo "== Run FD selftest v6 on TV =="
ssh root@"$TV_IP" "R='$ANDROID_ROOTFS_DIR' S='$ANDROID_SIDE_DIR' FD_TEST_TIMEOUT='$FD_TEST_TIMEOUT' FD_TEST_BIN='$REMOTE_TEST_BIN' sh -s" <<'TVRUN'
set -u

run_with_timeout() {
  timeout_s="$1"
  shift
  if command -v setsid >/dev/null 2>&1; then
    setsid "$@" &
  else
    "$@" &
  fi
  pid="$!"

  elapsed=0
  while kill -0 "$pid" 2>/dev/null; do
    if [ "$elapsed" -ge "$timeout_s" ]; then
      echo "TIMEOUT after ${timeout_s}s: $*"
      kill "-$pid" 2>/dev/null || kill "$pid" 2>/dev/null || true
      sleep 1
      kill -9 "-$pid" 2>/dev/null || kill -9 "$pid" 2>/dev/null || true
      wait "$pid" 2>/dev/null || true
      return 124
    fi
    sleep 1
    elapsed=$((elapsed + 1))
  done

  wait "$pid"
  return "$?"
}

restart_sm() {
  echo
  echo "== restart real Android servicemanager =="
  pkill servicemanager 2>/dev/null || true
  if [ -x "$R/system/bin/servicemanager" ]; then
    nohup chroot "$R" /system/bin/servicemanager > "$S/logs/servicemanager.log" 2>&1 &
    echo $! > "$S/run/servicemanager.pid"
    sleep 2
    if kill -0 "$(cat "$S/run/servicemanager.pid")" 2>/dev/null; then
      echo "ANDROID_REAL_SERVICEMANAGER_RUNNING pid=$(cat "$S/run/servicemanager.pid")"
    else
      echo "WARN: servicemanager did not restart"
      cat "$S/logs/servicemanager.log" 2>/dev/null || true
    fi
  fi
}

echo "== preflight =="
pkill binder_fd_selftest 2>/dev/null || true
pkill servicemanager 2>/dev/null || true
pkill hwservicemanager 2>/dev/null || true
sleep 1
chmod 666 /dev/binder 2>/dev/null || true
ls -l /dev/binder /dev/hwbinder /dev/vndbinder 2>/dev/null || true
grep -i binder /proc/misc 2>/dev/null || true
lsmod 2>/dev/null | grep -i binder || true

echo
echo "== FD symbols =="
fd_ok=1
for f in \
  /sys/module/binder/parameters/sym___alloc_fd \
  /sys/module/binder/parameters/sym___fd_install \
  /sys/module/binder/parameters/sym___close_fd \
  /sys/module/binder/parameters/sym_get_files_struct \
  /sys/module/binder/parameters/sym_put_files_struct
do
  name="$(basename "$f")"
  val="$(cat "$f" 2>/dev/null || echo 0)"
  echo "$name=$val"
  [ "$val" != "0" ] || fd_ok=0
done

if [ "$fd_ok" != "1" ]; then
  echo "BINDER_FD_SYMBOLS_READY=NO"
  restart_sm
  exit 2
fi
echo "BINDER_FD_SYMBOLS_READY=YES"

rc=1

echo
echo "== run binder_fd_selftest v6 modern binder_fd_object mode =="
run_with_timeout "$FD_TEST_TIMEOUT" "$FD_TEST_BIN" > "$S/logs/binder-fd-selftest.log" 2>&1
rc=$?
cat "$S/logs/binder-fd-selftest.log"

if [ "$rc" != "0" ]; then
  echo
  echo "== modern mode failed, trying legacy flat FD object mode =="
  pkill binder_fd_selftest 2>/dev/null || true
  sleep 1
  run_with_timeout "$FD_TEST_TIMEOUT" "$FD_TEST_BIN" --legacy-flat-fd > "$S/logs/binder-fd-selftest-legacy.log" 2>&1
  rc=$?
  cat "$S/logs/binder-fd-selftest-legacy.log"
fi

restart_sm

echo
echo "== dmesg binder tail =="
dmesg | grep -i binder | tail -n 240 || true

if [ "$rc" = "0" ]; then
  echo
  echo "BINDER_FD_SELFTEST_OK"
else
  echo
  echo "BINDER_FD_SELFTEST_FAIL rc=$rc"
fi

exit "$rc"
TVRUN
