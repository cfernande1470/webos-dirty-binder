# webos-dirty-binder

Android-like Binder sidecar for LG webOS / Linux 4.4.84.

This project brings up a Binder-compatible IPC stack on LG webOS using a custom Binder kernel module, a small userspace service manager, low-level Binder probes, a `libbinder-lite` C++ layer, Android-like client/server wrappers, service lifecycle validation, restart recovery, concurrent stress tests, and Binder death-recipient notifications.

The project has been validated on an LG webOS TV reachable as:

```text
root@192.168.2.121
```

and developed from a NanoPi R3S workspace:

```text
pi@192.168.2.200:~/disk/webos-dirty-binder
```

The current `main` branch represents:

```text
Android-like Binder service v0
Binder lifecycle v0
Binder death notification v0
```

---

## Current status

The stack now validates:

```text
/dev/binder creation and Binder module load
Binder protocol version probing
Binder mmap setup
BINDER_SET_MAX_THREADS
low-level Binder ping transactions
mini service manager context manager
AOSP-style IServiceManager compatibility
listServices()
checkService()
getService()
BINDER_TYPE_HANDLE replies
BC_ACQUIRE
BC_RELEASE
BR_DEAD_REPLY stale-handle detection
service restart recovery
concurrent lifecycle stress
BC_REQUEST_DEATH_NOTIFICATION
BR_DEAD_BINDER
BC_DEAD_BINDER_DONE
BC_CLEAR_DEATH_NOTIFICATION
BR_CLEAR_DEATH_NOTIFICATION_DONE
unlink death-recipient validation
```

The full quick check should end with:

```text
BINDER_LIFECYCLE_V0_OK
BINDER_DEATH_NOTIFICATION_V0_OK
ALL_SIDECAR_SMOKE_OK
QUICK_CHECK_TV_OK
```

---

## Repository layout

```text
artifacts/
  binder-dirty-lgc1-o20-4.4.84-229.1.kavir.2.ko
    Prebuilt Binder kernel module for the tested LG webOS kernel.

scripts/
  build-sidecar.sh
    Cross-builds all static sidecar binaries.

  install-sidecar-tv.sh
    Installs binaries, module, loader, and support files to the TV.

  load-binder-tv.sh
    Loads the Binder kernel module on the TV and prepares /dev/binder.

  run-sidecar-all-smoke-tv.sh
    Runs the full sidecar smoke suite.

  quick-check-tv.sh
    Short end-to-end check for the current repo state and TV install.

  run-android-like-api-smoke-tv.sh
    Validates the Android-like client API path.

  run-android-like-service-smoke-tv.sh
    Validates the Android-like service path.

  run-android-like-lifecycle-smoke-tv.sh
    Validates repeated getService/echo/release lifecycle.

  run-android-like-restart-recovery-smoke-tv.sh
    Validates stale-handle detection and service restart recovery.

  run-android-like-concurrent-lifecycle-smoke-tv.sh
    Validates concurrent lifecycle clients.

  run-android-like-death-recipient-smoke-tv.sh
    Validates linkToDeath-style death notification.

  run-android-like-unlink-death-smoke-tv.sh
    Validates unlinkToDeath-style death notification cleanup.

tools/
  sidecar_binder.c
    Low-level Binder sidecar/service-manager utilities.

  libbinder_lite.hpp
  libbinder_lite.cpp
    Small C++ Binder client layer used by Android-like wrappers.

  android_like_binder.hpp
    Android-like façade: String16, Parcel, IBinder, IServiceManager, sp<T>.

  android_like_echo_iface.hpp
    Shared IEchoService contract:
      IEchoService
      BpEchoService
      BnEchoService
      android_like_echo_wire helpers

  android_like_echo_client.cpp
    Android-like API client.

  android_like_echo_service.cpp
    Android-like Binder service implementation.

  android_like_lifecycle_client.cpp
    Repeated acquire/transact/release lifecycle client.

  android_like_stale_handle_client.cpp
    Stale-handle / BR_DEAD_REPLY client.

  android_like_death_recipient_client.cpp
    Death-recipient / BR_DEAD_BINDER client.

  android_like_unlink_death_client.cpp
    Unlink death-recipient client.

  aidl_lite_echo_client.cpp
  aidl_lite_echo_service.cpp
    Earlier AIDL-lite echo experiments.

  aosp_sm_probe.c
  binder_probe.c
  binder_ping.c
    Low-level probes and compatibility tests.
```

---

## High-level architecture

```text
+-----------------------------+
| Android-like client         |
|                             |
| defaultServiceManager()     |
| getService()                |
| BpEchoService::echoText()   |
| linkToDeath()               |
| releaseRemote()             |
+--------------+--------------+
               |
               | libbinder-lite
               v
+--------------+--------------+
| /dev/binder                 |
| LG webOS kernel + module    |
+--------------+--------------+
               |
               v
+--------------+--------------+
| mini_servicemgr             |
|                             |
| context manager             |
| addService                  |
| listServices                |
| getService/checkService     |
| service liveness ping       |
+--------------+--------------+
               |
               v
+--------------+--------------+
| Android-like service        |
|                             |
| BnEchoService               |
| handleTransaction()         |
| echoText()                  |
+-----------------------------+
```

---

## Android-like Binder layer

The Android-like layer is intentionally small and explicit. It is not a full AOSP `libbinder`, but it mimics the important concepts:

```text
android::sp<T>
android::String16
android::Parcel
android::IBinder
android::IServiceManager
android::defaultServiceManager()
```

Service contract:

```text
IEchoService
  BpEchoService  -> client proxy
  BnEchoService  -> server stub
```

Wire helpers live in:

```text
android_like_echo_wire
```

and centralise:

```text
writeEchoRequest()
parseEchoRequest()
readEchoReply()
```

Important Android-like interface markers:

```text
ANDROID_LIKE_ECHO_WIRE_HELPERS_OK
ANDROID_LIKE_AIDL_WIRE_OK
ANDROID_LIKE_INTERFACE_CONTRACT_OK
ANDROID_LIKE_API_CLIENT_OK
ANDROID_LIKE_SERVICE_REGISTERED
ANDROID_LIKE_SERVICE_OK
ANDROID_LIKE_BN_SERVICE_OK
ANDROID_LIKE_BN_ECHO_TRANSACTION_OK
ANDROID_LIKE_SERVICE_SMOKE_OK
```

---

## Binder lifecycle v0

Binder lifecycle v0 validates that remote handles are not just used once, but acquired, used, released, stressed, and recovered.

Validated lifecycle flow:

```text
getService()
BC_ACQUIRE
echoText()
BC_RELEASE
```

Lifecycle markers:

```text
ANDROID_LIKE_HANDLE_ACQUIRE_OK
ANDROID_LIKE_HANDLE_RELEASE_OK
ANDROID_LIKE_LIFECYCLE_CLIENT_OK
ANDROID_LIKE_REFCOUNT_SMOKE_OK
```

Restart / stale-handle flow:

```text
getService()
echoText()
kill service
transact old handle
BR_DEAD_REPLY
release handle
restart service
fresh getService()
echoText()
```

Restart recovery markers:

```text
ANDROID_LIKE_SERVICE_KILLED_OK
ANDROID_LIKE_STALE_HANDLE_DETECTED_OK
ANDROID_LIKE_STALE_HANDLE_CLIENT_OK
ANDROID_LIKE_SERVICE_RESTART_OK
ANDROID_LIKE_RECOVERY_GETSERVICE_OK
ANDROID_LIKE_RESTART_RECOVERY_SMOKE_OK
```

Concurrent stress validates multiple clients in parallel:

```text
CLIENTS=N
ROUNDS=M
expected_transactions=N*M
actual_transactions=N*M
```

Concurrent lifecycle markers:

```text
ANDROID_LIKE_CONCURRENT_LIFECYCLE_CLIENT_OK
ANDROID_LIKE_CONCURRENT_LIFECYCLE_STRESS_OK
```

Final lifecycle marker:

```text
BINDER_LIFECYCLE_V0_OK
```

---

## Binder death notification v0

Binder death notification v0 validates real Binder death-recipient behaviour.

This is stronger than stale-handle detection. In stale-handle detection, the client discovers service death by doing a transaction and receiving `BR_DEAD_REPLY`.

With death notifications, the client explicitly registers interest in a remote Binder handle and receives an asynchronous death event:

```text
BC_REQUEST_DEATH_NOTIFICATION
BR_DEAD_BINDER
BC_DEAD_BINDER_DONE
```

Validated death-recipient flow:

```text
getService()
BC_ACQUIRE
BC_REQUEST_DEATH_NOTIFICATION
echoText()
kill service
BR_DEAD_BINDER
BC_DEAD_BINDER_DONE
BC_RELEASE
restart service
fresh getService()
echoText()
```

Death recipient markers:

```text
ANDROID_LIKE_LINK_TO_DEATH_OK
ANDROID_LIKE_DEATH_NOTIFICATION_RECEIVED_OK
ANDROID_LIKE_DEAD_BINDER_DONE_OK
ANDROID_LIKE_DEATH_RECIPIENT_CLIENT_OK
ANDROID_LIKE_DEATH_RECIPIENT_SMOKE_OK
```

### webOS Binder death-notification ABI note

On the tested LG webOS 4.4 Binder ABI, death-notification commands require the raw/old wire format:

```text
BC_REQUEST_DEATH_NOTIFICATION_RAW_COMPAT = 0x400c630e
BC_CLEAR_DEATH_NOTIFICATION_RAW_COMPAT   = 0x400c630f
```

The accepted request format is:

```text
u32 command
u32 handle
u64 cookie
```

Total write size:

```text
16 bytes
```

The padded aarch64 `struct binder_handle_cookie` form produced:

```text
cmd=0x4010630e
write_size=20
EINVAL
```

The project therefore uses raw-compatible handle/cookie writes for death-notification commands.

### Unlink death-recipient validation

Unlink flow:

```text
getService()
BC_REQUEST_DEATH_NOTIFICATION
BC_CLEAR_DEATH_NOTIFICATION
BR_CLEAR_DEATH_NOTIFICATION_DONE
kill service
confirm no BR_DEAD_BINDER arrives
BC_RELEASE
restart service
fresh getService()
echoText()
```

Unlink markers:

```text
ANDROID_LIKE_UNLINK_TO_DEATH_OK
ANDROID_LIKE_CLEAR_DEATH_NOTIFICATION_DONE_OK
ANDROID_LIKE_NO_DEATH_NOTIFICATION_AFTER_UNLINK_OK
ANDROID_LIKE_UNLINK_DEATH_RECIPIENT_CLIENT_OK
ANDROID_LIKE_UNLINK_DEATH_RECIPIENT_SMOKE_OK
```

Final death-notification marker:

```text
BINDER_DEATH_NOTIFICATION_V0_OK
```

---

## Build

From the NanoPi:

```bash
cd ~/disk/webos-dirty-binder

./scripts/build-sidecar.sh
```

Expected generated binaries include:

```text
build/mini_servicemgr_static
build/android_like_echo_client_static
build/android_like_echo_service_static
build/android_like_lifecycle_client_static
build/android_like_stale_handle_client_static
build/android_like_death_recipient_client_static
build/android_like_unlink_death_client_static
```

---

## Install to TV

```bash
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
./scripts/install-sidecar-tv.sh
```

Expected install path:

```text
/media/internal/android-sidecar
```

---

## Quick validation

Small validation:

```bash
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
CLIENTS=1 \
ROUNDS=1 \
./scripts/quick-check-tv.sh
```

Expected final markers:

```text
BINDER_LIFECYCLE_V0_OK
BINDER_DEATH_NOTIFICATION_V0_OK
ALL_SIDECAR_SMOKE_OK
QUICK_CHECK_TV_OK
```

---

## Full smoke suite

```bash
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
CLIENTS=1 \
ROUNDS=1 \
./scripts/run-sidecar-all-smoke-tv.sh
```

Stronger concurrent lifecycle run:

```bash
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
CLIENTS=6 \
ROUNDS=10 \
./scripts/run-android-like-concurrent-lifecycle-smoke-tv.sh
```

Expected concurrent marker:

```text
Android-like concurrent expected_transactions=60 actual_transactions=60
ANDROID_LIKE_CONCURRENT_LIFECYCLE_CLIENT_OK clients=6 rounds=10
ANDROID_LIKE_CONCURRENT_LIFECYCLE_STRESS_OK
```

---

## Individual smokes

Android-like API:

```bash
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
./scripts/run-android-like-api-smoke-tv.sh
```

Android-like service:

```bash
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
./scripts/run-android-like-service-smoke-tv.sh
```

Lifecycle/refcount:

```bash
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
ROUNDS=10 \
./scripts/run-android-like-lifecycle-smoke-tv.sh
```

Restart recovery:

```bash
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
./scripts/run-android-like-restart-recovery-smoke-tv.sh
```

Concurrent lifecycle:

```bash
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
CLIENTS=6 \
ROUNDS=10 \
./scripts/run-android-like-concurrent-lifecycle-smoke-tv.sh
```

Death recipient:

```bash
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
./scripts/run-android-like-death-recipient-smoke-tv.sh
```

Unlink death recipient:

```bash
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
./scripts/run-android-like-unlink-death-smoke-tv.sh
```

---

## Important smoke markers

Core:

```text
BINDER_PROBE_OK
BINDER_PING_OK
SIDE_CAR_SMOKE_OK
AOSP_LIST_SERVICES_OK
```

Android-like interface:

```text
ANDROID_LIKE_ECHO_WIRE_HELPERS_OK
ANDROID_LIKE_AIDL_WIRE_OK
ANDROID_LIKE_INTERFACE_CONTRACT_OK
ANDROID_LIKE_API_CLIENT_OK
ANDROID_LIKE_SERVICE_REGISTERED
ANDROID_LIKE_SERVICE_OK
ANDROID_LIKE_BN_SERVICE_OK
ANDROID_LIKE_BN_ECHO_TRANSACTION_OK
ANDROID_LIKE_SERVICE_SMOKE_OK
```

Lifecycle:

```text
ANDROID_LIKE_HANDLE_ACQUIRE_OK
ANDROID_LIKE_HANDLE_RELEASE_OK
ANDROID_LIKE_LIFECYCLE_CLIENT_OK
ANDROID_LIKE_REFCOUNT_SMOKE_OK
ANDROID_LIKE_STALE_HANDLE_DETECTED_OK
ANDROID_LIKE_SERVICE_RESTART_OK
ANDROID_LIKE_RECOVERY_GETSERVICE_OK
ANDROID_LIKE_RESTART_RECOVERY_SMOKE_OK
ANDROID_LIKE_CONCURRENT_LIFECYCLE_CLIENT_OK
ANDROID_LIKE_CONCURRENT_LIFECYCLE_STRESS_OK
BINDER_LIFECYCLE_V0_OK
```

Death notifications:

```text
ANDROID_LIKE_LINK_TO_DEATH_OK
ANDROID_LIKE_DEATH_NOTIFICATION_RECEIVED_OK
ANDROID_LIKE_DEAD_BINDER_DONE_OK
ANDROID_LIKE_DEATH_RECIPIENT_CLIENT_OK
ANDROID_LIKE_DEATH_RECIPIENT_SMOKE_OK
ANDROID_LIKE_UNLINK_TO_DEATH_OK
ANDROID_LIKE_CLEAR_DEATH_NOTIFICATION_DONE_OK
ANDROID_LIKE_NO_DEATH_NOTIFICATION_AFTER_UNLINK_OK
ANDROID_LIKE_UNLINK_DEATH_RECIPIENT_CLIENT_OK
ANDROID_LIKE_UNLINK_DEATH_RECIPIENT_SMOKE_OK
BINDER_DEATH_NOTIFICATION_V0_OK
```

Final suite:

```text
ALL_SIDECAR_SMOKE_OK
QUICK_CHECK_TV_OK
```

---

## Git release tags

Important tags used during development:

```text
android-like-service-v0
binder-lifecycle-v0-step1
binder-lifecycle-v0-step2
binder-lifecycle-v0-step3
binder-death-notification-v0-step1
binder-death-notification-v0-step2
binder-death-notification-v0
```

Suggested final tag for this state:

```bash
git tag -a binder-death-notification-v0 -m "Binder death notification v0"
git push origin binder-death-notification-v0
```

---

## Troubleshooting

### `/dev/binder` missing

Install and load the sidecar:

```bash
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
./scripts/install-sidecar-tv.sh
```

Then on the TV:

```bash
cd /media/internal/android-sidecar
./load-binder-tv.sh modules/binder.ko
ls -l /dev/binder
```

### Death notification returns `EINVAL`

If `BC_REQUEST_DEATH_NOTIFICATION` fails with:

```text
ioctl failed errno=22 (Invalid argument)
```

check the write format. On this webOS kernel, the accepted form is:

```text
cmd=0x400c630e
write_size=16
```

not:

```text
cmd=0x4010630e
write_size=20
```

The project uses `BC_REQUEST_DEATH_NOTIFICATION_RAW_COMPAT` and `BC_CLEAR_DEATH_NOTIFICATION_RAW_COMPAT` to match the tested kernel.

### Stale handle should fail cleanly

Expected stale-handle signal:

```text
BR_DEAD_REPLY
ANDROID_LIKE_STALE_HANDLE_DETECTED_OK
```

### Death recipient should receive death asynchronously

Expected death-recipient signal:

```text
BR_DEAD_BINDER
ANDROID_LIKE_DEATH_NOTIFICATION_RECEIVED_OK
ANDROID_LIKE_DEAD_BINDER_DONE_OK
```

### Do not accidentally commit the `.ko`

Local smoke runs may leave this file modified:

```text
artifacts/binder-dirty-lgc1-o20-4.4.84-229.1.kavir.2.ko
```

Unless intentionally updating the module artifact, restore it before commits:

```bash
git restore artifacts/binder-dirty-lgc1-o20-4.4.84-229.1.kavir.2.ko
```

---

## Current limitations

This is still a deliberately small Binder-compatible sidecar, not a complete AOSP Binder userspace.

Known limitations:

```text
No full AOSP libbinder replacement
No generated AIDL compiler integration
No SELinux integration
No Android service process model
No full Binder threadpool abstraction yet
No production-grade service registry persistence
No automatic dead-service cleanup policy beyond tested flows
No multi-interface AIDL registry yet
```

---

## Next milestone ideas

### Milestone 7: Binder callbacks v0

Implement a reverse Binder object callback:

```text
client registers callback object
service calls back into client
client handles incoming BR_TRANSACTION
callback death/lifecycle validation
```

Target markers:

```text
ANDROID_LIKE_CALLBACK_REGISTER_OK
ANDROID_LIKE_CALLBACK_TRANSACTION_OK
ANDROID_LIKE_CALLBACK_SMOKE_OK
```

### Milestone 8: Multi-service registry v0

Multiple Android-like services:

```text
test.android.echo
test.android.time
test.android.counter
```

Validate:

```text
listServices() returns all
getService() returns each
concurrent clients call mixed services
service restart for one service does not break the others
```

### Milestone 9: Threadpool / looper model v0

Move closer to Android Binder's process model:

```text
joinThreadPool()
spawn loopers
multiple service threads
controlled shutdown
stress under concurrent clients
```

---

## Safety note

This project loads a custom kernel module on a TV and interacts directly with Binder ioctls. Use only on devices you own and can recover. Keep SSH access available while testing.
