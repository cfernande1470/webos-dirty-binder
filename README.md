# webos-dirty-binder — Android-like Binder sidecar for LG webOS

Experimental Android Binder IPC sidecar for rooted LG webOS TVs.

This project builds and loads a modified Android Binder kernel module on a real LG webOS TV, exposes `/dev/binder`, and runs a small Binder userspace stack on top of it. The current validated stack includes a mini service manager, AOSP-style `android.os.IServiceManager` compatibility, a small C++ `libbinder-lite`, and a hand-written Android-like echo interface with client/server proxy-stub structure.

> Current milestone: **Android-like Service v0 complete.**
>
> `IEchoService` + `BpEchoService` + `BnEchoService` + shared echo wire helpers are validated end-to-end on the TV through the full smoke suite.

This is **not Android TV** and it does not run APKs. It is a validated Binder foundation for future Android userspace experiments on webOS.

---

## Current status

Validated on the target TV:

- Binder module loads on LG webOS.
- `/dev/binder` is created and usable.
- Binder protocol version `8` works.
- `BINDER_VERSION` works.
- `BINDER_SET_MAX_THREADS` works.
- `BINDER_SET_CONTEXT_MGR` works.
- Binder `mmap()` path works through the experimental allocation shim.
- Basic Binder ping works.
- Binder object passing works.
- Mini sidecar service manager works.
- Native sidecar `addService`, `getService`, and `listServices` work.
- Death notifications work.
- Duplicate service replacement works.
- Context-manager restart works.
- Multi-service and stress tests work.
- AOSP-style `android.os.IServiceManager` compatibility works:
  - `listServices`
  - `checkService`
  - `getService`
  - `addService`
- `libbinder-lite` exists as a reusable C++ mini-library.
- `libbinder-lite` provides:
  - `BinderDriver`
  - `Parcel`
  - `BpBinder`
  - `ServiceManagerProxy`
  - `defaultServiceManager()`
  - generic `BpBinder::transact()`
- AIDL-lite echo client works.
- AIDL-lite echo service works.
- Android-like echo API client works.
- Android-like echo service works.
- Android-like proxy/stub interface works:
  - `IEchoService`
  - `BpEchoService`
  - `BnEchoService`
- Shared Android-like echo wire helpers work.
- Full sidecar suite passes.
- Full TV quick check passes.

Latest validated success markers include:

```text
AOSP_LIST_SERVICES_OK
AOSP_CHECK_SERVICE_OK
AOSP_GET_SERVICE_OK
AOSP_ADD_SERVICE_OK
AOSP_ALIAS_SERVICE_OK
AOSP_SM_COMPAT_OK
AOSP_SM_COMPAT_SMOKE_OK
LIBBINDER_LITE_CHECK_SERVICE_OK
LIBBINDER_LITE_GET_SERVICE_OK
LIBBINDER_LITE_ADD_SERVICE_OK
LIBBINDER_LITE_ALIAS_SERVICE_OK
LIBBINDER_LITE_PARCEL_TRANSACT_OK
LIBBINDER_LITE_API_CLIENT_OK
LIBBINDER_LITE_CLIENT_OK
LIBBINDER_LITE_CLIENT_SMOKE_OK
AIDL_LITE_ECHO_CLIENT_OK
AIDL_LITE_ECHO_SMOKE_OK
AIDL_LITE_SERVICE_REGISTERED
AIDL_LITE_SERVICE_OK
AIDL_LITE_SERVICE_SMOKE_OK
ANDROID_LIKE_ECHO_WIRE_HELPERS_OK
ANDROID_LIKE_AIDL_WIRE_OK
ANDROID_LIKE_INTERFACE_CONTRACT_OK
ANDROID_LIKE_API_CLIENT_OK
ANDROID_LIKE_SERVICE_REGISTERED
ANDROID_LIKE_SERVICE_OK
ANDROID_LIKE_BN_SERVICE_OK
ANDROID_LIKE_BN_ECHO_TRANSACTION_OK
ANDROID_LIKE_SERVICE_SMOKE_OK
ALL_SIDECAR_SMOKE_OK
QUICK_CHECK_TV_OK
```

Current flagship stack:

```text
Android-like C++ client
  -> BpEchoService
  -> libbinder-lite
  -> /dev/binder on LG webOS
  -> mini service manager
  -> Android-like C++ Binder service
  -> BnEchoService
  -> AndroidLikeEchoService::echoText()
```

---

## Target tested

Validated environment:

```text
Device: LG webOS TV
Kernel: 4.4.84-229.1.kavir.2
Arch: aarch64
Binder protocol: 8
TV SSH: root@192.168.2.121
Build host: NanoPi R3S
Build host path: ~/disk/webos-dirty-binder
Deployment directory on TV: /media/internal/android-sidecar
```

Observed TV kernel:

```text
Linux LGwebOSTV 4.4.84-229.1.kavir.2 #1 SMP PREEMPT Mon Jan 17 08:08:42 UTC 2022 aarch64 GNU/Linux
Linux version 4.4.84-229.1.kavir.2 (oe-user@oe-host) (gcc version 8.2.0 (GCC) ) #1 SMP PREEMPT Mon Jan 17 08:08:42 UTC 2022
```

Observed Binder devices:

```text
/dev/binder present
/dev/hwbinder not present
/dev/vndbinder not present
```

---

## Safety notes

This is a kernel/module research project for your own hardware.

Important:

- The Binder module is loaded manually.
- The module is effectively permanent until reboot on this target.
- A kernel Oops may require rebooting the TV.
- Do not write to LG system partitions unless you have a recovery path.
- The Binder `mmap()` shim is experimental.
- Deployment is manual over SSH.
- Treat `/tmp` and `/media/internal/android-sidecar` as disposable deployment areas.
- Avoid committing generated or modified binary artifacts unless intentional.

Recommended reboot after serious kernel-side failures:

```sh
ssh root@192.168.2.121 'sync; reboot'
```

---

## Repository layout

Important paths:

```text
artifacts/
  binder-dirty-lgc1-o20-4.4.84-229.1.kavir.2.ko

build/
  binder_probe_static
  binder_ping_static
  sidecar_binder_static
  mini_servicemgr_static
  echo_service_static
  echo_client_static
  list_services_static
  aosp_sm_probe_static
  libbinder_lite_client_static
  aidl_lite_echo_client_static
  aidl_lite_echo_service_static
  android_like_echo_client_static
  android_like_echo_service_static

build/linux-4.4.84/
  LG kernel tree used to build binder.ko

scripts/
  build-module.sh
  build-probe.sh
  build-ping.sh
  build-sidecar.sh
  install-sidecar-tv.sh
  load-binder-tv.sh
  run-sidecar-smoke-tv.sh
  run-sidecar-list-smoke-tv.sh
  run-sidecar-death-smoke-tv.sh
  run-sidecar-multiservice-smoke-tv.sh
  run-sidecar-stress-smoke-tv.sh
  run-sidecar-rebind-smoke-tv.sh
  run-sidecar-context-restart-smoke-tv.sh
  run-sidecar-duplicate-smoke-tv.sh
  run-aosp-sm-compat-smoke-tv.sh
  run-libbinder-lite-client-smoke-tv.sh
  run-aidl-lite-echo-smoke-tv.sh
  run-aidl-lite-service-smoke-tv.sh
  run-android-like-api-smoke-tv.sh
  run-android-like-service-smoke-tv.sh
  run-sidecar-all-smoke-tv.sh
  quick-check-tv.sh

tools/
  binder_probe.c
  binder_ping.c
  sidecar_binder.c
  aosp_sm_probe.c
  libbinder_lite.hpp
  libbinder_lite.cpp
  libbinder_lite_client.cpp
  aidl_lite_echo_client.cpp
  aidl_lite_echo_service.cpp
  android_like_echo_iface.hpp
  android_like_echo_client.cpp
  android_like_echo_service.cpp
```

---

## Build requirements

Validated build host:

```text
NanoPi R3S
Ubuntu 24.04.4 LTS
aarch64
```

Typical dependencies:

```sh
sudo apt update
sudo apt install -y \
  build-essential \
  gcc-aarch64-linux-gnu \
  make \
  git \
  bc \
  bison \
  flex \
  libssl-dev \
  dwarves \
  rsync \
  file
```

---

## Build

Build Binder module:

```sh
cd ~/disk/webos-dirty-binder
./scripts/build-module.sh
```

Expected module output includes:

```text
vermagic: 4.4.84-229.1.kavir.2 SMP preempt mod_unload aarch64
OK: no known non-exported Binder symbols remain
Build completed: artifacts/binder-dirty-lgc1-o20-4.4.84-229.1.kavir.2.ko
```

Build basic tools:

```sh
./scripts/build-probe.sh
./scripts/build-ping.sh
```

Build sidecar, AOSP probe, libbinder-lite clients, AIDL-lite binaries, and Android-like binaries:

```sh
./scripts/build-sidecar.sh
```

Expected outputs include:

```text
build/binder_probe_static
build/binder_ping_static
build/sidecar_binder_static
build/mini_servicemgr_static
build/echo_service_static
build/echo_client_static
build/list_services_static
build/aosp_sm_probe_static
build/libbinder_lite_client_static
build/aidl_lite_echo_client_static
build/aidl_lite_echo_service_static
build/android_like_echo_client_static
build/android_like_echo_service_static
```

Important:

> Build Binder userland tools with the project scripts, not with arbitrary host Binder headers.

Correct:

```sh
./scripts/build-ping.sh
./scripts/build-sidecar.sh
```

Incorrect:

```sh
gcc -O2 -static -Wall -Wextra -o build/binder_ping_static tools/binder_ping.c
```

Mismatched Binder UAPI headers can produce invalid Binder command streams and kernel crashes.

---

## Load Binder manually

Copy module and loader:

```sh
scp artifacts/binder-dirty-lgc1-o20-4.4.84-229.1.kavir.2.ko root@192.168.2.121:/tmp/binder.ko
scp scripts/load-binder-tv.sh root@192.168.2.121:/tmp/load-binder-tv.sh
```

Load:

```sh
ssh root@192.168.2.121 '
  chmod +x /tmp/load-binder-tv.sh
  /tmp/load-binder-tv.sh /tmp/binder.ko
'
```

Expected:

```text
Loaded: binder ... [permanent], Live ... (O)
/dev/binder
```

The loader resolves non-exported symbols from `/proc/kallsyms` and passes their addresses to `insmod`.

Known module parameters include:

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

---

## Sidecar deployment

Build:

```sh
./scripts/build-sidecar.sh
```

Install:

```sh
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
./scripts/install-sidecar-tv.sh
```

Installed layout:

```text
/media/internal/android-sidecar/
  bin/
    mini_servicemgr
    echo_service
    echo_client
    list_services
    aosp_sm_probe
    libbinder_lite_client
    aidl_lite_echo_client
    aidl_lite_echo_service
    android_like_echo_client
    android_like_echo_service
  modules/
    binder.ko
  logs/
  run/
  load-binder-tv.sh
```

---

## Native sidecar protocol

The sidecar has a minimal Binder protocol for internal testing:

```c
SC_CODE_ADD_SERVICE = 0x53434144U /* SCAD */
SC_CODE_GET_SERVICE = 0x53434745U /* SCGE */
SC_CODE_LIST_SERVICES = 0x53434c53U /* SCLS */
SC_CODE_ECHO = 0x4543484fU /* ECHO */
SC_CODE_PING = 0x50494e47U /* PING */
```

Implemented:

```text
addService(name, binder)
getService(name)
listServices()
ping service before returning handle
echo request/reply
death notification registration
death notification cleanup
duplicate service replacement
context manager restart handling
```

---

## AOSP IServiceManager compatibility v0

The sidecar implements a minimal AOSP-style `android.os.IServiceManager` compatibility layer.

Supported transaction codes:

```c
AOSP_SM_GET_SERVICE_TRANSACTION = 1
AOSP_SM_CHECK_SERVICE_TRANSACTION = 2
AOSP_SM_ADD_SERVICE_TRANSACTION = 3
AOSP_SM_LIST_SERVICES_TRANSACTION = 4
```

Supported interface token:

```text
android.os.IServiceManager
```

Supported operations:

```text
listServices(index) -> String16
checkService(name) -> strong Binder handle
getService(name) -> strong Binder handle
addService(name, handle/local object) -> status int32
```

Expected AOSP smoke markers:

```text
AOSP_LIST_SERVICES_OK
AOSP_CHECK_SERVICE_OK
AOSP_GET_SERVICE_OK
AOSP_ADD_SERVICE_OK
AOSP_ALIAS_SERVICE_OK
AOSP_SM_COMPAT_OK
AOSP_SM_COMPAT_SMOKE_OK
```

Wire-format notes:

```text
ServiceManager header:
  int32 strict_policy
  String16 "android.os.IServiceManager"

checkService/getService:
  String16 service_name -> BINDER_TYPE_HANDLE reply

listServices:
  int32 index -> String16 service_name reply

addService:
  String16 service_name
  flat_binder_object BINDER_TYPE_HANDLE or BINDER_TYPE_BINDER
  int32 allowIsolated
  -> int32 status reply
```

---

## libbinder-lite

Files:

```text
tools/libbinder_lite.hpp
tools/libbinder_lite.cpp
tools/libbinder_lite_client.cpp
```

Main classes:

```cpp
android_lite::BinderDriver
android_lite::Parcel
android_lite::BpBinder
android_lite::ServiceManagerProxy
```

Main API shape:

```cpp
android_lite::BinderDriver driver;
android_lite::ServiceManagerProxy sm = android_lite::defaultServiceManager(driver);

auto echo = sm.checkService("test.aosp");
auto echo2 = sm.getService("test.aosp");
sm.addService("test.aosp.alias", echo);

android_lite::Parcel data;
android_lite::Parcel reply;
data.writeCString("hello");
echo.transact(SC_CODE_ECHO, data, &reply);
```

Validated markers:

```text
LIBBINDER_LITE_CHECK_SERVICE_OK
LIBBINDER_LITE_GET_SERVICE_OK
LIBBINDER_LITE_ADD_SERVICE_OK
LIBBINDER_LITE_ALIAS_SERVICE_OK
LIBBINDER_LITE_PARCEL_TRANSACT_OK
LIBBINDER_LITE_API_CLIENT_OK
LIBBINDER_LITE_CLIENT_OK
LIBBINDER_LITE_CLIENT_SMOKE_OK
```

---

## AIDL-lite echo client

File:

```text
tools/aidl_lite_echo_client.cpp
```

Flow:

```text
defaultServiceManager()
listServicesContains(service)
getService(service)
BpEchoService echo(binder)
echo.echoText(message, reply)
```

Expected markers:

```text
AIDL_LITE_ECHO_CLIENT_OK
AIDL_LITE_ECHO_SMOKE_OK
```

---

## AIDL-lite echo service

File:

```text
tools/aidl_lite_echo_service.cpp
```

Flow:

```text
open /dev/binder
BINDER_VERSION
BINDER_SET_MAX_THREADS
mmap
Binder AOSP addService(service_name, local Binder object)
enter Binder looper
handle SC_CODE_PING
handle SC_CODE_ECHO
reply with sc_text_reply
```

Expected markers:

```text
AIDL_LITE_SERVICE_REGISTERED
AIDL_LITE_SERVICE_OK
AIDL_LITE_SERVICE_SMOKE_OK
```

Validated end-to-end flow:

```text
aidl_lite_echo_client
  -> libbinder-lite
  -> ServiceManagerProxy::getService("test.aidl.service")
  -> returned Binder handle
  -> BpEchoService::echoText()
  -> Binder transact
  -> aidl_lite_echo_service
  -> reply text
```

---

## Android-like echo service v0

Files:

```text
tools/android_like_echo_iface.hpp
tools/android_like_echo_client.cpp
tools/android_like_echo_service.cpp
scripts/run-android-like-api-smoke-tv.sh
scripts/run-android-like-service-smoke-tv.sh
```

The Android-like echo interface is the current flagship Binder API experiment.

It models a real Android Binder interface layout:

```text
IEchoService
  BpEchoService -> client-side proxy
  BnEchoService -> server-side native stub
```

Shared contract:

```cpp
class IEchoService {
public:
    virtual ~IEchoService() {}
    virtual int echoText(const char *message, char *out, size_t out_len) = 0;
};
```

Client-side proxy:

```text
BpEchoService
  -> writes interface token
  -> writes echo request
  -> transacts with ANDROID_LIKE_TRANSACTION_ECHO_TEXT
  -> reads sidecar text reply
```

Server-side native stub:

```text
BnEchoService
  -> validates transaction code
  -> parses interface token and payload
  -> calls virtual echoText()
  -> emits ANDROID_LIKE_BN_ECHO_TRANSACTION_OK
```

Concrete server:

```text
AndroidLikeEchoService : public BnEchoService
  -> implements echoText()
  -> replies with "Android-like service reply from webOS sidecar"
```

Shared wire helpers:

```text
namespace android_like_echo_wire
  align4()
  readI32()
  readString16Ascii()
  writeEchoRequest()
  parseEchoRequest()
  readEchoReply()
```

Validated Android-like markers:

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

Validated end-to-end Android-like flow:

```text
android_like_echo_client
  -> defaultServiceManager()
  -> listServices("test.android.service")
  -> getService("test.android.service")
  -> BpEchoService::echoText("hello Android-like service")
  -> Binder transact code 0x1
  -> android_like_echo_service
  -> BnEchoService::handleTransaction()
  -> AndroidLikeEchoService::echoText()
  -> text reply
```

Expected service log fragment:

```text
ANDROID_LIKE_SERVICE_REGISTERED name=test.android.service
ANDROID_LIKE_SERVICE_OK
ANDROID_LIKE_BN_SERVICE_OK
Android-like BnEchoService descriptor=webos.dirtybinder.IEchoService message=hello Android-like service
Android-like service echoText message=hello Android-like service
ANDROID_LIKE_BN_ECHO_TRANSACTION_OK
ANDROID_LIKE_SERVICE_SMOKE_OK
```

---

## Smoke tests

### Basic sidecar

```sh
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
./scripts/run-sidecar-smoke-tv.sh
```

Expected:

```text
CLIENT_EXIT=0
echo-client reply status=0 text=echo-service reply from webOS sidecar
```

### Native listServices

```sh
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
./scripts/run-sidecar-list-smoke-tv.sh
```

Expected:

```text
LIST_SMOKE_OK
```

### Death notification

```sh
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
./scripts/run-sidecar-death-smoke-tv.sh
```

Expected:

```text
BR_DEAD_BINDER_RAW
sm-server: service died name=test.death
BC_DEAD_BINDER_DONE
DEATH_SMOKE_OK
```

### Multiservice

```sh
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
./scripts/run-sidecar-multiservice-smoke-tv.sh
```

Expected:

```text
MULTISERVICE_SMOKE_OK
```

### Stress

```sh
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
CLIENTS=12 \
ROUNDS=20 \
./scripts/run-sidecar-stress-smoke-tv.sh
```

Expected:

```text
TOTAL_CALLS=240
FAILURES=0
STRESS_SMOKE_OK
```

### Rebind

```sh
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
./scripts/run-sidecar-rebind-smoke-tv.sh
```

Expected:

```text
REBIND_SMOKE_OK
```

### Context-manager restart

```sh
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
./scripts/run-sidecar-context-restart-smoke-tv.sh
```

Expected:

```text
CONTEXT_RESTART_SMOKE_OK
```

### Duplicate service replacement

```sh
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
./scripts/run-sidecar-duplicate-smoke-tv.sh
```

Expected:

```text
DUPLICATE_SMOKE_OK
```

Important bug fixed:

```text
Old bug: death cookie tied to registry slot address
Fixed: monotonic unique death-cookie sequence per service registration
```

### AOSP ServiceManager

```sh
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
./scripts/run-aosp-sm-compat-smoke-tv.sh
```

Expected:

```text
AOSP_SM_COMPAT_SMOKE_OK
```

### libbinder-lite

```sh
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
./scripts/run-libbinder-lite-client-smoke-tv.sh
```

Expected:

```text
LIBBINDER_LITE_CLIENT_SMOKE_OK
```

### AIDL-lite echo client

This runs the AIDL-lite client against the original C sidecar `echo_service`.

```sh
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
./scripts/run-aidl-lite-echo-smoke-tv.sh
```

Expected:

```text
AIDL_LITE_ECHO_SMOKE_OK
```

### AIDL-lite service

This runs the AIDL-lite client against the C++ AIDL-lite Binder service.

```sh
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
./scripts/run-aidl-lite-service-smoke-tv.sh
```

Expected:

```text
AIDL_LITE_SERVICE_SMOKE_OK
```

### Android-like API client

This runs the Android-like API client against the Android-like service path.

```sh
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
./scripts/run-android-like-api-smoke-tv.sh
```

Expected:

```text
ANDROID_LIKE_ECHO_WIRE_HELPERS_OK
ANDROID_LIKE_AIDL_WIRE_OK
ANDROID_LIKE_INTERFACE_CONTRACT_OK
ANDROID_LIKE_API_CLIENT_OK
```

### Android-like service

This runs the Android-like client against the Android-like Binder service.

```sh
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
./scripts/run-android-like-service-smoke-tv.sh
```

Expected:

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

## Full suite

```sh
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
CLIENTS=6 \
ROUNDS=10 \
./scripts/run-sidecar-all-smoke-tv.sh
```

Current full suite includes:

```text
run-sidecar-smoke-tv.sh
run-sidecar-list-smoke-tv.sh
run-sidecar-death-smoke-tv.sh
run-sidecar-multiservice-smoke-tv.sh
run-sidecar-stress-smoke-tv.sh
run-sidecar-rebind-smoke-tv.sh
run-sidecar-context-restart-smoke-tv.sh
run-sidecar-duplicate-smoke-tv.sh
run-aosp-sm-compat-smoke-tv.sh
run-libbinder-lite-client-smoke-tv.sh
run-aidl-lite-echo-smoke-tv.sh
run-aidl-lite-service-smoke-tv.sh
run-android-like-api-smoke-tv.sh
run-android-like-service-smoke-tv.sh
```

Expected final markers:

```text
ANDROID_LIKE_SERVICE_SMOKE_OK
ALL_SIDECAR_SMOKE_OK
```

---

## Complete TV quick check

Current one-command validation path:

```sh
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
CLIENTS=6 \
ROUNDS=10 \
./scripts/quick-check-tv.sh
```

Fast validation used during development:

```sh
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
CLIENTS=1 \
ROUNDS=1 \
./scripts/quick-check-tv.sh
```

Expected:

```text
ANDROID_LIKE_SERVICE_SMOKE_OK
ALL_SIDECAR_SMOKE_OK
QUICK_CHECK_TV_OK
```

This validates:

- current git commit/status
- Binder module build
- Binder probe build
- Binder ping build
- sidecar build
- sidecar install
- standalone Binder probe
- standalone Binder ping transaction
- full sidecar smoke suite

---

## Troubleshooting

### `/dev/binder` missing

```sh
ssh root@192.168.2.121 '
  /media/internal/android-sidecar/load-binder-tv.sh \
  /media/internal/android-sidecar/modules/binder.ko
'
```

Check:

```sh
ssh root@192.168.2.121 '
  ls -l /dev/binder
  grep binder /proc/misc
  grep "^binder " /proc/modules
'
```

### Installed binary names

Build host:

```text
build/mini_servicemgr_static
build/echo_service_static
build/echo_client_static
build/list_services_static
build/aosp_sm_probe_static
build/libbinder_lite_client_static
build/aidl_lite_echo_client_static
build/aidl_lite_echo_service_static
build/android_like_echo_client_static
build/android_like_echo_service_static
```

TV:

```text
bin/mini_servicemgr
bin/echo_service
bin/echo_client
bin/list_services
bin/aosp_sm_probe
bin/libbinder_lite_client
bin/aidl_lite_echo_client
bin/aidl_lite_echo_service
bin/android_like_echo_client
bin/android_like_echo_service
```

### Kernel Oops after Binder transaction

Use project scripts to build Binder userland tools:

```sh
./scripts/build-ping.sh
./scripts/build-sidecar.sh
```

Do not compile Binder tools with arbitrary system headers.

### `BR_DEAD_REPLY`

Expected if the Binder context manager is gone. Restart `mini_servicemgr` and re-register services.

### AOSP probe include errors

`aosp_sm_probe` needs Binder UAPI headers, but including full internal kernel headers can conflict with glibc types. The build script uses a small `build/uapi-compat/linux/compiler.h` shim and UAPI include paths.

Avoid broad includes like:

```text
-Ibuild/linux-4.4.84/include
```

for userspace builds.

### AOSP `checkService` gets handle but echo returns `BR_FAILED_REPLY`

Acquire the returned handle before freeing the Binder reply buffer.

Correct flow:

```text
BR_REPLY with BINDER_TYPE_HANDLE
BC_ACQUIRE returned handle
BC_FREE_BUFFER reply buffer
transact on returned handle
```

### AIDL-lite service does not appear in `getService`

Check:

```sh
ssh root@192.168.2.121 '
  cd /media/internal/android-sidecar &&
  cat logs/aidl_service_service.log &&
  cat logs/aidl_service_sm.log
'
```

Verify:

```text
AIDL_LITE_SERVICE_REGISTERED
AIDL_LITE_SERVICE_OK
AOSP addService returned status 0
mini_servicemgr registry contains test.aidl.service
```

### Android-like service does not emit `ANDROID_LIKE_BN_ECHO_TRANSACTION_OK`

Check that the TV binary is current:

```sh
ssh root@192.168.2.121 \
"strings /media/internal/android-sidecar/bin/android_like_echo_service | grep -E 'ANDROID_LIKE_BN|BnEchoService|service request payload' || true"
```

Good output contains:

```text
Android-like BnEchoService descriptor=%s message=%s
ANDROID_LIKE_BN_ECHO_TRANSACTION_OK
ANDROID_LIKE_BN_SERVICE_OK
```

Bad output contains the old path:

```text
Android-like service request payload: %.*s
```

Rebuild and reinstall:

```sh
./scripts/build-sidecar.sh
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
./scripts/install-sidecar-tv.sh
```

---

## Development workflow

Typical validated workflow:

```sh
cd ~/disk/webos-dirty-binder

./scripts/build-module.sh
./scripts/build-probe.sh
./scripts/build-ping.sh
./scripts/build-sidecar.sh

TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
./scripts/install-sidecar-tv.sh

TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
CLIENTS=6 \
ROUNDS=10 \
./scripts/run-sidecar-all-smoke-tv.sh
```

Full check:

```sh
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
CLIENTS=6 \
ROUNDS=10 \
./scripts/quick-check-tv.sh
```

Before committing:

```sh
git status --short
git log --oneline -15 --decorate
```

Avoid committing generated artifacts unless intentional:

```sh
git restore artifacts/binder-dirty-lgc1-o20-4.4.84-229.1.kavir.2.ko
```

---

## Milestone history

### Native Binder sidecar foundation

Implemented:

```text
Binder module load
/dev/binder creation
Binder protocol probe
Binder ping
mini service manager
native addService/getService/listServices
object passing
death notifications
stress/rebind/context-manager restart/duplicate replacement smokes
```

### AOSP ServiceManager compatibility v0

Implemented:

```text
android.os.IServiceManager token parsing
listServices
checkService
getService
addService
AOSP-style service manager smoke
```

### libbinder-lite client v0

Implemented:

```text
BinderDriver
Parcel
BpBinder
ServiceManagerProxy
defaultServiceManager()
generic transact
libbinder-lite client smoke
```

### AIDL-lite service v0

Implemented:

```text
AIDL-lite echo client
AIDL-lite echo Binder service
C++ service registered through AOSP addService
end-to-end client -> service -> reply flow
```

### Android-like service v0

Implemented:

```text
IEchoService shared contract
BpEchoService client proxy
BnEchoService server stub
AndroidLikeEchoService concrete server
shared android_like_echo_wire helpers
Android-like API smoke
Android-like service smoke
full-suite integration
```

Recent commits in the milestone branch:

```text
8bc8333 iface: share Android-like echo wire helpers
c13bb4c smoke: include Android-like service in full suite
```

---

## Current limitations

Still not implemented:

- Android TV userspace.
- Waydroid or Anbox integration.
- Real Android `init`.
- Real Android `servicemanager` binary.
- Full upstream Android `libbinder`.
- AIDL-generated C++ stubs.
- ashmem/memfd compatibility layer.
- SELinux integration.
- Android graphics stack.
- Android audio HAL.
- Android input HAL.
- `/dev/hwbinder`.
- `/dev/vndbinder`.

Current technical limitations:

- Only `/dev/binder` is available on the tested TV.
- Binder module stays loaded until reboot.
- Binder `mmap()` shim is experimental.
- Logging is verbose.
- Deployment is manual over SSH.
- AOSP compatibility is implemented by a minimal sidecar bridge, not by a full Android framework stack.
- `libbinder-lite` is intentionally small.
- AIDL-lite and Android-like interfaces are hand-written, not generated by the real AIDL compiler.

---

## Merge milestone branch into main

Use this after the milestone branch is green.

### Option A: direct merge into `main`

```sh
cd ~/disk/webos-dirty-binder

# Start clean.
git checkout milestone/android-like-service-v0
git status --short

# Restore the Binder artifact if it was modified unintentionally.
git restore artifacts/binder-dirty-lgc1-o20-4.4.84-229.1.kavir.2.ko

git status --short

# Fetch latest remote state.
git fetch origin

# Update milestone branch.
git pull --ff-only origin milestone/android-like-service-v0

# Run final validation before merging.
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
CLIENTS=1 \
ROUNDS=1 \
./scripts/quick-check-tv.sh

# Move to main and update it.
git checkout main
git pull --ff-only origin main

# Merge the milestone branch into main.
git merge --no-ff milestone/android-like-service-v0 \
  -m "merge: Android-like Binder service v0"

# Optional: run validation on main.
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
CLIENTS=1 \
ROUNDS=1 \
./scripts/quick-check-tv.sh

# Push main.
git push origin main

# Optional tag.
git tag -a android-like-service-v0 -m "Android-like Binder service v0"
git push origin android-like-service-v0
```

### Option B: push branch and open PR

```sh
cd ~/disk/webos-dirty-binder

git checkout milestone/android-like-service-v0
git fetch origin
git pull --ff-only origin milestone/android-like-service-v0

git restore artifacts/binder-dirty-lgc1-o20-4.4.84-229.1.kavir.2.ko

git status --short

git push origin milestone/android-like-service-v0
```

Then open a pull request:

```text
base: main
compare: milestone/android-like-service-v0
title: Android-like Binder service v0
```

Suggested PR summary:

```text
This merges the Android-like Binder service v0 milestone.

Highlights:
- Adds shared IEchoService contract.
- Adds BpEchoService client proxy.
- Adds BnEchoService server stub.
- Adds shared android_like_echo_wire helpers.
- Adds Android-like API and service smokes.
- Includes Android-like service smoke in the full sidecar suite.
- Validated on LG webOS TV with QUICK_CHECK_TV_OK.
```

---

## Next milestone recommendation

Recommended next branch after merging to `main`:

```sh
git checkout main
git pull --ff-only origin main
git checkout -b milestone/binder-lifecycle-v0
```

Recommended next milestone:

```text
Milestone 5A: Binder object lifecycle / strong refs smoke
```

Goal:

```text
ANDROID_LIKE_HANDLE_ACQUIRE_OK
ANDROID_LIKE_HANDLE_RELEASE_OK
ANDROID_LIKE_REFCOUNT_SMOKE_OK
```

Why next:

```text
The project already proves service registration, lookup, transaction, proxy/stub split, and full smoke integration.
The next useful proof is Binder handle lifecycle: acquire, release, repeated lookup, and refcount stability.
```

Suggested files:

```text
tools/android_like_lifecycle_client.cpp
scripts/run-android-like-lifecycle-smoke-tv.sh
```

Suggested flow:

```text
defaultServiceManager()
listServices()
getService("test.android.service")
acquire handle
echoText()
release handle
getService("test.android.service") again
echoText() again
validate service still alive
```

Expected result:

```text
ANDROID_LIKE_SERVICE_REGISTERED
ANDROID_LIKE_API_CLIENT_OK
ANDROID_LIKE_HANDLE_ACQUIRE_OK
ANDROID_LIKE_HANDLE_RELEASE_OK
ANDROID_LIKE_REFCOUNT_SMOKE_OK
```

---

## Final milestone statement

This project currently proves:

```text
LG webOS kernel 4.4.84
+ dirty Binder kernel module
+ /dev/binder
+ mini Binder userspace sidecar
+ AOSP android.os.IServiceManager compatibility v0
+ libbinder-lite C++ client API
+ AIDL-lite C++ client/service
+ Android-like IEchoService/BpEchoService/BnEchoService
+ shared Android-like echo wire helpers
```

can support:

```text
Binder context manager
service registration
service lookup
service listing
Binder object passing
synchronous Binder transactions
death notifications
duplicate-name service replacement
context-manager restart
stress calls
AOSP-style listServices
AOSP-style checkService
AOSP-style getService
AOSP-style addService
libbinder-lite defaultServiceManager
libbinder-lite Parcel
libbinder-lite generic transact
AIDL-lite client and service
Android-like Bp/Bn proxy-stub contract
full reproducible smoke validation on TV
```

Current flagship validation:

```sh
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
CLIENTS=6 \
ROUNDS=10 \
./scripts/run-sidecar-all-smoke-tv.sh
```

Expected:

```text
ANDROID_LIKE_SERVICE_SMOKE_OK
ALL_SIDECAR_SMOKE_OK
```

Optional full validation:

```sh
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
CLIENTS=6 \
ROUNDS=10 \
./scripts/quick-check-tv.sh
```

Expected:

```text
QUICK_CHECK_TV_OK
```
