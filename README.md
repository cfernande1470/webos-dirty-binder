# webOS Dirty Binder Android Sidecar

> Experimental Android userspace sidecar for LG webOS TV, using a backported Binder kernel module, a ServiceManager shim, and a UNIX-domain FD bridge instead of direct `BINDER_TYPE_FD`.

## Project status

Current state: **Android userspace sidecar proof-of-life is working.**

The project has evolved from a Binder driver bring-up into a controlled Android sidecar architecture running on top of webOS, not replacing webOS. The TV remains the host operating system; Android components are mounted and executed from an external USB-backed rootfs.

The current working stack is:

```text
LG webOS TV host
  ├─ stock webOS compositor, drivers, audio, input, network
  ├─ custom/backported binder.ko
  ├─ /dev/binder
  ├─ mini_servicemgr compatibility shim
  ├─ ParcelFD-lite over SCM_RIGHTS
  └─ Android/Lineage userspace images mounted from USB
       ├─ /system
       ├─ /vendor
       ├─ /apex
       ├─ Android linker works
       ├─ Android toybox/sh/getprop work
       └─ /system/bin/service can see mini_servicemgr services
```

Latest confirmed milestones:

```text
Direct BINDER_TYPE_FD path quarantined                         ✅
SCM_RIGHTS FD Bridge v0                                       ✅
ParcelFileDescriptor-lite v0                                  ✅
Android userspace preflight v1                                ✅
USB-backed Waydroid/Lineage system/vendor images              ✅
Synthetic Android rootfs v1                                   ✅
Modern Binder ioctl compatibility for Android servicemanager  ✅
Android /system/bin/service sees mini_servicemgr services      ✅
```

## What this is

This project is an attempt to run selected Android userspace components on an LG webOS TV without flashing Android TV and without replacing webOS.

The guiding idea is:

```text
webOS stays in charge of hardware and UI.
Android runs as a sidecar userspace.
Binder is provided by our sidecar kernel module.
File descriptors are passed outside Binder using SCM_RIGHTS.
A ServiceManager shim exposes Android-like service discovery.
```

This is closer to a **Waydroid-style Android userspace sidecar** than to a traditional Android TV ROM. However, it is **not Waydroid proper**. We currently use Waydroid/Lineage images as a convenient source of ARM64 Android `system.img` and `vendor.img`, but we are not running the full Waydroid daemon/LXC stack.

## What this is not

This is not:

```text
an Android TV ROM
an attempt to flash/replace webOS
a complete Waydroid port yet
a complete Android boot
a zygote/system_server/SurfaceFlinger launch yet
a production-ready Android container
```

webOS remains essential because it owns:

```text
TV compositor/windowing
display pipeline
audio/video stack
remote/input handling
network integration
vendor drivers
power management
```

The realistic end goal is an Android sidecar integrated into a webOS window or service bridge, not a full replacement firmware.

## Hardware and paths used

Development machine:

```text
NanoPi-R3S
user: pi
project path: ~/disk/webos-dirty-binder
```

TV:

```text
root@192.168.2.121
LG webOS TV
aarch64 kernel 4.4.84 family
```

Sidecar install path on TV:

```text
/media/internal/android-sidecar
```

USB-backed Android storage:

```text
/media/internal/android-usb
/media/internal/android-usb/android-rootfs
/media/internal/android-usb/android-images
/media/internal/android-usb/android-downloads
/media/internal/android-usb/android-mounts
/media/internal/android-usb/android-data
/media/internal/android-usb/android-cache
```

Compatibility symlinks:

```text
/media/internal/android-rootfs    -> /media/internal/android-usb/android-rootfs
/media/internal/android-images    -> /media/internal/android-usb/android-images
/media/internal/android-downloads -> /media/internal/android-usb/android-downloads
/media/internal/android-mounts    -> /media/internal/android-usb/android-mounts
```

USB became necessary because the TV internal appstore partition is too small for Android images. The USB test environment currently uses an ext filesystem mounted at:

```text
/media/internal/android-usb
```

## Why USB is required

The TV internal storage was not large enough for safe extraction of Android images.

Observed internal space after early bootstrap:

```text
/dev/mmcblk0p56  2.7G total, about 1.6-2.0G free depending on cleanup
```

Waydroid/Lineage images require substantially more space:

```text
system.zip       ~700-800 MB compressed
system.img       ~1.5 GB extracted
vendor.img       ~225 MB
working mounts   additional metadata/cache space
```

After moving Android staging to USB, the layout became practical:

```text
/dev/sda1 /media/internal/android-usb  ~14.7G total, ~11G+ free after images
```

## Repository layout

Important areas:

```text
build/linux-4.4.84/drivers/android/binder.c
build/linux-4.4.84/include/uapi/linux/android/binder.h

tools/
  mini_servicemgr / sidecar Binder helpers
  fd_bridge_* files
  parcel_fd_lite_* files
  android_userspace_preflight_v1.cpp

scripts/
  build-sidecar.sh
  install-sidecar-tv.sh
  load-binder-tv.sh
  run-fd-bridge-smoke-tv.sh
  run-parcel-fd-lite-smoke-tv.sh
  run-android-userspace-preflight-v1-tv.sh
  prepare-android-usb-tv.sh
  bootstrap-waydroid-rootfs-v0-usb-tv.sh
  inspect-android-rootfs-v1-tv.sh
  inspect-android-rootfs-layout-v2-tv.sh
  run-android-synthetic-rootfs-v1-tv.sh
  run-android-real-servicemanager-v0-tv.sh
  collect-android-real-sm-v0-failure-tv.sh
  diag-android-real-servicemanager-names-tv.sh
  run-android-service-tool-mini-smoke-tv.sh
  ensure-android-usb-mounted-tv.sh

docs/
  milestone and diagnostic notes
```

## Binder module status

The Binder module is a backported/customized `binder.ko` for the LG webOS kernel environment.

Confirmed working:

```text
/dev/binder creation
BINDER_VERSION protocol query
BINDER_SET_MAX_THREADS
BINDER_SET_CONTEXT_MGR legacy path
BINDER_WRITE_READ
BR_TRANSACTION / BC_TRANSACTION flow
BR_REPLY flow
local service registration with mini_servicemgr
Android client tools talking to mini_servicemgr
```

Modern ioctl compatibility was added for Android userspace:

```text
BINDER_ENABLE_ONEWAY_SPAM_DETECTION  ioctl 0x40046210
BINDER_SET_CONTEXT_MGR_EXT           ioctl 0x4018620d
```

These now log:

```text
DIRTY_BINDER_IOCTL_COMPAT_V0 oneway spam detection noop enable=1
DIRTY_BINDER_IOCTL_COMPAT_V0 set context mgr ext type=0x0 flags=0x0
```

This fixed the earlier `-EINVAL` failures from Android's real `servicemanager` startup path.

## Direct Binder FD path status

Direct `BINDER_TYPE_FD` is **quarantined**.

Early FD experiments showed that Android-like direct FD passing through Binder can freeze or reboot the TV. The failure signature included failed Binder replies and unstable allocator behavior in the FD transaction path.

Important conclusion:

```text
Do not use direct BINDER_TYPE_FD on this TV/kernel path.
Use SCM_RIGHTS for real FD transport.
Use Binder only for control/token exchange.
```

The direct Binder FD test scripts were intentionally quarantined to avoid freezing the TV.

## FD Bridge v0

The first safe solution for FD passing was the FD Bridge:

```text
client sends real FD over UNIX socket using SCM_RIGHTS
client sends token over Binder
service receives token over Binder
service matches token to FD received by socket
service reads FD
service replies by Binder
```

Confirmed markers:

```text
FD_BRIDGE_CLIENT_SOCKET_SEND_OK
FD_BRIDGE_BINDER_CONTROL_OK
FD_BRIDGE_SERVICE_GOT_FD
FD_BRIDGE_SERVICE_READ_OK
FD_BRIDGE_CLIENT_BINDER_REPLY_OK
FD_BRIDGE_SMOKE_OK
FD_BRIDGE_SMOKE_TV_OK
```

## ParcelFileDescriptor-lite v0

ParcelFD-lite wraps the FD Bridge into an Android-like abstraction:

```text
parcel_fd_lite_write_fd(fd)
  creates token
  sends FD by SCM_RIGHTS
  writes token/kind into Binder payload

parcel_fd_lite_read_fd(token)
  receives Binder token
  matches SCM_RIGHTS FD
  returns local FD
```

Confirmed markers:

```text
PARCELFD_LITE_TOKEN_ENCODE_OK
PARCELFD_LITE_SOCKET_SEND_OK
PARCELFD_LITE_WRITE_FD_OK
PARCELFD_LITE_BINDER_CONTROL_OK
PARCELFD_LITE_READ_FD_OK
PARCELFD_LITE_PAYLOAD_READ_OK
PARCELFD_LITE_CLIENT_BINDER_REPLY_OK
PARCELFD_LITE_SMOKE_OK
PARCELFD_LITE_SMOKE_TV_OK
```

This is the current safe FD transport layer for Android sidecar work.

## Android userspace preflight v1

A static preflight binary was run from an Android-like rootfs directory and confirmed that a process launched from that rootfs can:

```text
open /dev/binder
query mini_servicemgr
resolve test.android.parcelfd
send an FD through ParcelFD-lite
receive a Binder reply
```

Confirmed marker:

```text
ANDROID_USERSPACE_PREFLIGHT_V1_TV_OK
```

## Android image source

Waydroid/Lineage ARM64 images are used as Android userspace material:

```text
system.img
vendor.img
```

This project does not currently run Waydroid itself. The images are used because they provide Android userspace components designed to run above a Linux kernel, which matches the sidecar direction better than a flashable Android TV ROM.

Current images are stored on USB:

```text
/media/internal/android-images/system.img
/media/internal/android-images/vendor.img
```

## Synthetic Android rootfs

The Waydroid/Lineage `system.img` layout is not a simple `/system/bin` subdirectory mount. The useful layout is:

```text
system.img mounted raw:
  /system/bin
  /system/lib64
  /system/apex
  /system/product
  /system/system_ext

vendor.img mounted raw:
  /bin
  /lib64
  /etc
  /build.prop
```

A synthetic rootfs is created at:

```text
/media/internal/android-rootfs
```

with bind mounts:

```text
/system -> system_raw/system
/vendor -> vendor_raw
/apex   -> system_raw/system/apex
/data   -> USB-backed writable data
/cache  -> USB-backed writable cache
/proc   -> procfs
/sys    -> sysfs
/dev    -> minimal dev nodes including binder
```

Confirmed working inside chroot:

```text
/system/bin/toybox true
/system/bin/toybox uname -a
/system/bin/sh -c 'echo ...'
/system/bin/getprop
/apex/com.android.runtime/bin/linker64
```

Confirmed markers:

```text
ANDROID_SYNTH_ROOTFS_SYSTEM_RAW_MOUNT_OK
ANDROID_SYNTH_ROOTFS_VENDOR_RAW_MOUNT_OK
ANDROID_SYNTH_ROOTFS_BIND_SYSTEM_OK
ANDROID_SYNTH_ROOTFS_BIND_VENDOR_OK
ANDROID_SYNTH_ROOTFS_BIND_APEX_OK
ANDROID_SYNTH_ROOTFS_TOYBOX_OK
ANDROID_SYNTH_ROOTFS_SH_OK_MARKER
ANDROID_SYNTH_ROOTFS_V1_OK
ANDROID_SYNTHETIC_ROOTFS_V1_DONE
```

Known warning:

```text
linker: Warning: failed to find generated linker configuration from "/linkerconfig/ld.config.txt"
```

This does not block simple binaries. It will matter later for more complex Android services.

## Real Android servicemanager status

The real Android `/system/bin/servicemanager` now starts and remains alive after modern Binder ioctl compatibility was added.

Confirmed:

```text
ANDROID_REAL_SM_STARTED
ANDROID_REAL_SM_PROCESS_ALIVE
DIRTY_BINDER_IOCTL_COMPAT_V0 oneway spam detection noop
DIRTY_BINDER_IOCTL_COMPAT_V0 set context mgr ext
```

However, it does **not currently register and return external services correctly**.

Observed behavior:

```text
ParcelFD-lite service calls addService
service side receives a BR_REPLY
service believes registration completed
client getService returns null handle
Android service list shows only:
  manager: [android.os.IServiceManager]
```

Tested names included:

```text
test.android.parcelfd
activity
package
media.metrics
gpu
surfaceflinger
```

All failed to return a usable handle through the real Android `servicemanager`.

### Does this mean real servicemanager is impossible?

No. It means the next gap is not basic Binder transport anymore; it is likely one of:

```text
Android servicemanager policy/service_contexts restrictions
missing SELinux/service_manager behavior
addService reply/status not being parsed correctly by our libbinder-lite
modern IServiceManager protocol mismatch
missing linkerconfig/properties affecting servicemanager behavior
```

The important point: the kernel-level modern ioctl blocker was fixed. The remaining problem is higher-level Android ServiceManager compatibility.

### Why not keep using real servicemanager now?

Because `mini_servicemgr` already works and Android `/system/bin/service` can see services registered in it.

Current practical decision:

```text
Use mini_servicemgr as the active ServiceManager shim.
Keep real servicemanager as a diagnostic/compatibility target.
Do not block Android sidecar progress on real servicemanager yet.
```

## mini_servicemgr status

`mini_servicemgr` is now the main compatibility shim.

Confirmed:

```text
ParcelFD-lite service registers into mini_servicemgr
ParcelFD-lite native client resolves and calls it
Android /system/bin/service list/check can see the service
```

Latest confirmed markers:

```text
ANDROID_SERVICE_TOOL_MINI_NATIVE_PFD_OK
ANDROID_SERVICE_TOOL_MINI_ANDROID_CLIENT_SEES_SERVICE
ANDROID_SERVICE_TOOL_MINI_SMOKE_DONE
```

This is a major milestone: an Android userspace tool from the mounted Android rootfs can talk to our ServiceManager shim.

## Why mini_servicemgr is acceptable

Android itself primarily needs a Binder context manager that can answer expected `IServiceManager` transactions. It does not strictly require the exact stock `servicemanager` binary if the shim behaves compatibly enough for the components we want to run.

Advantages of mini_servicemgr:

```text
simple and observable
under our control
already supports tested AOSP-style add/get/list/check flows
works with our backported Binder
works with ParcelFD-lite
can be extended incrementally
avoids Android SELinux policy/service_contexts blockers for now
```

Disadvantages:

```text
not full Android servicemanager yet
may miss newer IServiceManager transactions
may not implement declared service policies
may not satisfy system_server/zygote expectations yet
needs more protocol coverage as Android components become more complex
```

This is acceptable for the sidecar path because the project is not trying to boot full Android immediately.

## Current architecture decision

Current winning architecture:

```text
webOS host
  └─ custom Binder sidecar
       ├─ binder.ko
       ├─ mini_servicemgr as context manager
       ├─ ParcelFD-lite for FD passing
       └─ Android userspace chroot from USB
```

Real Android `servicemanager` remains a future compatibility target, not the current default.

## How to prepare after TV reboot

After every TV reboot, the USB may need to be remounted and symlinks restored:

```bash
TV_IP=192.168.2.121 ./scripts/ensure-android-usb-mounted-tv.sh
```

Expected marker:

```text
ANDROID_USB_READY_AFTER_REBOOT
```

Then re-establish synthetic rootfs mounts:

```bash
TV_IP=192.168.2.121 \
ROOTFS=/media/internal/android-rootfs \
IMG_DIR=/media/internal/android-images \
MNT_DIR=/media/internal/android-mounts \
USB_DIR=/media/internal/android-usb \
./scripts/run-android-synthetic-rootfs-v1-tv.sh
```

Expected markers:

```text
ANDROID_SYNTH_ROOTFS_V1_OK
ANDROID_SYNTHETIC_ROOTFS_V1_DONE
```

## Core smoke tests

### ParcelFD-lite smoke

```bash
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
./scripts/run-parcel-fd-lite-smoke-tv.sh
```

Expected:

```text
PARCELFD_LITE_SMOKE_TV_OK
```

### Android userspace preflight

```bash
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
ROOTFS=/media/internal/android-rootfs \
./scripts/run-android-userspace-preflight-v1-tv.sh
```

Expected:

```text
ANDROID_USERSPACE_PREFLIGHT_V1_TV_OK
```

### Synthetic Android rootfs

```bash
TV_IP=192.168.2.121 \
ROOTFS=/media/internal/android-rootfs \
IMG_DIR=/media/internal/android-images \
MNT_DIR=/media/internal/android-mounts \
USB_DIR=/media/internal/android-usb \
./scripts/run-android-synthetic-rootfs-v1-tv.sh
```

Expected:

```text
ANDROID_SYNTH_ROOTFS_V1_OK
ANDROID_SYNTHETIC_ROOTFS_V1_DONE
```

### Android service tool against mini_servicemgr

```bash
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
ROOTFS=/media/internal/android-rootfs \
SERVICE=test.android.parcelfd \
./scripts/run-android-service-tool-mini-smoke-tv.sh
```

Expected:

```text
ANDROID_SERVICE_TOOL_MINI_NATIVE_PFD_OK
ANDROID_SERVICE_TOOL_MINI_ANDROID_CLIENT_SEES_SERVICE
ANDROID_SERVICE_TOOL_MINI_SMOKE_DONE
```

## Build and install

Typical build/install:

```bash
cd ~/disk/webos-dirty-binder
set -euo pipefail

./scripts/build-sidecar.sh

TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
./scripts/install-sidecar-tv.sh
```

Important: `binder.ko` is effectively permanent once loaded. To test a new Binder module, install it and reboot the TV:

```bash
ssh root@192.168.2.121 'sync; reboot' || true
```

After reboot:

```bash
TV_IP=192.168.2.121 ./scripts/ensure-android-usb-mounted-tv.sh
```

## How to verify patched binder.ko

Local:

```bash
strings build/linux-4.4.84/drivers/android/binder.ko | grep DIRTY_BINDER_IOCTL_COMPAT_V0
```

TV-installed:

```bash
ssh root@192.168.2.121 \
  'strings /media/internal/android-sidecar/modules/binder.ko | grep DIRTY_BINDER_IOCTL_COMPAT_V0'
```

Runtime dmesg:

```bash
ssh root@192.168.2.121 \
  "dmesg | grep -Ei 'DIRTY_BINDER_IOCTL_COMPAT_V0|ioctl 40046210|ioctl 4018620d' | tail -80"
```

Expected:

```text
DIRTY_BINDER_IOCTL_COMPAT_V0 oneway spam detection noop enable=1
DIRTY_BINDER_IOCTL_COMPAT_V0 set context mgr ext type=0x0 flags=0x0
```

## Known risks

### Direct FD path can freeze/reboot the TV

Do not run direct `BINDER_TYPE_FD` smoke tests unless intentionally debugging that dangerous path. Use ParcelFD-lite instead.

### binder.ko cannot be unloaded normally

The module is loaded as permanent/live. Reboot the TV to load a new version.

### USB mount is not persistent yet

After reboot, run:

```bash
TV_IP=192.168.2.121 ./scripts/ensure-android-usb-mounted-tv.sh
```

### Android linkerconfig is missing

Simple binaries work, but complex Android services may need generated `/linkerconfig/ld.config.txt`.

Current warning:

```text
linker: Warning: failed to find generated linker configuration from "/linkerconfig/ld.config.txt"
```

### Real Android servicemanager is not usable yet

It starts, but does not return externally added services correctly. Continue using `mini_servicemgr`.

## Troubleshooting

### `mkdir: can't create directory '/media/internal/android-rootfs': No such file or directory`

Usually the USB is not mounted after reboot.

Run:

```bash
TV_IP=192.168.2.121 ./scripts/ensure-android-usb-mounted-tv.sh
```

### `LOCAL_KO_MISSING_IOCTL_COMPAT_STRING`

The Binder module was not rebuilt from patched source.

Check:

```bash
strings build/linux-4.4.84/drivers/android/binder.ko | grep DIRTY_BINDER_IOCTL_COMPAT_V0
```

If missing, force rebuild the Binder module and reinstall.

### Android servicemanager returns null handles

Current known limitation. Use `mini_servicemgr`.

### `Too many levels of symbolic links` in Android chroot

The `system.img` layout was mounted incorrectly. Use synthetic rootfs with `/system`, `/vendor`, and `/apex` bind mounts.

### `No such file or directory` for Android binaries that exist

Usually dynamic linker/APEX layout issue. Ensure `/apex` is bind-mounted from `system_raw/system/apex`.

## Why real servicemanager failed but mini_servicemgr works

The stock Android `servicemanager` is more than a simple registry. It expects modern Android environment pieces:

```text
newer Binder ioctls
service_contexts / service policy
possibly SELinux service_manager checks
modern IServiceManager protocol details
linkerconfig/properties setup
```

The kernel ioctl part is now fixed. The remaining failure is likely policy/protocol/environment.

`mini_servicemgr` works because it implements the subset we need directly and avoids policy restrictions for now. Android `/system/bin/service` can already see it, which proves the shim approach is viable.

## Roadmap

Recommended next milestones:

### 1. Android ServiceManager shim v1

Goal:

```text
harden mini_servicemgr as the default Android ServiceManager shim
implement more IServiceManager transactions
improve service list/check/add/get behavior
add better status parsing and diagnostics
```

### 2. Android tool smoke set

Try low-risk Android tools:

```text
/system/bin/service list
/system/bin/service check <name>
/system/bin/logcat --help
/system/bin/cmd --help
/system/bin/dumpsys --help
/system/bin/app_process64 --help
```

### 3. linkerconfig/properties bootstrap

Create enough `/linkerconfig`, `default.prop`, and property service behavior for more Android binaries.

### 4. hwbinder/vndbinder decision

Do not implement too early. Add only when a real component requires:

```text
/dev/hwbinder
/dev/vndbinder
hwservicemanager
vndservicemanager
HAL registration
```

### 5. Graphics/window bridge

Because webOS remains the host, the long-term UI path likely needs:

```text
webOS app/window shell
Android buffer/graphics bridge
input bridge
possibly software rendering first
later hardware acceleration investigation
```

### 6. Android runtime escalation

Only after service shim, properties, linkerconfig, logging, and basic tools are stable:

```text
try selected native Android daemons
then app_process experiments
then zygote/system_server research
```

Do not start `zygote`, `system_server`, or `surfaceflinger` yet.

## Current recommended default stack

```text
binder.ko with modern ioctl compat
mini_servicemgr as context manager
ParcelFD-lite for FD transport
Android synthetic rootfs from USB
Waydroid/Lineage system/vendor images
Android /system/bin/service for introspection
```

This is the current stable base for further development.

## Quick recovery sequence after reboot

```bash
cd ~/disk/webos-dirty-binder
set -euo pipefail

TV_IP=192.168.2.121 ./scripts/ensure-android-usb-mounted-tv.sh

TV_IP=192.168.2.121 \
ROOTFS=/media/internal/android-rootfs \
IMG_DIR=/media/internal/android-images \
MNT_DIR=/media/internal/android-mounts \
USB_DIR=/media/internal/android-usb \
./scripts/run-android-synthetic-rootfs-v1-tv.sh

TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
ROOTFS=/media/internal/android-rootfs \
SERVICE=test.android.parcelfd \
./scripts/run-android-service-tool-mini-smoke-tv.sh
```

Expected final markers:

```text
ANDROID_USB_READY_AFTER_REBOOT
ANDROID_SYNTHETIC_ROOTFS_V1_DONE
ANDROID_SERVICE_TOOL_MINI_NATIVE_PFD_OK
ANDROID_SERVICE_TOOL_MINI_ANDROID_CLIENT_SEES_SERVICE
ANDROID_SERVICE_TOOL_MINI_SMOKE_DONE
```

## Summary

The project is no longer just a Binder bring-up. It is now a working foundation for an Android sidecar on webOS:

```text
Binder works.
Direct Binder FD path is unsafe and quarantined.
SCM_RIGHTS FD passing works.
ParcelFD-lite works.
Android userspace rootfs works from USB.
Android linker and shell work.
Modern Binder ioctls needed by Android servicemanager are handled.
Real Android servicemanager starts but is not usable yet for external services.
mini_servicemgr is the current correct shim and is visible to Android /system/bin/service.
```

The next engineering step is to evolve `mini_servicemgr` into a more complete Android ServiceManager shim, then gradually run more Android userspace components without trying to boot the full Android framework too early.
