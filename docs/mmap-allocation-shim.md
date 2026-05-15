# Binder mmap allocation shim

## Problem

The first Binder proof of concept loaded successfully and passed basic ioctls:

- `BINDER_VERSION`
- `BINDER_SET_MAX_THREADS`

However, real Binder transactions require `mmap` on `/dev/binder`.

The first `mmap` attempt caused a kernel oops inside the page allocator:

```txt
binder_mmap_debug: binder_mmap enter ...
binder_mmap_debug: update_page_range enter allocate=1 ...
Unable to handle kernel paging request
PC is at get_page_from_freelist
Call trace:
  get_page_from_freelist
  __alloc_pages_nodemask
  binder_update_page_range [binder]
  binder_mmap [binder]
```

## Fix

`webos-dirty-binder` now patches Binder page allocation during build.

Instead of using the upstream Binder `alloc_page(...)` path, the module uses:

```c
addr = __get_free_page(GFP_KERNEL);
memset((void *)addr, 0, PAGE_SIZE);
page = virt_to_page((void *)addr);
```

This is implemented as `binder_dirty_alloc_page_for_mmap()` and injected by `scripts/build-module.sh`.

## Validated result

With the allocation shim:

```txt
open /dev/binder...
open ok fd=3
ioctl BINDER_VERSION...
BINDER_VERSION ok protocol_version=8
ioctl BINDER_SET_MAX_THREADS...
BINDER_SET_MAX_THREADS ok
mmap size=4096...
mmap ok ptr=0x7facd0d000 size=4096
munmap...
munmap ok
```

This means Binder is now working past the basic ioctl layer and can complete the `mmap` setup needed for Binder transactions.

## Next milestone

Test real Binder transactions:

- start `binder_echo server`
- run `binder_echo client "hello from LG webOS"`
- verify `BR_TRANSACTION` and `BR_REPLY`
