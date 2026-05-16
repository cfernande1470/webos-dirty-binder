# webos-dirty-binder — AOSP ServiceManager Compatibility v0

Experimental Android Binder IPC sidecar for LG webOS TVs.

This project builds and loads a modified Android Binder kernel module on a real LG webOS TV, then runs a small Binder userspace sidecar on top of `/dev/binder`.

The current milestone is:

> **Android Binder IPC works on LG webOS, with a reproducible sidecar test suite and AOSP `android.os.IServiceManager` compatibility v0.**

This is not Android TV yet. It is a validated Binder foundation for future Android userspace experiments on webOS.

---

## 1. Current milestone summary

Validated milestones:

1. Dirty Binder kernel module loads on LG webOS.
2. `/dev/binder` is created and usable.
3. Binder protocol `8` works.
4. Basic Binder transaction ping works.
5. Binder object passing works.
6. A mini Binder sidecar service manager works.
7. Sidecar service registration, lookup, listing, death notifications, duplicate replacement, context-manager restart and stress tests work.
8. AOSP-style `android.os.IServiceManager` compatibility works for:
   - `listServices`
   - `checkService`
   - `getService`
   - `addService`
9. Full smoke suite passes.

Latest known good AOSP markers:

```text
AOSP_LIST_SERVICES_OK
AOSP_CHECK_SERVICE_OK
AOSP_GET_SERVICE_OK
AOSP_ADD_SERVICE_OK
AOSP_ALIAS_SERVICE_OK
AOSP_SM_COMPAT_OK
AOSP_SM_COMPAT_SMOKE_OK
ALL_SIDECAR_SMOKE_OK
```

Latest known good quick-check markers:

```text
DUPLICATE_SMOKE_OK
ALL_SIDECAR_SMOKE_OK
QUICK_CHECK_TV_OK
```

---

## 2. Target tested

Validated on:

```text
Device: LG webOS TV
Kernel: 4.4.84-229.1.kavir.2
Arch: aarch64
Binder protocol: 8
TV SSH: root@192.168.2.121
Build host: NanoPi R3S
Build host OS: Ubuntu 24.04.4 LTS
Project path on build host: ~/disk/webos-dirty-binder
```

Observed TV kernel:

```text
Linux LGwebOSTV 4.4.84-229.1.kavir.2 #1 SMP PREEMPT Mon Jan 17 08:08:42 UTC 2022 aarch64 GNU/Linux
Linux version 4.4.84-229.1.kavir.2 (oe-user@oe-host) (gcc version 8.2.0 (GCC) ) #1 SMP PREEMPT Mon Jan 17 08:08:42 UTC 2022
```

Observed Binder device:

```text
crw------- 1 root root 10, 53 ... /dev/binder
```

On the tested target:

```text
/dev/binder     present
/dev/hwbinder   not present
/dev/vndbinder  not present
```

---

## 3. Safety notes

This is a kernel/module research project for your own hardware.

Important:

- The Binder module is loaded manually.
- The module is effectively permanent until reboot on this target.
- A kernel Oops may require rebooting the TV.
- Do not write to LG system partitions unless you have a recovery path.
- The Binder mmap shim is experimental.
- The current deployment is manual over SSH.
- Treat `/tmp` and `/media/internal/android-sidecar` as disposable deployment areas.
- Reboot after serious kernel-side failures.

Recommended reboot after kernel-side failures:

```sh
ssh root@192.168.2.121 'sync; reboot'
```

---

## 4. Repository layout

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

build/linux-4.4.84/
  LG kernel tree used to build binder.ko

patches/
  Binder / LG kernel integration patches

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
  run-sidecar-all-smoke-tv.sh
  quick-check-tv.sh

tools/
  binder_probe.c
  binder_ping.c
  sidecar_binder.c
  aosp_sm_probe.c
```

---

## 5. Build requirements

Validated build host:

```text
NanoPi R3S
Ubuntu 24.04.4 LTS
aarch64
```

Install typical dependencies:

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

The sidecar and test tools are built as static aarch64 binaries.

---

## 6. Build Binder module

```sh
cd ~/disk/webos-dirty-binder
./scripts/build-module.sh
```

Expected output includes:

```text
vermagic: 4.4.84-229.1.kavir.2 SMP preempt mod_unload aarch64
OK: no known non-exported Binder symbols remain
Build completed: artifacts/binder-dirty-lgc1-o20-4.4.84-229.1.kavir.2.ko
```

The module build script:

- resets the kernel tree
- applies the LG config
- applies Binder integration patches
- injects the Binder mmap allocation shim
- configures Binder as a module
- patches incompatible kernel API usage where needed
- builds `drivers/android/binder.ko`
- copies the final module to `artifacts/`

---

## 7. Build userland tools

Build basic probe:

```sh
./scripts/build-probe.sh
```

Build basic Binder ping:

```sh
./scripts/build-ping.sh
```

Build sidecar and AOSP probe:

```sh
./scripts/build-sidecar.sh
```

Expected outputs:

```text
build/binder_probe_static
build/binder_ping_static
build/sidecar_binder_static
build/mini_servicemgr_static
build/echo_service_static
build/echo_client_static
build/list_services_static
build/aosp_sm_probe_static
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

Using mismatched host Binder headers can produce incompatible Binder command streams and kernel crashes.

---

## 8. Load Binder manually

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
Loaded:
binder 118784 0 [permanent], Live ... (O)
 53 binder
crw------- 1 root root 10, 53 ... /dev/binder
```

The loader resolves non-exported symbols from `/proc/kallsyms` and passes their addresses to `insmod`.

Module parameters include:

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

## 9. Basic Binder probe

```sh
scp build/binder_probe_static root@192.168.2.121:/tmp/binder_probe

ssh root@192.168.2.121 '
  chmod +x /tmp/binder_probe
  /tmp/binder_probe
'
```

Expected:

```text
BINDER_VERSION protocol_version=8
BINDER_SET_MAX_THREADS ok
```

---

## 10. Basic Binder transaction ping

```sh
./scripts/build-ping.sh

scp build/binder_ping_static root@192.168.2.121:/tmp/binder_ping

ssh root@192.168.2.121 '
  chmod +x /tmp/binder_ping

  rm -f /tmp/binder_ping_server.log /tmp/binder_ping_server.pid

  /tmp/binder_ping server > /tmp/binder_ping_server.log 2>&1 &
  echo $! > /tmp/binder_ping_server.pid

  sleep 1

  /tmp/binder_ping client
  rc=$?

  kill "$(cat /tmp/binder_ping_server.pid)" 2>/dev/null || true
  cat /tmp/binder_ping_server.log || true

  exit "$rc"
'
```

Expected client result:

```text
client BR_TRANSACTION_COMPLETE
client BR_REPLY code=0x0 flags=0x0
client reply payload: PONG from webOS binder server
```

Expected server result:

```text
BINDER_SET_CONTEXT_MGR ok
server BR_TRANSACTION code=0x50494e47
server payload: PING from webOS binder client
server_reply completed
```

---

## 11. Sidecar deployment

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
  modules/
    binder.ko
  logs/
  run/
  load-binder-tv.sh
```

Mapping from build host to TV:

```text
build/mini_servicemgr_static -> bin/mini_servicemgr
build/echo_service_static    -> bin/echo_service
build/echo_client_static     -> bin/echo_client
build/list_services_static   -> bin/list_services
build/aosp_sm_probe_static   -> bin/aosp_sm_probe
binder.ko                    -> modules/binder.ko
load-binder-tv.sh            -> load-binder-tv.sh
```

---

## 12. Sidecar native protocol

The sidecar has a minimal Binder protocol for internal testing:

```c
SC_CODE_ADD_SERVICE    = 0x53434144U  /* SCAD */
SC_CODE_GET_SERVICE    = 0x53434745U  /* SCGE */
SC_CODE_LIST_SERVICES  = 0x53434c53U  /* SCLS */
SC_CODE_ECHO           = 0x4543484fU  /* ECHO */
SC_CODE_PING           = 0x50494e47U  /* PING */
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

## 13. AOSP IServiceManager compatibility v0

The sidecar implements a minimal AOSP-style `android.os.IServiceManager` compatibility layer.

Supported transaction codes:

```c
AOSP_SM_GET_SERVICE_TRANSACTION    = 1
AOSP_SM_CHECK_SERVICE_TRANSACTION  = 2
AOSP_SM_ADD_SERVICE_TRANSACTION    = 3
AOSP_SM_LIST_SERVICES_TRANSACTION  = 4
```

Supported interface token:

```text
android.os.IServiceManager
```

Supported AOSP-style operations:

```text
listServices(index) -> String16
checkService(name)  -> strong Binder handle
getService(name)    -> strong Binder handle
addService(name, handle) -> status int32
```

The AOSP probe validates:

```text
AOSP listServices contains test.aosp
AOSP checkService("test.aosp") returns a handle
AOSP getService("test.aosp") returns a handle
the returned handle can call echo service
AOSP addService("test.aosp.alias", handle) succeeds
AOSP listServices contains test.aosp.alias
AOSP checkService("test.aosp.alias") returns a handle
the alias handle can call echo service
```

Expected success markers:

```text
AOSP_LIST_SERVICES_OK
AOSP_CHECK_SERVICE_OK
AOSP_GET_SERVICE_OK
AOSP_ADD_SERVICE_OK
AOSP_ALIAS_SERVICE_OK
AOSP_SM_COMPAT_OK
AOSP_SM_COMPAT_SMOKE_OK
```

This is the current high-level Android compatibility milestone.

---

## 14. AOSP wire-format notes

The compatibility layer currently understands a minimal Parcel shape.

For ServiceManager calls, the probe sends:

```text
int32 strict_policy
String16 "android.os.IServiceManager"
...
```

For `checkService(name)` and `getService(name)`:

```text
String16 service_name
```

Reply:

```text
BINDER_TYPE_HANDLE object
```

For `listServices(index)`:

```text
int32 index
```

Reply:

```text
String16 service_name
```

The current implementation follows the indexed `listServices` behavior used by classic native `IServiceManager` clients: call with index `0`, `1`, `2`, etc. until an empty string is returned.

For `addService(name, handle)`:

```text
String16 service_name
flat_binder_object BINDER_TYPE_HANDLE
int32 allowIsolated
```

Reply:

```text
int32 status
```

The current `aosp_sm_probe` uses `BINDER_TYPE_HANDLE` to alias an existing service handle:

```text
test.aosp       -> original echo service
test.aosp.alias -> alias registered via AOSP addService
```

Successful alias validation:

```text
AOSP addService name=test.aosp.alias handle=1
AOSP addService reply status=0
AOSP_ADD_SERVICE_OK
AOSP listServices[1]=test.aosp.alias
AOSP checkService alias got handle=1
aosp echo reply status=0 text=echo-service reply from webOS sidecar
AOSP_ALIAS_SERVICE_OK
```

---

## 15. Run basic sidecar smoke

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

---

## 16. Run listServices smoke

```sh
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
./scripts/run-sidecar-list-smoke-tv.sh
```

Expected:

```text
list-services reply status=0
(empty)

list-services reply status=0
test.echo

LIST_SMOKE_OK
```

---

## 17. Run service death smoke

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
getService text reply status=1 text=NOT FOUND
DEATH_SMOKE_OK
```

---

## 18. Run multiservice smoke

```sh
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
./scripts/run-sidecar-multiservice-smoke-tv.sh
```

Expected:

```text
test.echo.a
test.echo.b

test.echo.b

echo-client: getService failed for test.echo.a
MULTISERVICE_SMOKE_OK
```

---

## 19. Run stress smoke

Moderate:

```sh
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
CLIENTS=6 \
ROUNDS=10 \
./scripts/run-sidecar-stress-smoke-tv.sh
```

Heavier validated run:

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

---

## 20. Run service rebind smoke

```sh
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
./scripts/run-sidecar-rebind-smoke-tv.sh
```

Expected:

```text
REBIND_SMOKE_OK
```

This validates:

- service registers
- client can call it
- service dies
- registry becomes clean
- same service name registers again
- client can call the new instance
- final cleanup works

---

## 21. Run context-manager restart smoke

```sh
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
./scripts/run-sidecar-context-restart-smoke-tv.sh
```

Expected:

```text
getService got BR_DEAD_REPLY
client_without_sm_rc=1
CONTEXT_RESTART_SMOKE_OK
```

This validates:

- client succeeds before context manager death
- killing `mini_servicemgr` causes clients to fail cleanly
- a new `mini_servicemgr` can become context manager
- a new service can register
- clients succeed again after restart

---

## 22. Run duplicate service smoke

```sh
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
./scripts/run-sidecar-duplicate-smoke-tv.sh
```

Expected:

```text
DUPLICATE_SMOKE_OK
```

This validates duplicate-name replacement:

```text
first service registers test.duplicate -> handle=1
second service registers test.duplicate -> handle=2
registry contains one entry only
killing first service does not remove replacement
client still succeeds through handle=2
killing second service removes registry entry
final getService returns NOT FOUND
```

Important bug fixed:

Earlier, death notification cookies were tied to the service registry slot. When a duplicate registration replaced the old service in the same slot, the old service death could incorrectly clear the new service.

Fixed behavior:

```text
cookie=0x53444301  old service
cookie=0x53444302  replacement service

death cookie 0x53444301 -> not found, registry remains
death cookie 0x53444302 -> service removed
```

---

## 23. Run AOSP ServiceManager compatibility smoke

```sh
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
./scripts/run-aosp-sm-compat-smoke-tv.sh
```

Expected final markers:

```text
AOSP_LIST_SERVICES_OK
AOSP_CHECK_SERVICE_OK
AOSP_GET_SERVICE_OK
AOSP_ADD_SERVICE_OK
AOSP_ALIAS_SERVICE_OK
AOSP_SM_COMPAT_OK
AOSP_SM_COMPAT_SMOKE_OK
```

Example successful flow:

```text
AOSP listServices[0]=test.aosp
AOSP_LIST_SERVICES_OK

AOSP checkService name=test.aosp
AOSP reply object: offset=0 type=0x73682a85 handle=1
AOSP_CHECK_SERVICE_OK

AOSP getService name=test.aosp
AOSP_GET_SERVICE_OK

AOSP addService name=test.aosp.alias handle=1
AOSP addService reply status=0
AOSP_ADD_SERVICE_OK

AOSP listServices[1]=test.aosp.alias
AOSP_ALIAS_SERVICE_OK

AOSP_SM_COMPAT_OK
AOSP_SM_COMPAT_SMOKE_OK
```

---

## 24. Run full smoke suite

```sh
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
CLIENTS=6 \
ROUNDS=10 \
./scripts/run-sidecar-all-smoke-tv.sh
```

Runs:

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
```

Expected:

```text
AOSP_SM_COMPAT_SMOKE_OK
ALL_SIDECAR_SMOKE_OK
```

---

## 25. Run complete TV quick check

Current one-command validation path:

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

Latest known successful quick-check:

```text
DUPLICATE_SMOKE_OK
ALL_SIDECAR_SMOKE_OK
QUICK_CHECK_TV_OK
```

---

## 26. Troubleshooting

### `/dev/binder` missing

Load Binder from sidecar install:

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

### `module not found`

Check install layout:

```sh
ssh root@192.168.2.121 '
  cd /media/internal/android-sidecar &&
  find . -maxdepth 3 -type f -o -type d | sort
'
```

Expected:

```text
/media/internal/android-sidecar/modules/binder.ko
```

### Installed binary names

Build host names:

```text
build/mini_servicemgr_static
build/echo_service_static
build/echo_client_static
build/list_services_static
build/aosp_sm_probe_static
```

TV install names:

```text
bin/mini_servicemgr
bin/echo_service
bin/echo_client
bin/list_services
bin/aosp_sm_probe
```

### Kernel Oops after Binder transaction

Use project scripts to build Binder userland tools:

```sh
./scripts/build-ping.sh
./scripts/build-sidecar.sh
```

Do not compile Binder tools with arbitrary system headers.

### `BR_DEAD_REPLY`

Expected if the Binder context manager is gone.

Restart `mini_servicemgr` and re-register services.

### Duplicate registration deletes replacement

Check that `sidecar_binder.c` uses unique death cookies per registration.

Wrong:

```text
death cookie tied to registry slot address
```

Correct:

```text
monotonic death-cookie sequence
```

### AOSP probe include errors

`aosp_sm_probe` needs Binder UAPI headers, but including full internal kernel headers can conflict with glibc types.

The build script uses a tiny `build/uapi-compat/linux/compiler.h` shim and UAPI include paths.

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

Freeing the buffer first can drop the returned reference before the probe uses it.

---

## 27. Development workflow

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
git log --oneline -12 --decorate
```

Avoid committing generated artifacts unless intentional.

---

## 28. Git branches and milestones

Main branch:

```text
main
```

AOSP compatibility branch:

```text
milestone/aosp-sm-compat-v0
```

Relevant commit sequence on the AOSP branch:

```text
test: add AOSP service manager compatibility smoke skeleton
sidecar: add AOSP IServiceManager checkService compatibility probe
sidecar: add AOSP IServiceManager listServices compatibility
sidecar: add AOSP IServiceManager getService compatibility
sidecar: add AOSP IServiceManager addService compatibility
test: include AOSP ServiceManager compatibility in smoke suite
```

Recommended release tag:

```text
aosp-sm-compat-v0
```

---

## 29. Current limitations

Still not implemented:

- Android TV userspace
- Waydroid or Anbox integration
- real Android init
- real Android `servicemanager` binary
- full `libbinder` client binary
- AIDL-generated service/client
- ashmem/memfd compatibility layer
- SELinux integration
- Android graphics stack
- Android audio HAL
- Android input HAL
- `/dev/hwbinder`
- `/dev/vndbinder`

Current technical limitations:

- only `/dev/binder` is available on the tested TV
- Binder module stays loaded until reboot
- Binder mmap shim is experimental
- logging is verbose
- deployment is manual over SSH
- AOSP compatibility is implemented by a minimal probe and sidecar protocol bridge, not by a full Android framework stack

---

## 30. Final milestone statement

This project currently proves:

```text
LG webOS kernel 4.4.84
+
dirty Binder kernel module
+
minimal Binder userspace sidecar
+
AOSP android.os.IServiceManager compatibility v0
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
AOSP_SM_COMPAT_SMOKE_OK
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

---

## 31. Next high-level milestones

### Milestone 3: real Android/libbinder client

Goal:

```text
Compile or port a native Android-style libbinder client that uses:
  defaultServiceManager()
  checkService("test.aosp")
  getService("test.aosp")
  transact()
```

Purpose:

```text
Move from a manual C probe to real Android/libbinder client behavior.
```

### Milestone 4: AIDL-style echo interface

Goal:

```text
Generate or mimic an AIDL echo interface and validate client/service calls through the Binder sidecar.
```

### Milestone 5: Android servicemanager experiment

Goal:

```text
Try running a real or lightly patched Android servicemanager against webOS Binder.
```

### Milestone 6: memory compatibility

Goal:

```text
Investigate ashmem/memfd compatibility required by larger Android userspace components.
```

### Milestone 7: Binder domains

Goal:

```text
Investigate why /dev/hwbinder and /dev/vndbinder are not created on this LG webOS target.
```

### Milestone 8: minimal Android-native userspace island

Goal:

```text
Run a small cluster of Android-native binaries using Binder IPC on webOS.
```

Immediate recommended next step:

```text
Milestone 3: real Android/libbinder client
```

---

## 32. Commands to close this milestone

Copy this file into the repo as `README_AOSP_SM_COMPAT_V0.md` first.

Then:

```sh
cd ~/disk/webos-dirty-binder

git checkout milestone/aosp-sm-compat-v0
git status --short

git add README_AOSP_SM_COMPAT_V0.md
git commit -m "docs: document AOSP ServiceManager compatibility v0 milestone"

TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
CLIENTS=6 \
ROUNDS=10 \
./scripts/run-sidecar-all-smoke-tv.sh

git push origin milestone/aosp-sm-compat-v0
```

Merge into `main`:

```sh
git checkout main
git pull --ff-only origin main

git merge --no-ff milestone/aosp-sm-compat-v0 \
  -m "merge: AOSP ServiceManager compatibility milestone"

git push origin main
```

Tag the milestone:

```sh
git tag -a aosp-sm-compat-v0 \
  -m "AOSP IServiceManager compatibility v0 on LG webOS Binder sidecar"

git push origin aosp-sm-compat-v0
```

Final check:

```sh
git status --short
git log --oneline -12 --decorate
```
