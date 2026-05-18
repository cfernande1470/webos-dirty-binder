#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
[ -f "$ROOT/configs/android-usb.env" ] && . "$ROOT/configs/android-usb.env"

TV_IP="${TV_IP:-192.168.2.121}"
ANDROID_USB_PART="${ANDROID_USB_PART:-/dev/sda1}"
ANDROID_USB_MOUNT="${ANDROID_USB_MOUNT:-/tmp/android-usb}"
ANDROID_SIDE_DIR="${ANDROID_SIDE_DIR:-$ANDROID_USB_MOUNT/android-sidecar}"
ANDROID_IMAGES_DIR="${ANDROID_IMAGES_DIR:-$ANDROID_USB_MOUNT/android-images}"
ANDROID_DOWNLOADS_DIR="${ANDROID_DOWNLOADS_DIR:-$ANDROID_USB_MOUNT/android-downloads}"
ANDROID_MOUNTS_DIR="${ANDROID_MOUNTS_DIR:-$ANDROID_USB_MOUNT/android-mounts}"
ANDROID_ROOTFS_DIR="${ANDROID_ROOTFS_DIR:-$ANDROID_USB_MOUNT/android-rootfs}"
ANDROID_DATA_DIR="${ANDROID_DATA_DIR:-$ANDROID_USB_MOUNT/android-data}"
ANDROID_CACHE_DIR="${ANDROID_CACHE_DIR:-$ANDROID_USB_MOUNT/android-cache}"
ANDROID_BINDER_KO="${ANDROID_BINDER_KO:-$ANDROID_SIDE_DIR/modules/binder.ko}"
REQUIRE_BINDER="${REQUIRE_BINDER:-1}"
START_SERVICEMANAGER="${START_SERVICEMANAGER:-1}"
FORMAT_USB="${FORMAT_USB:-0}"
CONFIRM_FORMAT_ANDROID_USB="${CONFIRM_FORMAT_ANDROID_USB:-NO}"
FORCE_DOWNLOAD="${FORCE_DOWNLOAD:-0}"

SYSTEM_ZIP_URL="${SYSTEM_ZIP_URL:?SYSTEM_ZIP_URL vacío}"
VENDOR_ZIP_URL="${VENDOR_ZIP_URL:?VENDOR_ZIP_URL vacío}"

echo "== Locate/build binder.ko =="
KO_LOCAL="${KO_LOCAL:-}"
if [ -z "$KO_LOCAL" ]; then
  for candidate in \
    "$ROOT/build/linux-4.4.84/drivers/android/binder.ko" \
    "$ROOT/artifacts/binder-dirty-lgc1-o20-4.4.84-229.1.kavir.2.ko"
  do
    if [ -f "$candidate" ]; then
      KO_LOCAL="$candidate"
      break
    fi
  done
fi

if [ -z "$KO_LOCAL" ] && [ -x "$ROOT/scripts/build-module.sh" ]; then
  echo "No binder.ko found; running scripts/build-module.sh"
  "$ROOT/scripts/build-module.sh"
fi

if [ -z "$KO_LOCAL" ]; then
  for candidate in \
    "$ROOT/build/linux-4.4.84/drivers/android/binder.ko" \
    "$ROOT/artifacts/binder-dirty-lgc1-o20-4.4.84-229.1.kavir.2.ko"
  do
    if [ -f "$candidate" ]; then
      KO_LOCAL="$candidate"
      break
    fi
  done
fi

if [ -z "$KO_LOCAL" ] || [ ! -f "$KO_LOCAL" ]; then
  echo "ERROR: binder.ko not found. Run ./scripts/build-module.sh first." >&2
  [ "$REQUIRE_BINDER" = "1" ] && exit 1
fi

echo "Using binder.ko: $KO_LOCAL"

echo
echo "== Remote prepare/install =="
ssh root@"$TV_IP" \
  "ANDROID_USB_PART='$ANDROID_USB_PART' \
   ANDROID_USB_MOUNT='$ANDROID_USB_MOUNT' \
   ANDROID_SIDE_DIR='$ANDROID_SIDE_DIR' \
   ANDROID_IMAGES_DIR='$ANDROID_IMAGES_DIR' \
   ANDROID_DOWNLOADS_DIR='$ANDROID_DOWNLOADS_DIR' \
   ANDROID_MOUNTS_DIR='$ANDROID_MOUNTS_DIR' \
   ANDROID_ROOTFS_DIR='$ANDROID_ROOTFS_DIR' \
   ANDROID_DATA_DIR='$ANDROID_DATA_DIR' \
   ANDROID_CACHE_DIR='$ANDROID_CACHE_DIR' \
   FORMAT_USB='$FORMAT_USB' \
   CONFIRM_FORMAT_ANDROID_USB='$CONFIRM_FORMAT_ANDROID_USB' \
   sh -s" <<'TVPREP'
set -eu

log() { printf '%s %s\n' "$(date '+%F %T' 2>/dev/null || true)" "$*"; }
is_mounted() { awk -v m="$1" '$2 == m { f=1 } END { exit f ? 0 : 1 }' /proc/mounts; }

part="${ANDROID_USB_PART:-/dev/sda1}"
mp="${ANDROID_USB_MOUNT:-/tmp/android-usb}"

format_usb() {
  [ "$CONFIRM_FORMAT_ANDROID_USB" = "YES" ] || {
    log "ERROR: FORMAT_USB=1 requires CONFIRM_FORMAT_ANDROID_USB=YES"
    exit 1
  }
  [ -b "$part" ] || { log "ERROR: $part not found"; cat /proc/partitions 2>/dev/null || true; exit 1; }

  log "ANDROID_USB_FORMAT_BEGIN part=$part"

  pkill servicemanager 2>/dev/null || true
  pkill hwservicemanager 2>/dev/null || true
  rmmod binder 2>/dev/null || true

  for m in \
    "$mp/android-rootfs/dev" "$mp/android-rootfs/sys" "$mp/android-rootfs/proc" \
    "$mp/android-rootfs/cache" "$mp/android-rootfs/data" "$mp/android-rootfs/linkerconfig" \
    "$mp/android-rootfs/apex" "$mp/android-rootfs/vendor" "$mp/android-rootfs/system" \
    "$mp/android-mounts/vendor_raw" "$mp/android-mounts/system_raw" "$mp"
  do
    if is_mounted "$m"; then
      log "umount $m"
      umount "$m" 2>/dev/null || umount -l "$m" 2>/dev/null || true
    fi
  done

  awk -v p="$part" '$1 == p { print $2 }' /proc/mounts | while read -r m; do
    [ -n "$m" ] || continue
    log "umount automount $m"
    umount "$m" 2>/dev/null || umount -l "$m" 2>/dev/null || true
  done

  # Extra safety: after all lazy/unmount attempts, refuse to format if the
  # partition is still visible in /proc/mounts. This avoids mke2fs half-failures.
  sync

  log "remaining mounts before mkfs:"
  grep -E "$part|android-usb|/tmp/usb/sda/sda1" /proc/mounts || true

  if grep -q "$part" /proc/mounts; then
    log "ERROR: $part is still mounted/in use; refusing to format"
    exit 1
  fi

  if command -v losetup >/dev/null 2>&1; then
    losetup -a 2>/dev/null | grep 'android-usb' | cut -d: -f1 | while read -r loop; do
      [ -n "$loop" ] || continue
      log "losetup -d $loop"
      losetup -d "$loop" 2>/dev/null || true
    done
  fi

  if command -v mkfs.ext4 >/dev/null 2>&1; then
    mkfs.ext4 -F -L androidusb "$part"
  elif command -v mke2fs >/dev/null 2>&1; then
    mke2fs -t ext4 -F -L androidusb "$part"
  else
    log "ERROR: no mkfs.ext4/mke2fs on TV"
    exit 1
  fi
  log "ANDROID_USB_FORMAT_DONE"
}

ensure_usb() {
  mkdir -p "$mp"
  if is_mounted "$mp"; then
    log "ANDROID_USB_ALREADY_MOUNTED $mp"
  else
    src="$(awk -v p="$part" '$1 == p { print $2; exit }' /proc/mounts 2>/dev/null || true)"
    if [ -n "$src" ]; then
      log "USB already mounted at $src; bind mount to $mp"
      mount -o bind "$src" "$mp"
    else
      [ -b "$part" ] || {
        log "Configured $part not found; autodetecting"
        part="$(for p in /dev/sd[a-z][0-9] /dev/sd[a-z]; do [ -b "$p" ] && echo "$p" && break; done)"
        [ -n "$part" ] || { log "ERROR: no USB block device found"; cat /proc/partitions 2>/dev/null || true; exit 1; }
      }
      mount "$part" "$mp"
    fi
  fi

  fs="$(awk -v m="$mp" '$2 == m { print $3; exit }' /proc/mounts)"
  log "ANDROID_USB_READY fs=$fs mp=$mp"
  if [ "$fs" != "ext4" ] && [ "$fs" != "ext3" ] && [ "$fs" != "ext2" ]; then
    log "WARN: filesystem is $fs; ext4 recommended"
  fi

  mkdir -p \
    "$mp/android-sidecar/bin" "$mp/android-sidecar/modules" "$mp/android-sidecar/logs" "$mp/android-sidecar/run" \
    "$mp/android-images" "$mp/android-downloads" "$mp/android-mounts" "$mp/android-rootfs" \
    "$mp/android-data" "$mp/android-cache"

  echo ok > "$mp/.android-usb-write-test"
  rm -f "$mp/.android-usb-write-test"
  df -h "$mp" 2>/dev/null || true
}

if [ "${FORMAT_USB:-0}" = "1" ]; then
  format_usb
fi
ensure_usb
TVPREP

echo
echo "== Copy binder.ko =="
ssh root@"$TV_IP" "mkdir -p '$ANDROID_SIDE_DIR/modules' '$ANDROID_SIDE_DIR/bin' '$ANDROID_SIDE_DIR/logs' '$ANDROID_SIDE_DIR/run'"
scp "$KO_LOCAL" root@"$TV_IP":"$ANDROID_SIDE_DIR/modules/binder.ko"

echo
echo "== Start remote Android USB installer =="
ssh root@"$TV_IP" \
  "ANDROID_USB_MOUNT='$ANDROID_USB_MOUNT' \
   ANDROID_SIDE_DIR='$ANDROID_SIDE_DIR' \
   ANDROID_IMAGES_DIR='$ANDROID_IMAGES_DIR' \
   ANDROID_DOWNLOADS_DIR='$ANDROID_DOWNLOADS_DIR' \
   ANDROID_MOUNTS_DIR='$ANDROID_MOUNTS_DIR' \
   ANDROID_ROOTFS_DIR='$ANDROID_ROOTFS_DIR' \
   ANDROID_DATA_DIR='$ANDROID_DATA_DIR' \
   ANDROID_CACHE_DIR='$ANDROID_CACHE_DIR' \
   ANDROID_BINDER_KO='$ANDROID_BINDER_KO' \
   REQUIRE_BINDER='$REQUIRE_BINDER' \
   START_SERVICEMANAGER='$START_SERVICEMANAGER' \
   SYSTEM_ZIP_URL='$SYSTEM_ZIP_URL' \
   VENDOR_ZIP_URL='$VENDOR_ZIP_URL' \
   FORCE_DOWNLOAD='$FORCE_DOWNLOAD' \
   sh -s" <<'TVRUN'
set -eu

side="${ANDROID_SIDE_DIR:-/tmp/android-usb/android-sidecar}"
mkdir -p "$side/logs" "$side/run"

cat > "$side/run/tv-install-android-usb.sh" <<'REMOTE_INSTALL'
#!/bin/sh
set -eu

ANDROID_USB_MOUNT="${ANDROID_USB_MOUNT:-/tmp/android-usb}"
ANDROID_SIDE_DIR="${ANDROID_SIDE_DIR:-$ANDROID_USB_MOUNT/android-sidecar}"
ANDROID_IMAGES_DIR="${ANDROID_IMAGES_DIR:-$ANDROID_USB_MOUNT/android-images}"
ANDROID_DOWNLOADS_DIR="${ANDROID_DOWNLOADS_DIR:-$ANDROID_USB_MOUNT/android-downloads}"
ANDROID_MOUNTS_DIR="${ANDROID_MOUNTS_DIR:-$ANDROID_USB_MOUNT/android-mounts}"
ANDROID_ROOTFS_DIR="${ANDROID_ROOTFS_DIR:-$ANDROID_USB_MOUNT/android-rootfs}"
ANDROID_DATA_DIR="${ANDROID_DATA_DIR:-$ANDROID_USB_MOUNT/android-data}"
ANDROID_CACHE_DIR="${ANDROID_CACHE_DIR:-$ANDROID_USB_MOUNT/android-cache}"
ANDROID_BINDER_KO="${ANDROID_BINDER_KO:-$ANDROID_SIDE_DIR/modules/binder.ko}"
REQUIRE_BINDER="${REQUIRE_BINDER:-1}"
START_SERVICEMANAGER="${START_SERVICEMANAGER:-1}"
FORCE_DOWNLOAD="${FORCE_DOWNLOAD:-0}"

SYSTEM_ZIP_URL="${SYSTEM_ZIP_URL:-}"
VENDOR_ZIP_URL="${VENDOR_ZIP_URL:-}"

log() { printf '%s %s\n' "$(date '+%F %T' 2>/dev/null || true)" "$*"; }
fail() { log "ERROR: $*"; exit 1; }
is_mounted() { awk -v m="$1" '$2 == m { f=1 } END { exit f ? 0 : 1 }' /proc/mounts; }

download_file() {
  url="$1"; out="$2"; tmp="$out.tmp"
  rm -f "$tmp"
  log "DOWNLOADING $url"
  if command -v curl >/dev/null 2>&1; then
    curl -L --fail --retry 5 --retry-delay 5 -o "$tmp" "$url"
  elif command -v wget >/dev/null 2>&1; then
    wget -O "$tmp" "$url"
  elif command -v busybox >/dev/null 2>&1 && busybox wget --help >/dev/null 2>&1; then
    busybox wget -O "$tmp" "$url"
  else
    fail "no curl/wget available"
  fi
  [ -s "$tmp" ] || fail "empty download: $url"
  mv -f "$tmp" "$out"
  log "DOWNLOAD_OK $out"
}

unzip_dir() {
  zip="$1"; outdir="$2"
  rm -rf "$outdir"; mkdir -p "$outdir"
  if command -v unzip >/dev/null 2>&1; then
    unzip -o "$zip" -d "$outdir"
  elif command -v busybox >/dev/null 2>&1 && busybox unzip -h >/dev/null 2>&1; then
    busybox unzip -o "$zip" -d "$outdir"
  else
    fail "no unzip available"
  fi
}

extract_img_from_zip() {
  zip="$1"; name="$2"; final="$3"; tmpdir="$ANDROID_DOWNLOADS_DIR/unpack-$name.$$"
  log "EXTRACTING $name from $zip"
  unzip_dir "$zip" "$tmpdir"
  img="$(find "$tmpdir" -type f -name "$name.img" | head -n 1 || true)"
  [ -n "$img" ] || fail "missing $name.img inside $zip"
  rm -f "$final.tmp"
  mv "$img" "$final.tmp" 2>/dev/null || cp "$img" "$final.tmp"
  sync
  mv -f "$final.tmp" "$final"
  rm -rf "$tmpdir"
  log "IMAGE_OK $final"
}

ksym() {
  awk -v n="$1" '$3 == n && $1 != "0000000000000000" { print "0x"$1; exit }' /proc/kallsyms
}

load_binder_driver() {
  log "ANDROID_BINDER_BEGIN"
  echo 0 > /proc/sys/kernel/kptr_restrict 2>/dev/null || true

  BINDER_ARGS=""
  add_ksym() {
    param="$1"; name="$2"; val="$(ksym "$name" 2>/dev/null || true)"
    if [ -n "$val" ]; then
      BINDER_ARGS="$BINDER_ARGS $param=$val"
      log "ANDROID_BINDER_SYM $param=$val"
    else
      log "WARN: cannot resolve kernel symbol $name"
    fi
  }

  add_ksym sym_get_vm_area get_vm_area
  add_ksym sym_map_kernel_range_noflush map_kernel_range_noflush
  add_ksym sym_zap_page_range zap_page_range
  add_ksym sym___alloc_fd __alloc_fd
  add_ksym sym___fd_install __fd_install
  add_ksym sym___close_fd __close_fd
  add_ksym sym_get_files_struct get_files_struct
  add_ksym sym_put_files_struct put_files_struct
  add_ksym sym___lock_task_sighand __lock_task_sighand

  if grep -q '^binder ' /proc/modules 2>/dev/null; then
    if [ -f /sys/module/binder/parameters/sym_get_vm_area ]; then
      gv="$(cat /sys/module/binder/parameters/sym_get_vm_area 2>/dev/null || echo 0)"
      if [ "$gv" != "0" ]; then
        log "ANDROID_BINDER_ALREADY_LOADED"
      else
        log "WARN: binder loaded without symbols; reloading"
        pkill servicemanager 2>/dev/null || true
        pkill hwservicemanager 2>/dev/null || true
        rmmod binder 2>/tmp/android-binder-rmmod.err || {
          log "ERROR: cannot unload old binder"
          cat /tmp/android-binder-rmmod.err 2>/dev/null || true
          [ "$REQUIRE_BINDER" = "1" ] && exit 1
        }
      fi
    fi
  fi

  if ! grep -q '^binder ' /proc/modules 2>/dev/null; then
    [ -f "$ANDROID_BINDER_KO" ] || {
      log "ERROR: missing binder.ko: $ANDROID_BINDER_KO"
      [ "$REQUIRE_BINDER" = "1" ] && exit 1
      return 0
    }
    log "Loading binder module with symbols: $ANDROID_BINDER_KO"
    insmod "$ANDROID_BINDER_KO" devices=binder,hwbinder,vndbinder $BINDER_ARGS 2>/tmp/android-binder-insmod.err || {
      log "ERROR: insmod binder.ko failed"
      cat /tmp/android-binder-insmod.err 2>/dev/null || true
      dmesg | grep -i binder | tail -n 80 || true
      [ "$REQUIRE_BINDER" = "1" ] && exit 1
    }
    sleep 1
  fi

  missing=""
  for dev in binder hwbinder vndbinder; do
    if [ ! -e "/dev/$dev" ]; then
      minor="$(awk -v d="$dev" '$2 == d { print $1; exit }' /proc/misc 2>/dev/null || true)"
      if [ -n "$minor" ]; then
        rm -f "/dev/$dev"; mknod "/dev/$dev" c 10 "$minor"
        log "ANDROID_BINDER_NODE /dev/$dev c 10 $minor"
      else
        missing="$missing $dev"
        continue
      fi
    fi
    chmod 666 "/dev/$dev" 2>/dev/null || true
  done

  [ -e /dev/binder ] || fail "/dev/binder missing after binder load"

  if [ -f /sys/module/binder/parameters/sym_get_vm_area ]; then
    gv="$(cat /sys/module/binder/parameters/sym_get_vm_area 2>/dev/null || echo 0)"
    [ "$gv" != "0" ] || fail "sym_get_vm_area=0; Binder mmap will fail"
  fi

  if [ -n "$missing" ]; then
    log "WARN: missing Binder devices:$missing"
    log "WARN: current binder.ko does not support binder,hwbinder,vndbinder"
  fi

  grep -i binder /proc/misc 2>/dev/null || true
  ls -l /dev/binder /dev/hwbinder /dev/vndbinder 2>/dev/null || true
  log "ANDROID_BINDER_READY"
}

umount_if_mounted() {
  m="$1"
  if is_mounted "$m"; then
    log "Unmounting stale mount $m"
    umount "$m" 2>/dev/null || umount -l "$m" 2>/dev/null || true
  fi
}

bind_mount() {
  src="$1"; dst="$2"
  mkdir -p "$dst"
  if is_mounted "$dst"; then
    log "BIND_ALREADY_MOUNTED $dst"
  else
    mount -o bind "$src" "$dst"
    log "BIND_OK $dst -> $src"
  fi
}

mount_android_rootfs() {
  mkdir -p \
    "$ANDROID_MOUNTS_DIR/system_raw" "$ANDROID_MOUNTS_DIR/vendor_raw" \
    "$ANDROID_ROOTFS_DIR/system" "$ANDROID_ROOTFS_DIR/vendor" "$ANDROID_ROOTFS_DIR/apex" \
    "$ANDROID_ROOTFS_DIR/data" "$ANDROID_ROOTFS_DIR/cache" "$ANDROID_ROOTFS_DIR/proc" \
    "$ANDROID_ROOTFS_DIR/sys" "$ANDROID_ROOTFS_DIR/dev" "$ANDROID_ROOTFS_DIR/linkerconfig" \
    "$ANDROID_DATA_DIR" "$ANDROID_CACHE_DIR"

  for m in \
    "$ANDROID_ROOTFS_DIR/dev" "$ANDROID_ROOTFS_DIR/sys" "$ANDROID_ROOTFS_DIR/proc" \
    "$ANDROID_ROOTFS_DIR/cache" "$ANDROID_ROOTFS_DIR/data" "$ANDROID_ROOTFS_DIR/linkerconfig" \
    "$ANDROID_ROOTFS_DIR/apex" "$ANDROID_ROOTFS_DIR/vendor" "$ANDROID_ROOTFS_DIR/system" \
    "$ANDROID_MOUNTS_DIR/vendor_raw" "$ANDROID_MOUNTS_DIR/system_raw"
  do
    umount_if_mounted "$m"
  done

  [ -f "$ANDROID_IMAGES_DIR/system.img" ] || fail "missing $ANDROID_IMAGES_DIR/system.img"
  [ -f "$ANDROID_IMAGES_DIR/vendor.img" ] || fail "missing $ANDROID_IMAGES_DIR/vendor.img"

  mount -o loop,ro "$ANDROID_IMAGES_DIR/system.img" "$ANDROID_MOUNTS_DIR/system_raw"
  log "ANDROID_USB_SYSTEM_RAW_MOUNT_OK"
  mount -o loop,ro "$ANDROID_IMAGES_DIR/vendor.img" "$ANDROID_MOUNTS_DIR/vendor_raw"
  log "ANDROID_USB_VENDOR_RAW_MOUNT_OK"

  [ -d "$ANDROID_MOUNTS_DIR/system_raw/system" ] || fail "system.img does not contain /system"

  bind_mount "$ANDROID_MOUNTS_DIR/system_raw/system" "$ANDROID_ROOTFS_DIR/system"
  bind_mount "$ANDROID_MOUNTS_DIR/vendor_raw" "$ANDROID_ROOTFS_DIR/vendor"

  if [ -d "$ANDROID_MOUNTS_DIR/system_raw/system/apex" ]; then
    bind_mount "$ANDROID_MOUNTS_DIR/system_raw/system/apex" "$ANDROID_ROOTFS_DIR/apex"
  fi

  bind_mount "$ANDROID_DATA_DIR" "$ANDROID_ROOTFS_DIR/data"
  bind_mount "$ANDROID_CACHE_DIR" "$ANDROID_ROOTFS_DIR/cache"

  mount -t proc proc "$ANDROID_ROOTFS_DIR/proc"
  log "ANDROID_USB_PROC_MOUNT_OK"
  mount -t sysfs sysfs "$ANDROID_ROOTFS_DIR/sys"
  log "ANDROID_USB_SYS_MOUNT_OK"
  mount -o bind /dev "$ANDROID_ROOTFS_DIR/dev"
  log "ANDROID_USB_DEV_BIND_OK"

  mkdir -p "$ANDROID_ROOTFS_DIR/linkerconfig"
  log "ANDROID_USB_LINKERCONFIG_DIR_READY"
  log "ANDROID_USB_ROOTFS_READY $ANDROID_ROOTFS_DIR"
}

start_servicemanager() {
  [ "$START_SERVICEMANAGER" = "1" ] || return 0
  [ -x "$ANDROID_ROOTFS_DIR/system/bin/servicemanager" ] || return 0

  pkill servicemanager 2>/dev/null || true
  sleep 1
  nohup chroot "$ANDROID_ROOTFS_DIR" /system/bin/servicemanager \
    > "$ANDROID_SIDE_DIR/logs/servicemanager.log" 2>&1 &
  echo $! > "$ANDROID_SIDE_DIR/run/servicemanager.pid"
  sleep 2
  if kill -0 "$(cat "$ANDROID_SIDE_DIR/run/servicemanager.pid")" 2>/dev/null; then
    log "ANDROID_REAL_SERVICEMANAGER_RUNNING pid=$(cat "$ANDROID_SIDE_DIR/run/servicemanager.pid")"
  else
    log "WARN: Android servicemanager died"
    cat "$ANDROID_SIDE_DIR/logs/servicemanager.log" 2>/dev/null || true
  fi
}

log "ANDROID_USB_INSTALL_BEGIN"
log "USB=$ANDROID_USB_MOUNT"
log "ROOTFS=$ANDROID_ROOTFS_DIR"

is_mounted "$ANDROID_USB_MOUNT" || fail "$ANDROID_USB_MOUNT is not mounted"

mkdir -p "$ANDROID_SIDE_DIR/logs" "$ANDROID_SIDE_DIR/run" "$ANDROID_IMAGES_DIR" "$ANDROID_DOWNLOADS_DIR" "$ANDROID_MOUNTS_DIR" "$ANDROID_ROOTFS_DIR" "$ANDROID_DATA_DIR" "$ANDROID_CACHE_DIR"
df -h "$ANDROID_USB_MOUNT" 2>/dev/null || true

if [ "$FORCE_DOWNLOAD" = "1" ] || [ ! -f "$ANDROID_IMAGES_DIR/system.img" ]; then
  download_file "$SYSTEM_ZIP_URL" "$ANDROID_DOWNLOADS_DIR/system.zip"
  extract_img_from_zip "$ANDROID_DOWNLOADS_DIR/system.zip" system "$ANDROID_IMAGES_DIR/system.img"
else
  log "SKIP system.img already exists"
fi

if [ "$FORCE_DOWNLOAD" = "1" ] || [ ! -f "$ANDROID_IMAGES_DIR/vendor.img" ]; then
  download_file "$VENDOR_ZIP_URL" "$ANDROID_DOWNLOADS_DIR/vendor.zip"
  extract_img_from_zip "$ANDROID_DOWNLOADS_DIR/vendor.zip" vendor "$ANDROID_IMAGES_DIR/vendor.img"
else
  log "SKIP vendor.img already exists"
fi

ls -lh "$ANDROID_IMAGES_DIR/system.img" "$ANDROID_IMAGES_DIR/vendor.img" 2>/dev/null || true

load_binder_driver
mount_android_rootfs

if [ -x "$ANDROID_ROOTFS_DIR/system/bin/toybox" ]; then
  chroot "$ANDROID_ROOTFS_DIR" /system/bin/toybox true && log "ANDROID_USB_TOYBOX_OK" || log "WARN: toybox test failed"
fi

start_servicemanager
log "ANDROID_USB_INSTALL_DONE"
REMOTE_INSTALL

chmod +x "$side/run/tv-install-android-usb.sh"

ANDROID_USB_MOUNT="$ANDROID_USB_MOUNT" \
ANDROID_SIDE_DIR="$ANDROID_SIDE_DIR" \
ANDROID_IMAGES_DIR="$ANDROID_IMAGES_DIR" \
ANDROID_DOWNLOADS_DIR="$ANDROID_DOWNLOADS_DIR" \
ANDROID_MOUNTS_DIR="$ANDROID_MOUNTS_DIR" \
ANDROID_ROOTFS_DIR="$ANDROID_ROOTFS_DIR" \
ANDROID_DATA_DIR="$ANDROID_DATA_DIR" \
ANDROID_CACHE_DIR="$ANDROID_CACHE_DIR" \
ANDROID_BINDER_KO="$ANDROID_BINDER_KO" \
REQUIRE_BINDER="$REQUIRE_BINDER" \
START_SERVICEMANAGER="$START_SERVICEMANAGER" \
SYSTEM_ZIP_URL="$SYSTEM_ZIP_URL" \
VENDOR_ZIP_URL="$VENDOR_ZIP_URL" \
FORCE_DOWNLOAD="$FORCE_DOWNLOAD" \
nohup sh "$side/run/tv-install-android-usb.sh" \
  > "$side/logs/android-usb-install.log" 2>&1 < /dev/null &

echo $! > "$side/run/android-usb-install.pid"
echo "ANDROID_USB_INSTALL_STARTED pid=$(cat "$side/run/android-usb-install.pid") log=$side/logs/android-usb-install.log"
TVRUN

echo
echo "Tail progress:"
echo "  TV_IP=$TV_IP ./scripts/tail-android-usb.sh"
