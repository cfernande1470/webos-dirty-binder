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

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    if (argc != 2) {
        fprintf(stderr, "usage: %s server|client\n", argv[0]);
        return 2;
    }

    if (strcmp(argv[1], "server") == 0)
        return run_server();

    if (strcmp(argv[1], "client") == 0)
        return run_client();

    fprintf(stderr, "unknown mode: %s\n", argv[1]);
    return 2;
}
