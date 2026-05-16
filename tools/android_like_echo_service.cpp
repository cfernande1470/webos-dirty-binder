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

#include "android_like_echo_iface.hpp"

#define BINDER_DEVICE "/dev/binder"
#define BINDER_MMAP_SIZE (1024 * 1024)

#define SC_MAGIC 0x42534f57U
#define SC_CODE_ECHO 0x4543484fU
#define SC_CODE_PING 0x50494e47U
#define PAYLOAD_LEN 1024

#define AOSP_SM_DESCRIPTOR "android.os.IServiceManager"
#define AOSP_SM_ADD_SERVICE_TRANSACTION 3U

struct sc_text_reply {
    uint32_t magic;
    uint32_t status;
    char text[PAYLOAD_LEN];
};

static const binder_uintptr_t kLocalBinderPtr =
    (binder_uintptr_t)0x4149444c4c495445ULL; /* AIDLLITE */
static const binder_uintptr_t kLocalBinderCookie =
    (binder_uintptr_t)0x4543484f53455256ULL; /* ECHOSERV */

class AndroidLikeEchoService : public BnEchoService {
public:
    explicit AndroidLikeEchoService(const char *name)
        : name_(name ? name : "test.android.service")
    {
    }

    const char *name() const
    {
        return name_;
    }

    int echoText(const char *message, char *out, size_t out_len) override
    {
        printf("Android-like service echoText message=%s\n",
               message ? message : "");

        snprintf(out,
                 out_len,
                 "Android-like service reply from webOS sidecar");

        return 0;
    }

private:
    const char *name_;
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

static size_t align8(size_t n)
{
    return (n + 7U) & ~7U;
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
    case BR_SPAWN_LOOPER: return "BR_SPAWN_LOOPER";
    case BR_DEAD_REPLY: return "BR_DEAD_REPLY";
    case BR_FAILED_REPLY: return "BR_FAILED_REPLY";
    default: return "UNKNOWN";
    }
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
           tag,
           write_size,
           read_size);

    if (ioctl(fd, BINDER_WRITE_READ, &bwr) < 0) {
        fprintf(stderr,
                "%s: ioctl failed errno=%d (%s)\n",
                tag,
                errno,
                strerror(errno));
        return -1;
    }

    printf("%s: write_consumed=%llu read_consumed=%llu\n",
           tag,
           (unsigned long long)bwr.write_consumed,
           (unsigned long long)bwr.read_consumed);

    return (int)bwr.read_consumed;
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

static int binder_send_ptr_cookie_cmd(int fd,
                                      uint32_t cmd,
                                      binder_uintptr_t ptr,
                                      binder_uintptr_t cookie,
                                      const char *tag)
{
    uint8_t writebuf[64];
    uint8_t *p = writebuf;
    struct binder_ptr_cookie pc;

    memset(&pc, 0, sizeof(pc));
    pc.ptr = ptr;
    pc.cookie = cookie;

    append_u32(&p, cmd);
    append_bytes(&p, &pc, sizeof(pc));

    printf("%s: cmd=0x%08x ptr=0x%llx cookie=0x%llx\n",
           tag,
           cmd,
           (unsigned long long)ptr,
           (unsigned long long)cookie);

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

    printf("%s ref cmd=%s ptr=0x%llx cookie=0x%llx\n",
           who,
           cmd_name(rcmd),
           (unsigned long long)pc.ptr,
           (unsigned long long)pc.cookie);

    if (rcmd == BR_INCREFS) {
        return binder_send_ptr_cookie_cmd(fd,
                                          BC_INCREFS_DONE,
                                          pc.ptr,
                                          pc.cookie,
                                          "BC_INCREFS_DONE");
    }

    if (rcmd == BR_ACQUIRE) {
        return binder_send_ptr_cookie_cmd(fd,
                                          BC_ACQUIRE_DONE,
                                          pc.ptr,
                                          pc.cookie,
                                          "BC_ACQUIRE_DONE");
    }

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

static int aosp_add_local_service(int fd, const char *name)
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

    printf("Android-like service addService name=%s local_ptr=0x%llx cookie=0x%llx\n",
           name,
           (unsigned long long)kLocalBinderPtr,
           (unsigned long long)kLocalBinderCookie);

    if (parcel_write_i32(parcel, sizeof(parcel), &parcel_size, 0) != 0 ||
        parcel_write_string16_ascii(parcel, sizeof(parcel), &parcel_size, AOSP_SM_DESCRIPTOR) != 0 ||
        parcel_write_string16_ascii(parcel, sizeof(parcel), &parcel_size, name) != 0) {
        fprintf(stderr, "failed to build addService parcel\n");
        return -1;
    }

    parcel_size = align8(parcel_size);

    memset(parcel + parcel_size, 0, sizeof(parcel) - parcel_size);

    memset(&obj, 0, sizeof(obj));
    obj.type = BINDER_TYPE_BINDER;
    obj.flags = FLAT_BINDER_FLAG_ACCEPTS_FDS;
    obj.binder = kLocalBinderPtr;
    obj.cookie = kLocalBinderCookie;

    offsets[0] = (binder_size_t)parcel_size;
    memcpy(parcel + parcel_size, &obj, sizeof(obj));
    parcel_size += sizeof(obj);

    parcel_size = align8(parcel_size);

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
                                  "Android-like addService call");
            first = 0;
        } else {
            n = binder_write_read(fd,
                                  NULL,
                                  0,
                                  readbuf,
                                  sizeof(readbuf),
                                  "Android-like addService wait");
        }

        if (n < 0)
            return -1;

        ptr = readbuf;
        end = readbuf + n;

        while (ptr + sizeof(uint32_t) <= end) {
            uint32_t rcmd;

            memcpy(&rcmd, ptr, sizeof(rcmd));
            ptr += sizeof(rcmd);

            printf("Android-like addService got %s 0x%08x\n", cmd_name(rcmd), rcmd);

            if (rcmd == BR_REPLY) {
                struct binder_transaction_data reply;
                int32_t status = -999;

                if (ptr + sizeof(reply) > end)
                    return -1;

                memcpy(&reply, ptr, sizeof(reply));
                ptr += sizeof(reply);

                if (reply.data.ptr.buffer &&
                    reply.data_size >= sizeof(status)) {
                    memcpy(&status,
                           (void *)(uintptr_t)reply.data.ptr.buffer,
                           sizeof(status));
                }

                if (reply.data.ptr.buffer)
                    binder_free_buffer(fd, reply.data.ptr.buffer);

                printf("Android-like addService reply status=%d\n", status);
                return status == 0 ? 0 : 1;
            }

            if (rcmd == BR_NOOP || rcmd == BR_TRANSACTION_COMPLETE)
                continue;

            if (rcmd == BR_INCREFS ||
                rcmd == BR_ACQUIRE ||
                rcmd == BR_RELEASE ||
                rcmd == BR_DECREFS) {
                if (handle_ref_cmd(fd, rcmd, &ptr, end, "Android-like addService") != 0)
                    return -1;
                continue;
            }

            if (rcmd == BR_DEAD_REPLY || rcmd == BR_FAILED_REPLY) {
                fprintf(stderr, "Android-like addService failed cmd=0x%08x\n", rcmd);
                return 1;
            }

            fprintf(stderr, "Android-like addService unhandled cmd=0x%08x\n", rcmd);
            return -1;
        }
    }
}

static int send_text_reply(int fd,
                           binder_uintptr_t incoming_buffer,
                           const char *text,
                           uint32_t status,
                           const char *tag)
{
    uint8_t writebuf[2048];
    uint8_t *p = writebuf;
    uint32_t cmd;
    struct binder_transaction_data reply_tr;
    struct sc_text_reply reply;

    memset(&reply, 0, sizeof(reply));
    reply.magic = SC_MAGIC;
    reply.status = status;
    snprintf(reply.text, sizeof(reply.text), "%s", text ? text : "");

    cmd = BC_FREE_BUFFER;
    append_u32(&p, cmd);
    append_bytes(&p, &incoming_buffer, sizeof(incoming_buffer));

    cmd = BC_REPLY;
    append_u32(&p, cmd);

    memset(&reply_tr, 0, sizeof(reply_tr));
    reply_tr.data_size = sizeof(reply);
    reply_tr.offsets_size = 0;
    reply_tr.data.ptr.buffer = (binder_uintptr_t)&reply;
    reply_tr.data.ptr.offsets = 0;

    append_bytes(&p, &reply_tr, sizeof(reply_tr));

    return binder_write_read(fd, writebuf, (size_t)(p - writebuf), NULL, 0, tag) < 0 ? -1 : 0;
}

struct aidl_like_parcel_view {
    const uint8_t *data;
    size_t size;
    size_t pos;
};

static int aidl_like_read_i32(struct aidl_like_parcel_view *v, int32_t *out)
{
    if (v->pos + sizeof(*out) > v->size)
        return -1;

    memcpy(out, v->data + v->pos, sizeof(*out));
    v->pos += sizeof(*out);
    return 0;
}

static int aidl_like_read_string16_ascii(struct aidl_like_parcel_view *v,
                                         char *out,
                                         size_t out_len)
{
    int32_t len;
    size_t bytes;
    size_t padded;
    size_t i;

    if (!out || out_len == 0)
        return -1;

    out[0] = '\0';

    if (aidl_like_read_i32(v, &len) != 0)
        return -1;

    if (len < 0)
        return 0;

    bytes = ((size_t)len + 1U) * 2U;
    padded = align4(bytes);

    if (v->pos + padded > v->size)
        return -1;

    for (i = 0; i < (size_t)len && i + 1 < out_len; i++) {
        uint8_t lo = v->data[v->pos + i * 2U];
        uint8_t hi = v->data[v->pos + i * 2U + 1U];

        out[i] = hi == 0 ? (char)lo : '?';
    }

    out[i < out_len ? i : out_len - 1] = '\0';
    v->pos += padded;
    return 0;
}

static int process_transaction(int fd,
                               AndroidLikeEchoService &service,
                               struct binder_transaction_data *tr)
{
    char out[PAYLOAD_LEN];

    printf("Android-like service BR_TRANSACTION code=0x%x sender_pid=%d sender_euid=%u data_size=%llu\n",
           tr->code,
           tr->sender_pid,
           tr->sender_euid,
           (unsigned long long)tr->data_size);

    if (tr->code == SC_CODE_PING) {
        return send_text_reply(fd,
                               tr->data.ptr.buffer,
                               "PONG from Android-like service",
                               0,
                               "Android-like ping reply");
    }

    if (tr->code == ANDROID_LIKE_TRANSACTION_ECHO_TEXT) {
        if (service.handleTransaction(tr->code,
                                      (void *)(uintptr_t)tr->data.ptr.buffer,
                                      (size_t)tr->data_size,
                                      out,
                                      sizeof(out)) != 0) {
            return send_text_reply(fd,
                                   tr->data.ptr.buffer,
                                   "Android-like BnEchoService transaction failed",
                                   1,
                                   "Android-like BnEchoService error reply");
        }

        return send_text_reply(fd,
                               tr->data.ptr.buffer,
                               out,
                               0,
                               "Android-like BnEchoService reply");
    }

    if (tr->code == SC_CODE_ECHO) {
        const char *msg = (const char *)(uintptr_t)tr->data.ptr.buffer;

        if (service.echoText(msg ? msg : "", out, sizeof(out)) != 0) {
            return send_text_reply(fd,
                                   tr->data.ptr.buffer,
                                   "Android-like echo failed",
                                   1,
                                   "Android-like echo error reply");
        }

        return send_text_reply(fd,
                               tr->data.ptr.buffer,
                               out,
                               0,
                               "Android-like echo reply");
    }

    return send_text_reply(fd,
                           tr->data.ptr.buffer,
                           "Android-like unknown transaction",
                           1,
                           "Android-like unknown reply");
}

static int join_thread_pool(int fd, AndroidLikeEchoService &service)
{
    uint8_t writebuf[64];
    uint8_t readbuf[8192];
    uint8_t *p = writebuf;
    uint32_t cmd = BC_ENTER_LOOPER;
    int first = 1;

    append_u32(&p, cmd);

    printf("Android-like service enter looper\n");

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
                                  "Android-like service enter looper");
            first = 0;
        } else {
            n = binder_write_read(fd,
                                  NULL,
                                  0,
                                  readbuf,
                                  sizeof(readbuf),
                                  "Android-like service loop");
        }

        if (n < 0)
            return -1;

        ptr = readbuf;
        end = readbuf + n;

        while (ptr + sizeof(uint32_t) <= end) {
            uint32_t rcmd;

            memcpy(&rcmd, ptr, sizeof(rcmd));
            ptr += sizeof(rcmd);

            printf("Android-like service got %s 0x%08x\n", cmd_name(rcmd), rcmd);

            if (rcmd == BR_NOOP || rcmd == BR_TRANSACTION_COMPLETE || rcmd == BR_SPAWN_LOOPER)
                continue;

            if (rcmd == BR_TRANSACTION) {
                struct binder_transaction_data tr;

                if (ptr + sizeof(tr) > end)
                    return -1;

                memcpy(&tr, ptr, sizeof(tr));
                ptr += sizeof(tr);

                if (process_transaction(fd, service, &tr) != 0)
                    return -1;

                continue;
            }

            if (rcmd == BR_INCREFS ||
                rcmd == BR_ACQUIRE ||
                rcmd == BR_RELEASE ||
                rcmd == BR_DECREFS) {
                if (handle_ref_cmd(fd, rcmd, &ptr, end, "Android-like service") != 0)
                    return -1;
                continue;
            }

            if (rcmd == BR_DEAD_REPLY || rcmd == BR_FAILED_REPLY) {
                fprintf(stderr, "Android-like service failed cmd=0x%08x\n", rcmd);
                return 1;
            }

            fprintf(stderr, "Android-like service unhandled cmd=0x%08x\n", rcmd);
            return -1;
        }
    }
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    const char *name = argc >= 2 ? argv[1] : "test.android.service";
    int fd;
    AndroidLikeEchoService service(name);

    fd = binder_open_and_init();

    if (aosp_add_local_service(fd, service.name()) != 0) {
        fprintf(stderr, "Android-like service failed to register %s\n", service.name());
        return 1;
    }

    printf("ANDROID_LIKE_SERVICE_REGISTERED name=%s\n", service.name());
    printf("ANDROID_LIKE_SERVICE_OK\n");
    printf("ANDROID_LIKE_BN_SERVICE_OK\n");

    return join_thread_pool(fd, service);
}
