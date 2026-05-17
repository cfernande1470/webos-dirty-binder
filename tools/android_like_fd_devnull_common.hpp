#ifndef WEBOS_DIRTY_BINDER_ANDROID_LIKE_FD_DEVNULL_COMMON_HPP
#define WEBOS_DIRTY_BINDER_ANDROID_LIKE_FD_DEVNULL_COMMON_HPP

#include "android_like_callback_common.hpp"

#include <sys/stat.h>

#define ANDROID_LIKE_FD_DEVNULL_SERVICE "test.android.fddevnull"
#define ANDROID_LIKE_FD_DEVNULL_DESCRIPTOR "webos.dirtybinder.IFdDevNullProbe"
#define ANDROID_LIKE_FD_DEVNULL_SEND 0x46444e55U

struct devnull_binder_fd_object {
    uint32_t type;
    uint32_t pad_flags;
    union {
        binder_uintptr_t pad_binder;
        uint32_t fd;
    };
    binder_uintptr_t cookie;
};

static inline int devnull_extract_fd_from_transaction(struct binder_transaction_data *tr, int *out_fd) {
    binder_size_t off;
    struct devnull_binder_fd_object obj;

    if (!out_fd)
        return -1;

    *out_fd = -1;

    if (!tr->offsets_size || !tr->data.ptr.buffer || !tr->data.ptr.offsets) {
        fprintf(stderr, "fd devnull: missing offsets buffer\n");
        return -1;
    }

    if (tr->offsets_size < sizeof(binder_size_t)) {
        fprintf(stderr, "fd devnull: offsets too small=%llu\n",
                (unsigned long long)tr->offsets_size);
        return -1;
    }

    memcpy(&off, (void *)(uintptr_t)tr->data.ptr.offsets, sizeof(off));

    if ((size_t)off + sizeof(obj) > (size_t)tr->data_size) {
        fprintf(stderr,
                "fd devnull: bad object offset=%llu data_size=%llu object_size=%zu\n",
                (unsigned long long)off,
                (unsigned long long)tr->data_size,
                sizeof(obj));
        return -1;
    }

    memset(&obj, 0, sizeof(obj));
    memcpy(&obj, (void *)((uintptr_t)tr->data.ptr.buffer + (uintptr_t)off), sizeof(obj));

    printf("fd devnull object: offset=%llu type=0x%08x pad_flags=0x%08x fd=%u cookie=0x%llx\n",
           (unsigned long long)off,
           obj.type,
           obj.pad_flags,
           obj.fd,
           (unsigned long long)obj.cookie);

    if (obj.type != BINDER_TYPE_FD) {
        fprintf(stderr, "fd devnull: wrong object type=0x%08x expected=0x%08x\n",
                obj.type,
                BINDER_TYPE_FD);
        return -1;
    }

    *out_fd = (int)obj.fd;
    return 0;
}

#endif
