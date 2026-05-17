# webos-dirty-binder

Android-like Binder sidecar for LG webOS / Linux 4.4.84.

This project brings up a Binder-compatible IPC stack on LG webOS without replacing webOS. It uses a custom Binder kernel module, a small userspace ServiceManager, low-level Binder probes, a lightweight C++ Binder layer, Android/AIDL-like client/server binaries, lifecycle validation, death notifications, callback/listener flows, returned Binder objects, one-way transactions, stress tests, and an Android userspace preflight path.

> Status: this is **not Android yet** and it is **not a production runtime**. It is a validated Binder/Android-like sidecar foundation for experiments on devices you own and can recover.

Tested development setup:

```text
NanoPi R3S workspace: ~/disk/webos-dirty-binder
LG webOS TV target:   root@192.168.2.121
Sidecar install dir:  /media/internal/android-sidecar
Android rootfs dir:  /media/internal/android-rootfs
```

All IPs and paths are configurable through environment variables such as `TV_IP`, `SIDE_DIR`, and `ANDROID_ROOTFS`.

---

## What works

The current sidecar stack validates:

- `/dev/binder` creation and Binder module loading on LG webOS.
- Binder protocol probing, `mmap`, `BINDER_SET_MAX_THREADS`, and low-level transactions.
- AOSP-style ServiceManager basics: context manager, `addService`, `getService`, `checkService`, `listServices`.
- Android-like C++ façade concepts: `String16`, `Parcel`, `IBinder`, `IServiceManager`, proxy/stub style interfaces.
- Service lifecycle: acquire, release, stale handle detection, restart recovery, concurrent lifecycle stress.
- Binder death-recipient flow: request, receive, acknowledge, clear/unlink.
- Reverse Binder callbacks and listener-style Binder objects.
- AIDL-like Parcel framing: interface descriptor token, exception code, String16-style payloads, int32 payloads.
- AIDL-like service recovery, stale-handle recovery, negative/error semantics, and meta transactions.
- Binder objects returned from AIDL-like calls, including concurrent stress and lifecycle cleanup.
- Android/libbinder-compatible `PING_TRANSACTION` handling.
- AIDL-like one-way transactions and concurrent one-way stress.
- Android userspace preflight for a reversible sidecar rootfs.

---

## Current state by milestone

| Area | Status | Notes |
| --- | --- | --- |
| Core Binder sidecar | ✅ validated | Binder module, `/dev/binder`, probes, mini ServiceManager. |
| Android-like service v0 | ✅ validated | Echo service via lightweight Android-like client/server wrappers. |
| Lifecycle / restart | ✅ validated | Acquire/release, stale handles, service restart recovery. |
| Death notifications | ✅ validated | `BR_DEAD_BINDER`, `BC_DEAD_BINDER_DONE`, unlink/clear flow. |
| Binder callbacks | ✅ validated | Client passes local Binder object; service calls back into client. |
| Binder threadpool | ✅ validated | Callback dispatched through client Binder looper thread. |
| Callback stress | ✅ validated | Multi-client callback stress tested. |
| AIDL-like Parcel | ✅ validated | `echo(String)` and `add(int,int)` with exception-code replies. |
| AIDL-like stress | ✅ validated | 32 clients × 100 rounds validated in stress run. |
| Service death/recovery | ✅ validated | Re-registration and client recovery across repeated service deaths. |
| Stale handle recovery | ✅ validated | Long-lived client detects dead handle, releases, re-resolves, continues. |
| AIDL-like callback listener | ✅ validated | Listener object inside AIDL-like Parcel, invoked on looper thread. |
| Negative/error semantics | ✅ validated | Bad descriptor, unknown code, truncated Parcel, bad args, recovery call. |
| Meta transactions | ✅ validated | `INTERFACE_TRANSACTION` and normal calls after descriptor query. |
| Binder return object | ✅ validated | Factory returns child Binder handle; client calls returned object. |
| Return object stress/lifecycle | ✅ validated | Concurrent returned object use and observable ref lifecycle. |
| Unique returned objects | ✅ validated | Per-client unique returned Binder objects and exact cleanup. |
| FD passing | ⚠️ quarantined/experimental | `BINDER_TYPE_FD` is guarded by `BINDER_FD_PASSING_UNSAFE=1`; do not run by default. |
| Android `PING_TRANSACTION` | ✅ validated | Real libbinder-style ping transaction supported. |
| AIDL one-way | ✅ validated | `TF_ONE_WAY` notify flow and sync `getCount()` validation. |
| AIDL one-way stress | ✅ validated | 16 clients × 1000 one-way calls = 16000 service notifications. |
| Android userspace preflight | ✅ validated | Binder, memfd, eventfd, signalfd, epoll, tmpfs/proc/devpts, mount namespace. |
| Android rootfs / zygote | ⏳ not yet | Planned as reversible sidecar, not as a webOS replacement. |

---

## Architecture

```text
+-------------------------------+
| Android-like / AIDL-like app  |
|                               |
| client proxy / listener       |
| Parcel / String16 / handles   |
+---------------+---------------+
                |
                | libbinder-lite / raw Binder ioctls
                v
+---------------+---------------+
| /dev/binder                   |
| LG webOS kernel + binder.ko   |
+---------------+---------------+
                |
                v
+---------------+---------------+
| mini_servicemgr               |
|                               |
| context manager               |
| addService / getService       |
| death notification tracking   |
+---------------+---------------+
                |
                v
+---------------+---------------+
| Android-like services         |
|                               |
| echo/add services             |
| callback/listener services    |
| one-way services              |
| Binder-object factory         |
+-------------------------------+
```

The goal is to keep webOS as the host OS and run Android-compatible pieces as a reversible sidecar. The near-term direction is an Android-app-sidecar runtime launched from webOS, not replacing webOS or flashing the TV.

---

## Repository layout

```text
artifacts/
  Prebuilt and captured artifacts used during sidecar experiments.

configs/
  Local configuration and build/runtime inputs.

docs/
  Milestone notes, experiment reports, and captured result artifacts.

patches/
  Kernel/module patches and experimental diffs.

scripts/
  Build, install, load, smoke-test, stress-test, and preflight scripts.

src/
  Source support files.

tools/
  Low-level Binder probes, mini ServiceManager, libbinder-lite, and
  Android/AIDL-like service/client implementations.
```

Key scripts:

```text
scripts/build-sidecar.sh
scripts/install-sidecar-tv.sh
scripts/load-binder-tv.sh
scripts/quick-check-tv.sh
scripts/run-sidecar-all-smoke-tv.sh
scripts/run-android-userspace-preflight-tv.sh
```

Many milestone-specific smokes live under `scripts/run-*-tv.sh`.

---

## Build

From the NanoPi workspace:

```bash
cd ~/disk/webos-dirty-binder
./scripts/build-sidecar.sh
```

Expected output includes statically linked ARM64 sidecar binaries under `build/`, plus the Binder kernel module under the configured kernel build tree.

---

## Install to the TV

```bash
cd ~/disk/webos-dirty-binder

TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
./scripts/install-sidecar-tv.sh
```

Expected TV install path:

```text
/media/internal/android-sidecar
```

---

## Quick validation

```bash
cd ~/disk/webos-dirty-binder

TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
CLIENTS=1 \
ROUNDS=1 \
./scripts/quick-check-tv.sh
```

Typical final markers:

```text
BINDER_LIFECYCLE_V0_OK
BINDER_DEATH_NOTIFICATION_V0_OK
ALL_SIDECAR_SMOKE_OK
QUICK_CHECK_TV_OK
```

---

## Full smoke suite

```bash
cd ~/disk/webos-dirty-binder

TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
CLIENTS=1 \
ROUNDS=1 \
./scripts/run-sidecar-all-smoke-tv.sh
```

For stronger targeted runs, use the milestone-specific scripts, for example:

```bash
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
CLIENTS=16 \
ROUNDS=1000 \
./scripts/run-android-like-aidl-oneway-stress-tv.sh
```

Expected one-way stress marker:

```text
AIDL_LIKE_ONEWAY_STRESS_SMOKE_TV_OK
```

---

## Android userspace preflight

This checks whether the TV can host a reversible Android userspace/rootfs sidecar without replacing webOS.

```bash
cd ~/disk/webos-dirty-binder

TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
ANDROID_ROOTFS=/media/internal/android-rootfs \
./scripts/run-android-userspace-preflight-tv.sh
```

Expected markers:

```text
ANDROID_PREFLIGHT_BINDER_DEVICE_OK
ANDROID_PREFLIGHT_BINDER_VERSION_OK
ANDROID_PREFLIGHT_MEMFD_OK
ANDROID_PREFLIGHT_EVENTFD_OK
ANDROID_PREFLIGHT_SIGNALFD_OK
ANDROID_PREFLIGHT_EPOLL_OK
ANDROID_PREFLIGHT_TMPFS_MOUNT_OK
ANDROID_PREFLIGHT_PROC_MOUNT_OK
ANDROID_PREFLIGHT_DEVPTS_MOUNT_OK
ANDROID_PREFLIGHT_MOUNT_NS_OK
ANDROID_PREFLIGHT_OK
ANDROID_USERSPACE_PREFLIGHT_SMOKE_TV_OK
```

Known preflight result on the tested TV:

```text
/dev/binder      present
/dev/hwbinder    missing
/dev/vndbinder   missing
```

---

## FD passing quarantine

`BINDER_TYPE_FD` is not safe by default on the tested TV/kernel path.

Observed behavior during the FD milestone:

```text
client sent BINDER_TYPE_FD
kernel returned BR_FAILED_REPLY
service did not receive the FD transaction
TV reboot was observed around the experiment
```

The FD smoke is guarded and should return safely unless explicitly enabled:

```bash
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
./scripts/run-binder-fd-passing-tv.sh
```

Expected safe marker:

```text
BINDER_FD_PASSING_QUARANTINED
```

Only run the unsafe probe intentionally:

```bash
BINDER_FD_PASSING_UNSAFE=1 ROUNDS=1 ./scripts/run-binder-fd-passing-tv.sh
```

Do not increase `ROUNDS` unless one round passes cleanly and logs are understood.

---

## Roadmap

Near-term direction:

1. Keep webOS as the host.
2. Build a reversible Android app sidecar under `/media/internal/android-rootfs` or `/media/internal/android-sidecar`.
3. Start with native/bionic-compatible pieces before attempting framework services.
4. Keep FD passing isolated until the kernel path is fully understood.
5. Later explore Android `servicemanager`, selected bionic binaries, and finally `zygote`/`app_process` experiments.

Planned safe milestones:

```text
Android rootfs sidecar unpack
First bionic binary
Android servicemanager/client smoke
Android app sidecar launched by webOS app
zygote/app_process experiment
FD passing diagnosis and fix, isolated from default smokes
```

---

## Current limitations

This is still a deliberately small Binder-compatible sidecar, not a full AOSP userspace.

Known limitations:

- No full AOSP `libbinder` replacement.
- No generated AIDL compiler integration.
- No Java framework.
- No Android `zygote` or `system_server`.
- No SELinux integration.
- No `hwbinder` / `vndbinder` on the tested setup.
- FD passing is quarantined and not part of default validation.
- No production-grade init/service supervision.
- No graphics stack, gralloc, SurfaceFlinger, or Android app framework yet.

---

## Safety note

This project loads a custom kernel module on a TV and interacts directly with Binder ioctls. Use it only on devices you own, understand, and can recover. Keep SSH access available while testing. Do not run unsafe FD probes by accident.

---

## License

Add a `LICENSE` file before publishing this as reusable code. Until then, treat the code and artifacts as research material owned by the repository author.
