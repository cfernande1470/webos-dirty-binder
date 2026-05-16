# webos-dirty-binder

Experimental Android Binder IPC module for rooted LG webOS TVs.

This repository explores whether the Android Binder driver can be built and loaded as an out-of-tree kernel module on LG webOS TV kernels, without flashing or replacing system partitions.

> **Status:** experimental research / proof of concept.  
> **Do not install this module at boot.** Load it manually from `/tmp` while testing.

---

## Current milestone

The project has moved beyond basic Binder ioctls.

Confirmed working on the tested LG webOS TV target:

- Load `binder.ko`
- Create `/dev/binder`
- `BINDER_VERSION`
- `BINDER_SET_MAX_THREADS`
- Binder `mmap()`
- `BINDER_SET_CONTEXT_MGR`
- `BC_ENTER_LOOPER`
- Plain Binder transaction round-trip:
  - client sends `BC_TRANSACTION`
  - server receives `BR_TRANSACTION`
  - server sends `BC_REPLY`
  - client receives `BR_REPLY`
  - client releases buffer with write-only `BC_FREE_BUFFER`
- Binder object passing:
  - client sends `BINDER_TYPE_BINDER`
  - server receives it as `BINDER_TYPE_HANDLE`
- Binder callback:
  - server calls back into the client-exported Binder object
  - client receives callback `BR_TRANSACTION`
  - client replies with `BC_REPLY`
  - server receives callback `BR_REPLY`

This means the PoC now demonstrates real Binder IPC in both directions.

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

Other LG webOS versions may require different kernel symbols, offsets, configs, or patches.

---

## What this is

This is a kernel/Binder research project.

It is useful for:

- Understanding Binder IPC on a non-Android Linux environment
- Testing Binder transactions on webOS
- Experimenting with Binder object passing
- Experimenting with Binder callbacks
- Building future Binder-to-webOS bridge experiments
- Investigating whether small Android-native Binder services can run on webOS

---

## What this is not

This is **not**:

- Android TV for LG webOS
- Waydroid for LG webOS
- Anbox for LG webOS
- APK app support
- A complete Android userspace
- A graphics/audio/input compatibility layer
- A safe production module
- A boot-time service

The module currently does **not** provide:

- `ashmem`
- `binderfs`
- Android `init`
- Android service manager integration
- SELinux policy
- Android HALs
- Android framework services
- SurfaceFlinger
- AudioFlinger
- ActivityManager
- PackageManager
- InputFlinger
- Hardware Composer

---

## Safety warning

This is an out-of-tree kernel module for a TV.

A bad Binder transaction can crash the kernel.

Recommended safety rules:

- Load only from `/tmp`
- Do not install into boot scripts
- Do not modify boot, recovery, kernel, rootfs, or tvservice partitions
- Reboot between risky tests
- Keep SSH access working
- Keep the TV on a trusted local network
- Do not expose SSH or Binder experiments to the internet

If the module Oopses, reboot the TV before continuing.

---

## Repository layout

Common files:

```text
scripts/build-module.sh       Build the dirty Binder kernel module
scripts/load-binder-tv.sh     Load the module on the TV
scripts/build-probe.sh        Build the basic Binder probe
scripts/build-ping.sh         Build the Binder transaction/object test tool
tools/binder_probe.c          Basic Binder ioctl probe
tools/binder_ping.c           Binder IPC test tool
patches/                      Kernel/module patches
artifacts/                    Built module artifacts
docs/                         Notes and milestone documentation
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

The build host used during testing was a NanoPi running Ubuntu 24.04 on arm64.

---

## Build module

From the repo root:

```bash
cd ~/disk/webos-dirty-binder
./scripts/build-module.sh
```

The generated module is expected at:

```text
build/linux-4.4.84/drivers/android/binder.ko
```

A copy may also be placed under `artifacts/`.

---

## Build test tools

Build the basic probe:

```bash
cd ~/disk/webos-dirty-binder
./scripts/build-probe.sh
```

Build the Binder IPC test tool:

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

## Copy files to TV

Replace the IP address with your TV IP.

```bash
cd ~/disk/webos-dirty-binder

TV_IP=192.168.2.121

scp build/linux-4.4.84/drivers/android/binder.ko root@$TV_IP:/tmp/binder-dirty.ko
scp scripts/load-binder-tv.sh root@$TV_IP:/tmp/load-binder-tv.sh
scp build/binder_probe_static root@$TV_IP:/tmp/binder_probe
scp build/binder_ping_static root@$TV_IP:/tmp/binder_ping
```

---

## Load module on TV

```bash
ssh root@192.168.2.121
cd /tmp
chmod +x /tmp/load-binder-tv.sh /tmp/binder_probe /tmp/binder_ping
/tmp/load-binder-tv.sh /tmp/binder-dirty.ko
```

Expected:

```text
/dev/binder exists
binder listed in /proc/modules
```

Check:

```bash
ls -l /dev/binder
grep binder /proc/modules
```

---

## Basic probe

```bash
/tmp/binder_probe
echo "probe_exit=$?"
```

Expected:

```text
BINDER_VERSION protocol_version=8
BINDER_SET_MAX_THREADS ok
probe_exit=0
```

---

## Plain transaction test

Start the server:

```bash
cd /tmp
./binder_ping server
```

In another SSH session, run the client:

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

## Object passing test

Start object server:

```bash
cd /tmp
./binder_ping object-server
```

In another SSH session, run object client:

```bash
cd /tmp
./binder_ping object-client
```

Expected server result:

```text
object-server object[0]: ... BINDER_TYPE_HANDLE ... handle=1
```

This confirms that the client sent a local `BINDER_TYPE_BINDER` object and the Binder driver translated it into a remote `BINDER_TYPE_HANDLE`.

---

## Callback test

The current `object-server` / `object-client` flow also tests callback behavior.

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

Expected server log fragment:

```text
object-server object[0]: offset=0 type=0x73682a85 BINDER_TYPE_HANDLE flags=0x00000100 binder=0x1 handle=1 cookie=0x0
object-server calling client handle=1
object-server callback BR_REPLY
object-server callback reply payload: CLIENT CALLBACK OK
```

Expected client log fragment:

```text
object-client BR_INCREFS ...
object-client BC_INCREFS_DONE ...
object-client BR_ACQUIRE ...
object-client BC_ACQUIRE_DONE ...
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

The module uses a Binder mmap allocation shim to avoid allocation/page mapping failures on this kernel.

The shim allocates a page and inserts it into the userspace VMA with `vm_insert_page()`.

Observed successful log pattern:

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

After receiving a `BR_REPLY`, the client releases the reply buffer using `BC_FREE_BUFFER`.

This must be sent write-only:

```c
read_size = 0;
read_buffer = 0;
```

Otherwise the client can wait for process work without being a looper.

### Binder object refcount commands

When exporting a Binder object, the client can receive commands such as:

```text
BR_INCREFS
BR_ACQUIRE
```

The client must consume their `binder_ptr_cookie` payloads and respond with:

```text
BC_INCREFS_DONE
BC_ACQUIRE_DONE
```

Otherwise the parser desynchronizes and payload words are mistaken for Binder commands.

---

## Current capabilities

Confirmed:

- Binder device creation
- Basic Binder ioctls
- Binder mmap
- Context manager
- Blocking Binder server loop
- Synchronous client transaction
- Synchronous server reply
- Buffer free
- Binder object passing
- Refcount command handling
- Callback transaction
- Callback reply

Not yet confirmed:

- `hwbinder`
- `vndbinder`
- Binder service manager compatibility
- AOSP `servicemanager`
- Android `libbinder` userspace
- Death notifications
- File descriptor transfer
- Stress testing
- Multiple clients
- Multiple exported objects
- Handle reuse/lifetime cleanup
- Real Android-native daemons

---

## Suggested next milestones

1. Split `binder_ping` into clearer subcommands:
   - `ping-server`
   - `ping-client`
   - `object-server`
   - `object-client`
   - `callback-server`
   - `callback-client`
   - `stress`

2. Add automated exit conditions so servers can terminate after one transaction.

3. Add a stress mode:
   - repeated transactions
   - repeated object exports
   - repeated callbacks
   - forked clients

4. Add Binder death notification tests:
   - `BC_REQUEST_DEATH_NOTIFICATION`
   - `BR_DEAD_BINDER`
   - `BC_CLEAR_DEATH_NOTIFICATION`

5. Test FD passing:
   - `BINDER_TYPE_FD`

6. Try a minimal AOSP-style Binder service manager experiment.

7. Prototype a Binder-to-webOS Luna Bus bridge.

---

## Git workflow

Recommended flow for milestones:

```bash
git status --short
git add README.md scripts/build-module.sh scripts/build-ping.sh tools/binder_ping.c
git commit -m "Describe milestone"
git push origin HEAD:main
```

Avoid committing generated files unless intentionally publishing binaries:

```text
build/
*.o
*.ko
```

---

## Troubleshooting

### `/dev/binder` does not exist

The module is not loaded, or the TV was rebooted.

Load it again:

```bash
cd /tmp
./load-binder-tv.sh /tmp/binder-dirty.ko
```

### `open /dev/binder: No such file or directory`

Same as above: reload the module.

### `scp: dest open "/tmp/binder_ping": Failure`

The binary may be running or locked.

```bash
ssh root@TV_IP 'killall binder_ping 2>/dev/null || true; rm -f /tmp/binder_ping'
scp build/binder_ping_static root@TV_IP:/tmp/binder_ping
ssh root@TV_IP 'chmod +x /tmp/binder_ping'
```

### Kernel Oops during transaction

Reboot before continuing.

```bash
ssh root@TV_IP 'sync; reboot'
```

Then verify that the module you load includes the `current_euid()` transaction fix.

### Server log missing

Use absolute paths under `/tmp` when redirecting logs from background processes.

Example:

```bash
cd /tmp
nohup ./binder_ping object-server > /tmp/binder_object_server.log 2>&1 &
echo $! > /tmp/binder_object_server.pid
```

---

## Disclaimer

This is experimental kernel research code.

Use at your own risk. It can crash the TV kernel. It is not intended for production use.
