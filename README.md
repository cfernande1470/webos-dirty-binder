# webos-dirty-binder

Proof of concept for loading Android Binder IPC as a dirty out-of-tree kernel module on rooted LG webOS TV systems.

This is **not Android TV**, **not Waydroid**, and **not a production-ready Android compatibility layer**. It is a reverse-engineering aid for experimenting with Binder on webOS TV kernels where Binder support was not enabled by LG.

## Tested device

First tested target:

- TV: LG OLED C1, model `OLED65C17LB`
- OS: `webOS TV 6.2.0`
- Kernel: `4.4.84-229.1.kavir.2`
- Architecture: `aarch64`
- Platform / SoC: `LG1212 / LG1K / O20B0`
- Root filesystem: squashfs, read-only
- Module loading: enabled
- Module signing: disabled

Kernel config observations:

- `CONFIG_ANDROID=y`
- `CONFIG_ANDROID_BINDER_IPC` was not enabled
- `CONFIG_ASHMEM` was not enabled
- `CONFIG_MODULES=y`
- `CONFIG_MODULE_UNLOAD=y`
- `CONFIG_MODULE_SIG` was not enabled

## Current status

Working:

- `binder.ko` loads manually with `insmod`
- `/dev/binder` is created
- `/proc/misc` shows Binder
- `BINDER_VERSION` returns protocol version `8`
- `BINDER_SET_MAX_THREADS` succeeds

Observed on the tested TV:

```text
binder 118784 0 [permanent], Live 0xffffffbffc35f000 (O)
53 binder
crw-------    1 root     root       10,  53 /dev/binder
BINDER_VERSION protocol_version=8
BINDER_SET_MAX_THREADS ok
```

Not implemented / not working yet:

- Android TV userspace
- Waydroid
- Anbox
- `/dev/hwbinder`
- `/dev/vndbinder`
- ashmem
- Android init
- Android SELinux policy
- Android graphics/audio/input HAL stack

## Why this is dirty

The LG webOS kernel contains internal functions required by Binder, but they are not exported to loadable kernel modules.

Examples of required non-exported symbols:

```text
zap_page_range
put_files_struct
get_vm_area
__fd_install
__close_fd
map_kernel_range_noflush
__lock_task_sighand
get_files_struct
__alloc_fd
```

A normal out-of-tree Binder module fails to load with errors like:

```text
binder: Unknown symbol zap_page_range
binder: Unknown symbol put_files_struct
binder: Unknown symbol get_vm_area
binder: Unknown symbol __fd_install
binder: Unknown symbol __close_fd
binder: Unknown symbol map_kernel_range_noflush
binder: Unknown symbol __alloc_fd
```

This proof of concept works around that by passing the required kernel symbol addresses from `/proc/kallsyms` as module parameters, then calling them through function pointers.

This is fragile and device/kernel-version-specific.

## Safety warning

Do not install this module into boot scripts.

Load it manually only.

The tested module appears as `[permanent]` in `/proc/modules`, so it may not be unloadable with `rmmod`. It disappears after reboot because it is not persistent.

Do not write to LG system partitions such as:

- kernel
- rootfs
- tvservice
- boot
- recovery
- eMMC boot areas

This project should not require modifying system partitions.

## Build notes

The proof of concept was built from Linux `v4.4.84` with LG kernel config copied from the TV.

Important target release string:

```text
4.4.84-229.1.kavir.2
```

The module `vermagic` must match:

```text
4.4.84-229.1.kavir.2 SMP preempt mod_unload aarch64
```

Example checks:

```sh
make ARCH=arm64 kernelrelease
modinfo drivers/android/binder.ko | grep vermagic
```

Expected:

```text
vermagic:       4.4.84-229.1.kavir.2 SMP preempt mod_unload aarch64
```

## Loading on the TV

Copy the module to the TV:

```sh
scp artifacts/binder-dirty-lgc1-o20-4.4.84-229.1.kavir.2.ko root@TV_IP:/tmp/binder-dirty.ko
```

Load it manually:

```sh
cd /tmp

addr() {
  awk -v s="$1" '$3 == s { print "0x"$1; exit }' /proc/kallsyms
}

insmod /tmp/binder-dirty.ko \
  sym_zap_page_range="$(addr zap_page_range)" \
  sym_put_files_struct="$(addr put_files_struct)" \
  sym_get_vm_area="$(addr get_vm_area)" \
  sym___fd_install="$(addr __fd_install)" \
  sym___close_fd="$(addr __close_fd)" \
  sym_map_kernel_range_noflush="$(addr map_kernel_range_noflush)" \
  sym___lock_task_sighand="$(addr __lock_task_sighand)" \
  sym_get_files_struct="$(addr get_files_struct)" \
  sym___alloc_fd="$(addr __alloc_fd)"
```

Check result:

```sh
cat /proc/modules | grep binder
grep binder /proc/misc
ls -l /dev/binder /dev/hwbinder /dev/vndbinder 2>&1
```

Expected minimum result:

```text
binder ... [permanent], Live ...
53 binder
/dev/binder
```

## Loader script

A convenience script can discover symbols from `/proc/kallsyms` automatically:

```sh
#!/bin/sh
set -eu

MOD="${1:-/tmp/binder-dirty.ko}"

addr() {
  awk -v s="$1" '$3 == s { print "0x"$1; exit }' /proc/kallsyms
}

if grep -q '^binder ' /proc/modules; then
  echo "binder already loaded:"
  grep '^binder ' /proc/modules
  ls -l /dev/binder 2>/dev/null || true
  exit 0
fi

echo "Loading $MOD"

insmod "$MOD" \
  sym_zap_page_range="$(addr zap_page_range)" \
  sym_put_files_struct="$(addr put_files_struct)" \
  sym_get_vm_area="$(addr get_vm_area)" \
  sym___fd_install="$(addr __fd_install)" \
  sym___close_fd="$(addr __close_fd)" \
  sym_map_kernel_range_noflush="$(addr map_kernel_range_noflush)" \
  sym___lock_task_sighand="$(addr __lock_task_sighand)" \
  sym_get_files_struct="$(addr get_files_struct)" \
  sym___alloc_fd="$(addr __alloc_fd)"

echo "Loaded:"
grep '^binder ' /proc/modules || true
grep binder /proc/misc || true
ls -l /dev/binder /dev/hwbinder /dev/vndbinder 2>&1 || true
```

Save as:

```text
scripts/load-binder-tv.sh
```

## Binder probe

Minimal userspace probe:

```c
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>

struct binder_version {
    int32_t protocol_version;
};

#define BINDER_VERSION _IOWR('b', 9, struct binder_version)
#define BINDER_SET_MAX_THREADS _IOW('b', 5, uint32_t)

int main(void) {
    int fd = open("/dev/binder", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "open /dev/binder failed: %s\n", strerror(errno));
        return 1;
    }

    struct binder_version ver;
    ver.protocol_version = 0;

    if (ioctl(fd, BINDER_VERSION, &ver) < 0) {
        fprintf(stderr, "BINDER_VERSION failed: %s\n", strerror(errno));
        close(fd);
        return 2;
    }

    printf("BINDER_VERSION protocol_version=%d\n", ver.protocol_version);

    uint32_t max_threads = 1;
    if (ioctl(fd, BINDER_SET_MAX_THREADS, &max_threads) < 0) {
        fprintf(stderr, "BINDER_SET_MAX_THREADS failed: %s\n", strerror(errno));
    } else {
        printf("BINDER_SET_MAX_THREADS ok\n");
    }

    close(fd);
    return 0;
}
```

Build on ARM64 Linux:

```sh
gcc -O2 -static -o binder_probe_static binder_probe.c
```

Copy to TV:

```sh
scp binder_probe_static root@TV_IP:/tmp/binder_probe
```

Run on TV:

```sh
chmod +x /tmp/binder_probe
/tmp/binder_probe
```

Expected:

```text
BINDER_VERSION protocol_version=8
BINDER_SET_MAX_THREADS ok
```

## Runtime symbol example

Example addresses observed on one boot of the tested LG C1:

```text
zap_page_range=0xffffffc000194da8
put_files_struct=0xffffffc0001e3078
get_vm_area=0xffffffc0001a28c0
__fd_install=0xffffffc0001e3578
__close_fd=0xffffffc0001e35c0
map_kernel_range_noflush=0xffffffc0001a2878
__lock_task_sighand=0xffffffc0000b3290
get_files_struct=0xffffffc0001e3028
__alloc_fd=0xffffffc0001e3230
```

Do not hardcode these addresses. Resolve them from `/proc/kallsyms` on each boot.

## Future work

Possible next steps:

- Add `/dev/hwbinder`
- Add `/dev/vndbinder`
- Investigate ashmem or memfd compatibility
- Build a minimal Binder userspace service
- Test Android `servicemanager`
- Create a cleaner patch series
- Remove reliance on non-exported symbol addresses where possible
- Test on other rooted webOS TV kernels

## Disclaimer

This is an experimental reverse-engineering proof of concept. It can crash or reboot the TV.

It does not modify persistent system partitions when used as documented, but loading arbitrary kernel modules is inherently risky.
