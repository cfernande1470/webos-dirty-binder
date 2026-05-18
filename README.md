# webos-dirty-binder

> Milestone: **USB-only Android rootfs + Binder symbol loader + real Android 13 `servicemanager` running on webOS**  
> Milestone date: 2026-05-18  
> Tested target: LG webOS TV, `Linux 4.4.84-229.1.kavir.2`, `aarch64`  
> Control host: NanoPi R3S  
> Long-term goal: run Android inside webOS as an app-like sidecar, without relying on `/media/internal`.

---

## 1. Executive summary

`webos-dirty-binder` is an experimental project for bringing up enough Android userspace on a rooted LG webOS TV to run Android framework components on top of the TV's existing Linux/webOS kernel.

The current milestone is important because the real Android 13 `servicemanager` now runs inside the Android rootfs mounted from USB.

This is **not** the old mini service manager shim. The running binary is the real Android binary from:

```text
/tmp/android-usb/android-rootfs/system/bin/servicemanager
```

The latest validated diagnostic shows:

```text
== running service managers ==
 5945 ?        00:00:00 servicemanager

== servicemanager test ==
SERVICEMANAGER_ALREADY_RUNNING=YES
```

The installer log confirms:

```text
ANDROID_REAL_SERVICEMANAGER_RUNNING pid=5945
ANDROID_USB_INSTALL_DONE
```

This milestone took a while because the failure looked like a `servicemanager`, FD-passing, Android image, linker, or chroot problem. The actual blocker was lower-level: the dirty Binder module was loading without the hidden kernel symbol addresses it needs for Binder transaction memory mapping and FD handling.

Once those symbols were resolved dynamically from `/proc/kallsyms` and passed into `binder.ko`, Android's real `servicemanager` stopped dying and stayed alive.

---

## 2. Current project status

### 2.1 Working

The following pieces are currently working:

```text
USB ext4 storage
Android system.img downloaded directly to USB
Android vendor.img downloaded directly to USB
Android rootfs assembled under /tmp/android-usb/android-rootfs
/system mounted from system.img
/vendor mounted from vendor.img
/apex mounted from system.img
/data on USB
/cache on USB
/proc mounted
/sys mounted
/dev bind-mounted from webOS
binder.ko loaded on the webOS kernel
/dev/binder created
Binder mmap works
BINDER_SET_CONTEXT_MGR_EXT is reached
/system/bin/toybox runs inside the Android chroot
real Android 13 /system/bin/servicemanager starts and remains alive
```

Main rootfs path:

```text
/tmp/android-usb/android-rootfs
```

Main image paths:

```text
/tmp/android-usb/android-images/system.img
/tmp/android-usb/android-images/vendor.img
```

Main logs:

```text
/tmp/android-usb/android-sidecar/logs/android-usb-install.log
/tmp/android-usb/android-sidecar/logs/servicemanager.log
```

### 2.2 Not working yet

The following pieces are still pending:

```text
/dev/hwbinder
/dev/vndbinder
hwservicemanager
vndservicemanager
generated /linkerconfig/ld.config.txt
Android property service
real Android init or a controlled mini-init
zygote
system_server
SurfaceFlinger
running Android UI inside a webOS app
input/audio/network integration
```

The currently expected failure is:

```text
HWSERVICEMANAGER_EXITED_QUICKLY=YES
Binder driver could not be opened. Terminating.
```

This is expected because the current Binder module only exposes:

```text
/dev/binder
```

It does not expose:

```text
/dev/hwbinder
/dev/vndbinder
```

---

## 3. Why everything moved away from `/media/internal`

Earlier iterations used `/media/internal` for some sidecar and Android files. That is not viable on LG webOS TVs because `/media/internal` is small and can fill up quickly.

The project now treats USB storage as the only realistic place for Android state:

```text
sidecar files
logs
binder.ko copied to the TV
system.img
vendor.img
downloads
mount points
Android rootfs
/data
/cache
```

The stable logical mount point is:

```text
/tmp/android-usb
```

The actual USB partition may be mounted by webOS somewhere like:

```text
/tmp/usb/sda/sda1
```

but the project uses `/tmp/android-usb` as the stable path. This keeps scripts reproducible and avoids chasing webOS automount paths.

### 3.1 Why ext4

The USB should be ext4 because Android expects real UNIX filesystem behavior:

```text
permissions
ownership
symlinks
large files
bind mounts
loop-mounted images
/data and /cache semantics
```

FAT32, exFAT, or NTFS may work for carrying raw images, but they are not a good base for persistent Android state.

---

## 4. Clean script layout

The project has been simplified to three public Android USB scripts:

```text
scripts/install-android-usb.sh
scripts/tail-android-usb.sh
scripts/diagnose-android-usb.sh
```

This is intentional. The previous script set grew organically while debugging. For this milestone, the workflow is clean enough to collapse into three entry points.

### 4.1 `install-android-usb.sh`

Main installer.

Responsibilities:

```text
read configs/android-usb.env
optionally format the USB partition
mount the USB at /tmp/android-usb
locate or build binder.ko
copy binder.ko to the TV
download Android system/vendor images if missing
extract system.img and vendor.img
resolve required kernel symbols from /proc/kallsyms
load binder.ko with those symbols
create/fix /dev/binder
mount the Android rootfs
run a toybox smoke test
start the real Android /system/bin/servicemanager
```

### 4.2 `tail-android-usb.sh`

Remote log tail helper.

It tails:

```text
/tmp/android-usb/android-sidecar/logs/android-usb-install.log
```

Usage:

```sh
TV_IP=192.168.2.121 ./scripts/tail-android-usb.sh
```

### 4.3 `diagnose-android-usb.sh`

Diagnostic script.

It reports:

```text
kernel version
USB and Android mounts
Binder devices
Binder module parameters
Android build props
key Android binaries
getprop behavior
running service managers
servicemanager status
hwservicemanager status
install log tail
Binder-related dmesg tail
```

Usage:

```sh
TV_IP=192.168.2.121 ./scripts/diagnose-android-usb.sh
```

### 4.4 Old scripts replaced by the clean flow

The following older helper scripts are considered obsolete or replaced:

```text
ensure-android-usb-mounted-tv.sh
format-android-usb-tv.sh
tv-install-android-usb.sh
install-android-to-usb-tv.sh
tail-android-usb-install-tv.sh
diagnose-android-usb-tv.sh
start-real-android-sm-tv.sh
usb-android-status-tv.sh
load-binder-usb-tv.sh
reload-binder-with-symbols-tv.sh
apply-usb-only-migration.sh
apply-usb-safety-guards.sh
```

Build scripts should be kept if present:

```text
scripts/build-module.sh
scripts/build-sidecar.sh
```

`build-module.sh` is still required for rebuilding `binder.ko`.

`build-sidecar.sh` is no longer required for the clean Android USB installation path. It can remain as a debugging or test utility, especially for future Binder/FD tests, but the main installer should not depend on copying `android_like*`, `mini_servicemgr`, or old static helper binaries.

---

## 5. Configuration

Main config file:

```text
configs/android-usb.env
```

Typical configuration:

```sh
TV_IP="192.168.2.121"
ANDROID_USB_PART="/dev/sda1"
ANDROID_USB_MOUNT="/tmp/android-usb"

ANDROID_SIDE_DIR="/tmp/android-usb/android-sidecar"
ANDROID_IMAGES_DIR="/tmp/android-usb/android-images"
ANDROID_DOWNLOADS_DIR="/tmp/android-usb/android-downloads"
ANDROID_MOUNTS_DIR="/tmp/android-usb/android-mounts"
ANDROID_ROOTFS_DIR="/tmp/android-usb/android-rootfs"
ANDROID_DATA_DIR="/tmp/android-usb/android-data"
ANDROID_CACHE_DIR="/tmp/android-usb/android-cache"

ANDROID_BINDER_KO="/tmp/android-usb/android-sidecar/modules/binder.ko"
REQUIRE_BINDER="1"
START_SERVICEMANAGER="1"

SYSTEM_ZIP_URL="https://sourceforge.net/projects/waydroid/files/images/system/lineage/waydroid_arm64_only/lineage-20.0-20260403-VANILLA-waydroid_arm64_only-system.zip/download"
VENDOR_ZIP_URL="https://sourceforge.net/projects/waydroid/files/images/vendor/waydroid_arm64_only/lineage-20.0-20260403-MAINLINE-waydroid_arm64_only-vendor.zip/download"
```

---

## 6. Building from scratch

For the current milestone, the only required build artifact is:

```text
binder.ko
```

The sidecar static binaries are optional.

### 6.1 Safe cleanup

Do not delete the prepared kernel tree unless you really want to rebuild everything from scratch. A safe cleanup is:

```sh
cd /home/pi/disk/webos-dirty-binder

find build -type f \( \
  -name '*.o' -o \
  -name '*.ko' -o \
  -name '*.mod' -o \
  -name '*.mod.c' -o \
  -name '*.cmd' -o \
  -name '*.symvers' -o \
  -name 'modules.order' \
\) -delete 2>/dev/null || true
```

### 6.2 Build Binder

```sh
chmod +x scripts/*.sh
./scripts/build-module.sh
```

Verify:

```sh
ls -lh build/linux-4.4.84/drivers/android/binder.ko
modinfo build/linux-4.4.84/drivers/android/binder.ko 2>/dev/null || true
```

### 6.3 `build-sidecar.sh` is optional

The clean USB Android path does not require:

```sh
./scripts/build-sidecar.sh
```

Use it only for debugging tools, experimental Binder clients, FD-passing tests, or old sidecar helpers.

---

## 7. Installation

### 7.1 Normal installation

From the NanoPi:

```sh
cd /home/pi/disk/webos-dirty-binder
TV_IP=192.168.2.121 ./scripts/install-android-usb.sh
```

Tail progress:

```sh
TV_IP=192.168.2.121 ./scripts/tail-android-usb.sh
```

Run diagnostics:

```sh
TV_IP=192.168.2.121 ./scripts/diagnose-android-usb.sh
```

### 7.2 Format USB and install

Formatting is destructive and requires explicit confirmation:

```sh
TV_IP=192.168.2.121 \
ANDROID_USB_PART=/dev/sda1 \
FORMAT_USB=1 \
CONFIRM_FORMAT_ANDROID_USB=YES \
./scripts/install-android-usb.sh
```

If formatting fails with:

```text
/dev/sda1 is apparently in use by the system; will not make a filesystem here!
```

then webOS, a loop mount, or a previous Android mount is still using the USB partition.

The installer must unmount, deepest first:

```text
android-rootfs/dev
android-rootfs/sys
android-rootfs/proc
android-rootfs/cache
android-rootfs/data
android-rootfs/linkerconfig
android-rootfs/apex
android-rootfs/vendor
android-rootfs/system
android-mounts/vendor_raw
android-mounts/system_raw
/tmp/android-usb
/tmp/usb/sda/sda1
```

If anything remains mounted, the installer should refuse to run `mkfs`.

---

## 8. USB directory layout

After installation:

```text
/tmp/android-usb/
  android-sidecar/
    bin/
    modules/
      binder.ko
    logs/
      android-usb-install.log
      servicemanager.log
      hwservicemanager.log
    run/
      android-usb-install.pid
      servicemanager.pid

  android-images/
    system.img
    vendor.img

  android-downloads/
    system.zip
    vendor.zip

  android-mounts/
    system_raw/
    vendor_raw/

  android-rootfs/
    system/
    vendor/
    apex/
    data/
    cache/
    proc/
    sys/
    dev/
    linkerconfig/

  android-data/
  android-cache/
```

---

## 9. Android images

The current default images are Waydroid/Lineage Android 13 arm64-only images:

```text
lineage-20.0-20260403-VANILLA-waydroid_arm64_only-system.zip
lineage-20.0-20260403-MAINLINE-waydroid_arm64_only-vendor.zip
```

The reasons for using these images are pragmatic:

```text
they provide separate system/vendor images
they are easy to download and extract
they are Android 13
they are arm64-only, matching the aarch64 target
they are standard enough for Binder and servicemanager bring-up
```

The installer downloads the zip files, extracts:

```text
system.img
vendor.img
```

and mounts them with loop devices:

```text
/tmp/android-usb/android-mounts/system_raw
/tmp/android-usb/android-mounts/vendor_raw
```

The rootfs is then assembled using bind mounts.

---

## 10. The servicemanager problem

This was the major debugging milestone.

### 10.1 Initial symptom

The real Android `servicemanager` would not start.

At different points the likely cause looked like one of these:

```text
bad USB storage
bad chroot
missing linkerconfig
FD-passing bug
wrong servicemanager binary
old mini service manager conflict
/dev/binder permissions
incompatible Android image
```

The logs eventually narrowed it down.

The first failure was:

```text
Binder driver '/dev/binder' could not be opened.
Opening '/dev/binder' failed: No such file or directory
```

This meant the installer was not loading Binder or creating `/dev/binder`.

That was fixed by loading `binder.ko` and creating `/dev/binder`.

### 10.2 Second failure: Binder existed but mmap failed

After Binder was loaded, the failure became:

```text
Binder driver '/dev/binder' could not be opened.
Using /dev/binder failed: unable to mmap transaction memory.
```

Kernel log:

```text
binder_dirty: missing address for get_vm_area
binder_mmap ... get_vm_area failed -12
```

This was the key discovery.

The dirty Binder module was loading, but its required kernel symbol parameters were zero:

```text
sym_get_vm_area=0
sym_map_kernel_range_noflush=0
sym_zap_page_range=0
sym___alloc_fd=0
sym___fd_install=0
sym___close_fd=0
sym_get_files_struct=0
sym_put_files_struct=0
sym___lock_task_sighand=0
```

The module needs those internal kernel symbols because it is adapting Binder behavior to the LG webOS 4.4 kernel, where some APIs needed by the dirty Binder shim are not exported normally to modules.

### 10.3 `/proc/kallsyms` had the answer

The TV exposes the required addresses through `/proc/kallsyms`.

Command used:

```sh
ssh root@192.168.2.121 'sh -s' <<'TVSH'
set -u

echo 0 > /proc/sys/kernel/kptr_restrict 2>/dev/null || true

for s in \
  get_vm_area \
  map_kernel_range_noflush \
  zap_page_range \
  __alloc_fd \
  __fd_install \
  __close_fd \
  get_files_struct \
  put_files_struct \
  __lock_task_sighand
do
  echo "== $s =="
  grep -w "$s" /proc/kallsyms | head -n 5 || true
done
TVSH
```

Observed values:

```text
get_vm_area              ffffffc0001a28c0
map_kernel_range_noflush ffffffc0001a2878
zap_page_range           ffffffc000194da8
__alloc_fd               ffffffc0001e3230
__fd_install             ffffffc0001e3578
__close_fd               ffffffc0001e35c0
get_files_struct         ffffffc0001e3028
put_files_struct         ffffffc0001e3078
__lock_task_sighand      ffffffc0000b3290
```

This meant no external `System.map` was required for this target. The running kernel already exposed what we needed.

### 10.4 Fix

The installer now resolves kernel symbols dynamically:

```sh
ksym() {
  awk -v n="$1" '$3 == n && $1 != "0000000000000000" { print "0x"$1; exit }' /proc/kallsyms
}
```

Then it loads Binder with module parameters:

```sh
insmod "$ANDROID_BINDER_KO" \
  sym_get_vm_area="$GET_VM_AREA" \
  sym_map_kernel_range_noflush="$MAP_KERNEL_RANGE_NOFLUSH" \
  sym_zap_page_range="$ZAP_PAGE_RANGE" \
  sym___alloc_fd="$ALLOC_FD" \
  sym___fd_install="$FD_INSTALL" \
  sym___close_fd="$CLOSE_FD" \
  sym_get_files_struct="$GET_FILES_STRUCT" \
  sym_put_files_struct="$PUT_FILES_STRUCT" \
  sym___lock_task_sighand="$LOCK_TASK_SIGHAND"
```

In the clean script this is built dynamically, but the effect is the same.

### 10.5 Result

With symbols loaded, the module parameters become non-zero:

```text
sym_get_vm_area              != 0
sym_map_kernel_range_noflush != 0
sym_zap_page_range           != 0
sym___alloc_fd               != 0
sym___fd_install             != 0
sym___close_fd               != 0
sym_get_files_struct         != 0
sym_put_files_struct         != 0
sym___lock_task_sighand      != 0
```

The install log shows:

```text
ANDROID_BINDER_SYM sym_get_vm_area=0xffffffc0001a28c0
ANDROID_BINDER_SYM sym_map_kernel_range_noflush=0xffffffc0001a2878
ANDROID_BINDER_SYM sym_zap_page_range=0xffffffc000194da8
ANDROID_BINDER_SYM sym___alloc_fd=0xffffffc0001e3230
ANDROID_BINDER_SYM sym___fd_install=0xffffffc0001e3578
ANDROID_BINDER_SYM sym___close_fd=0xffffffc0001e35c0
ANDROID_BINDER_SYM sym_get_files_struct=0xffffffc0001e3028
ANDROID_BINDER_SYM sym_put_files_struct=0xffffffc0001e3078
ANDROID_BINDER_SYM sym___lock_task_sighand=0xffffffc0000b3290
ANDROID_BINDER_READY
```

Then:

```text
ANDROID_REAL_SERVICEMANAGER_RUNNING pid=5945
ANDROID_USB_INSTALL_DONE
```

---

## 11. Why this is the real Android servicemanager

Diagnostics show the Android binary exists at:

```text
/tmp/android-usb/android-rootfs/system/bin/servicemanager
```

with:

```text
--- /system/bin/servicemanager
-rwxr-xr-x    1 root     2000         67824 Apr  3 07:37 /tmp/android-usb/android-rootfs/system/bin/servicemanager
```

The running process is:

```text
5945 ?        00:00:00 servicemanager
```

The mini service manager shim is not part of the clean runtime path.

The Binder log also shows the modern context-manager path:

```text
DIRTY_BINDER_IOCTL_COMPAT_V0 set context mgr ext type=0x0 flags=0x0
```

That confirms the real Android binary reaches the Binder context manager registration path.

---

## 12. Binder and FD passing

At the beginning, it was reasonable to suspect that the FD issue and the `servicemanager` issue were related.

They were related, but the ordering mattered.

`servicemanager` was not failing because of FD passing yet. It was failing earlier, at Binder `mmap()`, because:

```text
sym_get_vm_area=0
```

However, the FD-related symbols are also important:

```text
sym___alloc_fd
sym___fd_install
sym___close_fd
sym_get_files_struct
sym_put_files_struct
```

If those remain zero, Binder FD passing will likely fail later.

The current milestone loads them as non-zero values. That does not prove FD passing is fully correct, but it removes the obvious failure mode.

Future test:

```text
create a minimal Binder client/server pair
send a real FD, such as a pipe or /dev/null
validate the destination process receives a usable FD
validate close/lifetime behavior
watch dmesg for Binder errors
```

---

## 13. Why `/dev/hwbinder` must not be faked

The current Binder module only registers:

```text
53 binder
/dev/binder
```

The kernel log says:

```text
binder: unknown parameter 'devices' ignored
```

So this module does not support:

```text
devices=binder,hwbinder,vndbinder
```

It may be tempting to create:

```sh
ln -s /dev/binder /dev/hwbinder
```

or to create `/dev/hwbinder` with the same major/minor as `/dev/binder`.

Do not do that.

Modern Android uses separate Binder domains:

```text
/dev/binder      -> framework Binder domain
/dev/hwbinder    -> HAL Binder domain
/dev/vndbinder   -> vendor Binder domain
```

`servicemanager` and `hwservicemanager` need separate Binder context managers. If both talk to the same underlying Binder device, they collide conceptually and practically.

The correct fix is to patch or replace `binder.ko` so that it registers multiple independent Binder devices.

---

## 14. Current `hwservicemanager` status

Current diagnostic:

```text
== hwservicemanager one-shot test ==
HWSERVICEMANAGER_EXITED_QUICKLY=YES
Binder driver could not be opened. Terminating.
```

This is expected until `/dev/hwbinder` exists as a real Binder device.

This is now the next major kernel-side milestone.

---

## 15. Linkerconfig

Current warning:

```text
linker: Warning: failed to find generated linker configuration from "/linkerconfig/ld.config.txt"
```

This currently does not block:

```text
/system/bin/toybox
/system/bin/servicemanager
```

But it will matter later for richer Android userspace.

The rootfs contains:

```text
/apex/com.android.runtime/bin/linkerconfig
/system/bin/linkerconfig -> /apex/com.android.runtime/bin/linkerconfig
```

and the installer creates:

```text
/tmp/android-usb/android-rootfs/linkerconfig
```

Future work:

```text
generate /linkerconfig/ld.config.txt properly
bind-mount /linkerconfig in the expected place
provide the properties linkerconfig expects
validate more complex dynamically linked binaries
```

---

## 16. Property service and getprop

Current behavior:

```text
chroot getprop
linker: Warning: failed to find generated linker configuration from "/linkerconfig/ld.config.txt"
```

`getprop` exists:

```text
/system/bin/getprop -> toolbox
```

but Android's property service is not running yet. Therefore `getprop` does not behave like it would in a full Android boot.

Future work:

```text
provide a minimal property service
or start the relevant part of Android init
or inject enough static properties for linkerconfig and core services
```

A good future success criterion:

```text
getprop ro.build.version.release
13
```

---

## 17. SELinux, namespaces, cgroups and init

This milestone does not solve:

```text
Android SELinux
Android cgroups
PID namespaces
mount namespaces
network namespaces
binderfs
ashmem/memfd policy
full Android init
```

That was intentional. Trying to solve those before Binder and `servicemanager` would make debugging too broad.

Current strategy:

```text
first prove USB rootfs
then prove Binder
then prove real servicemanager
then add hwbinder/vndbinder
then solve linkerconfig/property/init
then approach zygote/system_server
```

---

## 18. Validated diagnostic snapshot

The current validated state includes:

```text
/dev/sda1 /tmp/android-usb ext4 rw
/dev/loop3 /tmp/android-usb/android-mounts/system_raw ext4 ro
/dev/loop4 /tmp/android-usb/android-mounts/vendor_raw ext4 ro
/dev/loop3 /tmp/android-usb/android-rootfs/system ext4 ro
/dev/loop4 /tmp/android-usb/android-rootfs/vendor ext4 ro
/dev/loop3 /tmp/android-usb/android-rootfs/apex ext4 ro
/dev/sda1 /tmp/android-usb/android-rootfs/data ext4 rw
/dev/sda1 /tmp/android-usb/android-rootfs/cache ext4 rw
proc /tmp/android-usb/android-rootfs/proc proc rw
sysfs /tmp/android-usb/android-rootfs/sys sysfs rw
devtmpfs /tmp/android-usb/android-rootfs/dev devtmpfs rw
```

Binder:

```text
53 binder
crw-rw-rw- 1 root root 10, 53 /dev/binder
binder module loaded
sym_get_vm_area != 0
sym___alloc_fd != 0
sym___fd_install != 0
sym___close_fd != 0
sym_get_files_struct != 0
sym_put_files_struct != 0
```

Android:

```text
ro.build.version.release=13
/system/bin/servicemanager exists
/system/bin/hwservicemanager exists
/system/bin/toybox exists
```

Runtime:

```text
servicemanager running
hwservicemanager fails because /dev/hwbinder is missing
```

Installer success:

```text
ANDROID_BINDER_READY
ANDROID_USB_ROOTFS_READY
ANDROID_USB_TOYBOX_OK
ANDROID_REAL_SERVICEMANAGER_RUNNING
ANDROID_USB_INSTALL_DONE
```

---

## 19. Next milestones toward Android as a webOS app

### Milestone 1 — Binder multi-device support

Goal:

```text
/dev/binder
/dev/hwbinder
/dev/vndbinder
```

Tasks:

```text
patch binder.ko to register multiple independent misc devices
preserve separate context managers
make devices=binder,hwbinder,vndbinder work
test servicemanager on /dev/binder
test hwservicemanager on /dev/hwbinder
test vndbinder if required by the image
```

Success criteria:

```text
servicemanager running
hwservicemanager running
no fake symlinks
no context-manager collision
```

### Milestone 2 — Binder FD-passing validation

Goal:

```text
prove Binder FD passing works
```

Tasks:

```text
build minimal Binder client/server test
send a pipe or /dev/null FD
verify the target process receives a valid FD
verify lifecycle and close behavior
watch dmesg for Binder errors
```

Success criteria:

```text
BINDER_TYPE_FD works
BINDER_TYPE_FDA works if used by the image
no obvious leaks
no kernel crash
```

### Milestone 3 — linkerconfig

Goal:

```text
generate /linkerconfig/ld.config.txt
```

Tasks:

```text
run linkerconfig inside the rootfs
provide required properties
mount /linkerconfig correctly
validate dynamic linker namespaces
```

Success criteria:

```text
no linkerconfig warning for core binaries
ld.config.txt exists
more complex Android binaries run
```

### Milestone 4 — property service

Goal:

```text
getprop works
```

Tasks:

```text
start Android property service or a minimal equivalent
load build.prop/vendor props
provide property area/socket expected by Android
```

Success criteria:

```text
getprop ro.build.version.release -> 13
core services can read required properties
```

### Milestone 5 — init strategy

Goal:

```text
decide between real Android init, mini-init, or hybrid
```

Options:

```text
real Android init in a controlled environment
custom mini-init that starts only required services
hybrid approach
```

Likely path:

```text
mini-init first
real init later if kernel/userspace compatibility allows it
```

### Milestone 6 — HAL layer

Goal:

```text
minimal HAL layer alive
```

Tasks:

```text
make hwservicemanager run
inspect vendor.img services
start only essential HAL services
stub unsupported hardware if needed
```

Success criteria:

```text
hwservicemanager remains alive
basic HAL service list works
```

### Milestone 7 — zygote

Goal:

```text
start zygote64
```

Prerequisites:

```text
Binder base
hwbinder if needed
linkerconfig
property service
minimal cgroups/namespaces
SELinux strategy
```

Success criteria:

```text
zygote64 remains alive
app_process runs
no immediate linker/property/cgroup crash
```

### Milestone 8 — system_server

Goal:

```text
system_server remains alive
```

Tasks:

```text
resolve Java framework services
resolve permissions and runtime directories
resolve sockets
stub or provide missing hardware services
debug early Binder calls
```

Success criteria:

```text
system_server survives more than 30-60 seconds
framework services start registering
```

### Milestone 9 — graphics

Goal:

```text
render Android somewhere visible from webOS
```

Possible approaches:

```text
SurfaceFlinger with adapted backend
headless rendering plus stream/buffer bridge
Wayland/WAM bridge if available
EGL/GLES direct integration if webOS libraries allow it
experimental framebuffer or texture bridge
```

Likely first path:

```text
headless or buffer bridge first
webOS app integration later
```

### Milestone 10 — webOS app wrapper

Goal:

```text
launch Android as a webOS app-like experience
```

Tasks:

```text
create a webOS launcher app
create native/service bridge to start and stop Android sidecar
display Android output
inject remote-control/keyboard/mouse input
handle app lifecycle: start, pause, resume, stop
provide logs and watchdog
```

Success criteria:

```text
a webOS app launches
Android UI or a chosen Android app is visible
input reaches Android
shutdown is clean
```

### Milestone 11 — IPK packaging

Goal:

```text
reproducible install from webOS
```

Tasks:

```text
package scripts
detect USB
install/load binder.ko
download images
create launcher icon
provide logs
provide recovery/fallback path
```

---

## 20. Useful commands

Install:

```sh
TV_IP=192.168.2.121 ./scripts/install-android-usb.sh
```

Tail progress:

```sh
TV_IP=192.168.2.121 ./scripts/tail-android-usb.sh
```

Diagnose:

```sh
TV_IP=192.168.2.121 ./scripts/diagnose-android-usb.sh
```

Format and install:

```sh
TV_IP=192.168.2.121 \
ANDROID_USB_PART=/dev/sda1 \
FORMAT_USB=1 \
CONFIRM_FORMAT_ANDROID_USB=YES \
./scripts/install-android-usb.sh
```

Check Binder symbols:

```sh
ssh root@192.168.2.121 'sh -s' <<'TVSH'
set -u
echo 0 > /proc/sys/kernel/kptr_restrict 2>/dev/null || true

for s in \
  get_vm_area \
  map_kernel_range_noflush \
  zap_page_range \
  __alloc_fd \
  __fd_install \
  __close_fd \
  get_files_struct \
  put_files_struct \
  __lock_task_sighand
do
  echo "== $s =="
  grep -w "$s" /proc/kallsyms | head -n 5 || true
done
TVSH
```

---

## 21. Git workflow for this milestone

After copying this file to `README.md`:

```sh
cp README_V3.md README.md
```

Review:

```sh
git status --short
git diff -- README.md configs scripts
```

Add:

```sh
git add README.md configs/android-usb.env
git add scripts/install-android-usb.sh scripts/tail-android-usb.sh scripts/diagnose-android-usb.sh
git add -u scripts
```

Commit:

```sh
git commit -m "usb-only android milestone with real servicemanager"
```

Push:

```sh
git push origin main
```

---

## 22. Lessons learned

### 22.1 The USB was necessary, but not the servicemanager bug

Moving to USB solved storage and persistence. It did not directly solve `servicemanager`.

### 22.2 The Android servicemanager binary was fine

The Android 13 binary could run. Binder was preventing it from getting far enough.

### 22.3 The first Binder bug was obvious

`/dev/binder` did not exist.

### 22.4 The second Binder bug was subtle

`/dev/binder` existed, but Binder transaction memory mapping failed because `sym_get_vm_area` was zero.

### 22.5 `/proc/kallsyms` was the unlock

The TV exposed the hidden kernel addresses needed by the dirty Binder module.

### 22.6 The mini service manager is not needed for this milestone

The milestone is stronger because Android's own `servicemanager` is running.

### 22.7 Do not fake `hwbinder`

The next blocker must be fixed in the Binder module, not by symlinks or duplicated device nodes.

### 22.8 Keep the public UX small

Three scripts are enough:

```text
install
tail
diagnose
```

That makes the project easier to use, easier to review, and easier to push to `main`.

---

## 23. References

- Android Binder overview: https://source.android.com/docs/core/architecture/ipc/binder-overview
- Android HIDL Binder IPC and Binder devices: https://source.android.com/docs/core/architecture/hidl/binder-ipc
- Waydroid custom images: https://docs.waydro.id/faq/using-custom-waydroid-images
- Waydroid Lineage image build notes: https://docs.waydro.id/development/compile-waydroid-lineage-os-based-images

---

## 24. Recommended next milestone

The next milestone should be:

```text
binder.ko multi-device support: /dev/binder + /dev/hwbinder + /dev/vndbinder
```

Do not jump to zygote yet.

Recommended order:

```text
1. Binder multi-device support
2. hwservicemanager alive
3. real Binder FD-passing test
4. linkerconfig
5. property service
6. init or mini-init
7. zygote
8. system_server
9. Android rendering inside a webOS app
```

The current milestone is strong because the core Android framework Binder registry is alive:

```text
real Android 13 servicemanager running inside a USB-mounted Android rootfs on webOS
```
