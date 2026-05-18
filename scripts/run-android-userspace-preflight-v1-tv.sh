#!/usr/bin/env bash
set -euo pipefail

TV_IP="${TV_IP:-192.168.2.121}"
SIDE_DIR="${SIDE_DIR:-/tmp/android-usb/android-sidecar}"
ROOTFS="${ROOTFS:-/tmp/android-usb/android-rootfs}"
SERVICE="${SERVICE:-test.android.parcelfd}"

ssh root@"$TV_IP" "SIDE_DIR='$SIDE_DIR' ROOTFS='$ROOTFS' SERVICE='$SERVICE' sh -s" <<'TVSH'
set -eu

echo "== Android userspace preflight v1 =="
echo "SIDE_DIR=$SIDE_DIR"
echo "ROOTFS=$ROOTFS"
echo "SERVICE=$SERVICE"

cd "$SIDE_DIR"

for f in \
  bin/mini_servicemgr \
  bin/parcel_fd_lite_service \
  bin/android_userspace_preflight_v1 \
  modules/binder.ko \
  load-binder-tv.sh
do
  if [ ! -e "$f" ]; then
    echo "ERROR: missing $SIDE_DIR/$f"
    find . -maxdepth 3 \( -type f -o -type d \) | sort
    exit 1
  fi
done

chmod +x \
  bin/mini_servicemgr \
  bin/parcel_fd_lite_service \
  bin/android_userspace_preflight_v1 \
  load-binder-tv.sh

mkdir -p logs run

echo "== load binder if needed =="
if ! grep -q '^binder ' /proc/modules; then
  ./load-binder-tv.sh "$SIDE_DIR/modules/binder.ko"
else
  echo "binder already loaded"
  grep '^binder ' /proc/modules || true
fi

if [ ! -e /dev/binder ]; then
  echo "ERROR: /dev/binder missing after load"
  exit 1
fi

echo "== clean old processes =="
killall mini_servicemgr 2>/dev/null || true
killall parcel_fd_lite_service 2>/dev/null || true
killall parcel_fd_lite_client 2>/dev/null || true
killall android_userspace_preflight_v1 2>/dev/null || true

echo "== create Android-like rootfs skeleton =="
mkdir -p \
  "$ROOTFS/bin" \
  "$ROOTFS/dev" \
  "$ROOTFS/proc" \
  "$ROOTFS/sys" \
  "$ROOTFS/run" \
  "$ROOTFS/tmp" \
  "$ROOTFS/system/bin" \
  "$ROOTFS/system/lib" \
  "$ROOTFS/system/lib64" \
  "$ROOTFS/vendor" \
  "$ROOTFS/data" \
  "$ROOTFS/etc"

chmod 0755 "$ROOTFS" "$ROOTFS/bin" "$ROOTFS/dev" "$ROOTFS/run" "$ROOTFS/tmp"

cp "$SIDE_DIR/bin/android_userspace_preflight_v1" "$ROOTFS/bin/android_userspace_preflight_v1"
chmod +x "$ROOTFS/bin/android_userspace_preflight_v1"

echo "== expose binder inside rootfs =="
rm -f "$ROOTFS/dev/binder"

if command -v stat >/dev/null 2>&1; then
  echo "host /dev/binder:"
  ls -l /dev/binder || true
fi

# LG webOS binder is misc major 10 minor 53 in our environment.
mknod "$ROOTFS/dev/binder" c 10 53 2>/dev/null || {
  echo "WARN: mknod failed, trying cp -a /dev/binder"
  cp -a /dev/binder "$ROOTFS/dev/binder"
}

chmod 0600 "$ROOTFS/dev/binder" 2>/dev/null || true
ls -l "$ROOTFS/dev/binder" || true

SOCKET_PATH="$ROOTFS/run/parcel_fd_lite.sock"
CHROOT_SOCKET_PATH="/run/parcel_fd_lite.sock"

rm -f "$SOCKET_PATH"
rm -f logs/android_userspace_preflight_v1_*.log run/android_userspace_preflight_v1_*.pid

cleanup() {
  [ -f run/android_userspace_preflight_v1_parcelfd.pid ] && kill "$(cat run/android_userspace_preflight_v1_parcelfd.pid)" 2>/dev/null || true
  [ -f run/android_userspace_preflight_v1_sm.pid ] && kill "$(cat run/android_userspace_preflight_v1_sm.pid)" 2>/dev/null || true
  rm -f "$SOCKET_PATH"
}

trap cleanup EXIT

echo "== start mini_servicemgr on host =="
bin/mini_servicemgr > logs/android_userspace_preflight_v1_sm.log 2>&1 &
echo "$!" > run/android_userspace_preflight_v1_sm.pid

sleep 2

echo "== start ParcelFD-lite service on host, socket inside rootfs/run =="
bin/parcel_fd_lite_service "$SERVICE" "$SOCKET_PATH" > logs/android_userspace_preflight_v1_parcelfd.log 2>&1 &
echo "$!" > run/android_userspace_preflight_v1_parcelfd.pid

sleep 3

echo "== run Android-like process from rootfs =="
set +e

if command -v chroot >/dev/null 2>&1; then
  echo "using chroot mode"
  chroot "$ROOTFS" /bin/android_userspace_preflight_v1 "$SERVICE" "$CHROOT_SOCKET_PATH" \
    > logs/android_userspace_preflight_v1_client.log 2>&1
  client_rc="$?"
  echo "ANDROID_USERSPACE_PREFLIGHT_V1_CHROOT_MODE rc=$client_rc"
else
  echo "chroot not available; using path mode"
  "$ROOTFS/bin/android_userspace_preflight_v1" "$SERVICE" "$SOCKET_PATH" \
    > logs/android_userspace_preflight_v1_client.log 2>&1
  client_rc="$?"
  echo "ANDROID_USERSPACE_PREFLIGHT_V1_PATH_MODE rc=$client_rc"
fi

set -e

echo "== preflight client log =="
cat logs/android_userspace_preflight_v1_client.log || true

echo "== ParcelFD-lite service log =="
cat logs/android_userspace_preflight_v1_parcelfd.log || true

echo "== mini_servicemgr log tail =="
tail -180 logs/android_userspace_preflight_v1_sm.log || true

if [ "$client_rc" -ne 0 ]; then
  echo "FAIL: android_userspace_preflight_v1 rc=$client_rc"
  exit "$client_rc"
fi

grep -q 'ANDROID_USERSPACE_PREFLIGHT_V1_STARTED' logs/android_userspace_preflight_v1_client.log
grep -q 'ANDROID_USERSPACE_PREFLIGHT_V1_BINDER_HANDLE_OK' logs/android_userspace_preflight_v1_client.log
grep -q 'ANDROID_USERSPACE_PREFLIGHT_V1_PARCELFD_WRITE_OK' logs/android_userspace_preflight_v1_client.log
grep -q 'ANDROID_USERSPACE_PREFLIGHT_V1_BINDER_REPLY_OK' logs/android_userspace_preflight_v1_client.log
grep -q 'ANDROID_USERSPACE_PREFLIGHT_V1_SMOKE_OK' logs/android_userspace_preflight_v1_client.log
grep -q 'PARCELFD_LITE_SERVICE_SOCKET_READY' logs/android_userspace_preflight_v1_parcelfd.log
grep -q 'PARCELFD_LITE_BINDER_CONTROL_OK' logs/android_userspace_preflight_v1_parcelfd.log
grep -q 'PARCELFD_LITE_READ_FD_OK' logs/android_userspace_preflight_v1_parcelfd.log
grep -q 'PARCELFD_LITE_PAYLOAD_READ_OK' logs/android_userspace_preflight_v1_parcelfd.log

echo "ANDROID_USERSPACE_PREFLIGHT_V1_TV_OK"
TVSH
