#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#include <linux/android/binder.h>

#define BINDER_DEVICE "/dev/binder"
#define BINDER_MMAP_SIZE (1024 * 1024)

#define SC_MAGIC 0x42534f57U
#define SC_CODE_ECHO 0x4543484fU
#define PAYLOAD_LEN 1024

#define AOSP_SM_DESCRIPTOR "android.os.IServiceManager"
#define AOSP_SM_GET_SERVICE_TRANSACTION 1U
#define AOSP_SM_CHECK_SERVICE_TRANSACTION 2U
#define AOSP_SM_ADD_SERVICE_TRANSACTION 3U
#define AOSP_SM_LIST_SERVICES_TRANSACTION 4U

struct sc_text_reply {
    uint32_t magic;
    uint32_t status;
    char text[PAYLOAD_LEN];
};

static void die(const char *msg)
{
    perror(msg);
    exit(1);
}

static void append_bytes(uint8_t **p, const void *src, size_t n)
{
    memcpy(*p, src, n);
    *p += n;
}

static void append_u32(uint8_t **p, uint32_t v)
{
    append_bytes(p, &v, sizeof(v));
}

static size_t align4(size_t n)
{
    return (n + 3U) & ~3U;
}

static const char *cmd_name(uint32_t cmd)
{
    switch (cmd) {
    case BR_NOOP: return "BR_NOOP";
    case BR_TRANSACTION: return "BR_TRANSACTION";
    case BR_REPLY: return "BR_REPLY";
    case BR_TRANSACTION_COMPLETE: return "BR_TRANSACTION_COMPLETE";
    case BR_INCREFS: return "BR_INCREFS";
    case BR_ACQUIRE: return "BR_ACQUIRE";
    case BR_RELEASE: return "BR_RELEASE";
    case BR_DECREFS: return "BR_DECREFS";
    case BR_DEAD_REPLY: return "BR_DEAD_REPLY";
    case BR_FAILED_REPLY: return "BR_FAILED_REPLY";
    default: return "UNKNOWN";
    }
}

static int binder_open_and_init(void)
{
    int fd;
    void *map;
    struct binder_version ver;
    int max_threads = 8;

    printf("open %s\n", BINDER_DEVICE);

    fd = open(BINDER_DEVICE, O_RDWR | O_CLOEXEC);
    if (fd < 0)
        die("open /dev/binder");

    memset(&ver, 0, sizeof(ver));
    if (ioctl(fd, BINDER_VERSION, &ver) < 0)
        die("ioctl BINDER_VERSION");

    printf("binder protocol_version=%d\n", ver.protocol_version);

    if (ioctl(fd, BINDER_SET_MAX_THREADS, &max_threads) < 0)
        die("ioctl BINDER_SET_MAX_THREADS");

    printf("BINDER_SET_MAX_THREADS ok\n");

    map = mmap(NULL, BINDER_MMAP_SIZE, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED)
        die("mmap binder");

    printf("binder mmap=%p size=%d\n", map, BINDER_MMAP_SIZE);
    return fd;
}

static int binder_write_read(int fd,
                             void *write_buf,
                             size_t write_size,
                             void *read_buf,
                             size_t read_size,
                             const char *tag)
{
    struct binder_write_read bwr;

    memset(&bwr, 0, sizeof(bwr));
    bwr.write_size = write_size;
    bwr.write_buffer = (binder_uintptr_t)write_buf;
    bwr.read_size = read_size;
    bwr.read_buffer = (binder_uintptr_t)read_buf;

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

static int binder_free_buffer(int fd, binder_uintptr_t buffer)
{
    uint8_t writebuf[64];
    uint8_t *p = writebuf;
    uint32_t cmd = BC_FREE_BUFFER;

    append_u32(&p, cmd);
    append_bytes(&p, &buffer, sizeof(buffer));

    return binder_write_read(fd,
                             writebuf,
                             (size_t)(p - writebuf),
                             NULL,
                             0,
                             "free_buffer") < 0 ? -1 : 0;
}

static int binder_send_handle_cmd(int fd, uint32_t cmd, uint32_t handle, const char *tag)
{
    uint8_t writebuf[64];
    uint8_t *p = writebuf;

    append_u32(&p, cmd);
    append_u32(&p, handle);

    printf("%s: cmd=0x%08x handle=%u\n", tag, cmd, handle);

    return binder_write_read(fd,
                             writebuf,
                             (size_t)(p - writebuf),
                             NULL,
                             0,
                             tag) < 0 ? -1 : 0;
}

static int handle_ref_cmd(int fd, uint32_t rcmd, uint8_t **ptr, uint8_t *end, const char *who)
{
    struct binder_ptr_cookie pc;

    if (*ptr + sizeof(pc) > end) {
        fprintf(stderr, "%s: truncated ref cmd=0x%08x\n", who, rcmd);
        return -1;
    }

    memcpy(&pc, *ptr, sizeof(pc));
    *ptr += sizeof(pc);

    printf("%s ref cmd=%s ptr=0x%" PRIx64 " cookie=0x%" PRIx64 "\n",
           who,
           cmd_name(rcmd),
           (uint64_t)pc.ptr,
           (uint64_t)pc.cookie);

    if (rcmd == BR_INCREFS)
        return 0;
    if (rcmd == BR_ACQUIRE)
        return 0;
    if (rcmd == BR_RELEASE)
        return 0;
    if (rcmd == BR_DECREFS)
        return 0;

    return 0;
}

static int parcel_write_i32(uint8_t *buf, size_t cap, size_t *pos, int32_t v)
{
    if (*pos + sizeof(v) > cap)
        return -1;

    memcpy(buf + *pos, &v, sizeof(v));
    *pos += sizeof(v);
    return 0;
}

static int parcel_write_string16_ascii(uint8_t *buf, size_t cap, size_t *pos, const char *s)
{
    int32_t len;
    size_t bytes;
    size_t padded;
    size_t i;

    if (!s)
        s = "";

    len = (int32_t)strlen(s);
    bytes = ((size_t)len + 1U) * 2U;
    padded = align4(bytes);

    if (parcel_write_i32(buf, cap, pos, len) != 0)
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

static uint32_t first_handle_from_transaction(struct binder_transaction_data *tr)
{
    binder_size_t off;
    struct flat_binder_object *obj;

    if (!tr->offsets_size || !tr->data.ptr.buffer || !tr->data.ptr.offsets)
        return 0;

    if (tr->offsets_size < sizeof(binder_size_t))
        return 0;

    off = *(binder_size_t *)(uintptr_t)tr->data.ptr.offsets;

    if (off + sizeof(struct flat_binder_object) > tr->data_size)
        return 0;

    obj = (struct flat_binder_object *)((uint8_t *)(uintptr_t)tr->data.ptr.buffer + off);

    printf("libbinder-lite reply object: offset=%" PRIu64 " type=0x%08x handle=%u binder=0x%" PRIx64 " cookie=0x%" PRIx64 "\n",
           (uint64_t)off,
           obj->type,
           obj->handle,
           (uint64_t)obj->binder,
           (uint64_t)obj->cookie);

    if (obj->type != BINDER_TYPE_HANDLE)
        return 0;

    return obj->handle;
}

static int parcel_read_string16_ascii(const void *data, size_t size, char *out, size_t out_len)
{
    const uint8_t *p = (const uint8_t *)data;
    int32_t len;
    size_t bytes;
    size_t padded;
    size_t i;

    if (!data || !out || out_len == 0 || size < sizeof(len))
        return -1;

    out[0] = '\0';

    memcpy(&len, p, sizeof(len));
    p += sizeof(len);
    size -= sizeof(len);

    if (len < 0)
        return 0;

    bytes = ((size_t)len + 1U) * 2U;
    padded = align4(bytes);

    if (padded > size)
        return -1;

    for (i = 0; i < (size_t)len && i + 1 < out_len; i++) {
        uint8_t lo = p[i * 2U];
        uint8_t hi = p[i * 2U + 1U];

        out[i] = hi == 0 ? (char)lo : '?';
    }

    out[i < out_len ? i : out_len - 1] = '\0';
    return 0;
}

static int aosp_list_one_service(int fd, int32_t index, char *out, size_t out_len)
{
    uint8_t parcel[512];
    size_t parcel_size = 0;
    uint8_t writebuf[1024];
    uint8_t readbuf[8192];
    uint8_t *p = writebuf;
    uint32_t cmd;
    struct binder_transaction_data tr;
    int first = 1;

    if (parcel_write_i32(parcel, sizeof(parcel), &parcel_size, 0) != 0 ||
        parcel_write_string16_ascii(parcel, sizeof(parcel), &parcel_size, AOSP_SM_DESCRIPTOR) != 0 ||
        parcel_write_i32(parcel, sizeof(parcel), &parcel_size, index) != 0) {
        fprintf(stderr, "failed to build libbinder-lite listServices Parcel\n");
        return -1;
    }

    memset(&tr, 0, sizeof(tr));
    tr.target.handle = 0;
    tr.code = AOSP_SM_LIST_SERVICES_TRANSACTION;
    tr.flags = TF_ACCEPT_FDS;
    tr.data_size = parcel_size;
    tr.offsets_size = 0;
    tr.data.ptr.buffer = (binder_uintptr_t)parcel;
    tr.data.ptr.offsets = 0;

    cmd = BC_TRANSACTION;
    append_u32(&p, cmd);
    append_bytes(&p, &tr, sizeof(tr));

    for (;;) {
        int n;
        uint8_t *ptr;
        uint8_t *end;

        if (first) {
            n = binder_write_read(fd,
                                  writebuf,
                                  (size_t)(p - writebuf),
                                  readbuf,
                                  sizeof(readbuf),
                                  "libbinder-lite listServices call");
            first = 0;
        } else {
            n = binder_write_read(fd,
                                  NULL,
                                  0,
                                  readbuf,
                                  sizeof(readbuf),
                                  "libbinder-lite listServices wait");
        }

        if (n < 0)
            return -1;

        ptr = readbuf;
        end = readbuf + n;

        while (ptr + sizeof(uint32_t) <= end) {
            uint32_t rcmd;

            memcpy(&rcmd, ptr, sizeof(rcmd));
            ptr += sizeof(rcmd);

            printf("libbinder-lite listServices got %s 0x%08x\n", cmd_name(rcmd), rcmd);

            if (rcmd == BR_REPLY) {
                struct binder_transaction_data reply;

                if (ptr + sizeof(reply) > end)
                    return -1;

                memcpy(&reply, ptr, sizeof(reply));
                ptr += sizeof(reply);

                if (reply.data.ptr.buffer) {
                    if (parcel_read_string16_ascii((void *)(uintptr_t)reply.data.ptr.buffer,
                                                   (size_t)reply.data_size,
                                                   out,
                                                   out_len) != 0) {
                        binder_free_buffer(fd, reply.data.ptr.buffer);
                        return -1;
                    }

                    binder_free_buffer(fd, reply.data.ptr.buffer);
                    return 0;
                }

                out[0] = '\0';
                return 0;
            }

            if (rcmd == BR_NOOP || rcmd == BR_TRANSACTION_COMPLETE)
                continue;

            if (rcmd == BR_DEAD_REPLY || rcmd == BR_FAILED_REPLY) {
                fprintf(stderr, "libbinder-lite listServices failed cmd=0x%08x\n", rcmd);
                return 1;
            }

            if (rcmd == BR_INCREFS ||
                rcmd == BR_ACQUIRE ||
                rcmd == BR_RELEASE ||
                rcmd == BR_DECREFS) {
                if (handle_ref_cmd(fd, rcmd, &ptr, end, "libbinder-lite listServices") != 0)
                    return -1;
                continue;
            }

            fprintf(stderr, "libbinder-lite listServices unhandled cmd=0x%08x\n", rcmd);
            return -1;
        }
    }
}

static int aosp_list_services_contains(int fd, const char *wanted)
{
    int32_t i;

    printf("libbinder-lite listServices looking for %s\n", wanted);

    for (i = 0; i < 32; i++) {
        char name[128];

        if (aosp_list_one_service(fd, i, name, sizeof(name)) != 0)
            return -1;

        printf("libbinder-lite listServices[%d]=%s\n", i, name[0] ? name : "(empty)");

        if (name[0] == '\0')
            break;

        if (strcmp(name, wanted) == 0) {
            printf("AOSP_LIST_SERVICES_OK\n");
            return 0;
        }
    }

    fprintf(stderr, "libbinder-lite listServices did not contain %s\n", wanted);
    return 1;
}

static int aosp_check_service(int fd, const char *name, uint32_t *out_handle)
{
    uint8_t parcel[512];
    size_t parcel_size = 0;
    uint8_t writebuf[1024];
    uint8_t readbuf[8192];
    uint8_t *p = writebuf;
    uint32_t cmd;
    struct binder_transaction_data tr;
    int first = 1;

    if (parcel_write_i32(parcel, sizeof(parcel), &parcel_size, 0) != 0 ||
        parcel_write_string16_ascii(parcel, sizeof(parcel), &parcel_size, AOSP_SM_DESCRIPTOR) != 0 ||
        parcel_write_string16_ascii(parcel, sizeof(parcel), &parcel_size, name) != 0) {
        fprintf(stderr, "failed to build libbinder-lite Parcel\n");
        return -1;
    }

    memset(&tr, 0, sizeof(tr));
    tr.target.handle = 0;
    tr.code = AOSP_SM_CHECK_SERVICE_TRANSACTION;
    tr.flags = TF_ACCEPT_FDS;
    tr.data_size = parcel_size;
    tr.offsets_size = 0;
    tr.data.ptr.buffer = (binder_uintptr_t)parcel;
    tr.data.ptr.offsets = 0;

    cmd = BC_TRANSACTION;
    append_u32(&p, cmd);
    append_bytes(&p, &tr, sizeof(tr));

    for (;;) {
        int n;
        uint8_t *ptr;
        uint8_t *end;

        if (first) {
            n = binder_write_read(fd,
                                  writebuf,
                                  (size_t)(p - writebuf),
                                  readbuf,
                                  sizeof(readbuf),
                                  "libbinder-lite checkService call");
            first = 0;
        } else {
            n = binder_write_read(fd,
                                  NULL,
                                  0,
                                  readbuf,
                                  sizeof(readbuf),
                                  "libbinder-lite checkService wait");
        }

        if (n < 0)
            return -1;

        ptr = readbuf;
        end = readbuf + n;

        while (ptr + sizeof(uint32_t) <= end) {
            uint32_t rcmd;

            memcpy(&rcmd, ptr, sizeof(rcmd));
            ptr += sizeof(rcmd);

            printf("libbinder-lite checkService got %s 0x%08x\n", cmd_name(rcmd), rcmd);

            if (rcmd == BR_REPLY) {
                struct binder_transaction_data reply;
                uint32_t handle;

                if (ptr + sizeof(reply) > end)
                    return -1;

                memcpy(&reply, ptr, sizeof(reply));
                ptr += sizeof(reply);

                handle = first_handle_from_transaction(&reply);

                if (!handle) {
                    if (reply.data.ptr.buffer)
                        binder_free_buffer(fd, reply.data.ptr.buffer);

                    fprintf(stderr, "libbinder-lite checkService returned null handle for %s\n", name);
                    return 1;
                }

                /*
                 * Important: acquire the returned handle before freeing the
                 * Binder reply buffer. This matches the working echo_client
                 * flow and prevents the returned reference from disappearing
                 * before the probe can transact on it.
                 */
                if (binder_send_handle_cmd(fd,
                                           BC_ACQUIRE,
                                           handle,
                                           "libbinder-lite checkService BC_ACQUIRE returned handle") != 0) {
                    if (reply.data.ptr.buffer)
                        binder_free_buffer(fd, reply.data.ptr.buffer);
                    return -1;
                }

                if (reply.data.ptr.buffer)
                    binder_free_buffer(fd, reply.data.ptr.buffer);

                *out_handle = handle;
                return 0;
            }

            if (rcmd == BR_NOOP || rcmd == BR_TRANSACTION_COMPLETE)
                continue;

            if (rcmd == BR_DEAD_REPLY || rcmd == BR_FAILED_REPLY) {
                fprintf(stderr, "libbinder-lite checkService failed cmd=0x%08x\n", rcmd);
                return 1;
            }

            if (rcmd == BR_INCREFS ||
                rcmd == BR_ACQUIRE ||
                rcmd == BR_RELEASE ||
                rcmd == BR_DECREFS) {
                if (handle_ref_cmd(fd, rcmd, &ptr, end, "libbinder-lite checkService") != 0)
                    return -1;
                continue;
            }

            fprintf(stderr, "libbinder-lite checkService unhandled cmd=0x%08x\n", rcmd);
            return -1;
        }
    }
}

static int aosp_get_service(int fd, const char *name, uint32_t *out_handle)
{
    uint8_t parcel[512];
    size_t parcel_size = 0;
    uint8_t writebuf[1024];
    uint8_t readbuf[8192];
    uint8_t *p = writebuf;
    uint32_t cmd;
    struct binder_transaction_data tr;
    int first = 1;

    if (parcel_write_i32(parcel, sizeof(parcel), &parcel_size, 0) != 0 ||
        parcel_write_string16_ascii(parcel, sizeof(parcel), &parcel_size, AOSP_SM_DESCRIPTOR) != 0 ||
        parcel_write_string16_ascii(parcel, sizeof(parcel), &parcel_size, name) != 0) {
        fprintf(stderr, "failed to build libbinder-lite Parcel\n");
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
    append_u32(&p, cmd);
    append_bytes(&p, &tr, sizeof(tr));

    for (;;) {
        int n;
        uint8_t *ptr;
        uint8_t *end;

        if (first) {
            n = binder_write_read(fd,
                                  writebuf,
                                  (size_t)(p - writebuf),
                                  readbuf,
                                  sizeof(readbuf),
                                  "libbinder-lite getService call");
            first = 0;
        } else {
            n = binder_write_read(fd,
                                  NULL,
                                  0,
                                  readbuf,
                                  sizeof(readbuf),
                                  "libbinder-lite getService wait");
        }

        if (n < 0)
            return -1;

        ptr = readbuf;
        end = readbuf + n;

        while (ptr + sizeof(uint32_t) <= end) {
            uint32_t rcmd;

            memcpy(&rcmd, ptr, sizeof(rcmd));
            ptr += sizeof(rcmd);

            printf("libbinder-lite getService got %s 0x%08x\n", cmd_name(rcmd), rcmd);

            if (rcmd == BR_REPLY) {
                struct binder_transaction_data reply;
                uint32_t handle;

                if (ptr + sizeof(reply) > end)
                    return -1;

                memcpy(&reply, ptr, sizeof(reply));
                ptr += sizeof(reply);

                handle = first_handle_from_transaction(&reply);

                if (!handle) {
                    if (reply.data.ptr.buffer)
                        binder_free_buffer(fd, reply.data.ptr.buffer);

                    fprintf(stderr, "libbinder-lite getService returned null handle for %s\n", name);
                    return 1;
                }

                /*
                 * Important: acquire the returned handle before freeing the
                 * Binder reply buffer. This matches the working echo_client
                 * flow and prevents the returned reference from disappearing
                 * before the probe can transact on it.
                 */
                if (binder_send_handle_cmd(fd,
                                           BC_ACQUIRE,
                                           handle,
                                           "libbinder-lite getService BC_ACQUIRE returned handle") != 0) {
                    if (reply.data.ptr.buffer)
                        binder_free_buffer(fd, reply.data.ptr.buffer);
                    return -1;
                }

                if (reply.data.ptr.buffer)
                    binder_free_buffer(fd, reply.data.ptr.buffer);

                *out_handle = handle;
                return 0;
            }

            if (rcmd == BR_NOOP || rcmd == BR_TRANSACTION_COMPLETE)
                continue;

            if (rcmd == BR_DEAD_REPLY || rcmd == BR_FAILED_REPLY) {
                fprintf(stderr, "libbinder-lite getService failed cmd=0x%08x\n", rcmd);
                return 1;
            }

            if (rcmd == BR_INCREFS ||
                rcmd == BR_ACQUIRE ||
                rcmd == BR_RELEASE ||
                rcmd == BR_DECREFS) {
                if (handle_ref_cmd(fd, rcmd, &ptr, end, "libbinder-lite getService") != 0)
                    return -1;
                continue;
            }

            fprintf(stderr, "libbinder-lite getService unhandled cmd=0x%08x\n", rcmd);
            return -1;
        }
    }
}

static size_t align8(size_t n)
{
    return (n + 7U) & ~7U;
}

static int parcel_read_i32(const void *data, size_t size, int32_t *out)
{
    if (!data || !out || size < sizeof(*out))
        return -1;

    memcpy(out, data, sizeof(*out));
    return 0;
}

static int aosp_add_service_handle(int fd, const char *name, uint32_t service_handle)
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

    printf("libbinder-lite addService name=%s handle=%u\n", name, service_handle);

    if (parcel_write_i32(parcel, sizeof(parcel), &parcel_size, 0) != 0 ||
        parcel_write_string16_ascii(parcel, sizeof(parcel), &parcel_size, AOSP_SM_DESCRIPTOR) != 0 ||
        parcel_write_string16_ascii(parcel, sizeof(parcel), &parcel_size, name) != 0) {
        fprintf(stderr, "failed to build libbinder-lite addService Parcel header\n");
        return -1;
    }

    parcel_size = align8(parcel_size);

    if (parcel_size + sizeof(obj) + sizeof(int32_t) > sizeof(parcel)) {
        fprintf(stderr, "libbinder-lite addService Parcel too large\n");
        return -1;
    }

    memset(parcel + parcel_size, 0, sizeof(parcel) - parcel_size);

    memset(&obj, 0, sizeof(obj));
    obj.type = BINDER_TYPE_HANDLE;
    obj.flags = FLAT_BINDER_FLAG_ACCEPTS_FDS;
    obj.handle = service_handle;
    obj.cookie = 0;

    offsets[0] = (binder_size_t)parcel_size;
    memcpy(parcel + parcel_size, &obj, sizeof(obj));
    parcel_size += sizeof(obj);

    parcel_size = align8(parcel_size);

    /*
     * Classic IServiceManager::addService also writes allowIsolated.
     * Newer implementations may include dump flags, but allowIsolated is
     * enough for this compatibility probe.
     */
    if (parcel_write_i32(parcel, sizeof(parcel), &parcel_size, 0) != 0) {
        fprintf(stderr, "failed to append allowIsolated\n");
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
    append_u32(&p, cmd);
    append_bytes(&p, &tr, sizeof(tr));

    for (;;) {
        int n;
        uint8_t *ptr;
        uint8_t *end;

        if (first) {
            n = binder_write_read(fd,
                                  writebuf,
                                  (size_t)(p - writebuf),
                                  readbuf,
                                  sizeof(readbuf),
                                  "libbinder-lite addService call");
            first = 0;
        } else {
            n = binder_write_read(fd,
                                  NULL,
                                  0,
                                  readbuf,
                                  sizeof(readbuf),
                                  "libbinder-lite addService wait");
        }

        if (n < 0)
            return -1;

        ptr = readbuf;
        end = readbuf + n;

        while (ptr + sizeof(uint32_t) <= end) {
            uint32_t rcmd;

            memcpy(&rcmd, ptr, sizeof(rcmd));
            ptr += sizeof(rcmd);

            printf("libbinder-lite addService got %s 0x%08x\n", cmd_name(rcmd), rcmd);

            if (rcmd == BR_REPLY) {
                struct binder_transaction_data reply;
                int32_t status = -999;

                if (ptr + sizeof(reply) > end)
                    return -1;

                memcpy(&reply, ptr, sizeof(reply));
                ptr += sizeof(reply);

                if (reply.data.ptr.buffer &&
                    parcel_read_i32((void *)(uintptr_t)reply.data.ptr.buffer,
                                    (size_t)reply.data_size,
                                    &status) != 0) {
                    binder_free_buffer(fd, reply.data.ptr.buffer);
                    return -1;
                }

                if (reply.data.ptr.buffer)
                    binder_free_buffer(fd, reply.data.ptr.buffer);

                printf("libbinder-lite addService reply status=%d\n", status);

                if (status == 0) {
                    printf("AOSP_ADD_SERVICE_OK\n");
                    return 0;
                }

                return 1;
            }

            if (rcmd == BR_NOOP || rcmd == BR_TRANSACTION_COMPLETE)
                continue;

            if (rcmd == BR_DEAD_REPLY || rcmd == BR_FAILED_REPLY) {
                fprintf(stderr, "libbinder-lite addService failed cmd=0x%08x\n", rcmd);
                return 1;
            }

            if (rcmd == BR_INCREFS ||
                rcmd == BR_ACQUIRE ||
                rcmd == BR_RELEASE ||
                rcmd == BR_DECREFS) {
                if (handle_ref_cmd(fd, rcmd, &ptr, end, "libbinder-lite addService") != 0)
                    return -1;
                continue;
            }

            fprintf(stderr, "libbinder-lite addService unhandled cmd=0x%08x\n", rcmd);
            return -1;
        }
    }
}

static int call_echo_handle(int fd, uint32_t handle)
{
    const char payload[] = "hello from libbinder-lite SM probe";
    uint8_t writebuf[1024];
    uint8_t readbuf[8192];
    uint8_t *p = writebuf;
    uint32_t cmd;
    struct binder_transaction_data tr;
    int first = 1;

    memset(&tr, 0, sizeof(tr));
    tr.target.handle = handle;
    tr.code = SC_CODE_ECHO;
    tr.flags = TF_ACCEPT_FDS;
    tr.data_size = sizeof(payload);
    tr.offsets_size = 0;
    tr.data.ptr.buffer = (binder_uintptr_t)payload;
    tr.data.ptr.offsets = 0;

    cmd = BC_TRANSACTION;
    append_u32(&p, cmd);
    append_bytes(&p, &tr, sizeof(tr));

    for (;;) {
        int n;
        uint8_t *ptr;
        uint8_t *end;

        if (first) {
            n = binder_write_read(fd,
                                  writebuf,
                                  (size_t)(p - writebuf),
                                  readbuf,
                                  sizeof(readbuf),
                                  "libbinder-lite echo call");
            first = 0;
        } else {
            n = binder_write_read(fd,
                                  NULL,
                                  0,
                                  readbuf,
                                  sizeof(readbuf),
                                  "libbinder-lite echo wait");
        }

        if (n < 0)
            return -1;

        ptr = readbuf;
        end = readbuf + n;

        while (ptr + sizeof(uint32_t) <= end) {
            uint32_t rcmd;

            memcpy(&rcmd, ptr, sizeof(rcmd));
            ptr += sizeof(rcmd);

            printf("libbinder-lite echo got %s 0x%08x\n", cmd_name(rcmd), rcmd);

            if (rcmd == BR_REPLY) {
                struct binder_transaction_data reply;

                if (ptr + sizeof(reply) > end)
                    return -1;

                memcpy(&reply, ptr, sizeof(reply));
                ptr += sizeof(reply);

                if (reply.data.ptr.buffer &&
                    reply.data_size >= sizeof(struct sc_text_reply)) {
                    struct sc_text_reply *txt =
                        (struct sc_text_reply *)(uintptr_t)reply.data.ptr.buffer;

                    printf("libbinder-lite echo reply status=%u text=%s\n",
                           txt->status,
                           txt->text);

                    binder_free_buffer(fd, reply.data.ptr.buffer);

                    return txt->status == 0 ? 0 : 1;
                }

                if (reply.data.ptr.buffer)
                    binder_free_buffer(fd, reply.data.ptr.buffer);

                return -1;
            }

            if (rcmd == BR_NOOP || rcmd == BR_TRANSACTION_COMPLETE)
                continue;

            if (rcmd == BR_DEAD_REPLY || rcmd == BR_FAILED_REPLY) {
                fprintf(stderr, "libbinder-lite echo failed cmd=0x%08x\n", rcmd);
                return 1;
            }

            if (rcmd == BR_INCREFS ||
                rcmd == BR_ACQUIRE ||
                rcmd == BR_RELEASE ||
                rcmd == BR_DECREFS) {
                if (handle_ref_cmd(fd, rcmd, &ptr, end, "libbinder-lite echo") != 0)
                    return -1;
                continue;
            }

            fprintf(stderr, "libbinder-lite echo unhandled cmd=0x%08x\n", rcmd);
            return -1;
        }
    }
}

namespace android_lite {

class BinderDriver {
public:
    BinderDriver()
        : fd_(binder_open_and_init())
    {
    }

    int fd() const
    {
        return fd_;
    }

private:
    int fd_;
};

class BpBinder {
public:
    BpBinder()
        : fd_(-1), handle_(0)
    {
    }

    BpBinder(int fd, uint32_t handle)
        : fd_(fd), handle_(handle)
    {
    }

    bool valid() const
    {
        return fd_ >= 0 && handle_ != 0;
    }

    uint32_t handle() const
    {
        return handle_;
    }

    int transactEcho() const
    {
        if (!valid()) {
            fprintf(stderr, "libbinder-lite BpBinder invalid handle\n");
            return 1;
        }

        return call_echo_handle(fd_, handle_);
    }

private:
    int fd_;
    uint32_t handle_;
};

class ServiceManagerProxy {
public:
    explicit ServiceManagerProxy(BinderDriver &driver)
        : driver_(driver)
    {
    }

    bool listServicesContains(const char *name)
    {
        return aosp_list_services_contains(driver_.fd(), name) == 0;
    }

    BpBinder checkService(const char *name)
    {
        uint32_t handle = 0;

        printf("libbinder-lite API checkService(%s)\n", name);

        if (aosp_check_service(driver_.fd(), name, &handle) != 0)
            return BpBinder();

        return BpBinder(driver_.fd(), handle);
    }

    BpBinder getService(const char *name)
    {
        uint32_t handle = 0;

        printf("libbinder-lite API getService(%s)\n", name);

        if (aosp_get_service(driver_.fd(), name, &handle) != 0)
            return BpBinder();

        return BpBinder(driver_.fd(), handle);
    }

    int addService(const char *name, const BpBinder &service)
    {
        if (!service.valid()) {
            fprintf(stderr, "libbinder-lite API addService(%s): invalid service\n", name);
            return 1;
        }

        printf("libbinder-lite API addService(%s, handle=%u)\n",
               name,
               service.handle());

        return aosp_add_service_handle(driver_.fd(), name, service.handle());
    }

private:
    BinderDriver &driver_;
};

static ServiceManagerProxy defaultServiceManager(BinderDriver &driver)
{
    return ServiceManagerProxy(driver);
}

}  // namespace android_lite

int main(int argc, char **argv)
{
    const char *name = argc >= 2 ? argv[1] : "test.aosp";
    const char *alias = "test.aosp.alias";

    android_lite::BinderDriver driver;
    android_lite::ServiceManagerProxy sm =
        android_lite::defaultServiceManager(driver);

    printf("libbinder-lite API defaultServiceManager OK\n");

    if (!sm.listServicesContains(name))
        return 1;

    android_lite::BpBinder checked = sm.checkService(name);
    if (!checked.valid()) {
        fprintf(stderr, "libbinder-lite API checkService returned invalid binder\n");
        return 1;
    }

    printf("libbinder-lite API checkService got handle=%u\n", checked.handle());

    if (checked.transactEcho() != 0)
        return 1;

    printf("LIBBINDER_LITE_CHECK_SERVICE_OK\n");

    android_lite::BpBinder got = sm.getService(name);
    if (!got.valid()) {
        fprintf(stderr, "libbinder-lite API getService returned invalid binder\n");
        return 1;
    }

    printf("libbinder-lite API getService got handle=%u\n", got.handle());

    if (got.transactEcho() != 0)
        return 1;

    printf("LIBBINDER_LITE_GET_SERVICE_OK\n");

    if (sm.addService(alias, got) != 0)
        return 1;

    printf("LIBBINDER_LITE_ADD_SERVICE_OK\n");

    if (!sm.listServicesContains(alias))
        return 1;

    android_lite::BpBinder aliasBinder = sm.checkService(alias);
    if (!aliasBinder.valid()) {
        fprintf(stderr, "libbinder-lite API alias checkService returned invalid binder\n");
        return 1;
    }

    printf("libbinder-lite API alias checkService got handle=%u\n",
           aliasBinder.handle());

    if (aliasBinder.transactEcho() != 0)
        return 1;

    printf("LIBBINDER_LITE_ALIAS_SERVICE_OK\n");
    printf("LIBBINDER_LITE_API_CLIENT_OK\n");
    printf("LIBBINDER_LITE_CLIENT_OK\n");
    return 0;
}
