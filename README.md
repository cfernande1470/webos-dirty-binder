# webos-dirty-binder

USB-only Android sidecar experiments for rooted LG webOS TVs.

This repository is about running enough Android userspace on an LG webOS TV to experiment with Android Binder, `servicemanager`, Android rootfs mounting, and later Android framework bring-up. The current target observed in testing is:

```text
LG webOS TV kernel: Linux 4.4.84-229.1.kavir.2 aarch64
Host controller: NanoPi R3S
TV root access: root@192.168.2.121
Android userspace: Waydroid/Lineage Android 13 arm64-only images
Storage: USB ext4
```

The current project status is **USB-only rootfs + real Android `servicemanager` working on `/dev/binder`**. `hwservicemanager` is not working yet because the current Binder module only exposes `/dev/binder`; Android modern HAL services need `/dev/hwbinder` and usually `/dev/vndbinder` as separate Binder devices.

---

## Current state

### Working

- USB ext4 storage is used for Android images, Android rootfs mount points, Android `/data`, Android `/cache`, downloads, logs and sidecar files.
- Android `system.img` and `vendor.img` are downloaded directly on the TV into the USB.
- Android rootfs is assembled under:

```text
/tmp/android-usb/android-rootfs
```

- Android `/system`, `/vendor`, `/apex`, `/data`, `/cache`, `/proc`, `/sys` and `/dev` are mounted into that rootfs.
- Android 13 `/system/bin/toybox` executes in `chroot`.
- The real Android 13 `/system/bin/servicemanager` starts and stays running when `binder.ko` is loaded with the required kernel symbol addresses.
- Binder `mmap()` works after resolving `sym_get_vm_area`.
- `BINDER_SET_CONTEXT_MGR_EXT` is reached by the real Android `servicemanager`.

### Not working yet

- `/dev/hwbinder` is missing.
- `/dev/vndbinder` is missing.
- `hwservicemanager` exits because it cannot open its Binder driver.
- `vndservicemanager` is not present in the current Android image layout.
- `/linkerconfig/ld.config.txt` is not generated yet. This currently prints warnings but does not block `toybox` or `servicemanager`.
- Full Android boot, property service, zygote, system_server and HAL bring-up are not done.

---

## The important discovery: real Android servicemanager works

Earlier attempts failed with:

```text
Binder driver '/dev/binder' could not be opened.
Using /dev/binder failed: unable to mmap transaction memory.
binder_dirty: missing address for get_vm_area
```

The cause was not the USB and not the Android image. The cause was that the dirty Binder module was loaded without the kernel symbol addresses it needs for its shims.

Bad state:

```text
/sys/module/binder/parameters/sym_get_vm_area = 0
/sys/module/binder/parameters/sym___alloc_fd = 0
/sys/module/binder/parameters/sym___fd_install = 0
/sys/module/binder/parameters/sym___close_fd = 0
/sys/module/binder/parameters/sym_get_files_struct = 0
/sys/module/binder/parameters/sym_put_files_struct = 0
```

Fixed state:

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

The working loader resolves these from `/proc/kallsyms` on the TV and passes them to `insmod`.

Example symbols observed on the target:

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

After that, Binder `mmap()` succeeds and Android's real `servicemanager` stays alive:

```text
SERVICEMANAGER_STILL_RUNNING_AFTER_3S=YES
DIRTY_BINDER_IOCTL_COMPAT_V0 set context mgr ext type=0x0 flags=0x0
ANDROID_REAL_SERVICEMANAGER_RUNNING
```

This is not the mini service manager shim. It is the Android binary from:

```text
/tmp/android-usb/android-rootfs/system/bin/servicemanager
```

---

## Why hwservicemanager still fails

The current Binder module only creates:

```text
/dev/binder
```

The module ignores:

```text
devices=binder,hwbinder,vndbinder
```

The kernel log shows:

```text
binder: unknown parameter 'devices' ignored
```

`hwservicemanager` therefore fails with:

```text
Binder driver could not be opened. Terminating.
```

Do **not** fix this by symlinking or duplicating `/dev/binder` as `/dev/hwbinder`. Android needs distinct Binder contexts. `servicemanager` and `hwservicemanager` are separate context managers and must not share the same Binder device.

The next serious kernel/module patch is to add true multi-device Binder support:

```text
/dev/binder
/dev/hwbinder
/dev/vndbinder
```

or backport a Binder implementation that supports:

```text
CONFIG_ANDROID_BINDER_DEVICES="binder,hwbinder,vndbinder"
```

---

## Repository layout after cleanup

Only three public Android USB scripts are kept:

```text
scripts/install-android-usb.sh
scripts/tail-android-usb.sh
scripts/diagnose-android-usb.sh
```

Supporting build scripts such as `scripts/build-module.sh` and `scripts/build-sidecar.sh` are kept if present.

Config:

```text
configs/android-usb.env
```

Main documentation:

```text
README.md
```

---

## Quick start

From the NanoPi:

```sh
cd /home/pi/disk/webos-dirty-binder
```

Check or edit config:

```sh
cat configs/android-usb.env
```

Typical defaults:

```sh
TV_IP=192.168.2.121
ANDROID_USB_PART=/dev/sda1
ANDROID_USB_MOUNT=/tmp/android-usb
```

### 1. Install without formatting

```sh
TV_IP=192.168.2.121 ./scripts/install-android-usb.sh
```

### 2. Tail progress

```sh
TV_IP=192.168.2.121 ./scripts/tail-android-usb.sh
```

### 3. Diagnose

```sh
TV_IP=192.168.2.121 ./scripts/diagnose-android-usb.sh
```

---

## Formatting the USB

Formatting is intentionally protected.

This destroys the selected USB partition:

```sh
TV_IP=192.168.2.121 \
ANDROID_USB_PART=/dev/sda1 \
FORMAT_USB=1 \
CONFIRM_FORMAT_ANDROID_USB=YES \
./scripts/install-android-usb.sh
```

The installer formats the partition as ext4, mounts it at `/tmp/android-usb`, recreates the Android directory tree, downloads the Android images again, installs Binder, and mounts the Android rootfs.

---

## What the installer does

`scripts/install-android-usb.sh` does all of this:

1. Loads `configs/android-usb.env`.
2. Ensures the USB is mounted at a stable logical path.
3. Optionally formats the USB to ext4 if `FORMAT_USB=1 CONFIRM_FORMAT_ANDROID_USB=YES`.
4. Builds or locates `binder.ko`.
5. Copies `binder.ko` and optional static sidecar binaries to USB on the TV.
6. Starts a remote installation job on the TV.
7. Downloads Android `system.img` and `vendor.img` zip files if missing.
8. Extracts `system.img` and `vendor.img`.
9. Resolves required kernel symbols from `/proc/kallsyms`.
10. Loads `binder.ko` with those symbols.
11. Creates/fixes `/dev/binder`.
12. Mounts Android rootfs from USB.
13. Verifies `/system/bin/toybox`.
14. Starts the real Android `/system/bin/servicemanager` if `START_SERVICEMANAGER=1`.

---

## Environment variables

Common:

```sh
TV_IP=192.168.2.121
ANDROID_USB_PART=/dev/sda1
ANDROID_USB_MOUNT=/tmp/android-usb
```

Formatting:

```sh
FORMAT_USB=1
CONFIRM_FORMAT_ANDROID_USB=YES
```

Downloads:

```sh
FORCE_DOWNLOAD=1
SYSTEM_ZIP_URL=...
VENDOR_ZIP_URL=...
```

Binder:

```sh
REQUIRE_BINDER=1
ANDROID_BINDER_KO=/tmp/android-usb/android-sidecar/modules/binder.ko
```

Runtime:

```sh
START_SERVICEMANAGER=1
```

---

## Android USB directory layout

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

## Android image sources

Default images are Waydroid/Lineage Android 13 arm64-only images:

```text
lineage-20.0-20260403-VANILLA-waydroid_arm64_only-system.zip
lineage-20.0-20260403-MAINLINE-waydroid_arm64_only-vendor.zip
```

The installer can be pointed at other images through:

```sh
SYSTEM_ZIP_URL=...
VENDOR_ZIP_URL=...
```

---

## Binder details

The Binder module currently needs hidden/internal kernel symbols because it uses dirty shims for memory mapping and FD passing compatibility on the LG webOS 4.4 kernel.

The installer passes these module parameters:

```text
sym_get_vm_area
sym_map_kernel_range_noflush
sym_zap_page_range
sym___alloc_fd
sym___fd_install
sym___close_fd
sym_get_files_struct
sym_put_files_struct
sym___lock_task_sighand
```

The symbols are resolved dynamically:

```sh
awk -v n="$symbol" '$3 == n && $1 != "0000000000000000" { print "0x"$1; exit }' /proc/kallsyms
```

If `sym_get_vm_area=0`, `servicemanager` fails at Binder `mmap()`.

If the FD-related symbols are zero, Binder FD passing will probably fail later:

```text
sym___alloc_fd
sym___fd_install
sym___close_fd
sym_get_files_struct
sym_put_files_struct
```

The current successful state has all of those non-zero.

---

## Known warnings

### linkerconfig warning

This appears:

```text
linker: Warning: failed to find generated linker configuration from "/linkerconfig/ld.config.txt"
```

It does not currently block:

```text
/system/bin/toybox
/system/bin/servicemanager
```

It will matter later for richer Android userspace, HALs, zygote or system_server.

### getprop empty

This is expected at this stage:

```text
chroot /system/bin/getprop ro.build.version.release
```

The binary can run, but Android property service/init are not running.

---

## Validation checklist

Run:

```sh
TV_IP=192.168.2.121 ./scripts/diagnose-android-usb.sh
```

Good signs:

```text
/dev/sda1 /tmp/android-usb ext4 rw
android-rootfs/system mounted
android-rootfs/vendor mounted
android-rootfs/data ext4 rw
android-rootfs/cache ext4 rw
/dev/binder exists
sym_get_vm_area != 0
sym___alloc_fd != 0
sym___fd_install != 0
/system/bin/servicemanager exists
SERVICEMANAGER_STILL_RUNNING_AFTER_3S=YES
```

Expected current failure:

```text
HWSERVICEMANAGER_EXITED_QUICKLY=YES
Binder driver could not be opened. Terminating.
```

Reason:

```text
/dev/hwbinder is missing
```

---

## Development roadmap

### Phase 1: USB-only storage

Done.

- Move all Android state away from `/media/internal`.
- Use USB ext4.
- Keep `/tmp/android-usb` as stable mount path.
- Store images, downloads, rootfs mounts, data and cache on USB.

### Phase 2: Binder servicemanager

Done for `/dev/binder`.

- Load `binder.ko`.
- Resolve kernel symbols from `/proc/kallsyms`.
- Pass module parameters to avoid `mmap()` failure.
- Verify `BINDER_SET_CONTEXT_MGR_EXT`.
- Run real Android `/system/bin/servicemanager`.

### Phase 3: hwbinder/vndbinder

Pending.

- Patch or replace Binder module so it registers multiple independent Binder devices.
- Required devices:

```text
/dev/binder
/dev/hwbinder
/dev/vndbinder
```

- Do not fake these with symlinks.

### Phase 4: linkerconfig/property/init

Pending.

- Generate `/linkerconfig/ld.config.txt`.
- Provide minimal Android property environment.
- Decide whether to run Android `init` or a controlled mini-init.

### Phase 5: zygote/system_server

Pending.

- Requires functional Binder, hwbinder, property service, linkerconfig, cgroups/namespaces, SELinux strategy and likely more kernel compatibility.

---

## Git workflow

After applying cleanup and testing:

```sh
git status
git diff -- README.md configs scripts
git add README.md configs/android-usb.env scripts/install-android-usb.sh scripts/tail-android-usb.sh scripts/diagnose-android-usb.sh
git add -u scripts
git commit -m "usb-only android installer with binder symbol loader"
git push origin main
```

Before pushing, inspect removed scripts:

```sh
git status --short
```

The cleanup script backs up the previous `scripts/` and `configs/` directories under a timestamped backup directory before removing old Android USB helper scripts.

---

## Safety notes

- Formatting the USB is destructive.
- Always check `ANDROID_USB_PART` before formatting.
- Do not use `/media/internal` for Android rootfs/data/cache; it is too small.
- Do not create fake `/dev/hwbinder` or `/dev/vndbinder` by pointing them to `/dev/binder`.
- Do not treat `servicemanager` passing as full Android boot readiness. It is a major milestone, but HALs and framework still need more work.

