# webos-dirty-binder

> Milestone: **USB-only Android rootfs + Binder symbol loader + real Android 13 `servicemanager` + real Binder FD passing on LG webOS**  
> Milestone date: **2026-05-19**  
> Tested target: LG webOS TV, `Linux 4.4.84-229.1.kavir.2`, `aarch64`  
> Control host: NanoPi R3S  
> Long-term goal: run Android inside webOS as an app-like sidecar, without relying on `/media/internal`.

---

## 1. Executive summary

`webos-dirty-binder` is an experimental project for bringing up enough Android userspace on a rooted LG webOS TV to run Android framework components on top of the TV's existing Linux/webOS kernel.

This README keeps the structure of the original V3 milestone document and extends it with the later Binder FD milestone.

The V3 milestone proved that a USB-mounted Android rootfs can run the real Android 13 `servicemanager` inside webOS:

```text
/tmp/android-usb/android-rootfs/system/bin/servicemanager
```

The later FD milestone proved that `BINDER_TYPE_FD` can transfer a real file descriptor across Binder without freezing the TV. The final validated Binder FD result was:

```text
sent_count=16
received_count=16
read_count=16
round_count=16
expected=16
BINDER_FD_SMOKE_TV_OK
BINDER_FD_STAGE7_REAL_FD_OK
```

The final validated unload result was:

```text
binder 118784 0 - Live ...
rmmod rc=0
binder unloaded OK
```

The important fix for FD passing is:

```text
Do not use task_get_unused_fd_flags()
Do not use target_proc->tsk->files
Do not use current->files

Use Binder's target_proc->files directly:

    file = fget(fp->handle)
    target_fd = dirty___alloc_fd(target_proc->files, 0, 1024, O_CLOEXEC)
    dirty___fd_install(target_proc->files, target_fd, file)
    fp->handle = target_fd
```

Why this matters:

```text
Binder mmap now works
real Android /system/bin/servicemanager can start
Binder handle acquisition works
Binder FD transfer works
the dirty Binder module is unloadable for iterative testing
```

The remaining major kernel-side milestone is multi-device Binder support:

```text
/dev/binder
/dev/hwbinder
/dev/vndbinder
```

Do not jump to zygote before that.

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
dirty binder.ko loaded on the webOS kernel
/dev/binder created
Binder mmap works
BINDER_SET_CONTEXT_MGR_EXT is reached
/system/bin/toybox runs inside the Android chroot
real Android 13 /system/bin/servicemanager starts and remains alive
mini servicemanager test harness works
Binder basic transactions work
explicit BC_INCREFS / BC_ACQUIRE works
BINDER_TYPE_FD works through target_proc->files
Binder FD smoke test passes 16/16 rounds
binder.ko is unloadable for iterative development
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

Main sidecar path:

```text
/tmp/android-usb/android-sidecar
```

Main logs:

```text
/tmp/android-usb/android-sidecar/logs/android-usb-install.log
/tmp/android-usb/android-sidecar/logs/servicemanager.log
/tmp/android-usb/android-sidecar/logs/hwservicemanager.log
```

### 2.2 Not working yet

The following pieces are still pending:

```text
real /dev/hwbinder
real /dev/vndbinder
hwservicemanager on an independent hwbinder domain
vndservicemanager on an independent vndbinder domain
generated /linkerconfig/ld.config.txt
Android property service
real Android init or a controlled mini-init
zygote
system_server
SurfaceFlinger
running Android UI inside a webOS app
input/audio/network integration
long-running stress and leak testing
```

The expected `hwservicemanager` failure is still:

```text
HWSERVICEMANAGER_EXITED_QUICKLY=YES
Binder driver could not be opened. Terminating.
```

This is expected until the Binder module registers a real `/dev/hwbinder`.

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

The public Android USB workflow remains intentionally small:

```text
scripts/install-android-usb.sh
scripts/tail-android-usb.sh
scripts/diagnose-android-usb.sh
```

The Binder/FD development workflow adds explicit test helpers:

```text
scripts/build-module.sh
scripts/build-sidecar.sh
scripts/reload-build-binder-tv.sh
scripts/reboot-tv-and-load-build-binder.sh
scripts/run-binder-fd-stage-tv.sh
scripts/run-binder-fd-smoke-tv.sh
scripts/test-real-android-servicemanager-tv.sh
```

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
set fd_debug_stage=7 by default
create/fix /dev/binder
mount the Android rootfs
run a toybox smoke test
start the real Android /system/bin/servicemanager
```

The installer should prefer:

```text
build/linux-4.4.84/drivers/android/binder.ko
```

over any historical artifact under:

```text
artifacts/
```

because old artifacts may lack FD passing or `module_exit()`.

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

### 4.4 FD smoke helpers

`run-binder-fd-smoke-tv.sh` is the simplest FD validation entry point.

Expected success:

```text
BINDER_FD_SMOKE_TV_OK
BINDER_FD_STAGE7_REAL_FD_OK
```

`run-binder-fd-stage-tv.sh` is the lower-level staged diagnostic runner. It remains useful while debugging, but the normal FD path is now stage 7.

### 4.5 Old scripts replaced by the clean flow

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
FD_DEBUG_STAGE="7"

SYSTEM_ZIP_URL="https://sourceforge.net/projects/waydroid/files/images/system/lineage/waydroid_arm64_only/lineage-20.0-20260403-VANILLA-waydroid_arm64_only-system.zip/download"
VENDOR_ZIP_URL="https://sourceforge.net/projects/waydroid/files/images/vendor/waydroid_arm64_only/lineage-20.0-20260403-MAINLINE-waydroid_arm64_only-vendor.zip/download"
```

Important defaults:

```text
START_SERVICEMANAGER=1
FD_DEBUG_STAGE=7
```

`FD_DEBUG_STAGE=7` means the final validated real Binder FD path is active.

---

## 6. Building from scratch

For the current milestone, the required build artifact is:

```text
build/linux-4.4.84/drivers/android/binder.ko
```

Sidecar static binaries are needed for Binder FD smoke tests.

### 6.1 Safe cleanup

Do not delete the prepared kernel tree unless you really want to rebuild everything from scratch. A safe cleanup is:

```sh
cd /home/pi/disk/webos-dirty-binder

find build -type f \(   -name '*.o' -o   -name '*.ko' -o   -name '*.mod' -o   -name '*.mod.c' -o   -name '*.cmd' -o   -name '*.symvers' -o   -name 'modules.order' \) -delete 2>/dev/null || true
```

### 6.2 Build Binder

```sh
chmod +x scripts/*.sh
KCFLAGS="-Wno-error -Wno-error=unused-variable -Wno-error=unused-function" ./scripts/build-module.sh
```

Verify:

```sh
ls -lh build/linux-4.4.84/drivers/android/binder.ko
modinfo -p build/linux-4.4.84/drivers/android/binder.ko
readelf -sW build/linux-4.4.84/drivers/android/binder.ko | grep cleanup_module
```

Expected:

```text
fd_debug_stage:Dirty Binder FD debug stage (int)
cleanup_module
```

`cleanup_module` matters. Without it, the module loads as:

```text
binder 118784 0 [permanent], Live ...
```

and cannot be removed with `rmmod`.

### 6.3 Build sidecar tools

```sh
./scripts/build-sidecar.sh
```

Useful outputs include:

```text
build/mini_servicemgr_static
build/android_like_fd_passing_service_static
build/android_like_fd_passing_client_static
```

The deploy scripts copy these into:

```text
/tmp/android-usb/android-sidecar/bin/
```

under the expected runtime names:

```text
mini_servicemgr
android_like_fd_passing_service
android_like_fd_passing_client
```

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

The installer starts the real Android `servicemanager` by default:

```text
START_SERVICEMANAGER=1
```

### 7.2 Format USB and install

Formatting is destructive and requires explicit confirmation:

```sh
TV_IP=192.168.2.121 ANDROID_USB_PART=/dev/sda1 FORMAT_USB=1 CONFIRM_FORMAT_ANDROID_USB=YES ./scripts/install-android-usb.sh
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
      mini_servicemgr
      android_like_fd_passing_service
      android_like_fd_passing_client
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

This was the original V3 debugging milestone.

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

for s in   get_vm_area   map_kernel_range_noflush   zap_page_range   __alloc_fd   __fd_install   __close_fd   get_files_struct   put_files_struct   __lock_task_sighand
do
  echo "== $s =="
  grep -w "$s" /proc/kallsyms | head -n 5 || true
done
TVSH
```

Observed values on the tested TV:

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

This meant no external `System.map` was required for this target. The running kernel already exposed what was needed.

### 10.4 Fix

The installer resolves kernel symbols dynamically:

```sh
ksym() {
  awk -v n="$1" '$3 == n && $1 != "0000000000000000" { print "0x"$1; exit }' /proc/kallsyms
}
```

Then it loads Binder with module parameters:

```sh
insmod "$ANDROID_BINDER_KO"   sym_get_vm_area="$GET_VM_AREA"   sym_map_kernel_range_noflush="$MAP_KERNEL_RANGE_NOFLUSH"   sym_zap_page_range="$ZAP_PAGE_RANGE"   sym___alloc_fd="$ALLOC_FD"   sym___fd_install="$FD_INSTALL"   sym___close_fd="$CLOSE_FD"   sym_get_files_struct="$GET_FILES_STRUCT"   sym_put_files_struct="$PUT_FILES_STRUCT"   sym___lock_task_sighand="$LOCK_TASK_SIGHAND"   fd_debug_stage="${FD_DEBUG_STAGE:-7}"
```

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

The install log should show:

```text
ANDROID_BINDER_READY
ANDROID_USB_ROOTFS_READY
ANDROID_USB_TOYBOX_OK
ANDROID_REAL_SERVICEMANAGER_RUNNING pid=...
ANDROID_USB_INSTALL_DONE
```

---

## 11. Why this is the real Android servicemanager

Diagnostics show the Android binary exists at:

```text
/tmp/android-usb/android-rootfs/system/bin/servicemanager
```

with a binary from the Android image, for example:

```text
-rwxr-xr-x 1 root 2000 67824 Apr  3 07:37 /tmp/android-usb/android-rootfs/system/bin/servicemanager
```

The running process appears as:

```text
servicemanager
```

and the process path or command line points into:

```text
/tmp/android-usb/android-rootfs/system/bin/servicemanager
```

The mini service manager shim is not part of the clean runtime path.

The Binder log also shows the modern context-manager path:

```text
DIRTY_BINDER_IOCTL_COMPAT_V0 set context mgr ext type=0x0 flags=0x0
```

That confirms the real Android binary reaches the Binder context manager registration path.

For timing-sensitive checks, use:

```sh
TV_IP=192.168.2.121 WAIT_SECS=120 scripts/test-real-android-servicemanager-tv.sh
```

The installer can return before the background remote install has fully brought up the Android rootfs, so immediate `/proc` checks can race.

---

## 12. Binder and FD passing

This section is the major addition after V3.

At the beginning, it was reasonable to suspect that the FD issue and the `servicemanager` issue were the same bug. They were related through Binder, but the ordering mattered.

`servicemanager` was not initially failing because of FD passing. It was failing earlier, at Binder `mmap()`, because:

```text
sym_get_vm_area=0
```

Once Binder mmap and the symbol loader were fixed, the next blocker became real FD passing through Binder.

### 12.1 Why FD passing matters

Android framework code uses Binder not only to transfer objects and handles, but also file descriptors. If `BINDER_TYPE_FD` is broken, later Android services will fail even if basic Binder calls work.

The test goal was:

```text
client opens or creates an FD
client sends it through Binder using BINDER_TYPE_FD
service receives the FD
service reads expected payload
counts match across multiple rounds
no kernel freeze
no Binder transaction corruption
```

Final validated result:

```text
sent_count=16
received_count=16
read_count=16
round_count=16
expected=16
BINDER_FD_SMOKE_TV_OK
BINDER_FD_STAGE7_REAL_FD_OK
```

### 12.2 Initial FD symptom

Direct `BINDER_TYPE_FD` transactions failed or could freeze the TV.

The client could send:

```text
BINDER_FD_OBJECT_SENT label=round-0 payload=payload-0-from-client fd=4
```

but the transaction could become:

```text
BR_FAILED_REPLY
```

or the TV could hang.

### 12.3 Stage 0: reject before `fget`

Stage 0 inserted a rejection before the source FD lookup.

Marker:

```text
DIRTY_BINDER_FD_STAGE stage=0 reject_before_fget
BINDER_FD_STAGE_SAFE_FAILURE_OK
```

Meaning:

```text
the FD object reaches the Binder kernel path
the userland service/client setup is valid
the fault is later than object parsing
```

### 12.4 Stage 1: `fget` + `fput`

Stage 1 did:

```text
file = fget(fp->handle)
fput(file)
reject
```

Marker:

```text
DIRTY_BINDER_FD_STAGE stage=1 after_fget file=...
BINDER_FD_STAGE_SAFE_FAILURE_OK
```

Meaning:

```text
the source FD lookup is safe
the source file pointer is real
the fault is not fget itself
```

### 12.5 Stage 2: reject before allocation

Stage 2 rejected before FD allocation in the target process.

Marker:

```text
DIRTY_BINDER_FD_STAGE stage=2 reject_before_alloc_no_security_hook file=...
BINDER_FD_STAGE_SAFE_FAILURE_OK
```

Meaning:

```text
the transaction survives until the target FD allocation point
the freeze is beyond fget
```

### 12.6 Early Stage 3: freeze in old allocation path

The first stage 3 attempted the old allocation path. That froze the TV.

This isolated the bug to the allocation path used by the old Binder 4.4 code.

The dangerous path was conceptually:

```text
target_proc->tsk
task_get_unused_fd_flags(...)
current->files or task_struct fields from the build tree
```

### 12.7 Pre-allocation diagnostics

A safe diagnostic stage printed the target state before allocation. It showed:

```text
target_proc != NULL
target_proc->files != NULL
target_proc->tsk suspicious
task_pid_nr(target_proc->tsk) = 0
target comm empty or corrupt
```

Another diagnostic showed `current->pid` and `current->comm` could also be corrupt or misleading when read directly from this module.

Conclusion:

```text
Do not trust task_struct layout from the public build tree on this LG webOS kernel.
Do not use target_proc->tsk->files.
Do not use current->files.
Use Binder's own target_proc->files.
```

### 12.8 Source and target allocation probes

The next probes avoided task_struct fields and used Binder's `binder_proc` file tables.

Stage 4 proved allocation and close on source Binder process files:

```text
DIRTY_BINDER_FD_STAGE stage=4 proc_files_alloc_probe ...
DIRTY_BINDER_FD_STAGE stage=4 proc_alloc_ret=5
BINDER_FD_STAGE_SAFE_FAILURE_OK
```

Stage 5 proved allocation and close on target Binder process files:

```text
DIRTY_BINDER_FD_STAGE stage=5 target_files_alloc_probe ...
DIRTY_BINDER_FD_STAGE stage=5 target_alloc_ret=4
BINDER_FD_STAGE_SAFE_FAILURE_OK
```

Meaning:

```text
__alloc_fd works
__close_fd works
proc->files works
target_proc->files works
```

### 12.9 Install + close probe

Stage 6 installed the file into the target and immediately closed it:

```text
dirty___alloc_fd(target_proc->files, ...)
dirty___fd_install(target_proc->files, fd, file)
dirty___close_fd(target_proc->files, fd)
```

Markers:

```text
DIRTY_BINDER_FD_STAGE stage=6 target_alloc_ret=4
DIRTY_BINDER_FD_STAGE stage=6 before_fd_install fd=4
DIRTY_BINDER_FD_STAGE stage=6 after_fd_install fd=4 closing_now
DIRTY_BINDER_FD_STAGE stage=6 after_close fd=4
BINDER_FD_STAGE_SAFE_FAILURE_OK
```

Meaning:

```text
__fd_install is safe when called with target_proc->files
the remaining step is to let Binder deliver the installed FD
```

### 12.10 Stage 7: real FD path

Stage 7 became the validated real FD path:

```text
file = fget(fp->handle)
target_fd = dirty___alloc_fd(target_proc->files, 0, 1024, O_CLOEXEC)
dirty___fd_install(target_proc->files, target_fd, file)
file = NULL
fp->handle = target_fd
break
```

Markers:

```text
DIRTY_BINDER_FD_STAGE stage=7 real_fd_path target_proc=... target_files=... file=...
DIRTY_BINDER_FD_STAGE stage=7 alloc_ret=4
DIRTY_BINDER_FD_STAGE stage=7 before_fd_install fd=4 file=...
DIRTY_BINDER_FD_STAGE stage=7 installed fd=4 fp_handle=4
```

Final userland result:

```text
sent_count=16
received_count=16
read_count=16
round_count=16
expected=16
BINDER_FD_SMOKE_TV_OK
BINDER_FD_STAGE7_REAL_FD_OK
```

### 12.11 Final Binder FD implementation

The final implementation should not depend on the old `task_get_unused_fd_flags()` path.

Conceptual final code:

```c
case BINDER_TYPE_FD: {
    int target_fd;
    struct file *file;

    file = fget(fp->handle);
    if (!file) {
        return_error = BR_FAILED_REPLY;
        goto err_fget_failed;
    }

    if (!target_proc || !target_proc->files) {
        fput(file);
        return_error = BR_FAILED_REPLY;
        goto err_fd_not_allowed;
    }

    target_fd = dirty___alloc_fd(target_proc->files, 0, 1024, O_CLOEXEC);
    if (target_fd < 0) {
        fput(file);
        return_error = BR_FAILED_REPLY;
        goto err_fd_not_allowed;
    }

    dirty___fd_install(target_proc->files, target_fd, file);
    file = NULL;
    fp->handle = target_fd;
    break;
}
```

The exact variable names and error labels depend on the generated Binder source, so the repo patches the generated kernel tree after applying the base dirty Binder patch.

### 12.12 Why not `rlimit(RLIMIT_NOFILE)`

An earlier probe used:

```text
dirty___alloc_fd(..., 0, rlimit(RLIMIT_NOFILE), ...)
```

and returned:

```text
-24
```

`-24` is `EMFILE`.

Using a fixed end of `1024` validated the real path reliably on the target:

```text
dirty___alloc_fd(target_proc->files, 0, 1024, O_CLOEXEC)
```

This is a pragmatic milestone fix. A later cleanup can replace the fixed bound with a known-safe target-process limit if needed.

### 12.13 FD smoke command

One round:

```sh
ROUNDS=1 NO_BUILD=1 TV_IP=192.168.2.121 FD_DEBUG_STAGE=7 FD_STAGE_TIMEOUT=20 scripts/run-binder-fd-stage-tv.sh
```

Sixteen rounds:

```sh
ROUNDS=16 NO_BUILD=1 TV_IP=192.168.2.121 FD_DEBUG_STAGE=7 FD_STAGE_TIMEOUT=40 scripts/run-binder-fd-stage-tv.sh
```

Or via wrapper:

```sh
ROUNDS=16 TV_IP=192.168.2.121 scripts/run-binder-fd-smoke-tv.sh
```

Required markers:

```text
BINDER_FD_SMOKE_TV_OK
BINDER_FD_STAGE7_REAL_FD_OK
```

---

## 13. Why `/dev/hwbinder` must not be faked

The current Binder module is still primarily validated for:

```text
/dev/binder
```

Modern Android uses separate Binder domains:

```text
/dev/binder      -> framework Binder domain
/dev/hwbinder    -> HAL Binder domain
/dev/vndbinder   -> vendor Binder domain
```

`servicemanager`, `hwservicemanager`, and `vndservicemanager` need separate Binder context managers. If both talk to the same underlying Binder device, they collide conceptually and practically.

Do not do this:

```sh
ln -s /dev/binder /dev/hwbinder
```

Do not create `/dev/hwbinder` with the same major/minor as `/dev/binder`.

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

That was intentional. Trying to solve those before Binder, FD passing, and `servicemanager` would make debugging too broad.

Current strategy:

```text
first prove USB rootfs
then prove Binder mmap
then prove real servicemanager
then prove Binder FD passing
then add hwbinder/vndbinder
then solve linkerconfig/property/init
then approach zygote/system_server
```

---

## 18. Validated diagnostic snapshot

The current validated state includes:

```text
/dev/sda1 /tmp/android-usb ext4 rw
/dev/loop* /tmp/android-usb/android-mounts/system_raw ext4 ro
/dev/loop* /tmp/android-usb/android-mounts/vendor_raw ext4 ro
/dev/loop* /tmp/android-usb/android-rootfs/system ext4 ro
/dev/loop* /tmp/android-usb/android-rootfs/vendor ext4 ro
/dev/loop* /tmp/android-usb/android-rootfs/apex ext4 ro
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
fd_debug_stage=7
sym_get_vm_area != 0
sym___alloc_fd != 0
sym___fd_install != 0
sym___close_fd != 0
sym_get_files_struct != 0
sym_put_files_struct != 0
cleanup_module exists
rmmod binder works
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
real Android servicemanager running in the USB Android rootfs
FD smoke test passes 16/16 rounds
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

FD success:

```text
BINDER_FD_SMOKE_TV_OK
BINDER_FD_STAGE7_REAL_FD_OK
```

Unload success:

```text
rmmod rc=0
binder unloaded OK
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
make devices=binder,hwbinder,vndbinder work if possible
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

### Milestone 2 — Binder FD-passing cleanup

The FD path now works, but the implementation should be cleaned up.

Tasks:

```text
promote stage 7 logic into the normal BINDER_TYPE_FD path
remove debug-only stages from production builds
keep a diagnostic mode behind an explicit module parameter if useful
document the fixed 1024 fd limit or replace it with a safe target limit
run longer stress tests
watch for file reference leaks
```

Success criteria:

```text
BINDER_TYPE_FD works without debug-stage language
no FD leaks over repeated rounds
no kernel warnings
module remains unloadable
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
Binder FD passing
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
TV_IP=192.168.2.121 ANDROID_USB_PART=/dev/sda1 FORMAT_USB=1 CONFIRM_FORMAT_ANDROID_USB=YES ./scripts/install-android-usb.sh
```

Check Binder symbols:

```sh
ssh root@192.168.2.121 'sh -s' <<'TVSH'
set -u
echo 0 > /proc/sys/kernel/kptr_restrict 2>/dev/null || true

for s in   get_vm_area   map_kernel_range_noflush   zap_page_range   __alloc_fd   __fd_install   __close_fd   get_files_struct   put_files_struct   __lock_task_sighand
do
  echo "== $s =="
  grep -w "$s" /proc/kallsyms | head -n 5 || true
done
TVSH
```

Build Binder:

```sh
KCFLAGS="-Wno-error -Wno-error=unused-variable -Wno-error=unused-function" ./scripts/build-module.sh
```

Verify Binder module:

```sh
modinfo -p build/linux-4.4.84/drivers/android/binder.ko
readelf -sW build/linux-4.4.84/drivers/android/binder.ko | grep cleanup_module
```

Reload Binder without reboot:

```sh
TV_IP=192.168.2.121 FD_DEBUG_STAGE=7 scripts/reload-build-binder-tv.sh
```

FD smoke:

```sh
ROUNDS=16 NO_BUILD=1 TV_IP=192.168.2.121 FD_DEBUG_STAGE=7 FD_STAGE_TIMEOUT=40 scripts/run-binder-fd-stage-tv.sh
```

Unload Binder:

```sh
ssh root@192.168.2.121 '
killall servicemanager hwservicemanager vndservicemanager mini_servicemgr android_like_fd_passing_service android_like_fd_passing_client 2>/dev/null || true
sleep 1
rmmod binder
echo "rmmod rc=$?"
grep "^binder " /proc/modules || echo "binder unloaded OK"
'
```

Test real Android servicemanager with wait:

```sh
TV_IP=192.168.2.121 ROUNDS=1 WAIT_SECS=120 scripts/test-real-android-servicemanager-tv.sh
```

---

## 21. Git workflow for this milestone

After copying this file to `README.md`:

```sh
cp README.md /home/pi/disk/webos-dirty-binder/README.md
```

Review:

```sh
git status --short
git diff --stat
git diff -- README.md configs scripts src patches
```

Add:

```sh
git add README.md configs/android-usb.env
git add scripts/install-android-usb.sh scripts/tail-android-usb.sh scripts/diagnose-android-usb.sh
git add scripts/build-module.sh scripts/build-sidecar.sh
git add scripts/reload-build-binder-tv.sh scripts/reboot-tv-and-load-build-binder.sh
git add scripts/run-binder-fd-stage-tv.sh scripts/run-binder-fd-smoke-tv.sh
git add scripts/test-real-android-servicemanager-tv.sh
git add src patches configs
git add -u scripts src patches configs
```

Avoid committing generated local build outputs:

```sh
git reset build/ 2>/dev/null || true
git reset artifacts/*.ko 2>/dev/null || true
```

Commit:

```sh
git commit -m "fix binder fd passing and keep android usb servicemanager default"
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

### 22.6 The mini service manager is not the normal runtime path

The milestone is stronger because Android's own `servicemanager` is the default runtime target.

The mini service manager is still valuable for controlled Binder tests.

### 22.7 Binder FD passing was not fixed by guessing

The FD fix required staged isolation:

```text
before fget
after fget
before allocation
pre-allocation diagnostics
source allocation
target allocation
fd_install + close
real FD transfer
```

The final result came from proving each kernel step separately.

### 22.8 Do not trust task_struct layout from the public tree

The LG webOS kernel is close to the public 4.4.84 tree, but not identical enough to safely read arbitrary `task_struct` fields from this module.

The safe FD path uses:

```text
target_proc->files
```

### 22.9 Do not fake `hwbinder`

The next blocker must be fixed in the Binder module, not by symlinks or duplicated device nodes.

### 22.10 Keep the public UX small

The main Android USB UX should remain:

```text
install
tail
diagnose
```

Specialized Binder FD tools can stay under `scripts/`, but should not confuse the normal install flow.

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
3. clean Binder FD implementation without debug-stage scaffolding
4. linkerconfig
5. property service
6. init or mini-init
7. zygote
8. system_server
9. Android rendering inside a webOS app
```

The current milestone is strong because the core Android framework Binder registry path is alive, and Binder FD passing now works:

```text
real Android 13 servicemanager running inside a USB-mounted Android rootfs on webOS
BINDER_TYPE_FD transfers a real file descriptor successfully
binder.ko can be unloaded cleanly during development
```
