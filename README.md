# webOS Dirty Binder

Android-like Binder sidecar for LG webOS TVs.

This project brings a controlled subset of Android Binder semantics to LG webOS by loading a custom Binder kernel module, running a minimal Android-like ServiceManager, and building progressively higher-level IPC/runtime compatibility pieces around it.

The current direction is **not** to replace webOS. webOS remains the host operating system, compositor, driver stack, audio/video/input owner, and UI shell. Android runs as a sidecar userspace, staged on external USB storage, with Binder-compatible services and compatibility shims layered on top.

---

## Current status

The project has moved beyond a basic Binder smoke test. The current working stack includes:

- Custom Binder kernel module loaded on LG webOS.
- `/dev/binder` exposed and usable.
- Minimal ServiceManager-style registry.
- Binder handles, local Binder objects, callbacks, death notifications, lifecycle tests, one-way calls, AIDL-like Parcel transactions, stress tests, listener registries, stale handle recovery, and Binder-return-object tests.
- Direct Binder FD passing investigated and quarantined.
- Safe FD passing implemented through `SCM_RIGHTS` side channel.
- `ParcelFileDescriptor-lite` implemented on top of Binder token control + Unix socket FD transport.
- Android-like process preflight from `/media/internal/android-rootfs` verified.
- USB-backed Android rootfs staging added.
- Waydroid/LineageOS ARM64 `system.img` and `vendor.img` downloaded/extracted to USB.
- Synthetic Android rootfs created with `/system`, `/vendor`, `/apex`, `/dev`, `/proc`, `/sys`, `/data`, and `/cache`.
- Android linker works inside chroot.
- Android `/system/bin/toybox`, `/system/bin/sh`, and `getprop` execute inside the synthetic rootfs.

Most recent successful milestone markers:

```text
FD_BRIDGE_SMOKE_TV_OK
PARCELFD_LITE_SMOKE_TV_OK
ANDROID_USERSPACE_PREFLIGHT_V1_TV_OK
ANDROID_ROOTFS_BOOTSTRAP_V0_USB_DONE
ANDROID_SYNTH_ROOTFS_V1_OK
ANDROID_SYNTHETIC_ROOTFS_V1_DONE
```

---

## Big-picture architecture

```text
+-------------------------------------------------------------+
| LG webOS host                                                |
|                                                             |
|  - real boot chain                                           |
|  - compositor / window manager                               |
|  - display, audio, input, remote, network                    |
|  - proprietary LG services                                   |
|                                                             |
|  +----------------------+       +-------------------------+  |
|  | custom binder.ko      |       | webOS app/window layer  |  |
|  | /dev/binder           |       | future UI bridge        |  |
|  +----------+-----------+       +-------------------------+  |
|             |                                               |
|             v                                               |
|  +----------------------+                                    |
|  | mini_servicemgr       |                                    |
|  | Android-like registry |                                    |
|  +----------+-----------+                                    |
|             |                                               |
|             v                                               |
|  +----------------------+       +-------------------------+  |
|  | Binder control plane  |<----->| Android sidecar clients |  |
|  | handles/callbacks     |       | chroot/rootfs processes |  |
|  +----------+-----------+       +-------------------------+  |
|             |                                               |
|             v                                               |
|  +----------------------+                                    |
|  | ParcelFD-lite         |                                    |
|  | token over Binder     |                                    |
|  | FD over SCM_RIGHTS    |                                    |
|  +----------------------+                                    |
|                                                             |
+-------------------------------------------------------------+

External USB:

/media/internal/android-usb
  android-rootfs
  android-downloads
  android-images
  android-mounts
  android-data
  android-cache
```

The strategy is:

```text
Binder = control plane
SCM_RIGHTS = FD transport plane
webOS = hardware/UI host
Android = sidecar userspace
```

---

## Development environment

Typical development setup used so far:

```text
NanoPi R3S:
  repo path: ~/disk/webos-dirty-binder

LG webOS TV:
  ssh: root@192.168.2.121
  sidecar path: /media/internal/android-sidecar
  rootfs path: /media/internal/android-rootfs
  USB mount: /media/internal/android-usb
```

The scripts usually accept:

```bash
TV_IP=192.168.2.121
SIDE_DIR=/media/internal/android-sidecar
ROOTFS=/media/internal/android-rootfs
IMG_DIR=/media/internal/android-images
MNT_DIR=/media/internal/android-mounts
USB_DIR=/media/internal/android-usb
```

---

## Storage layout

Internal TV storage is too small for a real Android rootfs/image workflow.

Observed internal storage state:

```text
/dev/root        1.2G   1.2G     0   100% /
/dev/mmcblk0p56 2.7G   ~0.8G  ~1.9G /mnt/lg/appstore /media
```

Android images quickly require several gigabytes:

```text
android-downloads: ~768M
android-images:    ~1.7G
```

A USB stick was formatted and mounted as:

```text
/media/internal/android-usb
```

Final USB-backed layout:

```text
/media/internal/android-downloads -> /media/internal/android-usb/android-downloads
/media/internal/android-images    -> /media/internal/android-usb/android-images
/media/internal/android-mounts    -> /media/internal/android-usb/android-mounts
/media/internal/android-rootfs    -> /media/internal/android-usb/android-rootfs
```

Example successful USB state:

```text
/dev/sda1  14.7G total, 2.5G used, 11.4G available
```

Conclusion: **external USB storage is effectively required** for Android rootfs work on this TV.

---

## Binder module

The Binder module is loaded on the TV from:

```text
/media/internal/android-sidecar/modules/binder.ko
```

The loader resolves non-exported kernel symbols from `/proc/kallsyms` and passes them as module parameters:

```text
sym_zap_page_range
sym_put_files_struct
sym_get_vm_area
sym___fd_install
sym___close_fd
sym_map_kernel_range_noflush
sym___lock_task_sighand
sym_get_files_struct
sym___alloc_fd
```

Successful load example:

```text
binder 118784 0 [permanent], Live 0xffffffbffc35f000 (O)
/dev/binder -> char device major 10 minor 53
binder protocol_version=8
```

The module is usually permanent once loaded, so a TV reboot is required to load a newly built `binder.ko`.

---

## What works today

### ServiceManager / registry

`mini_servicemgr` works as a minimal Android-like service registry. It supports adding and resolving Binder services by name.

Representative services used during milestones:

```text
test.android.fdbridge
test.android.parcelfd
```

### Binder object basics

The project has working examples for:

- Opening `/dev/binder`.
- Binder mmap.
- Registering local Binder objects.
- Returning handles.
- Acquiring/releasing handles.
- Service lookup.
- Ping-style transactions.
- Handling `BR_TRANSACTION`, `BR_REPLY`, `BR_TRANSACTION_COMPLETE`, `BR_NOOP`, `BR_INCREFS`, `BR_ACQUIRE`, `BR_RELEASE`, and `BR_DECREFS`.

### Android-like AIDL / Parcel tests

Implemented and tested areas include:

- AIDL-like client/service Parcel calls.
- Negative/error calls.
- One-way calls.
- Callback services.
- Callback threadpool client.
- Listener registry.
- Unregister/recovery tests.
- Stale handle tests.
- Stress tests.
- Binder return object tests.
- Unique Binder return object stress/lifecycle tests.

The repository contains many tools/scripts around these milestones under:

```text
tools/
scripts/
docs/
```

---

## Direct Binder FD passing: quarantined

Direct `BINDER_TYPE_FD` was investigated in detail and is currently **disabled/quarantined** on this LG webOS TV.

Symptoms:

```text
client sends BINDER_TYPE_FD
kernel enters the Binder FD case
kernel returns BR_FAILED_REPLY before delivering the transaction
service never receives the real FD transaction
TV may freeze or reboot after the test
```

Observed kernel diagnostics:

```text
DIRTY_BINDER_FD_DIAG enter_fd_case line=1680
DIRTY_BINDER_FD_DIAG failed_reply_before line=1719
binder: transaction failed 29201, size 136-8
```

Interpretation:

```text
29201 == 0x7211 == BR_FAILED_REPLY
```

Important conclusion:

```text
Direct BINDER_TYPE_FD is unsafe on this TV.
Do not run direct Binder FD probes.
Use ParcelFD-lite / SCM_RIGHTS instead.
```

Quarantined direct-FD probes include:

```text
android_like_fd_object_client/service
android_like_fd_devnull_client/service
android_like_fd_passing_client/service
```

Replacement architecture:

```text
Binder transaction:
  token + metadata + control/status

Unix domain socket:
  SCM_RIGHTS carries the real FD

Receiver:
  matches token to received FD
  uses FD locally
  replies over Binder
```

---

## FD Bridge v0

FD Bridge v0 proves that FD transport works safely when moved out of Binder kernel FD passing.

Flow:

```text
client creates pipe with known payload
client sends read-end FD through Unix socket using SCM_RIGHTS
client sends token/control transaction over Binder
service receives FD on socket
service receives token over Binder
service matches token
service reads payload from FD
service replies over Binder
```

Target markers:

```text
FD_BRIDGE_SERVICE_SOCKET_READY
FD_BRIDGE_SERVICE_REGISTERED
FD_BRIDGE_CLIENT_SOCKET_SEND_OK
FD_BRIDGE_BINDER_CONTROL_OK
FD_BRIDGE_SERVICE_GOT_FD
FD_BRIDGE_SERVICE_READ_OK
FD_BRIDGE_CLIENT_BINDER_REPLY_OK
FD_BRIDGE_SMOKE_OK
FD_BRIDGE_SMOKE_TV_OK
```

Successful milestone result:

```text
FD_BRIDGE_SMOKE_TV_OK
```

Files:

```text
tools/fd_bridge_common.hpp
tools/fd_bridge_service.cpp
tools/fd_bridge_client.cpp
scripts/run-fd-bridge-smoke-tv.sh
```

---

## ParcelFileDescriptor-lite v0

`ParcelFileDescriptor-lite` wraps the FD Bridge behind an Android-like API shape.

Conceptual API:

```cpp
parcel_fd_lite_write_fd(socket_path, fd, kind, label, &token);
parcel_fd_lite_call_binder(binder_fd, handle, token, kind, text, &reply);
```

Internal behavior:

```text
write_fd:
  create token
  send FD through SCM_RIGHTS
  write token/kind into Binder payload

read_fd:
  read token/kind from Binder payload
  match token with SCM_RIGHTS-received FD
  return/use local FD
```

Target markers:

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

Successful milestone result:

```text
PARCELFD_LITE_SMOKE_TV_OK
```

Files:

```text
tools/parcel_fd_lite_common.hpp
tools/parcel_fd_lite_service.cpp
tools/parcel_fd_lite_client.cpp
scripts/run-parcel-fd-lite-smoke-tv.sh
```

---

## Android userspace preflight v1

This milestone proves that an Android-like process launched from the rootfs staging area can still talk to the sidecar Binder services.

Flow:

```text
host starts mini_servicemgr
host starts parcel_fd_lite_service
rootfs process opens /dev/binder
rootfs process resolves test.android.parcelfd
rootfs process sends FD using ParcelFD-lite
service reads FD payload
rootfs process receives Binder reply
```

Target markers:

```text
ANDROID_USERSPACE_PREFLIGHT_V1_STARTED
ANDROID_USERSPACE_PREFLIGHT_V1_BINDER_HANDLE_OK
ANDROID_USERSPACE_PREFLIGHT_V1_PARCELFD_WRITE_OK
ANDROID_USERSPACE_PREFLIGHT_V1_BINDER_REPLY_OK
ANDROID_USERSPACE_PREFLIGHT_V1_SMOKE_OK
ANDROID_USERSPACE_PREFLIGHT_V1_TV_OK
```

Successful milestone result:

```text
ANDROID_USERSPACE_PREFLIGHT_V1_TV_OK
```

Files:

```text
tools/android_userspace_preflight_v1.cpp
scripts/run-android-userspace-preflight-v1-tv.sh
```

---

## Android rootfs bootstrap v0

Android rootfs bootstrap downloads/extracts Waydroid/LineageOS ARM64 images to USB-backed storage.

This project is **not running Waydroid itself**. Waydroid is used as a convenient source of Android/LineageOS ARM64 `system.img` and `vendor.img` suitable for a Linux-hosted Android userspace model.

Downloaded/staged images:

```text
/media/internal/android-images/system.img
/media/internal/android-images/vendor.img
```

Download/extract storage:

```text
/media/internal/android-downloads
/media/internal/android-images
```

Mount staging:

```text
/media/internal/android-mounts
```

Successful USB-backed bootstrap marker:

```text
ANDROID_ROOTFS_BOOTSTRAP_V0_USB_DONE
```

---

## Android image layout findings

The Waydroid/Lineage images use the following relevant layout:

```text
system.img:
  /system/bin
  /system/lib64
  /system/apex
  /system/bin/toybox
  /system/bin/sh
  /system/bin/getprop -> toolbox
  /system/bin/app_process64
  /system/bin/servicemanager
  /system/bin/hwservicemanager
  /system/bin/surfaceflinger
  /system/bin/init
  /system/bin/linker64 -> /apex/com.android.runtime/bin/linker64
  /system/apex/com.android.runtime/bin/linker64

vendor.img:
  /bin
  /lib64
  /etc
  /build.prop
  /bin/sh
  /bin/getprop -> toolbox
  /bin/vndservicemanager
  /bin/hw/android.hardware.* services
```

A naive chroot failed because Android binaries expect `/apex/...` to exist. The fix was to create a synthetic rootfs with `/apex` bind-mounted from `system.img`.

---

## Android synthetic rootfs v1

Synthetic rootfs layout:

```text
/media/internal/android-rootfs
  /system -> bind mount from system.img:/system
  /vendor -> bind mount from vendor.img:/
  /apex   -> bind mount from system.img:/system/apex
  /dev    -> minimal device nodes, including binder
  /proc   -> procfs
  /sys    -> sysfs
  /data   -> USB-backed writable data
  /cache  -> USB-backed writable cache
```

Successful mount markers:

```text
ANDROID_SYNTH_ROOTFS_SYSTEM_RAW_MOUNT_OK
ANDROID_SYNTH_ROOTFS_VENDOR_RAW_MOUNT_OK
ANDROID_SYNTH_ROOTFS_BIND_SYSTEM_OK
ANDROID_SYNTH_ROOTFS_BIND_VENDOR_OK
ANDROID_SYNTH_ROOTFS_BIND_APEX_OK
```

Successful runtime markers:

```text
ANDROID_SYNTH_ROOTFS_TOYBOX_OK
ANDROID_SYNTH_ROOTFS_SH_OK_MARKER
ANDROID_SYNTH_ROOTFS_V1_OK
ANDROID_SYNTHETIC_ROOTFS_V1_DONE
```

Known warning:

```text
linker: Warning: failed to find generated linker configuration from "/linkerconfig/ld.config.txt"
```

This does not block simple binaries. It means Android `init`/`linkerconfig` has not generated the normal linker namespace config yet. Future work should generate or provide `/linkerconfig/ld.config.txt` before starting more complex daemons.

Files:

```text
scripts/run-android-synthetic-rootfs-v1-tv.sh
```

---

## Waydroid: what it means here

Waydroid is a Linux project that runs Android in a container-like environment. This project does **not** currently run Waydroid as-is.

Waydroid is used here only as a source for Android/LineageOS ARM64 images:

```text
system.img
vendor.img
```

Project-specific interpretation:

```text
Waydroid official:
  Linux host + container + Android images + integration stack

webOS Dirty Binder:
  webOS host + custom binder.ko + mini_servicemgr + ParcelFD-lite + USB Android images
```

This is closer to an Android sidecar than to a full Android TV ROM.

---

## Why not flash Android TV?

Replacing webOS is not the current path.

Reasons:

- webOS owns the real compositor/windowing stack.
- webOS owns display, audio, input, remote, network, and proprietary TV services.
- Replacing webOS would require bootloader/kernel/display/audio/media/DRM/HAL work.
- Direct Binder FD passing is unstable in this kernel, so stock Android userspace would not work unmodified anyway.

Current path:

```text
Option B:
  keep webOS as host
  run Android-compatible userspace sidecar
  bridge missing pieces incrementally
```

---

## hwbinder / vndbinder status

Current TV state:

```text
/dev/binder    exists and works
/dev/hwbinder  missing
/dev/vndbinder missing
```

`hwbinder` and `vndbinder` are not implemented yet.

They should be addressed after:

1. Synthetic rootfs basic Android binaries work.
2. Android ServiceManager or service-manager shim strategy is clear.
3. `/linkerconfig` issue is handled.
4. The project knows whether to run Android `servicemanager`, keep `mini_servicemgr`, or bridge between them.

For now:

```text
Do not start HALs yet.
Do not start hwservicemanager yet unless in a contained smoke test.
Do not start zygote yet.
Do not start SurfaceFlinger yet.
```

---

## Current recommended next milestone

### Android servicemanager smoke v0

Goal:

```text
Run a controlled Android userspace daemon from the synthetic rootfs, starting with servicemanager or a safe service-manager probe, without starting zygote or SurfaceFlinger.
```

Questions to answer:

```text
Can Android /system/bin/servicemanager open our /dev/binder?
Does it understand this Binder protocol/module behavior?
Does it conflict with mini_servicemgr as context manager?
Can we run it only when mini_servicemgr is stopped?
If Android servicemanager fails, can mini_servicemgr remain the permanent shim?
```

Potential sequence:

```text
1. Keep synthetic rootfs mounted.
2. Stop mini_servicemgr.
3. Try Android /system/bin/servicemanager under chroot.
4. Capture logs and exit status.
5. If it becomes context manager, probe it carefully.
6. If it fails, continue using mini_servicemgr.
```

No zygote yet.

---

## Build and deploy basics

Build sidecar from NanoPi:

```bash
cd ~/disk/webos-dirty-binder
./scripts/build-sidecar.sh
```

Install to TV:

```bash
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
./scripts/install-sidecar-tv.sh
```

Load Binder on TV:

```bash
ssh root@192.168.2.121
cd /media/internal/android-sidecar
./load-binder-tv.sh /media/internal/android-sidecar/modules/binder.ko
```

Run ParcelFD-lite smoke:

```bash
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
./scripts/run-parcel-fd-lite-smoke-tv.sh
```

Run Android userspace preflight:

```bash
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
ROOTFS=/media/internal/android-rootfs \
./scripts/run-android-userspace-preflight-v1-tv.sh
```

Run synthetic Android rootfs:

```bash
TV_IP=192.168.2.121 \
ROOTFS=/media/internal/android-rootfs \
IMG_DIR=/media/internal/android-images \
MNT_DIR=/media/internal/android-mounts \
USB_DIR=/media/internal/android-usb \
./scripts/run-android-synthetic-rootfs-v1-tv.sh
```

---

## Safety rules

### Do not run direct Binder FD probes

Direct `BINDER_TYPE_FD` can freeze/reboot this TV.

Avoid:

```text
run-android-like-fd-object-smoke-tv.sh
run-android-like-fd-devnull-smoke-tv.sh
run-android-like-fd-passing-smoke-tv.sh
run-binder-fd-passing-tv.sh
```

Use instead:

```text
run-fd-bridge-smoke-tv.sh
run-parcel-fd-lite-smoke-tv.sh
```

### Do not write to internal partitions casually

Avoid touching:

```text
/dev/mmcblk0*
/mnt/lg/preload
/mnt/lg/appstore/preload
/var/palm
/webOS system partitions
```

Android staging should live on USB:

```text
/media/internal/android-usb
```

### Reboot after Binder module changes

The Binder module is permanent once loaded:

```text
binder ... [permanent]
```

Reboot TV before testing a newly built `binder.ko`.

---

## Useful scripts

Storage / setup:

```text
scripts/audit-android-rootfs-readiness-tv.sh
scripts/audit-tv-storage-for-android.sh
scripts/deep-tv-storage-map-busybox.sh
scripts/prepare-android-usb-tv.sh
scripts/cleanup-android-sidecar-tv.sh
```

Rootfs / Android image work:

```text
scripts/bootstrap-waydroid-rootfs-v0-tv.sh
scripts/bootstrap-waydroid-rootfs-v0-usb-tv.sh
scripts/inspect-android-rootfs-v1-tv.sh
scripts/inspect-android-rootfs-layout-v2-tv.sh
scripts/run-android-synthetic-rootfs-v1-tv.sh
```

FD bridge / ParcelFD:

```text
scripts/run-fd-bridge-smoke-tv.sh
scripts/run-parcel-fd-lite-smoke-tv.sh
```

Userspace preflight:

```text
scripts/run-android-userspace-preflight-v1-tv.sh
```

Build/deploy:

```text
scripts/build-sidecar.sh
scripts/install-sidecar-tv.sh
scripts/load-binder-tv.sh
```

---

## Milestone timeline

### Completed

```text
Binder module loads on LG webOS
/dev/binder usable
mini ServiceManager
Binder ping/echo
Lifecycle/death notification tests
Callback tests
Threadpool callback tests
AIDL-like Parcel tests
Negative/stale/recovery tests
One-way transaction tests
Stress tests
Binder return object tests
Direct Binder FD autopsy
Direct Binder FD quarantine
SCM_RIGHTS FD Bridge v0
ParcelFileDescriptor-lite v0
Android userspace preflight v1
USB Android staging
Waydroid/Lineage image bootstrap
Synthetic Android rootfs v1
```

### In progress / next

```text
Android servicemanager smoke v0
/linkerconfig generation or shim
Binder compatibility with Android native servicemanager
hwbinder/vndbinder strategy
minimal Android init subset or custom supervisor
webOS window bridge
graphics/audio/input bridges
```

### Not yet

```text
zygote
PackageManager
SurfaceFlinger
Android app launch
Android TV launcher
HAL stack
hwbinder/vndbinder full support
webOS replacement/flashing
```

---

## Current project philosophy

This project is not trying to immediately boot a stock Android TV ROM.

The project is building a compatibility sidecar:

```text
webOS stays alive
Android pieces are introduced one at a time
Binder semantics are preserved where safe
FD passing is emulated safely using SCM_RIGHTS
USB provides Android storage
UI/graphics will eventually be bridged through webOS
```

The immediate technical focus is to determine how much of Android userspace can run with:

```text
custom /dev/binder
mini_servicemgr or Android servicemanager shim
ParcelFD-lite
synthetic /system /vendor /apex rootfs
webOS as host
```

---

## Rename README_V2.md to README.md and push

After reviewing this file, rename it on the NanoPi and push to `main`:

```bash
cd ~/disk/webos-dirty-binder
set -euo pipefail

cp /path/to/README_V2.md README.md

git add -A
git commit -m "docs: refresh README with Android sidecar roadmap" || true

git switch main
git pull --ff-only origin main

git add -A
git commit -m "docs: refresh README with Android sidecar roadmap" || true

git push origin main
```

If working from a feature branch:

```bash
cd ~/disk/webos-dirty-binder
set -euo pipefail

FROM_BRANCH="$(git branch --show-current)"

git add -A
git commit -m "docs: refresh README with Android sidecar roadmap" || true

git push -u origin "$FROM_BRANCH"

git switch main
git pull --ff-only origin main

if [ "$FROM_BRANCH" != "main" ]; then
  git merge --no-ff "$FROM_BRANCH" -m "merge $FROM_BRANCH: README Android sidecar roadmap"
fi

git push origin main
```

---

## One-line current status

```text
webOS Dirty Binder now has a working Binder control plane, safe ParcelFD-lite FD transport, USB-backed Android/Waydroid images, and a synthetic Android rootfs capable of running basic Android binaries inside chroot on LG webOS.
```
