# webos-dirty-binder

Experimental Android Binder IPC sidecar for LG webOS TVs.

This project builds and loads a modified Android Binder kernel module on a real LG webOS TV, then runs a small Binder-based sidecar userspace consisting of:

- a mini Binder service manager
- an echo service
- an echo client
- a `listServices` client
- reproducible smoke tests for Binder transactions, service registration, death notifications, service replacement, context-manager restart, and stress calls

The current milestone is:

> **Binder IPC is working on LG webOS with a reproducible sidecar test suite.**

This is not Android TV, Waydroid, or Anbox yet. It is a validated Binder IPC foundation running directly on webOS.

---

## Target tested

Validated on:

```text
Device: LG webOS TV
Kernel: 4.4.84-229.1.kavir.2
Arch: aarch64
Binder protocol: 8
TV SSH: root@192.168.2.121
Build host: NanoPi R3S / Ubuntu 24.04.4 LTS / aarch64
Project path on build host: ~/disk/webos-dirty-binder
```

Observed kernel:

```text
Linux LGwebOSTV 4.4.84-229.1.kavir.2 #1 SMP PREEMPT Mon Jan 17 08:08:42 UTC 2022 aarch64 GNU/Linux
Linux version 4.4.84-229.1.kavir.2 (oe-user@oe-host) (gcc version 8.2.0 (GCC) ) #1 SMP PREEMPT Mon Jan 17 08:08:42 UTC 2022
```

Observed Binder device:

```text
/dev/binder
major 10, minor 53
```

On this target, only `/dev/binder` is created. `/dev/hwbinder` and `/dev/vndbinder` are not currently present.

---

## Current status

Confirmed working:

- Binder module builds for LG webOS kernel `4.4.84-229.1.kavir.2`
- module loads on TV with exact vermagic
- `/dev/binder` appears as misc device
- `BINDER_VERSION` returns Binder protocol `8`
- `BINDER_SET_MAX_THREADS` works
- `BINDER_SET_CONTEXT_MGR` works
- Binder mmap path works through the experimental allocation shim
- basic `BC_TRANSACTION` / `BR_TRANSACTION` works
- `BR_REPLY` works
- Binder object passing works
- `BINDER_TYPE_BINDER` to `BINDER_TYPE_HANDLE` works
- `BC_INCREFS_DONE` and `BC_ACQUIRE_DONE` work
- service handle acquisition works
- death notifications work
- `BR_DEAD_BINDER_RAW` is handled
- `BC_DEAD_BINDER_DONE` is sent
- `BR_DEAD_REPLY` is observed when the context manager is gone
- mini service manager works
- `addService` works
- `getService` works
- `listServices` works
- service replacement by duplicate name works
- service death cleanup works
- replacement service survives death of the old replaced service
- context manager restart works
- repeated service rebind works
- multi-service registry works
- concurrent stress calls work
- full TV quick check passes

Latest full validation:

```text
DUPLICATE_SMOKE_OK
ALL_SIDECAR_SMOKE_OK
QUICK_CHECK_TV_OK
```

---

## Safety notes

This is a kernel/module research project for your own device.

Important notes:

- The Binder module is loaded manually.
- The module is effectively permanent until reboot on this target.
- Do not load random kernel modules on devices you cannot recover.
- Do not write to LG system partitions unless you fully understand the recovery path.
- The current Binder mmap allocation shim is experimental and verbose.
- Reboot the TV after kernel Oops, failed module experiments, or unexpected Binder state.
- Treat `/tmp` and `/media/internal/android-sidecar` deployment as disposable.

Recommended workflow:

```sh
# TV reboot after serious kernel-side failures
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
  run-sidecar-all-smoke-tv.sh
  quick-check-tv.sh

tools/
  binder_probe.c
  binder_ping.c
  sidecar_binder.c
```

---

## Build requirements

The build host used for validation was a NanoPi R3S running Ubuntu 24.04.4 LTS on aarch64.

Required tools include:

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

## Build the Binder module

From the project directory:

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

The build script:

- resets the kernel tree
- applies the LG config
- applies the webOS dirty Binder patch
- injects the Binder mmap allocation shim
- configures Binder as a module
- patches incompatible kernel API usage where needed
- builds `drivers/android/binder.ko`
- copies the result to `artifacts/`

---

## Build userland tools

Build the basic Binder probe:

```sh
./scripts/build-probe.sh
```

Build the Binder ping transaction tester:

```sh
./scripts/build-ping.sh
```

Build sidecar tools:

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
```

Important: build Binder userland tools with the project scripts, not with plain host headers.

Correct:

```sh
./scripts/build-ping.sh
```

Incorrect:

```sh
gcc -O2 -static -Wall -Wextra -o build/binder_ping_static tools/binder_ping.c
```

Plain host headers can mismatch Binder UAPI structs and cause invalid Binder command streams or kernel crashes.

---

## Load Binder on the TV

Copy the module and loader manually:

```sh
scp artifacts/binder-dirty-lgc1-o20-4.4.84-229.1.kavir.2.ko root@192.168.2.121:/tmp/binder.ko
scp scripts/load-binder-tv.sh root@192.168.2.121:/tmp/load-binder-tv.sh
```

Load it:

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

The loader resolves non-exported symbols from `/proc/kallsyms` and passes them to `insmod`.

The module parameters include symbols such as:

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

## Basic Binder probe

Copy and run:

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

## Basic Binder transaction ping

Build with the project script:

```sh
./scripts/build-ping.sh
```

Copy:

```sh
scp build/binder_ping_static root@192.168.2.121:/tmp/binder_ping
```

Run server and client:

```sh
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

Expected client success:

```text
client BR_TRANSACTION_COMPLETE
client BR_REPLY code=0x0 flags=0x0
client reply payload: PONG from webOS binder server
```

Expected server side:

```text
BINDER_SET_CONTEXT_MGR ok
server BR_TRANSACTION code=0x50494e47
server payload: PING from webOS binder client
server_reply completed
```

---

## Sidecar deployment

Build:

```sh
./scripts/build-sidecar.sh
```

Install to the TV:

```sh
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
./scripts/install-sidecar-tv.sh
```

Installed layout on the TV:

```text
/media/internal/android-sidecar/
  bin/
    mini_servicemgr
    echo_service
    echo_client
    list_services
  modules/
    binder.ko
  logs/
  run/
  load-binder-tv.sh
```

The installer copies:

```text
build/mini_servicemgr_static -> bin/mini_servicemgr
build/echo_service_static    -> bin/echo_service
build/echo_client_static     -> bin/echo_client
build/list_services_static   -> bin/list_services
binder.ko                    -> modules/binder.ko
load-binder-tv.sh            -> load-binder-tv.sh
```

---

## Sidecar protocol

The sidecar uses Binder transactions to handle a small service-manager protocol.

Current operation codes:

```c
SC_CODE_ADD_SERVICE    = 0x53434144U  /* SCAD */
SC_CODE_GET_SERVICE    = 0x53434745U  /* SCGE */
SC_CODE_LIST_SERVICES  = 0x53434c53U  /* SCLS */
SC_CODE_ECHO           = 0x4543484fU  /* ECHO */
SC_CODE_PING           = 0x50494e47U  /* PING */
```

Implemented flows:

```text
addService(name, binder)
getService(name)
listServices()
ping service before returning handle
echo request/reply
death notification registration
death notification cleanup
duplicate service replacement
```

---

## Run the basic sidecar smoke test

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

This validates:

- mini service manager starts as Binder context manager
- echo service registers with `addService`
- client gets service with `getService`
- service handle is returned as Binder object
- client calls service handle
- echo service replies successfully

---

## Run listServices smoke test

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

This validates:

- empty service registry can be listed
- registered service appears in `listServices`
- regular echo call still works after `listServices`

---

## Run service death smoke test

```sh
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
./scripts/run-sidecar-death-smoke-tv.sh
```

Expected:

```text
echo-client reply status=0 text=echo-service reply from webOS sidecar
BR_DEAD_BINDER_RAW
sm-server: service died name=test.death
BC_DEAD_BINDER_DONE
getService text reply status=1 text=NOT FOUND
DEATH_SMOKE_OK
```

This validates:

- service registers
- client can call it
- service process dies
- Binder sends death notification
- service manager clears registry entry
- subsequent clients fail cleanly

---

## Run multiservice smoke test

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

This validates:

- multiple services can register
- `listServices` lists both
- both services can be called
- killing one service removes only that service
- the other service stays alive and callable

---

## Run stress smoke test

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

This validates repeated concurrent client calls against the same service.

---

## Run service rebind smoke test

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

- a service can register
- client can call it
- service can die
- registry becomes clean
- same service name can register again
- client can call the new instance
- final cleanup works

---

## Run context-manager restart smoke test

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

## Run duplicate service smoke test

```sh
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
./scripts/run-sidecar-duplicate-smoke-tv.sh
```

Expected:

```text
DUPLICATE_SMOKE_OK
```

This validates duplicate-name service replacement:

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

Earlier, death notification cookies were tied to the service registry slot. When a duplicate registration replaced the old service in the same slot, the old service death could incorrectly clear the new service. This was fixed by generating unique death cookies for each service registration.

Expected fixed behavior:

```text
cookie=0x53444301  old service
cookie=0x53444302  replacement service

death cookie 0x53444301 -> not found, registry remains
death cookie 0x53444302 -> service removed
```

---

## Run the full sidecar smoke suite

```sh
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
CLIENTS=6 \
ROUNDS=10 \
./scripts/run-sidecar-all-smoke-tv.sh
```

Expected final line:

```text
ALL_SIDECAR_SMOKE_OK
```

This runs:

```text
run-sidecar-smoke-tv.sh
run-sidecar-list-smoke-tv.sh
run-sidecar-death-smoke-tv.sh
run-sidecar-multiservice-smoke-tv.sh
run-sidecar-stress-smoke-tv.sh
run-sidecar-rebind-smoke-tv.sh
run-sidecar-context-restart-smoke-tv.sh
run-sidecar-duplicate-smoke-tv.sh
```

---

## Run complete TV quick check

This is the current one-command validation path.

```sh
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
CLIENTS=6 \
ROUNDS=10 \
./scripts/quick-check-tv.sh
```

Expected final line:

```text
QUICK_CHECK_TV_OK
```

This validates:

- current git status and commit
- module build
- probe build
- ping build
- sidecar build
- sidecar install
- standalone Binder probe
- standalone Binder ping transaction
- full sidecar smoke suite

Latest observed success:

```text
DUPLICATE_SMOKE_OK
ALL_SIDECAR_SMOKE_OK
QUICK_CHECK_TV_OK
```

---

## Known limitations

Current limitations:

- this is not Android TV userspace
- no Waydroid/Anbox integration yet
- no Android `servicemanager` binary yet
- no Android init
- no ashmem/memfd integration layer yet
- no SELinux integration
- no Android graphics stack
- no Android audio HAL
- no Android input HAL
- only `/dev/binder` is present on the tested TV
- `/dev/hwbinder` and `/dev/vndbinder` are not currently created
- Binder module remains loaded until reboot
- Binder mmap shim is experimental
- debug logging is very verbose
- deployment is manual over SSH

---

## Troubleshooting

### `/dev/binder` missing

Load Binder:

```sh
ssh root@192.168.2.121 '
  /media/internal/android-sidecar/load-binder-tv.sh \
    /media/internal/android-sidecar/modules/binder.ko
'
```

Check:

```sh
ssh root@192.168.2.121 'ls -l /dev/binder; grep binder /proc/misc; grep "^binder " /proc/modules'
```

### `module not found`

Check install layout:

```sh
ssh root@192.168.2.121 '
  cd /media/internal/android-sidecar &&
  find . -maxdepth 3 -type f -o -type d | sort
'
```

Expected module path:

```text
/media/internal/android-sidecar/modules/binder.ko
```

### `mini_servicemgr_static: not found`

The installed binary names are not the build names.

Build host names:

```text
build/mini_servicemgr_static
build/echo_service_static
build/echo_client_static
build/list_services_static
```

TV install names:

```text
bin/mini_servicemgr
bin/echo_service
bin/echo_client
bin/list_services
```

### Kernel Oops after Binder transaction

Do not compile Binder tools with arbitrary system headers. Use the project scripts:

```sh
./scripts/build-ping.sh
./scripts/build-sidecar.sh
```

A previous invalid test binary compiled against mismatched host Binder UAPI headers caused a crash in `binder_thread_write`. Rebuilding with the project script and kernel `4.4.84` UAPI headers fixed the issue.

### Context manager gone

Clients may receive:

```text
BR_DEAD_REPLY
```

This is expected if `mini_servicemgr` died. Restart the service manager and re-register services.

### Death cookie issues

Duplicate service replacement requires unique death cookies per registration. If old service death clears the replacement entry, check that `sidecar_binder.c` uses a monotonically increasing death-cookie sequence rather than the address of the registry slot.

---

## Development workflow

Recommended workflow:

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
./scripts/quick-check-tv.sh
```

Commit only source/scripts unless intentionally updating artifacts:

```sh
git status --short
git add tools/sidecar_binder.c scripts/<changed-script>.sh
git commit -m "..."
```

Avoid accidentally committing rebuilt `.ko` artifacts unless this is intentional.

---

## Milestone summary

This milestone proves:

```text
LG webOS kernel 4.4.84 + dirty Binder module + minimal Binder sidecar userspace
```

can support:

```text
context manager
service registration
service lookup
service listing
Binder object passing
synchronous calls
death notifications
service replacement
context manager restart
basic concurrent traffic
full reproducible TV validation
```

Current final validation command:

```sh
TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
CLIENTS=6 \
ROUNDS=10 \
./scripts/quick-check-tv.sh
```

Current expected success:

```text
QUICK_CHECK_TV_OK
```

---

## Next technical directions

Good next steps:

1. reduce or gate Binder mmap shim debug noise
2. add a service-manager protocol version command
3. add structured status output for `listServices`
4. add persistent sidecar launcher script
5. add controlled restart/watchdog for `mini_servicemgr`
6. investigate why only `/dev/binder` appears on this target
7. evaluate a minimal Android `servicemanager` compatibility layer
8. evaluate ashmem or memfd compatibility
9. experiment with a minimal Android-native Binder client
10. only after that, consider higher-level Android userspace experiments
