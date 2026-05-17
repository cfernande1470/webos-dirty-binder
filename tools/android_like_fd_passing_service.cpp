#include "android_like_aidl_common.hpp"

#include <string>

#define FD_PASS_DESCRIPTOR "webos.dirtybinder.IFdPassing"
#define FD_PASS_TX_SEND_FD 0x46445053U /* FDPS */
#define FD_PASS_PING 0x50494e47U

static const binder_uintptr_t kFdPassingServicePtr =
    (binder_uintptr_t)0x4644504153533030ULL; /* FDPASS00 */

static const binder_uintptr_t kFdPassingServiceCookie =
    (binder_uintptr_t)0x464450434b303030ULL; /* FDPCK000 */

static int write_token(uint8_t *buf, size_t cap, size_t *pos, const char *descriptor) {
    if (cb_parcel_write_i32(buf, cap, pos, 0) != 0)
        return -1;

    return cb_parcel_write_string16_ascii(buf, cap, pos, descriptor);
}

static int read_token(struct aidl_like_reader *r, const char *expected) {
    int32_t strict_header = 0;
    std::string descriptor;

    if (aidl_like_read_i32(r, &strict_header) != 0)
        return -1;

    if (aidl_like_read_string16_ascii(r, &descriptor) != 0)
        return -1;

    if (descriptor != expected) {
        fprintf(stderr,
                "fd-passing service: bad descriptor got=%s expected=%s\n",
                descriptor.c_str(),
                expected);
        return -1;
    }

    return 0;
}

static int send_string_reply(
    int fd,
    binder_uintptr_t incoming_buffer,
    int32_t exception_code,
    const char *text,
    const char *tag)
{
    uint8_t reply[1024];
    size_t pos = 0;

    if (cb_parcel_write_i32(reply, sizeof(reply), &pos, exception_code) != 0)
        return -1;

    if (exception_code == 0) {
        if (cb_parcel_write_string16_ascii(reply, sizeof(reply), &pos, text ? text : "") != 0)
            return -1;
    }

    return aidl_like_send_reply_parcel(fd, incoming_buffer, reply, pos, tag);
}

static int first_fd_from_transaction(struct binder_transaction_data *tr, int *out_fd) {
    binder_size_t off;
    struct flat_binder_object *obj;

    if (!out_fd)
        return -1;

    *out_fd = -1;

    if (!tr->offsets_size || !tr->data.ptr.buffer || !tr->data.ptr.offsets)
        return -1;

    if (tr->offsets_size < sizeof(binder_size_t))
        return -1;

    memcpy(&off, (void *)(uintptr_t)tr->data.ptr.offsets, sizeof(off));

    if ((size_t)off + sizeof(struct flat_binder_object) > (size_t)tr->data_size)
        return -1;

    obj = (struct flat_binder_object *)((uint8_t *)(uintptr_t)tr->data.ptr.buffer + off);

    printf("fd-passing service object: sizeof(flat_binder_object)=%zu offset=%" PRIu64 " type=0x%08x flags=0x%08x handle=%u binder=0x%" PRIx64 " cookie=0x%" PRIx64 "\n",
           sizeof(*obj),
           (uint64_t)off,
           obj->type,
           obj->flags,
           obj->handle,
           (uint64_t)obj->binder,
           (uint64_t)obj->cookie);

    if (obj->type != BINDER_TYPE_FD)
        return -1;

    *out_fd = (int)obj->handle;
    return 0;
}

static int process_send_fd(int binder_fd, struct binder_transaction_data *tr) {
    struct aidl_like_reader r;
    std::string label;
    int received_fd = -1;
    char buf[256];
    ssize_t n;
    std::string payload;
    std::string reply;

    memset(&r, 0, sizeof(r));
    r.data = (const uint8_t *)(uintptr_t)tr->data.ptr.buffer;
    r.size = (size_t)tr->data_size;
    r.pos = 0;

    if (read_token(&r, FD_PASS_DESCRIPTOR) != 0 ||
        aidl_like_read_string16_ascii(&r, &label) != 0) {
        return send_string_reply(binder_fd,
                                 tr->data.ptr.buffer,
                                 -1,
                                 NULL,
                                 "fd-passing service bad parcel reply");
    }

    if (first_fd_from_transaction(tr, &received_fd) != 0 || received_fd < 0) {
        return send_string_reply(binder_fd,
                                 tr->data.ptr.buffer,
                                 -1,
                                 NULL,
                                 "fd-passing service missing fd reply");
    }

    printf("BINDER_FD_RECEIVED_OK fd=%d label=%s\n", received_fd, label.c_str());
    fflush(stdout);

    memset(buf, 0, sizeof(buf));
    n = read(received_fd, buf, sizeof(buf) - 1);

    if (n < 0) {
        perror("fd-passing service read");
        close(received_fd);
        return send_string_reply(binder_fd,
                                 tr->data.ptr.buffer,
                                 -1,
                                 NULL,
                                 "fd-passing service read failed reply");
    }

    close(received_fd);

    payload.assign(buf, (size_t)n);

    printf("fd-passing service read label=%s payload=%s bytes=%zd\n",
           label.c_str(),
           payload.c_str(),
           n);
    printf("BINDER_FD_READ_OK\n");
    fflush(stdout);

    reply = "fd-ok:" + label + ":" + payload;

    return send_string_reply(binder_fd,
                             tr->data.ptr.buffer,
                             0,
                             reply.c_str(),
                             "fd-passing service reply");
}

static int process_transaction(int fd, struct binder_transaction_data *tr) {
    printf("fd-passing service BR_TRANSACTION code=0x%x data_size=%llu offsets_size=%llu flags=0x%x\n",
           tr->code,
           (unsigned long long)tr->data_size,
           (unsigned long long)tr->offsets_size,
           tr->flags);

    if (tr->code == FD_PASS_PING) {
        return cb_send_text_reply(fd,
                                  tr->data.ptr.buffer,
                                  0,
                                  "pong",
                                  "fd-passing ping reply");
    }

    if (tr->code == FD_PASS_TX_SEND_FD)
        return process_send_fd(fd, tr);

    return send_string_reply(fd,
                             tr->data.ptr.buffer,
                             -1,
                             NULL,
                             "fd-passing unknown reply");
}

static int service_loop(int fd) {
    uint8_t writebuf[64];
    uint8_t readbuf[8192];
    uint8_t *p = writebuf;
    uint32_t cmd = BC_ENTER_LOOPER;
    int first = 1;

    cb_append_u32(&p, cmd);

    for (;;) {
        int n;
        uint8_t *ptr;
        uint8_t *end;

        n = cb_binder_write_read(fd,
                                 first ? writebuf : NULL,
                                 first ? (size_t)(p - writebuf) : 0,
                                 readbuf,
                                 sizeof(readbuf),
                                 first ? "fd-passing service enter looper" : "fd-passing service loop");
        first = 0;

        if (n < 0)
            return -1;

        ptr = readbuf;
        end = readbuf + n;

        while (ptr + sizeof(uint32_t) <= end) {
            uint32_t rcmd;

            memcpy(&rcmd, ptr, sizeof(rcmd));
            ptr += sizeof(rcmd);

            printf("fd-passing service got %s 0x%08x\n", cb_cmd_name(rcmd), rcmd);

            if (rcmd == BR_NOOP || rcmd == BR_TRANSACTION_COMPLETE || rcmd == BR_SPAWN_LOOPER)
                continue;

            if (rcmd == BR_TRANSACTION) {
                struct binder_transaction_data tr;

                if (ptr + sizeof(tr) > end)
                    return -1;

                memcpy(&tr, ptr, sizeof(tr));
                ptr += sizeof(tr);

                if (process_transaction(fd, &tr) != 0)
                    return -1;

                continue;
            }

            if (rcmd == BR_INCREFS || rcmd == BR_ACQUIRE || rcmd == BR_RELEASE || rcmd == BR_DECREFS) {
                if (cb_handle_ref_cmd(fd, rcmd, &ptr, end, "fd-passing service") != 0)
                    return -1;

                continue;
            }

            if (rcmd == BR_DEAD_REPLY || rcmd == BR_FAILED_REPLY) {
                fprintf(stderr, "fd-passing service failed cmd=0x%08x\n", rcmd);
                return 1;
            }

            fprintf(stderr, "fd-passing service unhandled cmd=0x%08x\n", rcmd);
            return -1;
        }
    }
}

int main(int argc, char **argv) {
    const char *service_name = argc > 1 ? argv[1] : "test.android.fd";
    int fd;

    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);

    fd = cb_binder_open_and_init("fd-passing service");

    if (fd < 0)
        return 1;

    if (cb_register_local_service(fd,
                                  service_name,
                                  kFdPassingServicePtr,
                                  kFdPassingServiceCookie) != 0)
        return 1;

    printf("BINDER_FD_SERVICE_REGISTERED\n");
    fflush(stdout);

    return service_loop(fd) == 0 ? 0 : 1;
}
