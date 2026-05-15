# webos-dirty-binder

Experimental Android Binder IPC kernel module for rooted LG webOS TV systems.

This project documents a proof of concept for loading Binder on an LG webOS TV kernel without flashing boot, rootfs, kernel, recovery, tvservice, or other system partitions.

The first tested target is an LG OLED C1 running webOS TV 6.2.0 with kernel `4.4.84-229.1.kavir.2`.

## Status

Tested working at basic Binder ioctl level:

- `binder.ko` loads successfully.
- `/dev/binder` is created.
- `/proc/misc` shows Binder.
- `BINDER_VERSION` returns protocol version `8`.
- `BINDER_SET_MAX_THREADS` succeeds.

This does not make Android TV run on webOS.

## Tested target

- TV: LG OLED C1, `OLED65C17LB`
- Platform: LG1212 / LG1K / O20B0
- OS: webOS TV 6.2.0
- Kernel: `4.4.84-229.1.kavir.2`
- Architecture: `aarch64`
- Rootfs: readonly squashfs
- Module loading: enabled
- Module signing: disabled

## Why dirty?

The LG kernel contains several functions Binder needs, but they are not exported to loadable modules.

Examples:

- `zap_page_range`
- `put_files_struct`
- `get_vm_area`
- `__fd_install`
- `__close_fd`
- `map_kernel_range_noflush`
- `__lock_task_sighand`
- `get_files_struct`
- `__alloc_fd`

The module works around this by taking runtime addresses from `/proc/kallsyms` as module parameters and calling them through function pointers.

This is a reverse-engineering aid, not a clean upstreamable driver.

## Safety notes

Do not install this module into boot scripts.

Load it manually only.

The tested module appears as `[permanent]` in `/proc/modules`, so it may not unload with `rmmod`. Rebooting the TV removes it because it is not persistent.

Never write LG system partitions such as kernel, rootfs, boot, recovery, tvservice, or eMMC boot areas.

## Repository layout

- `patches/0001-lg-webos-dirty-binder-module.patch`  
  Patch against Linux `v4.4.84`.

- `src/binder_dirty_exports.h`  
  Dirty wrappers for non-exported LG kernel symbols.

- `scripts/build-module.sh`  
  Rebuilds `binder.ko` from a clean Linux `v4.4.84` tree.

- `scripts/load-binder-tv.sh`  
  Loads the dirty Binder module on the TV using symbol addresses from `/proc/kallsyms`.

- `tools/binder_probe.c`  
  Minimal userspace test for `/dev/binder`.

- `scripts/build-probe.sh`  
  Builds the static Binder probe binary.

- `artifacts/`  
  Optional prebuilt modules for known tested kernels.

## Build Binder module

Run this on an ARM64 Linux machine, not on the TV.

The tested LG C1 kernel config is included at:

```sh
configs/lg-c1-o20-4.4.84-229.1.kavir.2.config
```

Build:

```sh
./scripts/build-module.sh
```

Custom config path:

```sh
CONFIG=/path/to/config-lg-c1 ./scripts/build-module.sh
```

Expected result:

```txt
vermagic: 4.4.84-229.1.kavir.2 SMP preempt mod_unload aarch64
OK: no known non-exported Binder symbols remain
Build completed: artifacts/binder-dirty-lgc1-o20-4.4.84-229.1.kavir.2.ko
```

## Build Binder probe

Run on ARM64 Linux:

```sh
./scripts/build-probe.sh
```

Expected output:

```txt
build/binder_probe_static: ELF 64-bit LSB executable, ARM aarch64, statically linked
```

## Copy files to TV

Replace `TV_IP` with the TV address.

```sh
scp artifacts/binder-dirty-lgc1-o20-4.4.84-229.1.kavir.2.ko root@TV_IP:/tmp/binder-dirty.ko
scp scripts/load-binder-tv.sh root@TV_IP:/tmp/load-binder-tv.sh
scp build/binder_probe_static root@TV_IP:/tmp/binder_probe
```

## Load Binder on TV

Run on the TV:

```sh
cd /tmp
chmod +x /tmp/load-binder-tv.sh /tmp/binder_probe
/tmp/load-binder-tv.sh /tmp/binder-dirty.ko
```

Expected:

```txt
binder ... [permanent], Live ...
53 binder
/dev/binder
```

## Probe Binder

Run on the TV:

```sh
/tmp/binder_probe
echo "exit=$?"
```

Expected:

```txt
BINDER_VERSION protocol_version=8
BINDER_SET_MAX_THREADS ok
exit=0
```

## Current limitations

Not implemented:

- Android TV userspace
- Waydroid
- Anbox
- `/dev/hwbinder`
- `/dev/vndbinder`
- ashmem
- Android init
- SELinux Android policy
- graphics/audio/input Android HAL stack

This project currently proves only that Binder IPC can be loaded and minimally used on the LG webOS kernel via a dirty loadable module.

## Future work

- Create `/dev/hwbinder` and `/dev/vndbinder`.
- Build a minimal Binder userspace service.
- Test Android `servicemanager`.
- Investigate ashmem or memfd compatibility.
- Reduce dependency on raw kallsyms addresses.
- Add support for more LG webOS kernels.
- Document crash/recovery behavior.
