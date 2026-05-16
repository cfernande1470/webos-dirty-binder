# webos-dirty-binder

Experimental Android Binder IPC module and Binder sidecar for rooted LG webOS TVs.

This repository explores whether Android Binder IPC can be built, loaded, and used on LG webOS TV kernels as an out-of-tree module, without replacing the TV boot chain or overwriting system partitions.

> **Status:** experimental research / proof of concept.  
> **Do not install this module at boot yet.** Load it manually while testing.

---

## Current status

The project now demonstrates a working Binder IPC stack on LG webOS.

Confirmed on the tested LG webOS TV target:

- Binder kernel module loads successfully.
- `/dev/binder` is created.
- Basic Binder ioctls work.
- Binder mmap works through an allocation shim.
- A process can become Binder context manager.
- Plain synchronous Binder transactions work.
- Binder objects can be passed between processes.
- Binder callbacks work.
- A sidecar service manager works from internal storage.
- A service can register itself by name.
- A client can resolve that service by name.
- A client can call the service through the returned Binder handle.
- Dead/stale services are detected through lazy cleanup.

Latest important milestones:

```text
plain Binder transaction       OK
Binder object passing          OK
Binder callback                OK
mini service manager           OK
addService/getService/call     OK
lazy stale-handle cleanup      OK
```

The latest sidecar smoke test returned:

```text
CLIENT_EXIT=0
echo-client reply status=0 text=echo-service reply from webOS sidecar
```

The latest lazy cleanup test returned:

```text
BEFORE_EXIT=0
AFTER_EXIT=1
sm-server: lazy cleanup removing stale service name=test.echo handle=1
getService text reply status=1 text=NOT FOUND
```

---

## Tested target

Known working target:

```text
Device family: LG webOS TV
Kernel:        4.4.84-229.1.kavir.2
Architecture:  arm64 / aarch64
Binder proto:  8
```

Original development target:

```text
LG OLED C1
webOS TV 6.2.0
O20 platform
```

Other LG webOS versions may require different kernel symbols, configs, offsets, or patches.

---

## What this project is

This is a Binder IPC research project for webOS.

It currently provides:

- A dirty Binder kernel module for LG webOS.
- A Binder mmap allocation shim.
- Low-level Binder probes.
- A Binder transaction test tool.
- Binder object passing tests.
- Binder callback tests.
- A small Binder sidecar service manager.
- An echo service and echo client using Binder handles.
- Lazy cleanup of stale service handles.

It is useful for:

- Understanding Binder IPC outside Android.
- Testing Binder transactions on webOS.
- Testing Binder object lifetime rules.
- Experimenting with service registration and lookup.
- Building a possible Binder-to-webOS bridge.
- Preparing future Android-native userspace experiments.

---

## What this project is not

This is **not**:

- Android TV for LG webOS.
- Waydroid for LG webOS.
- Anbox for LG webOS.
- APK app support.
- A complete Android userspace.
- A graphics/audio/input compatibility layer.
- A production-ready module.
- A boot-time service.

The project does **not** currently provide:

- `ashmem`
- `binderfs`
- Android `init`
- Android `servicemanager`
- Android `zygote`
- ART
- `system_server`
- PackageManager
- SurfaceFlinger
- AudioFlinger
- InputFlinger
- Android HALs
- SELinux policy
- APK installation support

---

## Safety warning

This project loads an out-of-tree kernel module on a TV.

A bad Binder transaction can crash the kernel.

Recommended safety rules:

- Load only manually while testing.
- Keep the sidecar under internal user storage, not system partitions.
- Do not install into boot scripts yet.
- Do not modify `kernel`, `rootfs`, `tvservice`, recovery, boot, or other system partitions.
- Reboot after a kernel Oops before continuing.
- Keep SSH access working.
- Keep the TV on a trusted LAN.
- Do not expose SSH or Binder experiments to the internet.

---

## Repository layout

Common files:

```text
scripts/build-module.sh                Build the dirty Binder kernel module
scripts/load-binder-tv.sh              Load Binder on the TV
scripts/build-probe.sh                 Build the basic Binder probe
scripts/build-ping.sh                  Build Binder transaction/object tests
scripts/build-sidecar.sh               Build sidecar service manager tools
scripts/install-sidecar-tv.sh          Install sidecar files to the TV
scripts/run-sidecar-smoke-tv.sh        Run the sidecar smoke test on the TV
scripts/run-sidecar-death-smoke-tv.sh  Test Binder death notification behavior
scripts/run-sidecar-lazy-cleanup-tv.sh Test lazy cleanup of stale handles

tools/binder_probe.c                   Basic Binder ioctl probe
tools/binder_ping.c                    Low-level Binder transaction/object/callback tests
tools/sidecar_binder.c                 Mini service manager, echo service, echo client

patches/                               Kernel/module patches
artifacts/                             Built module artifacts
docs/                                  Notes and milestone documentation
```

Generated files under `build/` should normally not be committed.

---

## Build prerequisites

On the build host, install an aarch64 cross compiler.

Example on Ubuntu/Debian:

```bash
sudo apt update
sudo apt install -y build-essential gcc-aarch64-linux-gnu git python3
```

The tested build host was a NanoPi running Ubuntu 24.04 on arm64.

---

## Build Binder module

From the repository root:

```bash
cd ~/disk/webos-dirty-binder
./scripts/build-module.sh
```

Expected module:

```text
build/linux-4.4.84/drivers/android/binder.ko
```

---

## Build low-level test tools

Build the basic Binder probe:

```bash
cd ~/disk/webos-dirty-binder
./scripts/build-probe.sh
```

Build the Binder transaction/object/callback tool:

```bash
cd ~/disk/webos-dirty-binder
./scripts/build-ping.sh
```

Expected outputs:

```text
build/binder_probe_static
build/binder_ping_static
```

---

## Build sidecar tools

Build the sidecar service manager, echo service, and echo client:

```bash
cd ~/disk/webos-dirty-binder
./scripts/build-sidecar.sh
```

Expected outputs:

```text
build/sidecar_binder_static
build/mini_servicemgr_static
build/echo_service_static
build/echo_client_static
```

---

## Sidecar location on TV

Recommended sidecar location on the tested TV:

```text
/media/internal/android-sidecar
```

This is preferred over `/home/root/android-sidecar` on the tested device because `/media/internal` has more free space.

Installed layout:

```text
/media/internal/android-sidecar/
  bin/
    mini_servicemgr
    echo_service
    echo_client
  modules/
    binder.ko
  logs/
    mini_servicemgr.log
    echo_service.log
    echo_client.log
  run/
    mini_servicemgr.pid
    echo_service.pid
    echo_client.exit
  load-binder-tv.sh
```

The current sidecar is small:

```text
2.9M /media/internal/android-sidecar
```

---

## Install sidecar to internal storage

Install:

```bash
cd ~/disk/webos-dirty-binder

TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
./scripts/install-sidecar-tv.sh
```

---

## Run sidecar smoke test

From the build host:

```bash
cd ~/disk/webos-dirty-binder

TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
./scripts/run-sidecar-smoke-tv.sh
```

Expected result:

```text
CLIENT_EXIT=0
echo-client reply status=0 text=echo-service reply from webOS sidecar
```

The smoke test performs:

```text
1. Load binder.ko if /dev/binder does not exist.
2. Start mini_servicemgr as Binder context manager.
3. Start echo_service.
4. echo_service calls addService("test.echo").
5. mini_servicemgr stores test.echo -> handle=1.
6. echo_client calls getService("test.echo").
7. mini_servicemgr returns a Binder handle.
8. echo_client acquires the returned handle.
9. echo_client calls the service handle.
10. echo_service receives the transaction.
11. echo_service replies.
12. echo_client receives the reply and exits with 0.
```

---

## Confirmed sidecar smoke log

Successful module load:

```text
Loading /media/internal/android-sidecar/modules/binder.ko
Loaded:
binder 118784 0 [permanent], Live 0xffffffbffc35f000 (O)
crw-------    1 root     root       10,  53 May 16 04:20 /dev/binder
CLIENT_EXIT=0
```

Successful service registration:

```text
sm-server BR_TRANSACTION code=0x53434144 sender_pid=3531 sender_euid=0 data_size=96 offsets_size=8
object from txn: offset=72 type=0x73682a85 BINDER_TYPE_HANDLE handle=1 binder=0x1 cookie=0x0
sm-server BC_ACQUIRE service handle: cmd=0x40046305 handle=1
sm-server BC_ACQUIRE service handle: write_consumed=8 read_consumed=0
sm-server: addService name=test.echo handle=1
sm-server registry:
  test.echo -> handle=1
```

Successful service lookup:

```text
sm-server BR_TRANSACTION code=0x53434745 sender_pid=3587 sender_euid=0 data_size=72 offsets_size=0
sm-server: getService name=test.echo handle=1
sm-server: replying with handle=1 status=0
sm-server getService reply: write_consumed=80 read_consumed=0
```

Successful client handle acquisition:

```text
getService got BR_REPLY 0x80407203
object from txn: offset=8 type=0x73682a85 BINDER_TYPE_HANDLE handle=1 binder=0x1 cookie=0x0
getService: name=test.echo got handle=1
getService BC_ACQUIRE returned handle: cmd=0x40046305 handle=1
getService BC_ACQUIRE returned handle: write_consumed=8 read_consumed=0
```

Successful service call:

```text
echo-client: calling handle=1 message=hello from sidecar smoke
echo-client got BR_REPLY 0x80407203
echo-client reply status=0 text=echo-service reply from webOS sidecar
```

Successful service-side receive:

```text
echo-service BR_TRANSACTION code=0x4543484f sender_pid=3587 sender_euid=0 data_size=25
echo-service request payload: hello from sidecar smoke
echo-service reply: write_consumed=80 read_consumed=0
```

---

## Lazy cleanup smoke test

Because `BC_REQUEST_DEATH_NOTIFICATION` currently returns `EINVAL` on the tested LG webOS Binder target, the sidecar service manager uses lazy cleanup.

Run:

```bash
cd ~/disk/webos-dirty-binder

TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
./scripts/run-sidecar-lazy-cleanup-tv.sh
```

Expected result:

```text
BEFORE_EXIT=0
AFTER_EXIT=1
```

Flow:

```text
1. Start mini_servicemgr.
2. Start echo_service.
3. Register test.echo.
4. Run echo_client before killing the service.
5. Confirm the call succeeds.
6. Kill echo_service.
7. Run echo_client again.
8. mini_servicemgr pings the stored handle.
9. Binder returns BR_DEAD_REPLY.
10. mini_servicemgr removes test.echo from the registry.
11. getService returns NOT FOUND.
```

Confirmed lazy cleanup log:

```text
sm-server: ping service name=test.echo handle=1
sm-server ping got BR_DEAD_REPLY 0x00007205
sm-server: ping service dead/failed cmd=0x00007205
sm-server: lazy cleanup removing stale service name=test.echo handle=1
sm-server get stale-notfound reply: write_consumed=80 read_consumed=0
```

Confirmed client-side result after cleanup:

```text
getService text reply status=1 text=NOT FOUND
echo-client: getService failed for test.echo
```

---

## Binder death notification behavior

Death notifications were tested with:

```text
BC_REQUEST_DEATH_NOTIFICATION
```

The command is now packed as:

```text
uint32_t cmd
uint32_t handle
binder_uintptr_t cookie
```

On arm64 this produces:

```text
write_size=16
```

Confirmed log:

```text
BC_REQUEST_DEATH_NOTIFICATION service handle: BINDER_WRITE_READ write_size=16 read_size=0
BC_REQUEST_DEATH_NOTIFICATION service handle: ioctl failed errno=22 (Invalid argument)
```

This means the original struct-padding issue was eliminated, but this LG/webOS Binder 4.4 target still rejects the command with `EINVAL`.

Current policy:

```text
Death notification request fails -> continue running
getService() validates the stored handle through a Binder ping
dead handle -> lazy cleanup -> NOT FOUND
```

This keeps the sidecar functional even without automatic death notifications.

---

## Low-level Binder transaction test

Start server on the TV:

```bash
cd /tmp
./binder_ping server
```

In another SSH session:

```bash
cd /tmp
./binder_ping client
```

Expected client result:

```text
client BR_REPLY
client reply payload: PONG from webOS binder server
free_buffer: write_consumed=12 read_consumed=0
```

---

## Binder object passing test

Start object server:

```bash
cd /tmp
./binder_ping object-server
```

In another SSH session:

```bash
cd /tmp
./binder_ping object-client
```

Expected server result:

```text
object-server object[0]: offset=0 type=0x73682a85 BINDER_TYPE_HANDLE flags=0x00000100 binder=0x1 handle=1 cookie=0x0
```

This confirms:

```text
client BINDER_TYPE_BINDER -> server BINDER_TYPE_HANDLE
```

---

## Binder callback test

The `object-server` / `object-client` flow also demonstrates callbacks.

Expected sequence:

```text
client sends BINDER_TYPE_BINDER
server receives BINDER_TYPE_HANDLE handle=1
server calls handle=1
client receives callback BR_TRANSACTION
client replies with BC_REPLY
server receives callback BR_REPLY
server replies to original client transaction
client receives OBJECT OK
```

Expected server fragment:

```text
object-server calling client handle=1
object-server callback BR_REPLY
object-server callback reply payload: CLIENT CALLBACK OK
```

Expected client fragment:

```text
object-client callback transaction code=0x43424b31
object-client callback payload: CALLBACK from object-server
object-client callback reply write_consumed=80 read_consumed=0
object-client reply payload: OBJECT OK
```

---

## Important implementation notes

### `task_euid(proc->tsk)` crash

The stock Binder transaction path crashed on this LG webOS kernel at:

```c
t->sender_euid = task_euid(proc->tsk);
```

The observed crash was a NULL pointer dereference during `BC_TRANSACTION`.

For this PoC, the module patches that path to:

```c
t->sender_euid = current_euid();
```

This keeps `sender_euid` meaningful for the userspace task issuing the ioctl and avoids the crash on the tested target.

### Binder mmap allocation shim

The module uses a Binder mmap allocation shim.

Observed successful pattern:

```text
binder_alloc_shim: before __get_free_page
binder_alloc_shim: after __get_free_page
binder_alloc_shim: virt_to_page
binder_alloc_shim: before vm_insert_page
binder_alloc_shim: after vm_insert_page ret=0
```

### Do not ignore read data from `BC_ENTER_LOOPER`

`BINDER_WRITE_READ` can return `BR_TRANSACTION` in the same ioctl that writes `BC_ENTER_LOOPER`.

The server must process the read buffer returned by the enter-looper call.

### `BC_FREE_BUFFER` should be write-only

After receiving `BR_REPLY`, release the reply buffer using write-only `BC_FREE_BUFFER`:

```c
read_size = 0;
read_buffer = 0;
```

Otherwise the process can block waiting for Binder work without being a looper.

### Binder object refcount commands

When exporting a Binder object, the owner can receive:

```text
BR_INCREFS
BR_ACQUIRE
```

The owner must consume their `binder_ptr_cookie` payloads and respond with:

```text
BC_INCREFS_DONE
BC_ACQUIRE_DONE
```

Otherwise the command parser desynchronizes.

### Service manager handle lifetime

The sidecar service manager must acquire service handles it stores:

```text
BC_ACQUIRE service handle
```

The client must also acquire a handle received through `getService()` before freeing the reply buffer:

```text
getService BC_ACQUIRE returned handle
```

Without these acquisitions, later calls can fail with:

```text
BR_FAILED_REPLY
```

### Death notifications and lazy cleanup

`BC_REQUEST_DEATH_NOTIFICATION` currently fails on the tested LG/webOS kernel:

```text
errno=22 (Invalid argument)
```

The sidecar therefore uses lazy cleanup:

```text
getService(name)
  -> ping stored handle
  -> if alive, return handle
  -> if BR_DEAD_REPLY, remove service and return NOT FOUND
```

---

## Current capabilities

Confirmed:

- Binder device creation
- Basic Binder ioctls
- Binder mmap
- Context manager
- Blocking Binder server loop
- Synchronous Binder transactions
- Synchronous replies
- Write-only buffer free
- Binder object passing
- Binder callbacks
- Binder refcount command handling
- Internal-storage sidecar
- Mini service manager
- `addService(name, binder)`
- `getService(name) -> handle`
- Client call through returned handle
- Service response through Binder
- Stale service detection through Binder ping
- Lazy cleanup after `BR_DEAD_REPLY`

Known limitation:

- `BC_REQUEST_DEATH_NOTIFICATION` returns `EINVAL` on the tested target.

Not yet confirmed:

- `hwbinder`
- `vndbinder`
- Binder service manager compatibility with AOSP
- Android `libbinder` userspace binaries
- Android `servicemanager`
- File descriptor transfer
- Multiple services
- Multiple clients
- Full release/cleanup lifecycle
- Stress testing
- Android-native daemons
- APK support

---

## Suggested next milestones

1. Add sidecar lifecycle scripts:
   - `start.sh`
   - `stop.sh`
   - `status.sh`

2. Add multi-service support:
   - register more than one service
   - list registered services
   - replace service by name
   - handle stale services through lazy cleanup

3. Add a cleaner service manager protocol:
   - `ADD_SERVICE`
   - `GET_SERVICE`
   - `LIST_SERVICES`
   - `PING_SERVICE`
   - structured status codes

4. Add file descriptor passing:
   - `BINDER_TYPE_FD`

5. Add stress testing:
   - repeated service registration
   - repeated lookup
   - repeated stale cleanup
   - forked clients
   - service restart loops

6. Try Android-native userspace components:
   - Android `libbinder`
   - Android `service` command
   - AOSP `servicemanager`

7. Prototype a Binder-to-webOS Luna Bus bridge.

---

## Troubleshooting

### `/dev/binder` does not exist

The module is not loaded, or the TV rebooted.

Load it again:

```bash
cd /media/internal/android-sidecar
./load-binder-tv.sh modules/binder.ko
```

### `open /dev/binder: No such file or directory`

Same as above: reload the module.

### `BINDER_SET_CONTEXT_MGR already set`

A previous process is still holding the Binder context manager.

Stop sidecar processes:

```bash
killall mini_servicemgr 2>/dev/null || true
killall echo_service 2>/dev/null || true
killall echo_client 2>/dev/null || true
killall binder_ping 2>/dev/null || true
```

If it still fails, reboot the TV.

### `BR_FAILED_REPLY` after getService

Check handle lifetime.

The service manager must acquire the stored service handle, and the client must acquire the returned handle before freeing the reply buffer.

### `BR_DEAD_REPLY` after service death

This is expected if a client tries to call a dead handle directly.

The sidecar avoids returning stale handles by pinging services during `getService()` and removing dead entries through lazy cleanup.

### `BC_REQUEST_DEATH_NOTIFICATION` returns EINVAL

This is currently observed on the tested LG/webOS Binder 4.4 target.

Lazy cleanup is used as a working fallback.

### `scp: dest open "/tmp/binder_ping": Failure`

The binary may be running or locked.

```bash
ssh root@TV_IP 'killall binder_ping 2>/dev/null || true; rm -f /tmp/binder_ping'
scp build/binder_ping_static root@TV_IP:/tmp/binder_ping
ssh root@TV_IP 'chmod +x /tmp/binder_ping'
```

### Kernel Oops during transaction

Reboot before continuing:

```bash
ssh root@TV_IP 'sync; reboot'
```

Then verify that the loaded module includes the `current_euid()` transaction fix.

---

## Git workflow

Recommended milestone commit:

```bash
git status --short
git add README.md \
        tools/sidecar_binder.c \
        scripts/build-sidecar.sh \
        scripts/install-sidecar-tv.sh \
        scripts/run-sidecar-smoke-tv.sh \
        scripts/run-sidecar-death-smoke-tv.sh \
        scripts/run-sidecar-lazy-cleanup-tv.sh
git commit -m "Add Binder sidecar service manager lazy cleanup"
git push origin HEAD:main
```

Avoid committing generated files unless intentionally publishing binaries:

```text
build/
*.o
*.ko
```

---

## Disclaimer

This is experimental kernel research code.

Use at your own risk. It can crash the TV kernel. It is not intended for production use.
