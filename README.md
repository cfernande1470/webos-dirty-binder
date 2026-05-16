# webOS Dirty Binder

Experimental Android Binder kernel module work for LG webOS TVs.

> ⚠️ **Danger / brick warning**
>
> This repository is for kernel-module research on already-rooted LG webOS TVs.
> Do **not** overwrite LG system partitions such as kernel, rootfs, boot,
> recovery, tvservice, or similar partitions. A bad write there can brick the TV.
>
> Keep all tests temporary: copy binaries to `/tmp`, load the module manually,
> and reboot to clean up.

## Current status

This project now reaches a first real Binder transaction round-trip on an LG webOS TV using kernel:

```text
4.4.84-229.1.kavir.2
```

Confirmed working:

- Build an out-of-tree `binder.ko` for LG webOS kernel `4.4.84-229.1.kavir.2`.
- Load the module manually on the TV.
- Create `/dev/binder`.
- Run `BINDER_VERSION` successfully.
- Run `BINDER_SET_MAX_THREADS` successfully.
- `mmap()` the Binder buffer.
- Register a Binder context manager with `BINDER_SET_CONTEXT_MGR`.
- Enter the Binder looper with `BC_ENTER_LOOPER`.
- Send `BC_TRANSACTION` from a client to handle `0`.
- Receive `BR_TRANSACTION` in the server.
- Send `BC_REPLY` from the server.
- Receive `BR_REPLY` in the client.
- Release the reply buffer using write-only `BC_FREE_BUFFER`.

Observed successful client output:

```text
open /dev/binder
ioctl BINDER_VERSION
binder protocol_version=8
ioctl BINDER_SET_MAX_THREADS
BINDER_SET_MAX_THREADS ok
mmap binder size=1048576
client sending transaction to handle 0
client_call: BINDER_WRITE_READ write_size=68 read_size=8192
client_call: write_consumed=68 read_consumed=76
client got cmd=0x0000720c
client BR_NOOP
client got cmd=0x00007206
client BR_TRANSACTION_COMPLETE
client got cmd=0x80407203
client BR_REPLY code=0x0 flags=0x0 data_size=30 offsets_size=0
client reply payload: PONG from webOS binder server
free buffer ... write-only
free_buffer: write_consumed=12 read_consumed=0
```

Observed successful server output:

```text
open /dev/binder
ioctl BINDER_VERSION
binder protocol_version=8
ioctl BINDER_SET_MAX_THREADS
BINDER_SET_MAX_THREADS ok
mmap binder size=1048576
ioctl BINDER_SET_CONTEXT_MGR
BINDER_SET_CONTEXT_MGR ok
server_enter_looper: BINDER_WRITE_READ write_size=4 read_size=8192
server_enter_looper: write_consumed=4 read_consumed=72
server_process_readbuf: n=72
server got cmd=0x0000720d
server BR_SPAWN_LOOPER ignored
server got cmd=0x80407202
server BR_TRANSACTION code=0x50494e47 flags=0x10 sender_euid=0
server payload: PING from webOS binder client
server_reply: BINDER_WRITE_READ write_size=80 read_size=8192
server_reply: write_consumed=80 read_consumed=8
server_reply completed
server waiting for transactions
```

## What this is not

This is **not** Android TV on webOS.

This does not provide:

- Android userspace
- Android init
- SELinux policy
- Waydroid
- Anbox
- hwbinder support
- vndbinder support
- ashmem
- Android graphics stack
- Android audio HALs
- Android input HALs
- APK runtime support

This repository currently proves that a dirty Binder kernel-module experiment can create `/dev/binder` and complete a minimal Binder transaction round-trip.

## Repository layout

```text
patches/
  0001-lg-webos-dirty-binder-module.patch

scripts/
  build-module.sh        Build patched binder.ko
  build-probe.sh         Build simple Binder probe tool
  build-ping.sh          Build Binder transaction ping test
  load-binder-tv.sh      Load module on TV from /tmp

src/
  binder_dirty_exports.h Dirty wrappers for non-exported LG kernel symbols

tools/
  binder_probe.c         Minimal BINDER_VERSION / BINDER_SET_MAX_THREADS test
  binder_ping.c          Minimal Binder server/client transaction test

docs/
  transaction-ping.md    Notes about the first transaction milestone

artifacts/
  *.ko                   Built kernel module output
```

## Build requirements

The build host used during testing was an ARM64 Ubuntu machine:

```text
Ubuntu 24.04.x aarch64
```

Required tools:

```bash
sudo apt update
sudo apt install -y \
  git build-essential bc bison flex libssl-dev libelf-dev \
  python3 make gcc-aarch64-linux-gnu
```

If building directly on ARM64, native `gcc` may also work. The scripts prefer `aarch64-linux-gnu-gcc` when available.

## Build

From the repository root:

```bash
cd ~/disk/webos-dirty-binder

./scripts/build-module.sh
./scripts/build-probe.sh
./scripts/build-ping.sh
```

Expected outputs:

```text
artifacts/binder-dirty-lgc1-o20-4.4.84-229.1.kavir.2.ko
build/binder_probe_static
build/binder_ping_static
```

## Copy to TV

Set your TV IP:

```bash
TV_IP=192.168.2.121
```

Copy the module and tools to `/tmp`:

```bash
scp artifacts/binder-dirty-lgc1-o20-4.4.84-229.1.kavir.2.ko root@$TV_IP:/tmp/binder-dirty.ko
scp scripts/load-binder-tv.sh root@$TV_IP:/tmp/load-binder-tv.sh
scp build/binder_probe_static root@$TV_IP:/tmp/binder_probe
scp build/binder_ping_static root@$TV_IP:/tmp/binder_ping
```

## Load module on TV

SSH into the TV:

```bash
ssh root@$TV_IP
```

Then run:

```bash
cd /tmp
chmod +x /tmp/load-binder-tv.sh /tmp/binder_probe /tmp/binder_ping

echo marker-before-binder-load > /dev/kmsg
/tmp/load-binder-tv.sh /tmp/binder-dirty.ko

ls -l /dev/binder
grep binder /proc/modules
```

Expected:

```text
crw-------    1 root     root       10,  53 ... /dev/binder
binder ... Live ...
```

## Probe test

On the TV:

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

## Binder ping round-trip test

The transaction test has two processes:

- `binder_ping server`
- `binder_ping client`

### Start the server

```bash
cd /tmp

rm -f /tmp/binder_ping_server.log \
      /tmp/binder_ping_client.log \
      /tmp/binder_ping_server.pid \
      /tmp/binder_ping_client.exit

echo marker-binder-ping-server > /dev/kmsg

nohup /tmp/binder_ping server > /tmp/binder_ping_server.log 2>&1 &
echo $! > /tmp/binder_ping_server.pid

sleep 2
cat /tmp/binder_ping_server.log
```

Expected server startup:

```text
open /dev/binder
ioctl BINDER_VERSION
binder protocol_version=8
ioctl BINDER_SET_MAX_THREADS
BINDER_SET_MAX_THREADS ok
mmap binder size=1048576
ioctl BINDER_SET_CONTEXT_MGR
BINDER_SET_CONTEXT_MGR ok
server_enter_looper: BINDER_WRITE_READ write_size=4 read_size=8192
```

### Run the client

```bash
cd /tmp

echo marker-binder-ping-client > /dev/kmsg

/tmp/binder_ping client > /tmp/binder_ping_client.log 2>&1
echo "$?" > /tmp/binder_ping_client.exit

cat /tmp/binder_ping_client.exit
cat /tmp/binder_ping_client.log
cat /tmp/binder_ping_server.log
```

Expected client result:

```text
0
client reply payload: PONG from webOS binder server
free_buffer: write_consumed=12 read_consumed=0
```

### Stop the server

```bash
if [ -f /tmp/binder_ping_server.pid ]; then
  kill "$(cat /tmp/binder_ping_server.pid)" 2>/dev/null || true
fi

ps | grep binder_ping | grep -v grep || true
```

## Important implementation notes

### Dirty symbol access

LG's target kernel does not export every symbol needed by Binder as an external module. This project uses dirty wrappers and symbol addresses obtained from the target kernel to call required internals.

This is fragile by design. A different TV model, SoC, kernel release, or LG kernel configuration may need different handling.

### `task_euid(proc->tsk)` crash fix

The original Binder transaction path crashed during `BC_TRANSACTION` here:

```c
t->sender_euid = task_euid(proc->tsk);
```

On the tested LG webOS kernel, that path caused a NULL pointer dereference while reading task credentials.

For this PoC, the generated Binder source is patched during `scripts/build-module.sh` to use:

```c
t->sender_euid = current_euid();
```

This keeps `sender_euid` meaningful for the task issuing the ioctl and allows real Binder transactions to complete.

### Do not ignore read data from `BC_ENTER_LOOPER`

`BINDER_WRITE_READ` can return read commands in the same ioctl that writes `BC_ENTER_LOOPER`.

The server must process the read buffer returned by `server_enter_looper`; otherwise it can miss the first `BR_TRANSACTION`.

### `BC_FREE_BUFFER` must be write-only in this client

After receiving `BR_REPLY`, the client frees the reply buffer with `BC_FREE_BUFFER`.

This must be sent write-only:

```c
read_size = 0;
read_buffer = 0;
```

Using a non-zero read buffer here can make the client wait for process work without being a looper, producing:

```text
ERROR: Thread waiting for process work before calling BC_REGISTER_LOOPER or BC_ENTER_LOOPER
```

## Debugging commands

Useful TV-side commands:

```bash
dmesg | grep -E 'binder|Oops|Unable to handle|marker' | tail -n 300
cat /proc/modules | grep binder
cat /proc/misc | grep binder
ls -l /dev/binder
```

Useful host-side commands:

```bash
strings artifacts/*.ko | grep -E 'binder_alloc_shim|binder_dirty|current_euid'
modinfo artifacts/binder-dirty-lgc1-o20-4.4.84-229.1.kavir.2.ko
```

Map a kernel Oops offset back to source:

```bash
KO=build/linux-4.4.84/drivers/android/binder.ko
OFFSET_HEX=0xeb8

SYM_HEX="$(aarch64-linux-gnu-nm -n "$KO" | awk '$3=="binder_thread_write"{print $1; exit}')"
ADDR="$(python3 - <<PY
sym = int("$SYM_HEX", 16)
off = int("$OFFSET_HEX", 16)
print("0x%x" % (sym + off))
PY
)"

aarch64-linux-gnu-addr2line -f -C -e "$KO" "$ADDR"
```

## Safety workflow

Recommended workflow while testing:

1. Reboot TV before testing a newly built module.
2. Copy all files to `/tmp` only.
3. Load `binder.ko` manually.
4. Run probe first.
5. Run transaction test.
6. Inspect `dmesg`.
7. Reboot after kernel Oops or suspicious behavior.
8. Never install this module into boot scripts while developing.
9. Never write LG system partitions.

## Current milestone summary

Current milestone:

```text
/dev/binder + mmap + context manager + BC_TRANSACTION + BR_TRANSACTION + BC_REPLY + BC_FREE_BUFFER
```

This is the first confirmed Binder transaction round-trip for this dirty LG webOS Binder module experiment.

## License

Use the same license terms as the source tree and imported kernel/Binder code require.
