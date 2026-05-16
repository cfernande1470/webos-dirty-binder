#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <sys/types.h>
#include <linux/android/binder.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#define BINDER_DEVICE "/dev/binder"
#define BINDER_MMAP_SIZE (1024 * 1024)
#define PING_CODE 0x50494e47U /* ASCII: PING */

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

static int binder_open_and_init(void)
{
    int fd;
    void *map;
    struct binder_version ver;
    int max_threads = 4;

    printf("open %s\n", BINDER_DEVICE);
    fd = open(BINDER_DEVICE, O_RDWR | O_CLOEXEC);
    if (fd < 0)
        die("open /dev/binder");

    memset(&ver, 0, sizeof(ver));
    printf("ioctl BINDER_VERSION\n");
    if (ioctl(fd, BINDER_VERSION, &ver) < 0)
        die("ioctl BINDER_VERSION");
    printf("binder protocol_version=%d\n", ver.protocol_version);

    printf("ioctl BINDER_SET_MAX_THREADS\n");
    if (ioctl(fd, BINDER_SET_MAX_THREADS, &max_threads) < 0)
        die("ioctl BINDER_SET_MAX_THREADS");
    printf("BINDER_SET_MAX_THREADS ok\n");

    printf("mmap binder size=%d\n", BINDER_MMAP_SIZE);
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
        fprintf(stderr, "%s: ioctl BINDER_WRITE_READ failed: errno=%d (%s)\n",
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
    struct binder_write_read bwr;

    append_u32(&p, cmd);
    append_bytes(&p, &buffer, sizeof(buffer));

    memset(&bwr, 0, sizeof(bwr));
    bwr.write_size = (size_t)(p - writebuf);
    bwr.write_buffer = (binder_uintptr_t)writebuf;
    bwr.read_size = 0;
    bwr.read_buffer = 0;

    printf("free buffer 0x%" PRIx64 " write-only\n", (uint64_t)buffer);

    if (ioctl(fd, BINDER_WRITE_READ, &bwr) < 0) {
        fprintf(stderr, "free_buffer: ioctl BINDER_WRITE_READ failed: errno=%d (%s)\n",
                errno, strerror(errno));
        return -1;
    }

    printf("free_buffer: write_consumed=%" PRIu64 " read_consumed=%" PRIu64 "\n",
           (uint64_t)bwr.write_consumed,
           (uint64_t)bwr.read_consumed);

    return 0;
}

static int server_process_readbuf(int fd, uint8_t *readbuf, int n)
{
    uint8_t *ptr = readbuf;
    uint8_t *end = readbuf + n;

    printf("server_process_readbuf: n=%d\n", n);

    while (ptr + sizeof(uint32_t) <= end) {
        uint32_t cmd;
        memcpy(&cmd, ptr, sizeof(cmd));
        ptr += sizeof(cmd);

        printf("server got cmd=0x%08x\n", cmd);

        if (cmd == BR_TRANSACTION) {
            struct binder_transaction_data tr;
            const char reply[] = "PONG from webOS binder server";
            uint8_t writebuf[1024];
            uint8_t reply_readbuf[8192];
            uint8_t *wp = writebuf;
            uint32_t wcmd;
            struct binder_transaction_data reply_tr;

            if (ptr + sizeof(tr) > end) {
                fprintf(stderr, "server: truncated BR_TRANSACTION\n");
                return 1;
            }

            memcpy(&tr, ptr, sizeof(tr));
            ptr += sizeof(tr);

            printf("server BR_TRANSACTION code=0x%x flags=0x%x sender_pid=%d sender_euid=%u data_size=%" PRIu64 " offsets_size=%" PRIu64 " buffer=0x%" PRIx64 "\n",
                   tr.code,
                   tr.flags,
                   tr.sender_pid,
                   tr.sender_euid,
                   (uint64_t)tr.data_size,
                   (uint64_t)tr.offsets_size,
                   (uint64_t)tr.data.ptr.buffer);

            if (tr.data.ptr.buffer && tr.data_size > 0) {
                size_t to_print = tr.data_size;
                if (to_print > 120)
                    to_print = 120;

                printf("server payload: ");
                fwrite((const void *)(uintptr_t)tr.data.ptr.buffer, 1, to_print, stdout);
                printf("\n");
            }

            wcmd = BC_FREE_BUFFER;
            append_u32(&wp, wcmd);
            append_bytes(&wp, &tr.data.ptr.buffer, sizeof(tr.data.ptr.buffer));

            wcmd = BC_REPLY;
            append_u32(&wp, wcmd);

            memset(&reply_tr, 0, sizeof(reply_tr));
            reply_tr.data_size = sizeof(reply);
            reply_tr.offsets_size = 0;
            reply_tr.data.ptr.buffer = (binder_uintptr_t)reply;
            reply_tr.data.ptr.offsets = 0;

            append_bytes(&wp, &reply_tr, sizeof(reply_tr));

            if (binder_write_read(fd, writebuf, (size_t)(wp - writebuf),
                                  reply_readbuf, sizeof(reply_readbuf), "server_reply") < 0)
                return 1;

            /*
             * server_reply can itself return commands. Process them too,
             * but normally we only expect BR_TRANSACTION_COMPLETE / BR_NOOP.
             */
            printf("server_reply completed\n");
        } else if (cmd == BR_TRANSACTION_COMPLETE) {
            printf("server BR_TRANSACTION_COMPLETE\n");
        } else if (cmd == BR_NOOP) {
            printf("server BR_NOOP\n");
        } else if (cmd == BR_SPAWN_LOOPER) {
            printf("server BR_SPAWN_LOOPER ignored\n");
        } else {
            printf("server unhandled cmd=0x%08x\n", cmd);

            /*
             * Unknown commands may have payloads. To avoid desyncing the
             * parser, stop processing this buffer.
             */
            return 0;
        }
    }

    if (ptr != end) {
        printf("server trailing bytes=%ld\n", (long)(end - ptr));
    }

    return 0;
}

static int run_server(void)
{
    int fd = binder_open_and_init();

    printf("ioctl BINDER_SET_CONTEXT_MGR\n");
    if (ioctl(fd, BINDER_SET_CONTEXT_MGR, 0) < 0)
        die("ioctl BINDER_SET_CONTEXT_MGR");
    printf("BINDER_SET_CONTEXT_MGR ok\n");

    {
        uint8_t writebuf[64];
        uint8_t readbuf[8192];
        uint8_t *p = writebuf;
        uint32_t cmd = BC_ENTER_LOOPER;
        int n;

        append_u32(&p, cmd);

        n = binder_write_read(fd, writebuf, (size_t)(p - writebuf),
                              readbuf, sizeof(readbuf), "server_enter_looper");
        if (n < 0)
            return 1;

        /*
         * Important:
         * BINDER_WRITE_READ can return BR_TRANSACTION in the same ioctl
         * that sends BC_ENTER_LOOPER. Do not ignore this read buffer.
         */
        if (n > 0) {
            if (server_process_readbuf(fd, readbuf, n) != 0)
                return 1;
        }
    }

    printf("server waiting for transactions\n");

    for (;;) {
        uint8_t readbuf[8192];
        int n = binder_write_read(fd, NULL, 0, readbuf, sizeof(readbuf), "server_loop");

        if (n < 0)
            return 1;

        if (n > 0) {
            if (server_process_readbuf(fd, readbuf, n) != 0)
                return 1;
        }
    }

    return 0;
}


static int run_client(void)
{
    int fd = binder_open_and_init();
    uint8_t writebuf[1024];
    uint8_t readbuf[8192];
    uint8_t *p = writebuf;
    const char payload[] = "PING from webOS binder client";
    uint32_t cmd;
    struct binder_transaction_data tr;

    memset(&tr, 0, sizeof(tr));
    tr.target.handle = 0;
    tr.code = PING_CODE;
    tr.flags = TF_ACCEPT_FDS;
    tr.data_size = sizeof(payload);
    tr.offsets_size = 0;
    tr.data.ptr.buffer = (binder_uintptr_t)payload;
    tr.data.ptr.offsets = 0;

    printf("client sending transaction to handle 0\n");

    cmd = BC_TRANSACTION;
    append_u32(&p, cmd);
    append_bytes(&p, &tr, sizeof(tr));

    for (;;) {
        int n = binder_write_read(fd, writebuf, (size_t)(p - writebuf),
                                  readbuf, sizeof(readbuf), "client_call");

        p = writebuf;

        if (n < 0)
            return 1;

        uint8_t *ptr = readbuf;
        uint8_t *end = readbuf + n;

        while (ptr + sizeof(uint32_t) <= end) {
            uint32_t rcmd;
            memcpy(&rcmd, ptr, sizeof(rcmd));
            ptr += sizeof(rcmd);

            printf("client got cmd=0x%08x\n", rcmd);

            if (rcmd == BR_TRANSACTION_COMPLETE) {
                printf("client BR_TRANSACTION_COMPLETE\n");
            } else if (rcmd == BR_REPLY) {
                struct binder_transaction_data reply;

                if (ptr + sizeof(reply) > end) {
                    fprintf(stderr, "client: truncated BR_REPLY\n");
                    return 1;
                }

                memcpy(&reply, ptr, sizeof(reply));
                ptr += sizeof(reply);

                printf("client BR_REPLY code=0x%x flags=0x%x data_size=%" PRIu64 " offsets_size=%" PRIu64 " buffer=0x%" PRIx64 "\n",
                       reply.code,
                       reply.flags,
                       (uint64_t)reply.data_size,
                       (uint64_t)reply.offsets_size,
                       (uint64_t)reply.data.ptr.buffer);

                if (reply.data.ptr.buffer && reply.data_size > 0) {
                    printf("client reply payload: ");
                    fwrite((const void *)(uintptr_t)reply.data.ptr.buffer, 1, reply.data_size, stdout);
                    printf("\n");
                }

                binder_free_buffer(fd, reply.data.ptr.buffer);
                return 0;
            } else if (rcmd == BR_DEAD_REPLY) {
                fprintf(stderr, "client BR_DEAD_REPLY\n");
                return 2;
            } else if (rcmd == BR_FAILED_REPLY) {
                fprintf(stderr, "client BR_FAILED_REPLY\n");
                return 3;
            } else if (rcmd == BR_NOOP) {
                printf("client BR_NOOP\n");
            } else {
                printf("client unhandled cmd=0x%08x\n", rcmd);
            }
        }
    }

    return 0;
}


static const char *binder_obj_type_name(uint32_t type)
{
    switch (type) {
    case BINDER_TYPE_BINDER:
        return "BINDER_TYPE_BINDER";
    case BINDER_TYPE_WEAK_BINDER:
        return "BINDER_TYPE_WEAK_BINDER";
    case BINDER_TYPE_HANDLE:
        return "BINDER_TYPE_HANDLE";
    case BINDER_TYPE_WEAK_HANDLE:
        return "BINDER_TYPE_WEAK_HANDLE";
    case BINDER_TYPE_FD:
        return "BINDER_TYPE_FD";
    default:
        return "UNKNOWN";
    }
}

static int parse_and_print_objects(const char *who, struct binder_transaction_data *tr)
{
    binder_size_t *offp;
    binder_size_t count;
    binder_size_t i;

    printf("%s object parse: data_size=%" PRIu64 " offsets_size=%" PRIu64 " data=0x%" PRIx64 " offsets=0x%" PRIx64 "\n",
           who,
           (uint64_t)tr->data_size,
           (uint64_t)tr->offsets_size,
           (uint64_t)tr->data.ptr.buffer,
           (uint64_t)tr->data.ptr.offsets);

    if (!tr->offsets_size) {
        printf("%s object parse: no objects\n", who);
        return 0;
    }

    if (!tr->data.ptr.buffer || !tr->data.ptr.offsets) {
        fprintf(stderr, "%s object parse: missing buffer or offsets\n", who);
        return 1;
    }

    if (tr->offsets_size % sizeof(binder_size_t) != 0) {
        fprintf(stderr, "%s object parse: bad offsets_size\n", who);
        return 1;
    }

    count = tr->offsets_size / sizeof(binder_size_t);
    offp = (binder_size_t *)(uintptr_t)tr->data.ptr.offsets;

    printf("%s object parse: count=%" PRIu64 "\n", who, (uint64_t)count);

    for (i = 0; i < count; i++) {
        binder_size_t off = offp[i];

        if (off + sizeof(struct flat_binder_object) > tr->data_size) {
            fprintf(stderr, "%s object[%" PRIu64 "]: bad offset=%" PRIu64 "\n",
                    who, (uint64_t)i, (uint64_t)off);
            return 1;
        }

        struct flat_binder_object *obj =
            (struct flat_binder_object *)((uint8_t *)(uintptr_t)tr->data.ptr.buffer + off);

        printf("%s object[%" PRIu64 "]: offset=%" PRIu64 " type=0x%08x %s flags=0x%08x binder=0x%" PRIx64 " handle=%u cookie=0x%" PRIx64 "\n",
               who,
               (uint64_t)i,
               (uint64_t)off,
               obj->type,
               binder_obj_type_name(obj->type),
               obj->flags,
               (uint64_t)obj->binder,
               obj->handle,
               (uint64_t)obj->cookie);
    }

    return 0;
}

static uint32_t object_first_handle_from_transaction(struct binder_transaction_data *tr)
{
    if (!tr->offsets_size || !tr->data.ptr.buffer || !tr->data.ptr.offsets)
        return 0;

    if (tr->offsets_size < sizeof(binder_size_t))
        return 0;

    binder_size_t *offp = (binder_size_t *)(uintptr_t)tr->data.ptr.offsets;
    binder_size_t off = offp[0];

    if (off + sizeof(struct flat_binder_object) > tr->data_size)
        return 0;

    struct flat_binder_object *obj =
        (struct flat_binder_object *)((uint8_t *)(uintptr_t)tr->data.ptr.buffer + off);

    if (obj->type != BINDER_TYPE_HANDLE)
        return 0;

    return obj->handle;
}

static int object_client_reply_to_callback(int fd, struct binder_transaction_data *tr)
{
    const char reply[] = "CLIENT CALLBACK OK";
    uint8_t writebuf[1024];
    uint8_t *wp = writebuf;
    uint32_t wcmd;
    struct binder_transaction_data reply_tr;
    struct binder_write_read bwr;

    printf("object-client callback transaction code=0x%x flags=0x%x sender_pid=%d sender_euid=%u data_size=%" PRIu64 " offsets_size=%" PRIu64 " buffer=0x%" PRIx64 "\n",
           tr->code,
           tr->flags,
           tr->sender_pid,
           tr->sender_euid,
           (uint64_t)tr->data_size,
           (uint64_t)tr->offsets_size,
           (uint64_t)tr->data.ptr.buffer);

    if (tr->data.ptr.buffer && tr->data_size > 0) {
        printf("object-client callback payload: ");
        fwrite((const void *)(uintptr_t)tr->data.ptr.buffer, 1, tr->data_size, stdout);
        printf("\n");
    }

    wcmd = BC_FREE_BUFFER;
    append_u32(&wp, wcmd);
    append_bytes(&wp, &tr->data.ptr.buffer, sizeof(tr->data.ptr.buffer));

    wcmd = BC_REPLY;
    append_u32(&wp, wcmd);

    memset(&reply_tr, 0, sizeof(reply_tr));
    reply_tr.data_size = sizeof(reply);
    reply_tr.offsets_size = 0;
    reply_tr.data.ptr.buffer = (binder_uintptr_t)reply;
    reply_tr.data.ptr.offsets = 0;

    append_bytes(&wp, &reply_tr, sizeof(reply_tr));

    memset(&bwr, 0, sizeof(bwr));
    bwr.write_size = (size_t)(wp - writebuf);
    bwr.write_buffer = (binder_uintptr_t)writebuf;
    bwr.read_size = 0;
    bwr.read_buffer = 0;

    printf("object-client sending callback reply write_size=%" PRIu64 "\n",
           (uint64_t)bwr.write_size);

    if (ioctl(fd, BINDER_WRITE_READ, &bwr) < 0) {
        fprintf(stderr, "object-client callback reply ioctl failed: errno=%d (%s)\n",
                errno, strerror(errno));
        return -1;
    }

    printf("object-client callback reply write_consumed=%" PRIu64 " read_consumed=%" PRIu64 "\n",
           (uint64_t)bwr.write_consumed,
           (uint64_t)bwr.read_consumed);

    return 0;
}

static int object_server_call_client_handle(int fd, uint32_t handle)
{
    const char payload[] = "CALLBACK from object-server";
    uint8_t writebuf[1024];
    uint8_t readbuf[8192];
    uint8_t *wp = writebuf;
    uint32_t wcmd;
    struct binder_transaction_data tr;
    int first = 1;

    printf("object-server calling client handle=%u\n", handle);

    memset(&tr, 0, sizeof(tr));
    tr.target.handle = handle;
    tr.code = 0x43424b31U;
    tr.flags = TF_ACCEPT_FDS;
    tr.data_size = sizeof(payload);
    tr.offsets_size = 0;
    tr.data.ptr.buffer = (binder_uintptr_t)payload;
    tr.data.ptr.offsets = 0;

    wcmd = BC_TRANSACTION;
    append_u32(&wp, wcmd);
    append_bytes(&wp, &tr, sizeof(tr));

    for (;;) {
        int n;

        if (first) {
            n = binder_write_read(fd, writebuf, (size_t)(wp - writebuf),
                                  readbuf, sizeof(readbuf), "object-server_callback_call");
            first = 0;
        } else {
            n = binder_write_read(fd, NULL, 0,
                                  readbuf, sizeof(readbuf), "object-server_callback_wait");
        }

        if (n < 0)
            return -1;

        uint8_t *ptr = readbuf;
        uint8_t *end = readbuf + n;

        while (ptr + sizeof(uint32_t) <= end) {
            uint32_t rcmd;
            memcpy(&rcmd, ptr, sizeof(rcmd));
            ptr += sizeof(rcmd);

            printf("object-server callback got cmd=0x%08x\n", rcmd);

            if (rcmd == BR_TRANSACTION_COMPLETE) {
                printf("object-server callback BR_TRANSACTION_COMPLETE\n");
            } else if (rcmd == BR_NOOP) {
                printf("object-server callback BR_NOOP\n");
            } else if (rcmd == BR_REPLY) {
                struct binder_transaction_data reply;

                if (ptr + sizeof(reply) > end) {
                    fprintf(stderr, "object-server callback: truncated BR_REPLY\n");
                    return -1;
                }

                memcpy(&reply, ptr, sizeof(reply));
                ptr += sizeof(reply);

                printf("object-server callback BR_REPLY code=0x%x flags=0x%x data_size=%" PRIu64 " offsets_size=%" PRIu64 " buffer=0x%" PRIx64 "\n",
                       reply.code,
                       reply.flags,
                       (uint64_t)reply.data_size,
                       (uint64_t)reply.offsets_size,
                       (uint64_t)reply.data.ptr.buffer);

                if (reply.data.ptr.buffer && reply.data_size > 0) {
                    printf("object-server callback reply payload: ");
                    fwrite((const void *)(uintptr_t)reply.data.ptr.buffer, 1, reply.data_size, stdout);
                    printf("\n");
                }

                binder_free_buffer(fd, reply.data.ptr.buffer);
                return 0;
            } else {
                printf("object-server callback unhandled cmd=0x%08x\n", rcmd);
            }
        }
    }

    return 0;
}


static int object_server_process_readbuf(int fd, uint8_t *readbuf, int n)
{
    uint8_t *ptr = readbuf;
    uint8_t *end = readbuf + n;

    printf("object-server process readbuf n=%d\n", n);

    while (ptr + sizeof(uint32_t) <= end) {
        uint32_t rcmd;
        memcpy(&rcmd, ptr, sizeof(rcmd));
        ptr += sizeof(rcmd);

        printf("object-server got cmd=0x%08x\n", rcmd);

        if (rcmd == BR_TRANSACTION) {
            struct binder_transaction_data tr;
            const char reply[] = "OBJECT OK";
            uint8_t reply_writebuf[1024];
            uint8_t reply_readbuf[8192];
            uint8_t *wp = reply_writebuf;
            uint32_t wcmd;
            struct binder_transaction_data reply_tr;

            if (ptr + sizeof(tr) > end) {
                fprintf(stderr, "object-server: truncated BR_TRANSACTION\n");
                return 1;
            }

            memcpy(&tr, ptr, sizeof(tr));
            ptr += sizeof(tr);

            printf("object-server BR_TRANSACTION code=0x%x flags=0x%x sender_pid=%d sender_euid=%u data_size=%" PRIu64 " offsets_size=%" PRIu64 "\n",
                   tr.code,
                   tr.flags,
                   tr.sender_pid,
                   tr.sender_euid,
                   (uint64_t)tr.data_size,
                   (uint64_t)tr.offsets_size);

            if (parse_and_print_objects("object-server", &tr) != 0)
                return 1;

            {
                uint32_t callback_handle = object_first_handle_from_transaction(&tr);
                if (callback_handle != 0) {
                    if (object_server_call_client_handle(fd, callback_handle) != 0)
                        return 1;
                } else {
                    printf("object-server: no callback handle found in transaction\n");
                }
            }

            wcmd = BC_FREE_BUFFER;
            append_u32(&wp, wcmd);
            append_bytes(&wp, &tr.data.ptr.buffer, sizeof(tr.data.ptr.buffer));

            wcmd = BC_REPLY;
            append_u32(&wp, wcmd);

            memset(&reply_tr, 0, sizeof(reply_tr));
            reply_tr.data_size = sizeof(reply);
            reply_tr.offsets_size = 0;
            reply_tr.data.ptr.buffer = (binder_uintptr_t)reply;
            reply_tr.data.ptr.offsets = 0;

            append_bytes(&wp, &reply_tr, sizeof(reply_tr));

            if (binder_write_read(fd, reply_writebuf, (size_t)(wp - reply_writebuf),
                                  reply_readbuf, sizeof(reply_readbuf), "object-server_reply") < 0)
                return 1;

            printf("object-server reply sent\n");
        } else if (rcmd == BR_NOOP) {
            printf("object-server BR_NOOP\n");
        } else if (rcmd == BR_SPAWN_LOOPER) {
            printf("object-server BR_SPAWN_LOOPER ignored\n");
        } else if (rcmd == BR_TRANSACTION_COMPLETE) {
            printf("object-server BR_TRANSACTION_COMPLETE\n");
        } else {
            printf("object-server unhandled cmd=0x%08x\n", rcmd);
            return 0;
        }
    }

    return 0;
}

static int run_object_server(void)
{
    int fd = binder_open_and_init();

    printf("object-server ioctl BINDER_SET_CONTEXT_MGR\n");
    if (ioctl(fd, BINDER_SET_CONTEXT_MGR, 0) < 0)
        die("object-server ioctl BINDER_SET_CONTEXT_MGR");
    printf("object-server BINDER_SET_CONTEXT_MGR ok\n");

    {
        uint8_t writebuf[64];
        uint8_t readbuf[8192];
        uint8_t *p = writebuf;
        uint32_t cmd = BC_ENTER_LOOPER;
        int n;

        append_u32(&p, cmd);

        n = binder_write_read(fd, writebuf, (size_t)(p - writebuf),
                              readbuf, sizeof(readbuf), "object-server_enter_looper");
        if (n < 0)
            return 1;

        if (n > 0) {
            if (object_server_process_readbuf(fd, readbuf, n) != 0)
                return 1;
        }
    }

    printf("object-server waiting\n");

    for (;;) {
        uint8_t readbuf[8192];
        int n = binder_write_read(fd, NULL, 0, readbuf, sizeof(readbuf), "object-server_loop");

        if (n < 0)
            return 1;

        if (n > 0) {
            if (object_server_process_readbuf(fd, readbuf, n) != 0)
                return 1;
        }
    }

    return 0;
}


static int binder_send_ptr_cookie_cmd(int fd, uint32_t cmd, binder_uintptr_t ptr, binder_uintptr_t cookie, const char *tag)
{
    uint8_t writebuf[64];
    uint8_t *p = writebuf;
    struct binder_ptr_cookie pc;

    memset(&pc, 0, sizeof(pc));
    pc.ptr = ptr;
    pc.cookie = cookie;

    append_u32(&p, cmd);
    append_bytes(&p, &pc, sizeof(pc));

    printf("%s: cmd=0x%08x ptr=0x%" PRIx64 " cookie=0x%" PRIx64 "\n",
           tag, cmd, (uint64_t)ptr, (uint64_t)cookie);

    return binder_write_read(fd, writebuf, (size_t)(p - writebuf),
                             NULL, 0, tag) < 0 ? -1 : 0;
}

static int object_client_handle_ref_cmd(int fd, uint32_t rcmd, uint8_t **ptr, uint8_t *end)
{
    struct binder_ptr_cookie pc;

    if (*ptr + sizeof(pc) > end) {
        fprintf(stderr, "object-client: truncated ref cmd=0x%08x\n", rcmd);
        return -1;
    }

    memcpy(&pc, *ptr, sizeof(pc));
    *ptr += sizeof(pc);

    if (rcmd == BR_INCREFS) {
        printf("object-client BR_INCREFS ptr=0x%" PRIx64 " cookie=0x%" PRIx64 "\n",
               (uint64_t)pc.ptr, (uint64_t)pc.cookie);
        return binder_send_ptr_cookie_cmd(fd, BC_INCREFS_DONE, pc.ptr, pc.cookie, "object-client BC_INCREFS_DONE");
    }

    if (rcmd == BR_ACQUIRE) {
        printf("object-client BR_ACQUIRE ptr=0x%" PRIx64 " cookie=0x%" PRIx64 "\n",
               (uint64_t)pc.ptr, (uint64_t)pc.cookie);
        return binder_send_ptr_cookie_cmd(fd, BC_ACQUIRE_DONE, pc.ptr, pc.cookie, "object-client BC_ACQUIRE_DONE");
    }

    if (rcmd == BR_DECREFS) {
        printf("object-client BR_DECREFS ptr=0x%" PRIx64 " cookie=0x%" PRIx64 "\n",
               (uint64_t)pc.ptr, (uint64_t)pc.cookie);
        return 0;
    }

    if (rcmd == BR_RELEASE) {
        printf("object-client BR_RELEASE ptr=0x%" PRIx64 " cookie=0x%" PRIx64 "\n",
               (uint64_t)pc.ptr, (uint64_t)pc.cookie);
        return 0;
    }

    return 0;
}

static int run_object_client(void)
{
    int fd = binder_open_and_init();

    struct flat_binder_object obj;
    binder_size_t offsets[1];
    uint8_t writebuf[1024];
    uint8_t readbuf[8192];
    uint8_t *p = writebuf;
    uint32_t cmd;
    struct binder_transaction_data tr;

    memset(&obj, 0, sizeof(obj));
    obj.type = BINDER_TYPE_BINDER;
    obj.flags = FLAT_BINDER_FLAG_ACCEPTS_FDS;
    obj.binder = (binder_uintptr_t)&obj;
    obj.cookie = (binder_uintptr_t)0x434f4f4b49455ULL;

    offsets[0] = 0;

    memset(&tr, 0, sizeof(tr));
    tr.target.handle = 0;
    tr.code = 0x4f424a31U;
    tr.flags = TF_ACCEPT_FDS;
    tr.data_size = sizeof(obj);
    tr.offsets_size = sizeof(offsets);
    tr.data.ptr.buffer = (binder_uintptr_t)&obj;
    tr.data.ptr.offsets = (binder_uintptr_t)offsets;

    printf("object-client sending BINDER_TYPE_BINDER object to handle 0\n");
    printf("object-client local object type=0x%08x %s binder=0x%" PRIx64 " cookie=0x%" PRIx64 " offsets[0]=%" PRIu64 "\n",
           obj.type,
           binder_obj_type_name(obj.type),
           (uint64_t)obj.binder,
           (uint64_t)obj.cookie,
           (uint64_t)offsets[0]);

    cmd = BC_TRANSACTION;
    append_u32(&p, cmd);
    append_bytes(&p, &tr, sizeof(tr));

    for (;;) {
        int n = binder_write_read(fd, writebuf, (size_t)(p - writebuf),
                                  readbuf, sizeof(readbuf), "object-client_call");

        p = writebuf;

        if (n < 0)
            return 1;

        uint8_t *ptr = readbuf;
        uint8_t *end = readbuf + n;

        while (ptr + sizeof(uint32_t) <= end) {
            uint32_t rcmd;
            memcpy(&rcmd, ptr, sizeof(rcmd));
            ptr += sizeof(rcmd);

            printf("object-client got cmd=0x%08x\n", rcmd);

            if (rcmd == BR_TRANSACTION_COMPLETE) {
                printf("object-client BR_TRANSACTION_COMPLETE\n");
            } else if (rcmd == BR_TRANSACTION) {
                struct binder_transaction_data cb_tr;

                if (ptr + sizeof(cb_tr) > end) {
                    fprintf(stderr, "object-client: truncated callback BR_TRANSACTION\n");
                    return 1;
                }

                memcpy(&cb_tr, ptr, sizeof(cb_tr));
                ptr += sizeof(cb_tr);

                if (object_client_reply_to_callback(fd, &cb_tr) != 0)
                    return 1;
            } else if (rcmd == BR_REPLY) {
                struct binder_transaction_data reply;

                if (ptr + sizeof(reply) > end) {
                    fprintf(stderr, "object-client: truncated BR_REPLY\n");
                    return 1;
                }

                memcpy(&reply, ptr, sizeof(reply));
                ptr += sizeof(reply);

                printf("object-client BR_REPLY code=0x%x flags=0x%x data_size=%" PRIu64 " offsets_size=%" PRIu64 " buffer=0x%" PRIx64 "\n",
                       reply.code,
                       reply.flags,
                       (uint64_t)reply.data_size,
                       (uint64_t)reply.offsets_size,
                       (uint64_t)reply.data.ptr.buffer);

                if (reply.data.ptr.buffer && reply.data_size > 0) {
                    printf("object-client reply payload: ");
                    fwrite((const void *)(uintptr_t)reply.data.ptr.buffer, 1, reply.data_size, stdout);
                    printf("\n");
                }

                binder_free_buffer(fd, reply.data.ptr.buffer);
                return 0;
            } else if (rcmd == BR_INCREFS || rcmd == BR_ACQUIRE ||
                       rcmd == BR_DECREFS || rcmd == BR_RELEASE) {
                if (object_client_handle_ref_cmd(fd, rcmd, &ptr, end) != 0)
                    return 1;
            } else if (rcmd == BR_NOOP) {
                printf("object-client BR_NOOP\n");
            } else if (rcmd == BR_DEAD_REPLY) {
                fprintf(stderr, "object-client BR_DEAD_REPLY\n");
                return 2;
            } else if (rcmd == BR_FAILED_REPLY) {
                fprintf(stderr, "object-client BR_FAILED_REPLY\n");
                return 3;
            } else {
                printf("object-client unhandled cmd=0x%08x\n", rcmd);
            }
        }
    }

    return 0;
}


int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    if (argc != 2) {
        fprintf(stderr, "usage: %s server|client|object-server|object-client\n", argv[0]);
        return 2;
    }

    if (strcmp(argv[1], "server") == 0)
        return run_server();

    if (strcmp(argv[1], "client") == 0)
        return run_client();

    if (strcmp(argv[1], "object-server") == 0)
        return run_object_server();

    if (strcmp(argv[1], "object-client") == 0)
        return run_object_client();

    fprintf(stderr, "unknown mode: %s\n", argv[1]);
    return 2;
}
