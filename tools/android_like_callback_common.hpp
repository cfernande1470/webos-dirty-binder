#ifndef WEBOS_DIRTY_BINDER_ANDROID_LIKE_CALLBACK_COMMON_HPP
#define WEBOS_DIRTY_BINDER_ANDROID_LIKE_CALLBACK_COMMON_HPP

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <linux/android/binder.h>

#define BINDER_DEVICE "/dev/binder"
#define BINDER_MMAP_SIZE (1024 * 1024)

#define AOSP_SM_DESCRIPTOR "android.os.IServiceManager"
#define AOSP_SM_GET_SERVICE_TRANSACTION 1U
#define AOSP_SM_ADD_SERVICE_TRANSACTION 3U

#define ANDROID_LIKE_CALLBACK_DESCRIPTOR "webos.dirtybinder.ICallbackDemo"
#define ANDROID_LIKE_CALLBACK_REGISTER 0x43424752U
#define ANDROID_LIKE_CALLBACK_ON_EVENT 0x43424556U
#define ANDROID_LIKE_CALLBACK_MAGIC 0x43424B30U

struct callback_text_payload {
    uint32_t magic;
    uint32_t status;
    char text[512];
};

static inline void cb_append_bytes(uint8_t **p, const void *src, size_t n) {
    memcpy(*p, src, n);
    *p += n;
}

static inline void cb_append_u32(uint8_t **p, uint32_t v) {
    cb_append_bytes(p, &v, sizeof(v));
}

static inline size_t cb_align4(size_t n) {
    return (n + 3U) & ~3U;
}

static inline size_t cb_align8(size_t n) {
    return (n + 7U) & ~7U;
}

static inline const char *cb_cmd_name(uint32_t cmd) {
    switch (cmd) {
    case BR_NOOP: return "BR_NOOP";
    case BR_TRANSACTION: return "BR_TRANSACTION";
    case BR_REPLY: return "BR_REPLY";
    case BR_TRANSACTION_COMPLETE: return "BR_TRANSACTION_COMPLETE";
    case BR_INCREFS: return "BR_INCREFS";
    case BR_ACQUIRE: return "BR_ACQUIRE";
    case BR_RELEASE: return "BR_RELEASE";
    case BR_DECREFS: return "BR_DECREFS";
    case BR_SPAWN_LOOPER: return "BR_SPAWN_LOOPER";
    case BR_DEAD_REPLY: return "BR_DEAD_REPLY";
    case BR_FAILED_REPLY: return "BR_FAILED_REPLY";
    default: return "UNKNOWN";
    }
}

static inline int cb_binder_write_read(
    int fd,
    const void *write_buf,
    size_t write_size,
    void *read_buf,
    size_t read_size,
    const char *tag)
{
    struct binder_write_read bwr;
    memset(&bwr, 0, sizeof(bwr));

    bwr.write_size = write_size;
    bwr.write_buffer = (binder_uintptr_t)(uintptr_t)write_buf;
    bwr.read_size = read_size;
    bwr.read_buffer = (binder_uintptr_t)(uintptr_t)read_buf;

    printf("%s: BINDER_WRITE_READ write_size=%zu read_size=%zu\n",
           tag, write_size, read_size);

    if (ioctl(fd, BINDER_WRITE_READ, &bwr) < 0) {
        fprintf(stderr, "%s: ioctl failed errno=%d (%s)\n",
                tag, errno, strerror(errno));
        return -1;
    }

    printf("%s: write_consumed=%" PRIu64 " read_consumed=%" PRIu64 "\n",
           tag,
           (uint64_t)bwr.write_consumed,
           (uint64_t)bwr.read_consumed);

    return (int)bwr.read_consumed;
}

static inline int cb_binder_open_and_init(const char *who) {
    int fd;
    void *map;
    struct binder_version ver;
    int max_threads = 8;

    printf("%s: open %s\n", who, BINDER_DEVICE);

    fd = open(BINDER_DEVICE, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        perror("open /dev/binder");
        return -1;
    }

    memset(&ver, 0, sizeof(ver));
    if (ioctl(fd, BINDER_VERSION, &ver) < 0) {
        perror("ioctl BINDER_VERSION");
        close(fd);
        return -1;
    }

    printf("%s: binder protocol_version=%d\n", who, ver.protocol_version);

    if (ioctl(fd, BINDER_SET_MAX_THREADS, &max_threads) < 0) {
        perror("ioctl BINDER_SET_MAX_THREADS");
        close(fd);
        return -1;
    }

    map = mmap(NULL, BINDER_MMAP_SIZE, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED) {
        perror("mmap binder");
        close(fd);
        return -1;
    }

    printf("%s: binder mmap=%p size=%d\n", who, map, BINDER_MMAP_SIZE);
    return fd;
}

static inline int cb_binder_free_buffer(int fd, binder_uintptr_t buffer, const char *tag) {
    uint8_t writebuf[64];
    uint8_t *p = writebuf;
    uint32_t cmd;

    if (!buffer)
        return 0;

    cmd = BC_FREE_BUFFER;
    cb_append_u32(&p, cmd);
    cb_append_bytes(&p, &buffer, sizeof(buffer));

    return cb_binder_write_read(fd, writebuf, (size_t)(p - writebuf), NULL, 0, tag) < 0 ? -1 : 0;
}

static inline int cb_binder_send_handle_cmd(int fd, uint32_t cmd, uint32_t handle, const char *tag) {
    uint8_t writebuf[64];
    uint8_t *p = writebuf;

    cb_append_u32(&p, cmd);
    cb_append_u32(&p, handle);

    printf("%s: cmd=0x%08x handle=%u\n", tag, cmd, handle);

    return cb_binder_write_read(fd, writebuf, (size_t)(p - writebuf), NULL, 0, tag) < 0 ? -1 : 0;
}

static inline int cb_binder_acquire_handle(int fd, uint32_t handle, const char *tag) {
    return cb_binder_send_handle_cmd(fd, BC_ACQUIRE, handle, tag);
}

static inline int cb_binder_release_handle(int fd, uint32_t handle, const char *tag) {
    return cb_binder_send_handle_cmd(fd, BC_RELEASE, handle, tag);
}

static inline int cb_handle_ref_cmd(int fd, uint32_t rcmd, uint8_t **ptr, uint8_t *end, const char *who) {
    struct binder_ptr_cookie pc;

    if (*ptr + sizeof(pc) > end) {
        fprintf(stderr, "%s: truncated ref cmd=0x%08x\n", who, rcmd);
        return -1;
    }

    memcpy(&pc, *ptr, sizeof(pc));
    *ptr += sizeof(pc);

    printf("%s ref cmd=%s ptr=0x%" PRIx64 " cookie=0x%" PRIx64 "\n",
           who,
           cb_cmd_name(rcmd),
           (uint64_t)pc.ptr,
           (uint64_t)pc.cookie);

    if (rcmd == BR_INCREFS || rcmd == BR_ACQUIRE) {
        uint8_t writebuf[64];
        uint8_t *p = writebuf;
        uint32_t done = (rcmd == BR_INCREFS) ? BC_INCREFS_DONE : BC_ACQUIRE_DONE;

        cb_append_u32(&p, done);
        cb_append_bytes(&p, &pc, sizeof(pc));

        return cb_binder_write_read(fd, writebuf, (size_t)(p - writebuf), NULL, 0, "callback ref done") < 0 ? -1 : 0;
    }

    return 0;
}

static inline int cb_parcel_write_i32(uint8_t *buf, size_t cap, size_t *pos, int32_t v) {
    if (*pos + sizeof(v) > cap)
        return -1;

    memcpy(buf + *pos, &v, sizeof(v));
    *pos += sizeof(v);
    return 0;
}

static inline int cb_parcel_write_string16_ascii(uint8_t *buf, size_t cap, size_t *pos, const char *s) {
    int32_t len;
    size_t bytes;
    size_t padded;
    size_t i;

    if (!s)
        s = "";

    len = (int32_t)strlen(s);
    bytes = ((size_t)len + 1U) * 2U;
    padded = cb_align4(bytes);

    if (cb_parcel_write_i32(buf, cap, pos, len) != 0)
        return -1;

    if (*pos + padded > cap)
        return -1;

    memset(buf + *pos, 0, padded);

    for (i = 0; i < (size_t)len; i++) {
        buf[*pos + i * 2U] = (uint8_t)s[i];
        buf[*pos + i * 2U + 1U] = 0;
    }

    *pos += padded;
    return 0;
}

static inline uint32_t cb_first_handle_from_transaction(struct binder_transaction_data *tr) {
    binder_size_t off;
    struct flat_binder_object *obj;

    if (!tr->offsets_size || !tr->data.ptr.buffer || !tr->data.ptr.offsets)
        return 0;

    if (tr->offsets_size < sizeof(binder_size_t))
        return 0;

    memcpy(&off, (void *)(uintptr_t)tr->data.ptr.offsets, sizeof(off));

    if ((size_t)off + sizeof(struct flat_binder_object) > (size_t)tr->data_size)
        return 0;

    obj = (struct flat_binder_object *)((uint8_t *)(uintptr_t)tr->data.ptr.buffer + off);

    printf("callback object: offset=%" PRIu64 " type=0x%08x handle=%u binder=0x%" PRIx64 " cookie=0x%" PRIx64 "\n",
           (uint64_t)off,
           obj->type,
           obj->handle,
           (uint64_t)obj->binder,
           (uint64_t)obj->cookie);

    if (obj->type != BINDER_TYPE_HANDLE)
        return 0;

    return obj->handle;
}

static inline int cb_aosp_get_service_handle(int fd, const char *name, uint32_t *out_handle) {
    uint8_t parcel[512];
    size_t parcel_size = 0;
    uint8_t writebuf[1024];
    uint8_t readbuf[8192];
    uint8_t *p = writebuf;
    uint32_t cmd;
    struct binder_transaction_data tr;
    int first = 1;

    if (!out_handle)
        return -1;

    *out_handle = 0;

    if (cb_parcel_write_i32(parcel, sizeof(parcel), &parcel_size, 0) != 0 ||
        cb_parcel_write_string16_ascii(parcel, sizeof(parcel), &parcel_size, AOSP_SM_DESCRIPTOR) != 0 ||
        cb_parcel_write_string16_ascii(parcel, sizeof(parcel), &parcel_size, name) != 0) {
        fprintf(stderr, "callback getService: failed to build Parcel\n");
        return -1;
    }

    memset(&tr, 0, sizeof(tr));
    tr.target.handle = 0;
    tr.code = AOSP_SM_GET_SERVICE_TRANSACTION;
    tr.flags = TF_ACCEPT_FDS;
    tr.data_size = parcel_size;
    tr.offsets_size = 0;
    tr.data.ptr.buffer = (binder_uintptr_t)parcel;
    tr.data.ptr.offsets = 0;

    cmd = BC_TRANSACTION;
    cb_append_u32(&p, cmd);
    cb_append_bytes(&p, &tr, sizeof(tr));

    for (;;) {
        int n;
        uint8_t *ptr;
        uint8_t *end;

        n = cb_binder_write_read(fd,
                                 first ? writebuf : NULL,
                                 first ? (size_t)(p - writebuf) : 0,
                                 readbuf,
                                 sizeof(readbuf),
                                 first ? "callback getService call" : "callback getService wait");
        first = 0;

        if (n < 0)
            return -1;

        ptr = readbuf;
        end = readbuf + n;

        while (ptr + sizeof(uint32_t) <= end) {
            uint32_t rcmd;
            memcpy(&rcmd, ptr, sizeof(rcmd));
            ptr += sizeof(rcmd);

            printf("callback getService got %s 0x%08x\n", cb_cmd_name(rcmd), rcmd);

            if (rcmd == BR_NOOP || rcmd == BR_TRANSACTION_COMPLETE || rcmd == BR_SPAWN_LOOPER)
                continue;

            if (rcmd == BR_REPLY) {
                struct binder_transaction_data reply;
                uint32_t handle;

                if (ptr + sizeof(reply) > end)
                    return -1;

                memcpy(&reply, ptr, sizeof(reply));
                ptr += sizeof(reply);

                handle = cb_first_handle_from_transaction(&reply);

                if (!handle) {
                    cb_binder_free_buffer(fd, reply.data.ptr.buffer, "callback getService free null reply");
                    fprintf(stderr, "callback getService: null handle for %s\n", name);
                    return 1;
                }

                if (cb_binder_acquire_handle(fd, handle, "callback getService BC_ACQUIRE returned handle") != 0) {
                    cb_binder_free_buffer(fd, reply.data.ptr.buffer, "callback getService free failed reply");
                    return -1;
                }

                cb_binder_free_buffer(fd, reply.data.ptr.buffer, "callback getService free reply");

                *out_handle = handle;
                return 0;
            }

            if (rcmd == BR_INCREFS || rcmd == BR_ACQUIRE || rcmd == BR_RELEASE || rcmd == BR_DECREFS) {
                if (cb_handle_ref_cmd(fd, rcmd, &ptr, end, "callback getService") != 0)
                    return -1;
                continue;
            }

            if (rcmd == BR_DEAD_REPLY || rcmd == BR_FAILED_REPLY) {
                fprintf(stderr, "callback getService failed cmd=0x%08x\n", rcmd);
                return 1;
            }

            fprintf(stderr, "callback getService unhandled cmd=0x%08x\n", rcmd);
            return -1;
        }
    }
}

static inline int cb_send_text_reply(
    int fd,
    binder_uintptr_t incoming_buffer,
    uint32_t status,
    const char *text,
    const char *tag)
{
    uint8_t writebuf[2048];
    uint8_t *p = writebuf;
    uint32_t cmd;
    struct binder_transaction_data reply_tr;
    struct callback_text_payload reply;

    memset(&reply, 0, sizeof(reply));
    reply.magic = ANDROID_LIKE_CALLBACK_MAGIC;
    reply.status = status;
    snprintf(reply.text, sizeof(reply.text), "%s", text ? text : "");

    cmd = BC_FREE_BUFFER;
    cb_append_u32(&p, cmd);
    cb_append_bytes(&p, &incoming_buffer, sizeof(incoming_buffer));

    cmd = BC_REPLY;
    cb_append_u32(&p, cmd);

    memset(&reply_tr, 0, sizeof(reply_tr));
    reply_tr.data_size = sizeof(reply);
    reply_tr.offsets_size = 0;
    reply_tr.data.ptr.buffer = (binder_uintptr_t)&reply;
    reply_tr.data.ptr.offsets = 0;

    cb_append_bytes(&p, &reply_tr, sizeof(reply_tr));

    return cb_binder_write_read(fd, writebuf, (size_t)(p - writebuf), NULL, 0, tag) < 0 ? -1 : 0;
}

static inline int cb_register_local_service(
    int fd,
    const char *name,
    binder_uintptr_t local_ptr,
    binder_uintptr_t local_cookie)
{
    uint8_t parcel[1024];
    size_t parcel_size = 0;
    binder_size_t offsets[1];
    struct flat_binder_object obj;
    uint8_t writebuf[2048];
    uint8_t readbuf[8192];
    uint8_t *p = writebuf;
    uint32_t cmd;
    struct binder_transaction_data tr;
    int first = 1;

    printf("callback addService name=%s local_ptr=0x%" PRIx64 " cookie=0x%" PRIx64 "\n",
           name,
           (uint64_t)local_ptr,
           (uint64_t)local_cookie);

    if (cb_parcel_write_i32(parcel, sizeof(parcel), &parcel_size, 0) != 0 ||
        cb_parcel_write_string16_ascii(parcel, sizeof(parcel), &parcel_size, AOSP_SM_DESCRIPTOR) != 0 ||
        cb_parcel_write_string16_ascii(parcel, sizeof(parcel), &parcel_size, name) != 0) {
        fprintf(stderr, "callback addService: failed to build Parcel\n");
        return -1;
    }

    parcel_size = cb_align8(parcel_size);

    memset(parcel + parcel_size, 0, sizeof(parcel) - parcel_size);
    memset(&obj, 0, sizeof(obj));

    obj.type = BINDER_TYPE_BINDER;
    obj.flags = FLAT_BINDER_FLAG_ACCEPTS_FDS;
    obj.binder = local_ptr;
    obj.cookie = local_cookie;

    offsets[0] = (binder_size_t)parcel_size;
    memcpy(parcel + parcel_size, &obj, sizeof(obj));
    parcel_size += sizeof(obj);
    parcel_size = cb_align8(parcel_size);

    if (cb_parcel_write_i32(parcel, sizeof(parcel), &parcel_size, 0) != 0) {
        fprintf(stderr, "callback addService: failed to append allowIsolated\n");
        return -1;
    }

    memset(&tr, 0, sizeof(tr));
    tr.target.handle = 0;
    tr.code = AOSP_SM_ADD_SERVICE_TRANSACTION;
    tr.flags = TF_ACCEPT_FDS;
    tr.data_size = parcel_size;
    tr.offsets_size = sizeof(offsets);
    tr.data.ptr.buffer = (binder_uintptr_t)parcel;
    tr.data.ptr.offsets = (binder_uintptr_t)offsets;

    cmd = BC_TRANSACTION;
    cb_append_u32(&p, cmd);
    cb_append_bytes(&p, &tr, sizeof(tr));

    for (;;) {
        int n;
        uint8_t *ptr;
        uint8_t *end;

        n = cb_binder_write_read(fd,
                                 first ? writebuf : NULL,
                                 first ? (size_t)(p - writebuf) : 0,
                                 readbuf,
                                 sizeof(readbuf),
                                 first ? "callback addService call" : "callback addService wait");
        first = 0;

        if (n < 0)
            return -1;

        ptr = readbuf;
        end = readbuf + n;

        while (ptr + sizeof(uint32_t) <= end) {
            uint32_t rcmd;
            memcpy(&rcmd, ptr, sizeof(rcmd));
            ptr += sizeof(rcmd);

            printf("callback addService got %s 0x%08x\n", cb_cmd_name(rcmd), rcmd);

            if (rcmd == BR_NOOP || rcmd == BR_TRANSACTION_COMPLETE || rcmd == BR_SPAWN_LOOPER)
                continue;

            if (rcmd == BR_REPLY) {
                struct binder_transaction_data reply;

                if (ptr + sizeof(reply) > end)
                    return -1;

                memcpy(&reply, ptr, sizeof(reply));
                ptr += sizeof(reply);

                cb_binder_free_buffer(fd, reply.data.ptr.buffer, "callback addService free reply");
                return 0;
            }

            if (rcmd == BR_INCREFS || rcmd == BR_ACQUIRE || rcmd == BR_RELEASE || rcmd == BR_DECREFS) {
                if (cb_handle_ref_cmd(fd, rcmd, &ptr, end, "callback addService") != 0)
                    return -1;
                continue;
            }

            if (rcmd == BR_DEAD_REPLY || rcmd == BR_FAILED_REPLY) {
                fprintf(stderr, "callback addService failed cmd=0x%08x\n", rcmd);
                return 1;
            }

            fprintf(stderr, "callback addService unhandled cmd=0x%08x\n", rcmd);
            return -1;
        }
    }
}

static inline int cb_call_text_handle(
    int fd,
    uint32_t handle,
    uint32_t code,
    const char *text,
    struct callback_text_payload *out_reply,
    const char *tag)
{
    struct callback_text_payload payload;
    uint8_t writebuf[2048];
    uint8_t readbuf[8192];
    uint8_t *p = writebuf;
    uint32_t cmd;
    struct binder_transaction_data tr;
    int first = 1;

    memset(&payload, 0, sizeof(payload));
    payload.magic = ANDROID_LIKE_CALLBACK_MAGIC;
    payload.status = 0;
    snprintf(payload.text, sizeof(payload.text), "%s", text ? text : "");

    memset(&tr, 0, sizeof(tr));
    tr.target.handle = handle;
    tr.code = code;
    tr.flags = TF_ACCEPT_FDS;
    tr.data_size = sizeof(payload);
    tr.offsets_size = 0;
    tr.data.ptr.buffer = (binder_uintptr_t)&payload;
    tr.data.ptr.offsets = 0;

    cmd = BC_TRANSACTION;
    cb_append_u32(&p, cmd);
    cb_append_bytes(&p, &tr, sizeof(tr));

    for (;;) {
        int n;
        uint8_t *ptr;
        uint8_t *end;

        n = cb_binder_write_read(fd,
                                 first ? writebuf : NULL,
                                 first ? (size_t)(p - writebuf) : 0,
                                 readbuf,
                                 sizeof(readbuf),
                                 first ? tag : "callback call wait");
        first = 0;

        if (n < 0)
            return -1;

        ptr = readbuf;
        end = readbuf + n;

        while (ptr + sizeof(uint32_t) <= end) {
            uint32_t rcmd;
            memcpy(&rcmd, ptr, sizeof(rcmd));
            ptr += sizeof(rcmd);

            printf("%s got %s 0x%08x\n", tag, cb_cmd_name(rcmd), rcmd);

            if (rcmd == BR_NOOP || rcmd == BR_TRANSACTION_COMPLETE || rcmd == BR_SPAWN_LOOPER)
                continue;

            if (rcmd == BR_REPLY) {
                struct binder_transaction_data reply;

                if (ptr + sizeof(reply) > end)
                    return -1;

                memcpy(&reply, ptr, sizeof(reply));
                ptr += sizeof(reply);

                if (out_reply)
                    memset(out_reply, 0, sizeof(*out_reply));

                if (reply.data.ptr.buffer && reply.data_size >= sizeof(struct callback_text_payload)) {
                    struct callback_text_payload *rp =
                        (struct callback_text_payload *)(uintptr_t)reply.data.ptr.buffer;

                    if (out_reply)
                        memcpy(out_reply, rp, sizeof(*out_reply));
                }

                cb_binder_free_buffer(fd, reply.data.ptr.buffer, "callback call free reply");
                return 0;
            }

            if (rcmd == BR_INCREFS || rcmd == BR_ACQUIRE || rcmd == BR_RELEASE || rcmd == BR_DECREFS) {
                if (cb_handle_ref_cmd(fd, rcmd, &ptr, end, tag) != 0)
                    return -1;
                continue;
            }

            if (rcmd == BR_DEAD_REPLY || rcmd == BR_FAILED_REPLY) {
                fprintf(stderr, "%s failed cmd=0x%08x\n", tag, rcmd);
                return 1;
            }

            fprintf(stderr, "%s unhandled cmd=0x%08x\n", tag, rcmd);
            return -1;
        }
    }
}

#endif
