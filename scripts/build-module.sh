#!/bin/sh
set -eu

REPO_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$REPO_DIR/build/linux-4.4.84}"
CONFIG="${CONFIG:-$REPO_DIR/configs/lg-c1-o20-4.4.84-229.1.kavir.2.config}"
KERNEL_URL="${KERNEL_URL:-https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git}"
KERNEL_TAG="${KERNEL_TAG:-v4.4.84}"
KERNEL_RELEASE="${KERNEL_RELEASE:-4.4.84-229.1.kavir.2}"
ARTIFACT="$REPO_DIR/artifacts/binder-dirty-lgc1-o20-${KERNEL_RELEASE}.ko"

if [ ! -f "$CONFIG" ]; then
  echo "ERROR: kernel config not found: $CONFIG"
  echo "Set CONFIG=/path/to/config-lg-c1"
  exit 1
fi

mkdir -p "$(dirname "$BUILD_DIR")"

if [ ! -d "$BUILD_DIR/.git" ]; then
  echo "=== cloning Linux $KERNEL_TAG ==="
  git clone --depth 1 --branch "$KERNEL_TAG" "$KERNEL_URL" "$BUILD_DIR"
fi

cd "$BUILD_DIR"

echo "=== resetting kernel tree ==="
git reset --hard
git clean -fdx

echo "=== applying LG config ==="
cp "$CONFIG" .config

echo "=== applying webos-dirty-binder patch ==="
git apply "$REPO_DIR/patches/0001-lg-webos-dirty-binder-module.patch"

echo "=== copying dirty Binder header ==="
cp "$REPO_DIR/src/binder_dirty_exports.h" drivers/android/binder_dirty_exports.h

echo "=== configuring Binder module ==="
printf '' > .scmversion

./scripts/config --set-str CONFIG_LOCALVERSION "-229.1.kavir.2"
./scripts/config --disable CONFIG_LOCALVERSION_AUTO
./scripts/config --enable CONFIG_ANDROID
./scripts/config --module CONFIG_ANDROID_BINDER_IPC
./scripts/config --set-str CONFIG_ANDROID_BINDER_DEVICES "binder,hwbinder,vndbinder"

make ARCH=arm64 HOSTCFLAGS="-fcommon" olddefconfig

echo "=== preparing kernel build files ==="
make ARCH=arm64 HOSTCFLAGS="-fcommon" modules_prepare

echo "=== forcing exact kernel release ==="
mkdir -p include/generated include/config
echo "#define UTS_RELEASE \"$KERNEL_RELEASE\"" > include/generated/utsrelease.h
echo "$KERNEL_RELEASE" > include/config/kernel.release

echo "=== building Binder module ==="
make ARCH=arm64 HOSTCFLAGS="-fcommon" -j"$(nproc)" M=drivers/android modules

if [ ! -f drivers/android/binder.ko ]; then
  echo "ERROR: drivers/android/binder.ko was not generated"
  exit 1
fi

echo "=== module info ==="
modinfo drivers/android/binder.ko | grep -E 'filename|name|vermagic|depends'
modinfo -p drivers/android/binder.ko || true

echo "=== checking unresolved non-exported Binder symbols ==="
BAD_SYMBOLS='zap_page_range|put_files_struct|get_vm_area|__fd_install|__close_fd|map_kernel_range_noflush|__lock_task_sighand|get_files_struct|__alloc_fd|can_nice|security_binder_'

if nm -u drivers/android/binder.ko | sort | grep -E "$BAD_SYMBOLS"; then
  echo "ERROR: unresolved known non-exported Binder symbols remain"
  exit 1
fi

echo "OK: no known non-exported Binder symbols remain"

echo "=== copying artifact ==="
cp drivers/android/binder.ko "$ARTIFACT"
ls -lh "$ARTIFACT"

echo "Build completed: $ARTIFACT"
