
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
#include <sys/types.h>
#include <unistd.h>

#include <linux/android/binder.h>

#define BINDER_DEVICE "/dev/binder"
#define BINDER_MMAP_SIZE (1024 * 1024)

#define SC_MAGIC 0x42534f57U
#define SC_CODE_ADD_SERVICE 0x53434144U
#define SC_CODE_GET_SERVICE 0x53434745U
#define SC_CODE_LIST_SERVICES 0x53434c53U
#define SC_CODE_ECHO        0x4543484fU
#define SC_CODE_PING        0x50494e47U

#define MAX_SERVICES 16
#define NAME_LEN 64
#define PAYLOAD_LEN 1024
#define SIDE_BC_REQUEST_DEATH_NOTIFICATION_RAW 0x400c630eU
#define SIDE_BR_DEAD_BINDER_RAW 0x8008720fU

struct sc_add_msg {
    uint32_t magic;
    uint32_t reserved;
    char name[NAME_LEN];
    struct flat_binder_object obj;
};

struct sc_get_msg {
    uint32_t magic;
    uint32_t reserved;
    char name[NAME_LEN];
};

struct sc_get_reply {
    uint32_t magic;
    uint32_t status;
    struct flat_binder_object obj;
};

struct sc_text_reply {
    uint32_t magic;
    uint32_t status;
    char text[PAYLOAD_LEN];
};

struct service_entry {
    int used;
    char name[NAME_LEN];
    uint32_t handle;
    binder_uintptr_t death_cookie;
};

static struct service_entry services[MAX_SERVICES];
static binder_uintptr_t death_cookie_seq = (binder_uintptr_t)0x53444300U;

static binder_uintptr_t next_death_cookie(void)
{
    death_cookie_seq++;
    if (!death_cookie_seq)
        death_cookie_seq++;
    return death_cookie_seq;
}

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

static const char *cmd_name(uint32_t cmd)
{
    if (cmd == 0x8008720fU)
        return "BR_DEAD_BINDER_RAW";

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

static const char *obj_type_name(uint32_t type)
{
    switch (type) {
    case BINDER_TYPE_BINDER: return "BINDER_TYPE_BINDER";
    case BINDER_TYPE_WEAK_BINDER: return "BINDER_TYPE_WEAK_BINDER";
    case BINDER_TYPE_HANDLE: return "BINDER_TYPE_HANDLE";
    case BINDER_TYPE_WEAK_HANDLE: return "BINDER_TYPE_WEAK_HANDLE";
    case BINDER_TYPE_FD: return "BINDER_TYPE_FD";
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
    struct binder_write_read bwr;
    uint32_t cmd = BC_FREE_BUFFER;

    append_u32(&p, cmd);
    append_bytes(&p, &buffer, sizeof(buffer));

    memset(&bwr, 0, sizeof(bwr));
    bwr.write_size = (size_t)(p - writebuf);
    bwr.write_buffer = (binder_uintptr_t)writebuf;
    bwr.read_size = 0;
    bwr.read_buffer = 0;

    printf("free buffer 0x%" PRIx64 " write-only\n", (uint64_t)buffer);

    if (ioctl(fd, BINDER_WRITE_READ, &bwr) < 0) {
        fprintf(stderr, "free_buffer: ioctl failed errno=%d (%s)\n",
                errno, strerror(errno));
        return -1;
    }

    printf("free_buffer: write_consumed=%" PRIu64 " read_consumed=%" PRIu64 "\n",
           (uint64_t)bwr.write_consumed,
           (uint64_t)bwr.read_consumed);

    return 0;
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

    printf("%s: cmd=0x%08x ptr=0x%" PRIx64 " cookie=0x%" PRIx64 "\n",
           tag, cmd, (uint64_t)ptr, (uint64_t)cookie);

    return binder_write_read(fd, writebuf, (size_t)(p - writebuf),
                             NULL, 0, tag) < 0 ? -1 : 0;
}


static int binder_send_handle_cmd(int fd, uint32_t cmd, uint32_t handle, const char *tag)
{
    uint8_t writebuf[64];
    uint8_t *p = writebuf;

    append_u32(&p, cmd);
    append_u32(&p, handle);

    printf("%s: cmd=0x%08x handle=%u\n", tag, cmd, handle);

    return binder_write_read(fd, writebuf, (size_t)(p - writebuf),
                             NULL, 0, tag) < 0 ? -1 : 0;
}


static int binder_send_handle_cookie_cmd(int fd,
                                         uint32_t cmd,
                                         uint32_t handle,
                                         binder_uintptr_t cookie,
                                         const char *tag)
{
    uint8_t writebuf[64];
    uint8_t *p = writebuf;

    /*
     * LG/webOS Binder 4.4 consumes this command as:
     *
     *   uint32_t cmd;
     *   uint32_t handle;
     *   binder_uintptr_t cookie;
     *
     * i.e. 4 + 4 + 8 = 16 bytes total on arm64.
     *
     * Do not append struct binder_handle_cookie directly here: on arm64 that
     * struct can include padding after the 32-bit handle, producing a 20-byte
     * write including the command. The driver then consumes only 16 bytes and
     * the trailing padding desynchronizes the command stream / fails with
     * EINVAL on this target.
     */
    append_u32(&p, cmd);
    append_u32(&p, handle);
    append_bytes(&p, &cookie, sizeof(cookie));

    printf("%s: cmd=0x%08x handle=%u cookie=0x%" PRIx64 "\n",
           tag, cmd, handle, (uint64_t)cookie);

    return binder_write_read(fd, writebuf, (size_t)(p - writebuf),
                             NULL, 0, tag) < 0 ? -1 : 0;
}

static int binder_send_dead_binder_done(int fd, binder_uintptr_t cookie, const char *tag)
{
    uint8_t writebuf[64];
    uint8_t *p = writebuf;
    uint32_t cmd = BC_DEAD_BINDER_DONE;

    append_u32(&p, cmd);
    append_bytes(&p, &cookie, sizeof(cookie));

    printf("%s: cmd=0x%08x cookie=0x%" PRIx64 "\n",
           tag, cmd, (uint64_t)cookie);

    return binder_write_read(fd, writebuf, (size_t)(p - writebuf),
                             NULL, 0, tag) < 0 ? -1 : 0;
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

    if (rcmd == BR_INCREFS) {
        printf("%s BR_INCREFS ptr=0x%" PRIx64 " cookie=0x%" PRIx64 "\n",
               who, (uint64_t)pc.ptr, (uint64_t)pc.cookie);
        return binder_send_ptr_cookie_cmd(fd, BC_INCREFS_DONE, pc.ptr, pc.cookie, "BC_INCREFS_DONE");
    }

    if (rcmd == BR_ACQUIRE) {
        printf("%s BR_ACQUIRE ptr=0x%" PRIx64 " cookie=0x%" PRIx64 "\n",
               who, (uint64_t)pc.ptr, (uint64_t)pc.cookie);
        return binder_send_ptr_cookie_cmd(fd, BC_ACQUIRE_DONE, pc.ptr, pc.cookie, "BC_ACQUIRE_DONE");
    }

    if (rcmd == BR_RELEASE) {
        printf("%s BR_RELEASE ptr=0x%" PRIx64 " cookie=0x%" PRIx64 "\n",
               who, (uint64_t)pc.ptr, (uint64_t)pc.cookie);
        return 0;
    }

    if (rcmd == BR_DECREFS) {
        printf("%s BR_DECREFS ptr=0x%" PRIx64 " cookie=0x%" PRIx64 "\n",
               who, (uint64_t)pc.ptr, (uint64_t)pc.cookie);
        return 0;
    }

    return 0;
}

static int send_text_reply(int fd,
                           binder_uintptr_t incoming_buffer,
                           const char *text,
                           uint32_t status,
                           const char *tag)
{
    uint8_t writebuf[1024];
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

    return binder_write_read(fd, writebuf, (size_t)(p - writebuf),
                             NULL, 0, tag) < 0 ? -1 : 0;
}

static int send_get_reply(int fd,
                          binder_uintptr_t incoming_buffer,
                          uint32_t status,
                          uint32_t handle)
{
    uint8_t writebuf[1024];
    uint8_t *p = writebuf;
    binder_size_t offsets[1];
    uint32_t cmd;
    struct binder_transaction_data reply_tr;
    struct sc_get_reply reply;

    memset(&reply, 0, sizeof(reply));
    reply.magic = SC_MAGIC;
    reply.status = status;
    reply.obj.type = BINDER_TYPE_HANDLE;
    reply.obj.flags = FLAT_BINDER_FLAG_ACCEPTS_FDS;
    reply.obj.handle = handle;
    reply.obj.cookie = 0;

    offsets[0] = (binder_size_t)((uint8_t *)&reply.obj - (uint8_t *)&reply);

    cmd = BC_FREE_BUFFER;
    append_u32(&p, cmd);
    append_bytes(&p, &incoming_buffer, sizeof(incoming_buffer));

    cmd = BC_REPLY;
    append_u32(&p, cmd);

    memset(&reply_tr, 0, sizeof(reply_tr));
    reply_tr.data_size = sizeof(reply);
    reply_tr.offsets_size = sizeof(offsets);
    reply_tr.data.ptr.buffer = (binder_uintptr_t)&reply;
    reply_tr.data.ptr.offsets = (binder_uintptr_t)offsets;

    append_bytes(&p, &reply_tr, sizeof(reply_tr));

    printf("sm-server: replying with handle=%u status=%u\n", handle, status);

    return binder_write_read(fd, writebuf, (size_t)(p - writebuf),
                             NULL, 0, "sm-server getService reply") < 0 ? -1 : 0;
}

static int registry_add(const char *name, uint32_t handle)
{
    int i;

    for (i = 0; i < MAX_SERVICES; i++) {
        if (services[i].used && strcmp(services[i].name, name) == 0) {
            services[i].handle = handle;
            return 0;
        }
    }

    for (i = 0; i < MAX_SERVICES; i++) {
        if (!services[i].used) {
            services[i].used = 1;
            snprintf(services[i].name, sizeof(services[i].name), "%s", name);
            services[i].handle = handle;
            return 0;
        }
    }

    return -1;
}


static struct service_entry *registry_find_by_name(const char *name)
{
    int i;

    for (i = 0; i < MAX_SERVICES; i++) {
        if (services[i].used && strcmp(services[i].name, name) == 0)
            return &services[i];
    }

    return NULL;
}

static int registry_remove_by_death_cookie(binder_uintptr_t cookie)
{
    int i;

    for (i = 0; i < MAX_SERVICES; i++) {
        if (services[i].used && services[i].death_cookie == cookie) {
            printf("sm-server: service died name=%s handle=%u cookie=0x%" PRIx64 "\n",
                   services[i].name,
                   services[i].handle,
                   (uint64_t)cookie);
            memset(&services[i], 0, sizeof(services[i]));
            return 0;
        }
    }

    printf("sm-server: death cookie not found cookie=0x%" PRIx64 "\n",
           (uint64_t)cookie);
    return -1;
}

static int handle_dead_or_clear_cmd(int fd,
                                    uint32_t rcmd,
                                    uint8_t **ptr,
                                    uint8_t *end,
                                    const char *who)
{
    binder_uintptr_t cookie;

    if (*ptr + sizeof(cookie) > end) {
        fprintf(stderr, "%s: truncated death/clear cmd=0x%08x\n", who, rcmd);
        return -1;
    }

    memcpy(&cookie, *ptr, sizeof(cookie));
    *ptr += sizeof(cookie);

    printf("%s death/clear cmd=0x%08x cookie=0x%" PRIx64 "\n",
           who, rcmd, (uint64_t)cookie);

    /*
     * On the tested LG/webOS Binder 4.4 target, the alternate
     * BC_REQUEST_DEATH_NOTIFICATION encoding 0x400c630e is accepted, but the
     * return observed after service death is raw BR_DEAD_BINDER 0x8008720f.
     * Treat it as a death notification, remove the service entry, and ack it.
     */
    registry_remove_by_death_cookie(cookie);

    if (rcmd == BR_DEAD_BINDER || rcmd == SIDE_BR_DEAD_BINDER_RAW || rcmd == 0x8008720fU)
        return binder_send_dead_binder_done(fd, cookie, "BC_DEAD_BINDER_DONE");

    /*
     * BR_CLEAR_DEATH_NOTIFICATION_DONE-like returns already clear/free their
     * death work in the driver. Do not send BC_DEAD_BINDER_DONE for them.
     */
    return 0;
}

static uint32_t registry_get(const char *name)
{
    int i;

    for (i = 0; i < MAX_SERVICES; i++) {
        if (services[i].used && strcmp(services[i].name, name) == 0)
            return services[i].handle;
    }

    return 0;
}

static void registry_dump(void)
{
    int i;

    printf("sm-server registry:\n");
    for (i = 0; i < MAX_SERVICES; i++) {
        if (services[i].used)
            printf("  %s -> handle=%u\n", services[i].name, services[i].handle);
    }
}

static void registry_list_text(char *out, size_t out_len)
{
    int i;
    size_t used = 0;

    if (!out || out_len == 0)
        return;

    out[0] = '\0';

    for (i = 0; i < MAX_SERVICES; i++) {
        int n;

        if (!services[i].used)
            continue;

        n = snprintf(out + used, out_len - used, "%s\n", services[i].name);
        if (n < 0)
            break;

        if ((size_t)n >= out_len - used) {
            used = out_len - 1;
            out[used] = '\0';
            break;
        }

        used += (size_t)n;
    }

    if (out[0] == '\0')
        snprintf(out, out_len, "(empty)\n");
}
static uint32_t first_handle_from_transaction(struct binder_transaction_data *tr)
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

    printf("object from txn: offset=%" PRIu64 " type=0x%08x %s handle=%u binder=0x%" PRIx64 " cookie=0x%" PRIx64 "\n",
           (uint64_t)off,
           obj->type,
           obj_type_name(obj->type),
           obj->handle,
           (uint64_t)obj->binder,
           (uint64_t)obj->cookie);

    if (obj->type != BINDER_TYPE_HANDLE)
        return 0;

    return obj->handle;
}


static int sm_ping_service_handle(int fd, uint32_t handle, const char *name)
{
    const char payload[] = "PING from mini_servicemgr";
    uint8_t writebuf[1024];
    uint8_t readbuf[8192];
    uint8_t *p = writebuf;
    uint32_t cmd;
    struct binder_transaction_data tr;
    int first = 1;

    printf("sm-server: ping service name=%s handle=%u\n", name, handle);

    memset(&tr, 0, sizeof(tr));
    tr.target.handle = handle;
    tr.code = SC_CODE_PING;
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

        if (first) {
            n = binder_write_read(fd, writebuf, (size_t)(p - writebuf),
                                  readbuf, sizeof(readbuf), "sm-server ping service call");
            first = 0;
        } else {
            n = binder_write_read(fd, NULL, 0,
                                  readbuf, sizeof(readbuf), "sm-server ping service wait");
        }

        if (n < 0)
            return -1;

        uint8_t *ptr = readbuf;
        uint8_t *end = readbuf + n;

        while (ptr + sizeof(uint32_t) <= end) {
            uint32_t rcmd;
            memcpy(&rcmd, ptr, sizeof(rcmd));
            ptr += sizeof(rcmd);

            printf("sm-server ping got %s 0x%08x\n", cmd_name(rcmd), rcmd);

            if (rcmd == BR_REPLY) {
                struct binder_transaction_data reply;

                if (ptr + sizeof(reply) > end)
                    return -1;

                memcpy(&reply, ptr, sizeof(reply));
                ptr += sizeof(reply);

                if (reply.data.ptr.buffer && reply.data_size >= sizeof(struct sc_text_reply)) {
                    struct sc_text_reply *txt = (struct sc_text_reply *)(uintptr_t)reply.data.ptr.buffer;
                    printf("sm-server: ping reply status=%u text=%s\n", txt->status, txt->text);
                    binder_free_buffer(fd, reply.data.ptr.buffer);
                    return txt->status == 0 ? 0 : 1;
                }

                if (reply.data.ptr.buffer)
                    binder_free_buffer(fd, reply.data.ptr.buffer);

                return 0;
            }

            if (rcmd == BR_DEAD_REPLY || rcmd == BR_FAILED_REPLY) {
                printf("sm-server: ping service dead/failed cmd=0x%08x\n", rcmd);
                return 1;
            }

            if (rcmd == BR_NOOP || rcmd == BR_TRANSACTION_COMPLETE)
                continue;

            if (rcmd == BR_DEAD_BINDER || rcmd == BR_CLEAR_DEATH_NOTIFICATION_DONE || rcmd == 0x8008720fU) {
                if (handle_dead_or_clear_cmd(fd, rcmd, &ptr, end, "sm-server") != 0)
                    return -1;
                continue;
            }

            if (rcmd == BR_INCREFS || rcmd == BR_ACQUIRE ||
                rcmd == BR_RELEASE || rcmd == BR_DECREFS) {
                if (handle_ref_cmd(fd, rcmd, &ptr, end, "sm-server ping") != 0)
                    return -1;
                continue;
            }

            printf("sm-server ping unhandled cmd=0x%08x\n", rcmd);
            return -1;
        }
    }
}

static int process_sm_transaction(int fd, struct binder_transaction_data *tr)
{
    printf("sm-server BR_TRANSACTION code=0x%x sender_pid=%d sender_euid=%u data_size=%" PRIu64 " offsets_size=%" PRIu64 "\n",
           tr->code,
           tr->sender_pid,
           tr->sender_euid,
           (uint64_t)tr->data_size,
           (uint64_t)tr->offsets_size);

    if (tr->code == SC_CODE_ADD_SERVICE) {
        struct sc_add_msg *msg;
        uint32_t handle;

        if (tr->data_size < sizeof(*msg)) {
            fprintf(stderr, "sm-server: addService bad data_size\n");
            return send_text_reply(fd, tr->data.ptr.buffer, "BAD ADD", 1, "sm-server add bad reply");
        }

        msg = (struct sc_add_msg *)(uintptr_t)tr->data.ptr.buffer;
        if (msg->magic != SC_MAGIC) {
            fprintf(stderr, "sm-server: addService bad magic\n");
            return send_text_reply(fd, tr->data.ptr.buffer, "BAD MAGIC", 1, "sm-server add magic reply");
        }

        handle = first_handle_from_transaction(tr);
        if (!handle) {
            fprintf(stderr, "sm-server: addService missing binder handle\n");
            return send_text_reply(fd, tr->data.ptr.buffer, "NO HANDLE", 1, "sm-server add nohandle reply");
        }

        if (registry_add(msg->name, handle) != 0) {
            fprintf(stderr, "sm-server: registry full\n");
            return send_text_reply(fd, tr->data.ptr.buffer, "REGISTRY FULL", 1, "sm-server add full reply");
        }

        /*
         * Keep a strong reference to the service handle before freeing the
         * incoming transaction buffer. Without this, returning the stored
         * handle later to a third process can fail with BR_FAILED_REPLY.
         */
        if (binder_send_handle_cmd(fd, BC_ACQUIRE, handle, "sm-server BC_ACQUIRE service handle") != 0)
            return send_text_reply(fd, tr->data.ptr.buffer, "ACQUIRE FAILED", 1, "sm-server add acquire-failed reply");

        {
            struct service_entry *entry = registry_find_by_name(msg->name);

            if (!entry)
                return send_text_reply(fd, tr->data.ptr.buffer, "REGISTRY LOST", 1, "sm-server add registry-lost reply");

            entry->death_cookie = next_death_cookie();

            if (binder_send_handle_cookie_cmd(fd,
                                          SIDE_BC_REQUEST_DEATH_NOTIFICATION_RAW,
                                          handle,
                                          entry->death_cookie,
                                          "BC_REQUEST_DEATH_NOTIFICATION_RAW service handle") != 0) {
                /*
                 * Keep addService usable even if this experimental kernel
                 * rejects death notifications. The death smoke test will make
                 * this visible in the logs.
                 */
                printf("sm-server: death notification request failed; continuing without death cleanup\n");
                entry->death_cookie = 0;
            }
        }


        printf("sm-server: addService name=%s handle=%u\n", msg->name, handle);
        registry_dump();

        return send_text_reply(fd, tr->data.ptr.buffer, "ADD OK", 0, "sm-server addService reply");
    }

    
    if (tr->code == SC_CODE_LIST_SERVICES) {
        char list[PAYLOAD_LEN];

        registry_list_text(list, sizeof(list));
        printf("sm-server: listServices\n%s", list);

        return send_text_reply(fd,
                               tr->data.ptr.buffer,
                               list,
                               0,
                               "sm-server listServices reply");
    }
if (tr->code == SC_CODE_GET_SERVICE) {
        struct sc_get_msg *msg;
        uint32_t handle;

        if (tr->data_size < sizeof(*msg)) {
            fprintf(stderr, "sm-server: getService bad data_size\n");
            return send_text_reply(fd, tr->data.ptr.buffer, "BAD GET", 1, "sm-server get bad reply");
        }

        msg = (struct sc_get_msg *)(uintptr_t)tr->data.ptr.buffer;
        if (msg->magic != SC_MAGIC) {
            fprintf(stderr, "sm-server: getService bad magic\n");
            return send_text_reply(fd, tr->data.ptr.buffer, "BAD MAGIC", 1, "sm-server get magic reply");
        }

        handle = registry_get(msg->name);
        printf("sm-server: getService name=%s handle=%u\n", msg->name, handle);

        if (!handle)
            return send_text_reply(fd, tr->data.ptr.buffer, "NOT FOUND", 1, "sm-server get notfound reply");

        /*
         * LG/webOS Binder currently rejects BC_REQUEST_DEATH_NOTIFICATION
         * with EINVAL, so use lazy cleanup: verify the stored handle before
         * returning it to a client. If the service is already dead, remove it
         * from the registry and return NOT FOUND instead of a stale handle.
         */
        if (sm_ping_service_handle(fd, handle, msg->name) != 0) {
            struct service_entry *entry = registry_find_by_name(msg->name);

            if (entry) {
                printf("sm-server: lazy cleanup removing stale service name=%s handle=%u\n",
                       entry->name, entry->handle);
                memset(entry, 0, sizeof(*entry));
            }

            return send_text_reply(fd, tr->data.ptr.buffer, "NOT FOUND", 1, "sm-server get stale-notfound reply");
        }

        return send_get_reply(fd, tr->data.ptr.buffer, 0, handle);
    }

    return send_text_reply(fd, tr->data.ptr.buffer, "UNKNOWN CODE", 1, "sm-server unknown reply");
}

static int run_sm_server(void)
{
    int fd = binder_open_and_init();

    printf("sm-server set context manager\n");
    if (ioctl(fd, BINDER_SET_CONTEXT_MGR, 0) < 0)
        die("BINDER_SET_CONTEXT_MGR");

    {
        uint8_t writebuf[64];
        uint8_t readbuf[8192];
        uint8_t *p = writebuf;
        uint32_t cmd = BC_ENTER_LOOPER;
        int n;

        append_u32(&p, cmd);
        n = binder_write_read(fd, writebuf, (size_t)(p - writebuf),
                              readbuf, sizeof(readbuf), "sm-server enter looper");
        if (n < 0)
            return 1;

        if (n > 0) {
            uint8_t *ptr = readbuf;
            uint8_t *end = readbuf + n;

            while (ptr + sizeof(uint32_t) <= end) {
                uint32_t rcmd;
                memcpy(&rcmd, ptr, sizeof(rcmd));
                ptr += sizeof(rcmd);

                printf("sm-server got %s 0x%08x\n", cmd_name(rcmd), rcmd);

                if (rcmd == BR_TRANSACTION) {
                    struct binder_transaction_data tr;
                    if (ptr + sizeof(tr) > end)
                        return 1;
                    memcpy(&tr, ptr, sizeof(tr));
                    ptr += sizeof(tr);
                    if (process_sm_transaction(fd, &tr) != 0)
                        return 1;
                } else if (rcmd == BR_NOOP || rcmd == BR_SPAWN_LOOPER ||
                           rcmd == BR_TRANSACTION_COMPLETE) {
                    continue;
                } else if (rcmd == BR_DEAD_BINDER || rcmd == BR_CLEAR_DEATH_NOTIFICATION_DONE || rcmd == 0x8008720fU) {
                    if (handle_dead_or_clear_cmd(fd, rcmd, &ptr, end, "sm-server") != 0)
                        return 1;
                } else if (rcmd == BR_INCREFS || rcmd == BR_ACQUIRE ||
                           rcmd == BR_RELEASE || rcmd == BR_DECREFS) {
                    if (handle_ref_cmd(fd, rcmd, &ptr, end, "sm-server") != 0)
                        return 1;
                } else if (rcmd == BR_DEAD_BINDER ||
                           rcmd == SIDE_BR_DEAD_BINDER_RAW ||
                           rcmd == 0x8008720fU) {
                    if (handle_dead_or_clear_cmd(fd, rcmd, &ptr, end, "sm-server") != 0)
                        return 1;
                } else {
                    printf("sm-server unhandled cmd=0x%08x\n", rcmd);
                    return 1;
                }
            }
        }
    }

    printf("sm-server ready\n");

    for (;;) {
        uint8_t readbuf[8192];
        int n = binder_write_read(fd, NULL, 0, readbuf, sizeof(readbuf), "sm-server loop");

        if (n < 0)
            return 1;

        uint8_t *ptr = readbuf;
        uint8_t *end = readbuf + n;

        while (ptr + sizeof(uint32_t) <= end) {
            uint32_t rcmd;
            memcpy(&rcmd, ptr, sizeof(rcmd));
            ptr += sizeof(rcmd);

            printf("sm-server got %s 0x%08x\n", cmd_name(rcmd), rcmd);

            if (rcmd == BR_TRANSACTION) {
                struct binder_transaction_data tr;
                if (ptr + sizeof(tr) > end)
                    return 1;
                memcpy(&tr, ptr, sizeof(tr));
                ptr += sizeof(tr);
                if (process_sm_transaction(fd, &tr) != 0)
                    return 1;
            } else if (rcmd == BR_NOOP || rcmd == BR_SPAWN_LOOPER ||
                       rcmd == BR_TRANSACTION_COMPLETE) {
                continue;
            } else if (rcmd == BR_INCREFS || rcmd == BR_ACQUIRE ||
                       rcmd == BR_RELEASE || rcmd == BR_DECREFS) {
                if (handle_ref_cmd(fd, rcmd, &ptr, end, "sm-server") != 0)
                    return 1;
            } else if (rcmd == BR_DEAD_BINDER ||
                           rcmd == SIDE_BR_DEAD_BINDER_RAW ||
                           rcmd == 0x8008720fU) {
                    if (handle_dead_or_clear_cmd(fd, rcmd, &ptr, end, "sm-server") != 0)
                        return 1;
                } else {
                    printf("sm-server unhandled cmd=0x%08x\n", rcmd);
                    return 1;
                }
        }
    }

    return 0;
}

static int wait_for_reply_text(int fd, const char *who)
{
    for (;;) {
        uint8_t readbuf[8192];
        int n = binder_write_read(fd, NULL, 0, readbuf, sizeof(readbuf), who);

        if (n < 0)
            return -1;

        uint8_t *ptr = readbuf;
        uint8_t *end = readbuf + n;

        while (ptr + sizeof(uint32_t) <= end) {
            uint32_t rcmd;
            memcpy(&rcmd, ptr, sizeof(rcmd));
            ptr += sizeof(rcmd);

            printf("%s got %s 0x%08x\n", who, cmd_name(rcmd), rcmd);

            if (rcmd == BR_REPLY) {
                struct binder_transaction_data reply;
                if (ptr + sizeof(reply) > end)
                    return -1;
                memcpy(&reply, ptr, sizeof(reply));
                ptr += sizeof(reply);

                if (reply.data.ptr.buffer && reply.data_size >= sizeof(struct sc_text_reply)) {
                    struct sc_text_reply *txt = (struct sc_text_reply *)(uintptr_t)reply.data.ptr.buffer;
                    printf("%s reply status=%u text=%s\n", who, txt->status, txt->text);
                    binder_free_buffer(fd, reply.data.ptr.buffer);
                    return txt->status == 0 ? 0 : 1;
                }

                if (reply.data.ptr.buffer)
                    binder_free_buffer(fd, reply.data.ptr.buffer);
                return -1;
            } else if (rcmd == BR_NOOP || rcmd == BR_TRANSACTION_COMPLETE) {
                continue;
            } else if (rcmd == BR_INCREFS || rcmd == BR_ACQUIRE ||
                       rcmd == BR_RELEASE || rcmd == BR_DECREFS) {
                if (handle_ref_cmd(fd, rcmd, &ptr, end, who) != 0)
                    return -1;
            } else {
                printf("%s unhandled while waiting text reply cmd=0x%08x\n", who, rcmd);
                return -1;
            }
        }
    }
}

static int get_service_handle(int fd, const char *name, uint32_t *out_handle)
{
    uint8_t writebuf[1024];
    uint8_t readbuf[8192];
    uint8_t *p = writebuf;
    uint32_t cmd;
    struct binder_transaction_data tr;
    struct sc_get_msg msg;
    int first = 1;

    memset(&msg, 0, sizeof(msg));
    msg.magic = SC_MAGIC;
    snprintf(msg.name, sizeof(msg.name), "%s", name);

    memset(&tr, 0, sizeof(tr));
    tr.target.handle = 0;
    tr.code = SC_CODE_GET_SERVICE;
    tr.flags = TF_ACCEPT_FDS;
    tr.data_size = sizeof(msg);
    tr.offsets_size = 0;
    tr.data.ptr.buffer = (binder_uintptr_t)&msg;
    tr.data.ptr.offsets = 0;

    cmd = BC_TRANSACTION;
    append_u32(&p, cmd);
    append_bytes(&p, &tr, sizeof(tr));

    for (;;) {
        int n;

        if (first) {
            n = binder_write_read(fd, writebuf, (size_t)(p - writebuf),
                                  readbuf, sizeof(readbuf), "getService call");
            first = 0;
        } else {
            n = binder_write_read(fd, NULL, 0,
                                  readbuf, sizeof(readbuf), "getService wait");
        }

        if (n < 0)
            return -1;

        uint8_t *ptr = readbuf;
        uint8_t *end = readbuf + n;

        while (ptr + sizeof(uint32_t) <= end) {
            uint32_t rcmd;
            memcpy(&rcmd, ptr, sizeof(rcmd));
            ptr += sizeof(rcmd);

            printf("getService got %s 0x%08x\n", cmd_name(rcmd), rcmd);

            if (rcmd == BR_REPLY) {
                struct binder_transaction_data reply;
                uint32_t handle;

                if (ptr + sizeof(reply) > end)
                    return -1;
                memcpy(&reply, ptr, sizeof(reply));
                ptr += sizeof(reply);

                if (reply.offsets_size > 0) {
                    handle = first_handle_from_transaction(&reply);
                    if (!handle) {
                        fprintf(stderr, "getService: no handle in reply\n");
                        if (reply.data.ptr.buffer)
                            binder_free_buffer(fd, reply.data.ptr.buffer);
                        return -1;
                    }

                    printf("getService: name=%s got handle=%u\n", name, handle);

                    /*
                     * Keep a strong reference to the returned service handle
                     * before freeing the reply buffer. Otherwise the local
                     * handle can become unusable and the subsequent call can
                     * fail with BR_FAILED_REPLY.
                     */
                    if (binder_send_handle_cmd(fd, BC_ACQUIRE, handle, "getService BC_ACQUIRE returned handle") != 0) {
                        binder_free_buffer(fd, reply.data.ptr.buffer);
                        return -1;
                    }

                    *out_handle = handle;
                    binder_free_buffer(fd, reply.data.ptr.buffer);
                    return 0;
                }

                if (reply.data.ptr.buffer && reply.data_size >= sizeof(struct sc_text_reply)) {
                    struct sc_text_reply *txt = (struct sc_text_reply *)(uintptr_t)reply.data.ptr.buffer;
                    printf("getService text reply status=%u text=%s\n", txt->status, txt->text);
                    binder_free_buffer(fd, reply.data.ptr.buffer);
                }

                return -1;
            } else if (rcmd == BR_DEAD_REPLY || rcmd == BR_FAILED_REPLY) {
                fprintf(stderr, "getService got failure reply cmd=0x%08x\n", rcmd);
                return -1;
            } else if (rcmd == BR_NOOP || rcmd == BR_TRANSACTION_COMPLETE) {
                continue;
            } else if (rcmd == BR_INCREFS || rcmd == BR_ACQUIRE ||
                       rcmd == BR_RELEASE || rcmd == BR_DECREFS) {
                if (handle_ref_cmd(fd, rcmd, &ptr, end, "getService") != 0)
                    return -1;
            } else {
                printf("getService unhandled cmd=0x%08x\n", rcmd);
            }
        }
    }
}

static int run_list_services_client(void)
{
    int fd = binder_open_and_init();
    uint8_t writebuf[1024];
    uint8_t readbuf[8192];
    uint8_t *p = writebuf;
    uint32_t cmd;
    uint32_t magic = SC_MAGIC;
    struct binder_transaction_data tr;
    int first = 1;

    memset(&tr, 0, sizeof(tr));
    tr.target.handle = 0;
    tr.code = SC_CODE_LIST_SERVICES;
    tr.flags = TF_ACCEPT_FDS;
    tr.data_size = sizeof(magic);
    tr.offsets_size = 0;
    tr.data.ptr.buffer = (binder_uintptr_t)&magic;
    tr.data.ptr.offsets = 0;

    cmd = BC_TRANSACTION;
    append_u32(&p, cmd);
    append_bytes(&p, &tr, sizeof(tr));

    for (;;) {
        int n;

        if (first) {
            n = binder_write_read(fd,
                                  writebuf,
                                  (size_t)(p - writebuf),
                                  readbuf,
                                  sizeof(readbuf),
                                  "listServices call");
            first = 0;
        } else {
            n = binder_write_read(fd,
                                  NULL,
                                  0,
                                  readbuf,
                                  sizeof(readbuf),
                                  "listServices wait");
        }

        if (n < 0)
            return 1;

        uint8_t *ptr = readbuf;
        uint8_t *end = readbuf + n;

        while (ptr + sizeof(uint32_t) <= end) {
            uint32_t rcmd;

            memcpy(&rcmd, ptr, sizeof(rcmd));
            ptr += sizeof(rcmd);

            printf("listServices got %s 0x%08x\n", cmd_name(rcmd), rcmd);

            if (rcmd == BR_REPLY) {
                struct binder_transaction_data reply;

                if (ptr + sizeof(reply) > end)
                    return 1;

                memcpy(&reply, ptr, sizeof(reply));
                ptr += sizeof(reply);

                if (reply.data.ptr.buffer &&
                    reply.data_size >= sizeof(struct sc_text_reply)) {
                    struct sc_text_reply *txt =
                        (struct sc_text_reply *)(uintptr_t)reply.data.ptr.buffer;

                    printf("list-services reply status=%u\n", txt->status);
                    printf("%s", txt->text);

                    binder_free_buffer(fd, reply.data.ptr.buffer);
                    return txt->status == 0 ? 0 : 1;
                }

                if (reply.data.ptr.buffer)
                    binder_free_buffer(fd, reply.data.ptr.buffer);

                return 1;
            }

            if (rcmd == BR_DEAD_REPLY || rcmd == BR_FAILED_REPLY) {
                fprintf(stderr, "listServices got failure reply cmd=0x%08x\n", rcmd);
                return 1;
            }

            if (rcmd == BR_NOOP || rcmd == BR_TRANSACTION_COMPLETE)
                continue;

            if (rcmd == BR_INCREFS ||
                rcmd == BR_ACQUIRE ||
                rcmd == BR_RELEASE ||
                rcmd == BR_DECREFS) {
                if (handle_ref_cmd(fd, rcmd, &ptr, end, "listServices") != 0)
                    return 1;
                continue;
            }

            printf("listServices unhandled cmd=0x%08x\n", rcmd);
        }
    }
}
static int add_service(int fd, const char *name)
{
    uint8_t writebuf[1024];
    uint8_t readbuf[8192];
    uint8_t *p = writebuf;
    uint32_t cmd;
    struct binder_transaction_data tr;
    struct sc_add_msg msg;
    binder_size_t offsets[1];
    int first = 1;

    memset(&msg, 0, sizeof(msg));
    msg.magic = SC_MAGIC;
    snprintf(msg.name, sizeof(msg.name), "%s", name);
    msg.obj.type = BINDER_TYPE_BINDER;
    msg.obj.flags = FLAT_BINDER_FLAG_ACCEPTS_FDS;
    msg.obj.binder = (binder_uintptr_t)&msg.obj;
    msg.obj.cookie = (binder_uintptr_t)0x4543484f53455256ULL;

    offsets[0] = (binder_size_t)((uint8_t *)&msg.obj - (uint8_t *)&msg);

    memset(&tr, 0, sizeof(tr));
    tr.target.handle = 0;
    tr.code = SC_CODE_ADD_SERVICE;
    tr.flags = TF_ACCEPT_FDS;
    tr.data_size = sizeof(msg);
    tr.offsets_size = sizeof(offsets);
    tr.data.ptr.buffer = (binder_uintptr_t)&msg;
    tr.data.ptr.offsets = (binder_uintptr_t)offsets;

    printf("echo-service: addService name=%s binder=0x%" PRIx64 " cookie=0x%" PRIx64 " offset=%" PRIu64 "\n",
           name,
           (uint64_t)msg.obj.binder,
           (uint64_t)msg.obj.cookie,
           (uint64_t)offsets[0]);

    cmd = BC_TRANSACTION;
    append_u32(&p, cmd);
    append_bytes(&p, &tr, sizeof(tr));

    for (;;) {
        int n;

        if (first) {
            n = binder_write_read(fd, writebuf, (size_t)(p - writebuf),
                                  readbuf, sizeof(readbuf), "addService call");
            first = 0;
        } else {
            n = binder_write_read(fd, NULL, 0,
                                  readbuf, sizeof(readbuf), "addService wait");
        }

        if (n < 0)
            return -1;

        uint8_t *ptr = readbuf;
        uint8_t *end = readbuf + n;

        while (ptr + sizeof(uint32_t) <= end) {
            uint32_t rcmd;
            memcpy(&rcmd, ptr, sizeof(rcmd));
            ptr += sizeof(rcmd);

            printf("addService got %s 0x%08x\n", cmd_name(rcmd), rcmd);

            if (rcmd == BR_REPLY) {
                struct binder_transaction_data reply;

                if (ptr + sizeof(reply) > end)
                    return -1;
                memcpy(&reply, ptr, sizeof(reply));
                ptr += sizeof(reply);

                if (reply.data.ptr.buffer && reply.data_size >= sizeof(struct sc_text_reply)) {
                    struct sc_text_reply *txt = (struct sc_text_reply *)(uintptr_t)reply.data.ptr.buffer;
                    printf("addService reply status=%u text=%s\n", txt->status, txt->text);
                    binder_free_buffer(fd, reply.data.ptr.buffer);
                    return txt->status == 0 ? 0 : -1;
                }

                if (reply.data.ptr.buffer)
                    binder_free_buffer(fd, reply.data.ptr.buffer);
                return -1;
            } else if (rcmd == BR_NOOP || rcmd == BR_TRANSACTION_COMPLETE) {
                continue;
            } else if (rcmd == BR_INCREFS || rcmd == BR_ACQUIRE ||
                       rcmd == BR_RELEASE || rcmd == BR_DECREFS) {
                if (handle_ref_cmd(fd, rcmd, &ptr, end, "echo-service addService") != 0)
                    return -1;
            } else {
                printf("addService unhandled cmd=0x%08x\n", rcmd);
            }
        }
    }
}

static int echo_service_process_transaction(int fd, struct binder_transaction_data *tr)
{
    char reply_text[PAYLOAD_LEN];

    printf("echo-service BR_TRANSACTION code=0x%x sender_pid=%d sender_euid=%u data_size=%" PRIu64 "\n",
           tr->code,
           tr->sender_pid,
           tr->sender_euid,
           (uint64_t)tr->data_size);

    if (tr->code == SC_CODE_PING)
        return send_text_reply(fd, tr->data.ptr.buffer, "PONG from echo-service", 0, "echo-service ping reply");

    if (tr->code != SC_CODE_ECHO)
        return send_text_reply(fd, tr->data.ptr.buffer, "UNKNOWN ECHO CODE", 1, "echo-service unknown reply");

    if (tr->data.ptr.buffer && tr->data_size > 0) {
        printf("echo-service request payload: ");
        fwrite((const void *)(uintptr_t)tr->data.ptr.buffer, 1, tr->data_size, stdout);
        printf("\n");
    }

    snprintf(reply_text, sizeof(reply_text), "echo-service reply from webOS sidecar");

    return send_text_reply(fd, tr->data.ptr.buffer, reply_text, 0, "echo-service reply");
}

static int run_echo_service(const char *name)
{
    int fd = binder_open_and_init();

    if (add_service(fd, name) != 0) {
        fprintf(stderr, "echo-service: addService failed\n");
        return 1;
    }

    {
        uint8_t writebuf[64];
        uint8_t readbuf[8192];
        uint8_t *p = writebuf;
        uint32_t cmd = BC_ENTER_LOOPER;
        int n;

        append_u32(&p, cmd);
        n = binder_write_read(fd, writebuf, (size_t)(p - writebuf),
                              readbuf, sizeof(readbuf), "echo-service enter looper");
        if (n < 0)
            return 1;

        if (n > 0) {
            uint8_t *ptr = readbuf;
            uint8_t *end = readbuf + n;

            while (ptr + sizeof(uint32_t) <= end) {
                uint32_t rcmd;
                memcpy(&rcmd, ptr, sizeof(rcmd));
                ptr += sizeof(rcmd);

                printf("echo-service got %s 0x%08x\n", cmd_name(rcmd), rcmd);

                if (rcmd == BR_TRANSACTION) {
                    struct binder_transaction_data tr;
                    if (ptr + sizeof(tr) > end)
                        return 1;
                    memcpy(&tr, ptr, sizeof(tr));
                    ptr += sizeof(tr);
                    if (echo_service_process_transaction(fd, &tr) != 0)
                        return 1;
                } else if (rcmd == BR_NOOP || rcmd == BR_SPAWN_LOOPER ||
                           rcmd == BR_TRANSACTION_COMPLETE) {
                    continue;
                } else if (rcmd == BR_INCREFS || rcmd == BR_ACQUIRE ||
                           rcmd == BR_RELEASE || rcmd == BR_DECREFS) {
                    if (handle_ref_cmd(fd, rcmd, &ptr, end, "echo-service") != 0)
                        return 1;
                } else {
                    printf("echo-service unhandled cmd=0x%08x\n", rcmd);
                    return 1;
                }
            }
        }
    }

    printf("echo-service ready name=%s\n", name);

    for (;;) {
        uint8_t readbuf[8192];
        int n = binder_write_read(fd, NULL, 0, readbuf, sizeof(readbuf), "echo-service loop");

        if (n < 0)
            return 1;

        uint8_t *ptr = readbuf;
        uint8_t *end = readbuf + n;

        while (ptr + sizeof(uint32_t) <= end) {
            uint32_t rcmd;
            memcpy(&rcmd, ptr, sizeof(rcmd));
            ptr += sizeof(rcmd);

            printf("echo-service got %s 0x%08x\n", cmd_name(rcmd), rcmd);

            if (rcmd == BR_TRANSACTION) {
                struct binder_transaction_data tr;
                if (ptr + sizeof(tr) > end)
                    return 1;
                memcpy(&tr, ptr, sizeof(tr));
                ptr += sizeof(tr);
                if (echo_service_process_transaction(fd, &tr) != 0)
                    return 1;
            } else if (rcmd == BR_NOOP || rcmd == BR_SPAWN_LOOPER ||
                       rcmd == BR_TRANSACTION_COMPLETE) {
                continue;
            } else if (rcmd == BR_INCREFS || rcmd == BR_ACQUIRE ||
                       rcmd == BR_RELEASE || rcmd == BR_DECREFS) {
                if (handle_ref_cmd(fd, rcmd, &ptr, end, "echo-service") != 0)
                    return 1;
            } else {
                printf("echo-service unhandled cmd=0x%08x\n", rcmd);
                return 1;
            }
        }
    }

    return 0;
}

static int call_echo_handle(int fd, uint32_t handle, const char *message)
{
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
    tr.data_size = strlen(message) + 1;
    tr.offsets_size = 0;
    tr.data.ptr.buffer = (binder_uintptr_t)message;
    tr.data.ptr.offsets = 0;

    cmd = BC_TRANSACTION;
    append_u32(&p, cmd);
    append_bytes(&p, &tr, sizeof(tr));

    printf("echo-client: calling handle=%u message=%s\n", handle, message);

    for (;;) {
        int n;

        if (first) {
            n = binder_write_read(fd, writebuf, (size_t)(p - writebuf),
                                  readbuf, sizeof(readbuf), "echo-client call");
            first = 0;
        } else {
            n = binder_write_read(fd, NULL, 0,
                                  readbuf, sizeof(readbuf), "echo-client wait");
        }

        if (n < 0)
            return -1;

        uint8_t *ptr = readbuf;
        uint8_t *end = readbuf + n;

        while (ptr + sizeof(uint32_t) <= end) {
            uint32_t rcmd;
            memcpy(&rcmd, ptr, sizeof(rcmd));
            ptr += sizeof(rcmd);

            printf("echo-client got %s 0x%08x\n", cmd_name(rcmd), rcmd);

            if (rcmd == BR_REPLY) {
                struct binder_transaction_data reply;

                if (ptr + sizeof(reply) > end)
                    return -1;
                memcpy(&reply, ptr, sizeof(reply));
                ptr += sizeof(reply);

                if (reply.data.ptr.buffer && reply.data_size >= sizeof(struct sc_text_reply)) {
                    struct sc_text_reply *txt = (struct sc_text_reply *)(uintptr_t)reply.data.ptr.buffer;
                    printf("echo-client reply status=%u text=%s\n", txt->status, txt->text);
                    binder_free_buffer(fd, reply.data.ptr.buffer);
                    return txt->status == 0 ? 0 : 1;
                }

                if (reply.data.ptr.buffer)
                    binder_free_buffer(fd, reply.data.ptr.buffer);
                return -1;
            } else if (rcmd == BR_FAILED_REPLY || rcmd == BR_DEAD_REPLY) {
                fprintf(stderr, "echo-client got failure reply cmd=0x%08x\n", rcmd);
                return -1;
            } else if (rcmd == BR_NOOP || rcmd == BR_TRANSACTION_COMPLETE) {
                continue;
            } else if (rcmd == BR_INCREFS || rcmd == BR_ACQUIRE ||
                       rcmd == BR_RELEASE || rcmd == BR_DECREFS) {
                if (handle_ref_cmd(fd, rcmd, &ptr, end, "echo-client") != 0)
                    return -1;
            } else {
                printf("echo-client unhandled cmd=0x%08x\n", rcmd);
            }
        }
    }
}

static int run_echo_client(const char *name, const char *message)
{
    int fd = binder_open_and_init();
    uint32_t handle = 0;

    if (get_service_handle(fd, name, &handle) != 0) {
        fprintf(stderr, "echo-client: getService failed for %s\n", name);
        return 1;
    }

    if (!handle) {
        fprintf(stderr, "echo-client: got empty handle for %s\n", name);
        return 1;
    }

    if (call_echo_handle(fd, handle, message) != 0) {
        fprintf(stderr, "echo-client: echo call failed\n");
        return 1;
    }

    return 0;
}

static const char *basename_of(const char *s)
{
    const char *p = strrchr(s, '/');
    return p ? p + 1 : s;
}

int main(int argc, char **argv)
{
    const char *prog;

    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    prog = basename_of(argv[0]);

    if (strcmp(prog, "mini_servicemgr") == 0)
        return run_sm_server();

    if (strcmp(prog, "echo_service") == 0) {
        const char *name = argc > 1 ? argv[1] : "test.echo";
        return run_echo_service(name);
    }

    if (strcmp(prog, "echo_client") == 0) {
        const char *name = argc > 1 ? argv[1] : "test.echo";
        const char *message = argc > 2 ? argv[2] : "hello from echo_client";
        return run_echo_client(name, message);
    }

    
    if (strcmp(prog, "list_services") == 0)
        return run_list_services_client();
if (argc >= 2 && strcmp(argv[1], "sm-server") == 0)
        return run_sm_server();

    if (argc >= 2 && strcmp(argv[1], "echo-service") == 0) {
        const char *name = argc > 2 ? argv[2] : "test.echo";
        return run_echo_service(name);
    }

    
    if (argc >= 2 && strcmp(argv[1], "list-services") == 0)
        return run_list_services_client();
if (argc >= 2 && strcmp(argv[1], "echo-client") == 0) {
        const char *name = argc > 2 ? argv[2] : "test.echo";
        const char *message = argc > 3 ? argv[3] : "hello from echo_client";
        return run_echo_client(name, message);
    }

    fprintf(stderr,
            "usage:\n"
            "  mini_servicemgr\n"
            "  echo_service [name]\n"
            "  echo_client [name] [message]\n"
            "  sidecar_binder sm-server\n"
            "  sidecar_binder echo-service [name]\n"
            "  sidecar_binder echo-client [name] [message]\n");

    return 2;
}
