# webos-dirty-binder

Experimental Android Binder IPC module and Binder sidecar for rooted LG webOS TVs.

This repository explores whether Android Binder IPC can be built, loaded, and used on LG webOS TV kernels as an out-of-tree module, without replacing the TV boot chain or overwriting system partitions.

> **Status:** experimental research / proof of concept.  
> **Do not install this module at boot yet.** Load it manually while testing.

---

## Current status

The project now demonstrates a working Binder IPC stack on the tested LG webOS TV target.

Confirmed:

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
- Service handles are acquired by the service manager before being stored.
- Returned service handles are acquired by the client before freeing the reply buffer.
- Binder death notifications now work on the tested LG/webOS Binder 4.4 target by using the raw death-notification request encoding.
- Dead services are removed eagerly from the sidecar registry when Binder reports service death.
- Lazy cleanup remains available as a defensive fallback.

Latest important milestones:

```text
plain Binder transaction OK
Binder object passing OK
Binder callback OK
mini service manager OK
addService/getService/call OK
raw Binder death notification request OK
BR_DEAD_BINDER_RAW delivery OK
BC_DEAD_BINDER_DONE acknowledgement OK
eager service registry cleanup on death OK
lazy stale-handle cleanup fallback OK
```

Latest successful death-notification smoke test:

```text
BC_REQUEST_DEATH_NOTIFICATION_RAW service handle: cmd=0x400c630e handle=1 cookie=0x4a1988
BC_REQUEST_DEATH_NOTIFICATION_RAW service handle: write_consumed=16 read_consumed=0
sm-server got BR_DEAD_BINDER_RAW 0x8008720f
sm-server death/clear cmd=0x8008720f cookie=0x4a1988
sm-server: service died name=test.echo handle=1 cookie=0x4a1988
BC_DEAD_BINDER_DONE: cmd=0x40086310 cookie=0x4a1988
BC_DEAD_BINDER_DONE: write_consumed=12 read_consumed=0
sm-server: getService name=test.echo handle=0
getService text reply status=1 text=NOT FOUND
echo-client: getService failed for test.echo
```

`echo-client: getService failed for test.echo` is expected after the service is killed. It means the service manager no longer returns a stale handle.

---

## Tested target

Known working target:

```text
Device family: LG webOS TV
Kernel: 4.4.84-229.1.kavir.2
Architecture: arm64 / aarch64
Binder protocol: 8
Binder devices observed: /dev/binder
hwbinder: not present on tested target
vndbinder: not present on tested target
```

Original development target:

```text
LG OLED C1 webOS TV
webOS TV 6.2.0
O20 platform
```

Other LG webOS versions may require different kernel symbols, configs, offsets, command packing, or patches.

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
- Service registration through `addService`.
- Service lookup through `getService`.
- Binder death notification cleanup.
- Lazy stale-handle cleanup as fallback.

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

This project loads an out-of-tree kernel module on a TV. A bad Binder transaction can crash the kernel.

Recommended safety rules:

- Load the module only manually while testing.
- Keep the sidecar under internal user storage, not system partitions.
- Do not install into boot scripts yet.
- Do not modify `kernel`, `rootfs`, `tvservice`, recovery, boot, or other system partitions.
- Reboot after a kernel Oops before continuing.
- Keep SSH access working.
- Keep the TV on a trusted LAN.
- Do not expose SSH or Binder experiments to the internet.
- Treat every Binder parser change as potentially kernel-crashing until tested.

---

## Repository layout

Common files:

```text
scripts/build-module.sh
    Build the dirty Binder kernel module.

scripts/load-binder-tv.sh
    Load Binder on the TV.

scripts/build-probe.sh
    Build the basic Binder probe.

scripts/build-ping.sh
    Build Binder transaction/object/callback tests.

scripts/build-sidecar.sh
    Build sidecar service manager tools.

scripts/install-sidecar-tv.sh
    Install sidecar files to the TV.

scripts/run-sidecar-smoke-tv.sh
    Run the sidecar smoke test on the TV.

scripts/run-sidecar-death-smoke-tv.sh
    Test Binder death notification behavior.

scripts/run-sidecar-lazy-cleanup-tv.sh
    Test lazy cleanup of stale handles.

tools/binder_probe.c
    Basic Binder ioctl probe.

tools/binder_ping.c
    Low-level Binder transaction/object/callback tests.

tools/sidecar_binder.c
    Mini service manager, echo service, and echo client.

patches/
    Kernel/module patches.

artifacts/
    Built module artifacts.

docs/
    Notes and milestone documentation.
```

Generated files under `build/` should normally not be committed.

Backup files such as `*.bak`, temporary patch scripts, and local test artifacts should normally not be committed unless intentionally preserved.

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

The current sidecar is small, around a few megabytes on the tested target.

---

## Install sidecar to internal storage

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
6. mini_servicemgr requests a death notification for the stored handle.
7. echo_client calls getService("test.echo").
8. mini_servicemgr returns a Binder handle.
9. echo_client acquires the returned handle.
10. echo_client calls the service handle.
11. echo_service receives the transaction.
12. echo_service replies.
13. echo_client receives the reply and exits with 0.
```

---

## Confirmed sidecar smoke log

Successful module load:

```text
Loading /media/internal/android-sidecar/modules/binder.ko
Loaded:
binder 118784 0 [permanent], Live 0xffffffbffc35f000 (O)
crw------- 1 root root 10, 53 /dev/binder
```

Successful service registration:

```text
sm-server BR_TRANSACTION code=0x53434144 sender_pid=3500 sender_euid=0 data_size=96 offsets_size=8
object from txn: offset=72 type=0x73682a85 BINDER_TYPE_HANDLE handle=1 binder=0x1 cookie=0x0
sm-server BC_ACQUIRE service handle: cmd=0x40046305 handle=1
sm-server BC_ACQUIRE service handle: write_consumed=8 read_consumed=0
BC_REQUEST_DEATH_NOTIFICATION_RAW service handle: cmd=0x400c630e handle=1 cookie=0x4a1988
BC_REQUEST_DEATH_NOTIFICATION_RAW service handle: write_consumed=16 read_consumed=0
sm-server: addService name=test.echo handle=1
sm-server registry:
  test.echo -> handle=1
```

Successful service lookup:

```text
sm-server BR_TRANSACTION code=0x53434745 sender_pid=3545 sender_euid=0 data_size=72 offsets_size=0
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
echo-client: calling handle=1 message=before service death
echo-client got BR_REPLY 0x80407203
echo-client reply status=0 text=echo-service reply from webOS sidecar
```

Successful service-side receive:

```text
echo-service BR_TRANSACTION code=0x4543484f sender_pid=3545 sender_euid=0 data_size=21
echo-service request payload: before service death
echo-service reply: write_consumed=80 read_consumed=0
```

---

## Binder death notifications

Death notifications now work on the tested LG/webOS Binder 4.4 target.

The normal `BC_REQUEST_DEATH_NOTIFICATION` encoding produced:

```text
cmd=0x4010630e
ioctl failed errno=22 (Invalid argument)
```

The working request uses the raw 12-byte command encoding:

```text
BC_REQUEST_DEATH_NOTIFICATION_RAW = 0x400c630e
```

The sidecar writes:

```text
uint32_t cmd
uint32_t handle
binder_uintptr_t cookie
```

On the tested arm64 target this gives:

```text
write_size=16
```

The successful request log is:

```text
BC_REQUEST_DEATH_NOTIFICATION_RAW service handle: cmd=0x400c630e handle=1 cookie=0x4a1988
BC_REQUEST_DEATH_NOTIFICATION_RAW service handle: BINDER_WRITE_READ write_size=16 read_size=0
BC_REQUEST_DEATH_NOTIFICATION_RAW service handle: write_consumed=16 read_consumed=0
```

When the service dies, the tested Binder driver returns:

```text
BR_DEAD_BINDER_RAW = 0x8008720f
```

The sidecar handles it as a Binder death notification:

```text
sm-server got BR_DEAD_BINDER_RAW 0x8008720f
sm-server death/clear cmd=0x8008720f cookie=0x4a1988
sm-server: service died name=test.echo handle=1 cookie=0x4a1988
```

Then the sidecar acknowledges the death notification with:

```text
BC_DEAD_BINDER_DONE
```

Confirmed ACK log:

```text
BC_DEAD_BINDER_DONE: cmd=0x40086310 cookie=0x4a1988
BC_DEAD_BINDER_DONE: BINDER_WRITE_READ write_size=12 read_size=0
BC_DEAD_BINDER_DONE: write_consumed=12 read_consumed=0
```

After cleanup, `getService("test.echo")` returns `NOT FOUND` immediately:

```text
sm-server: getService name=test.echo handle=0
sm-server get notfound reply: BINDER_WRITE_READ write_size=80 read_size=0
getService text reply status=1 text=NOT FOUND
echo-client: getService failed for test.echo
```

This is the expected successful behavior after service death.

---

## Why `0x8008720f` matters

The observed death return on the tested target is:

```text
0x8008720f
```

This must be treated as raw `BR_DEAD_BINDER`, not as clear-death-notification-done.

Correct behavior:

```text
receive 0x8008720f
read binder_uintptr_t cookie
remove matching service registry entry
send BC_DEAD_BINDER_DONE(cookie)
```

Incorrect behavior:

```text
receive 0x8008720f
log it as unhandled
do not remove service
do not send BC_DEAD_BINDER_DONE
```

The old bad log was:

```text
sm-server got BR_DEAD_BINDER_RAW 0x8008720f
sm-server unhandled cmd=0x8008720f
```

The new good log is:

```text
sm-server got BR_DEAD_BINDER_RAW 0x8008720f
sm-server death/clear cmd=0x8008720f cookie=0x4a1988
sm-server: service died name=test.echo handle=1 cookie=0x4a1988
BC_DEAD_BINDER_DONE: cmd=0x40086310 cookie=0x4a1988
```

---

## Death notification smoke test

Run:

```bash
cd ~/disk/webos-dirty-binder

TV_IP=192.168.2.121 \
SIDE_DIR=/media/internal/android-sidecar \
./scripts/run-sidecar-death-smoke-tv.sh
```

Expected high-level result:

```text
BEFORE_EXIT=0
AFTER_EXIT=1
```

`AFTER_EXIT=1` is expected because the second client runs after the service has been killed. The important part is that the failure is clean and returns `NOT FOUND`, not a stale handle.

Expected flow:

```text
1. Load binder.ko if needed.
2. Start mini_servicemgr.
3. Start echo_service.
4. echo_service registers test.echo.
5. mini_servicemgr acquires the service handle.
6. mini_servicemgr requests raw Binder death notification.
7. Run echo_client before killing the service.
8. Confirm service call succeeds.
9. Kill echo_service.
10. Binder reports BR_DEAD_BINDER_RAW to mini_servicemgr.
11. mini_servicemgr removes test.echo from the registry by death cookie.
12. mini_servicemgr sends BC_DEAD_BINDER_DONE.
13. Run echo_client again.
14. getService returns NOT FOUND.
```

Good death-cleanup log:

```text
sm-server got BR_DEAD_BINDER_RAW 0x8008720f
sm-server death/clear cmd=0x8008720f cookie=0x4a1988
sm-server: service died name=test.echo handle=1 cookie=0x4a1988
BC_DEAD_BINDER_DONE: cmd=0x40086310 cookie=0x4a1988
BC_DEAD_BINDER_DONE: write_consumed=12 read_consumed=0
```

Good after-death client result:

```text
getService text reply status=1 text=NOT FOUND
echo-client: getService failed for test.echo
```

Bad signs:

```text
BC_REQUEST_DEATH_NOTIFICATION service handle: cmd=0x4010630e
ioctl failed errno=22 (Invalid argument)
```

```text
sm-server unhandled cmd=0x8008720f
```

```text
sm-server: getService name=test.echo handle=1
```

after the service has already died.

---

## Lazy cleanup

Lazy cleanup is no longer the primary service-death mechanism.

The primary path is now:

```text
addService(name, binder)
-> acquire service handle
-> request raw Binder death notification
-> receive BR_DEAD_BINDER_RAW when service dies
-> remove registry entry by death cookie
-> send BC_DEAD_BINDER_DONE
-> future getService(name) returns NOT FOUND
```

Lazy cleanup should still remain in the project as a defensive fallback.

It protects against cases such as:

- Running on a different LG/webOS kernel where the raw death request is rejected.
- A missed or delayed death notification.
- A parser bug in future Binder command handling.
- A service registered before death notifications were enabled.
- A stale entry left by an older build.
- A race where a client asks for the service while death cleanup is still pending.
- Manual experiments that bypass the normal addService path.

Fallback behavior:

```text
getService(name)
-> lookup stored handle
-> ping stored handle
-> if alive: return handle
-> if BR_DEAD_REPLY or BR_FAILED_REPLY: remove service and return NOT FOUND
```

Confirmed fallback log from earlier testing:

```text
sm-server: ping service name=test.echo handle=1
sm-server ping got BR_DEAD_REPLY 0x00007205
sm-server: ping service dead/failed cmd=0x00007205
sm-server: lazy cleanup removing stale service name=test.echo handle=1
sm-server get stale-notfound reply: write_consumed=80 read_consumed=0
```

Current policy:

```text
death notification available -> eager cleanup
death notification missing/failing/racy -> lazy cleanup fallback
```

So lazy cleanup is not strictly necessary for the happy path anymore, but it is still worth keeping.

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

### Death notification request packing

The standard command constant produced the wrong request for this target:

```text
BC_REQUEST_DEATH_NOTIFICATION service handle: cmd=0x4010630e
ioctl failed errno=22 (Invalid argument)
```

The accepted request is the raw 12-byte encoded command:

```text
0x400c630e
```

The sidecar still writes 16 bytes total on arm64 because the payload is:

```text
uint32_t cmd
uint32_t handle
binder_uintptr_t cookie
```

### Death notification response handling

The observed response after service death is:

```text
0x8008720f
```

The sidecar treats this as raw `BR_DEAD_BINDER`.

This response carries:

```text
binder_uintptr_t cookie
```

The service manager removes the registry entry whose `death_cookie` matches the received cookie and then sends:

```text
BC_DEAD_BINDER_DONE(cookie)
```

### Failure replies during getService

`getService` should fail fast on:

```text
BR_DEAD_REPLY
BR_FAILED_REPLY
```

This prevents hangs if the service manager receives a failure reply while trying to validate or return a handle.

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
- Raw Binder death notification request
- Raw dead-binder command handling
- Registry removal by death cookie
- `BC_DEAD_BINDER_DONE` acknowledgement
- `NOT FOUND` after service death
- Lazy cleanup fallback after `BR_DEAD_REPLY`

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

   ```text
   start.sh
   stop.sh
   status.sh
   restart.sh
   ```

2. Add multi-service support:

   ```text
   register more than one service
   list registered services
   replace service by name
   clean dead services by death cookie
   keep lazy cleanup fallback
   ```

3. Add a cleaner service manager protocol:

   ```text
   ADD_SERVICE
   GET_SERVICE
   LIST_SERVICES
   PING_SERVICE
   REMOVE_SERVICE
   structured status codes
   ```

4. Add file descriptor passing:

   ```text
   BINDER_TYPE_FD
   ```

5. Add stress testing:

   ```text
   repeated service registration
   repeated lookup
   repeated service death
   repeated death notification ACK
   forked clients
   service restart loops
   ```

6. Try Android-native userspace components:

   ```text
   Android libbinder
   Android service command
   AOSP servicemanager
   ```

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

The sidecar avoids returning stale handles by:

```text
primary path: death notification removes the service eagerly
fallback path: getService pings the stored handle and lazy-cleans if dead
```

### `BC_REQUEST_DEATH_NOTIFICATION` returns `EINVAL`

This means the normal encoded command is being used:

```text
cmd=0x4010630e
```

The sidecar should use:

```text
BC_REQUEST_DEATH_NOTIFICATION_RAW service handle: cmd=0x400c630e
```

Rebuild and reinstall the sidecar tools.

### `sm-server unhandled cmd=0x8008720f`

The service manager received the raw dead-binder notification but did not dispatch it.

Expected fixed behavior:

```text
sm-server got BR_DEAD_BINDER_RAW 0x8008720f
sm-server death/clear cmd=0x8008720f cookie=...
BC_DEAD_BINDER_DONE: cmd=0x40086310 cookie=...
```

If `unhandled` still appears, rebuild, reinstall, and confirm the installed `mini_servicemgr` is the newly built binary.

### `echo-client: getService failed for test.echo`

This is expected in the death smoke test after `echo_service` has been killed.

Good after-death behavior:

```text
getService text reply status=1 text=NOT FOUND
echo-client: getService failed for test.echo
```

Bad behavior would be returning a stale handle after the service died.

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

Recommended README update commit:

```bash
cd ~/disk/webos-dirty-binder

cp /path/to/README.md README.md

git status --short
git diff -- README.md

git add README.md
git commit -m "docs: update sidecar death notification status"
git push origin main
```

Avoid committing generated files unless intentionally publishing binaries:

```text
build/
*.o
*.ko
*.bak
*.bak.*
```

If local scratch scripts are useful, keep them untracked or move them into a deliberate `tools/` or `scripts/` commit.

---

## Disclaimer

This is experimental kernel research code.

Use at your own risk. It can crash the TV kernel. It is not intended for production use.
