#include "android_like_aidl_common.hpp"

static const binder_uintptr_t kAidlLikeServicePtr =
    (binder_uintptr_t)0x4149444c53455256ULL; /* AIDLSERV */

static const binder_uintptr_t kAidlLikeServiceCookie =
    (binder_uintptr_t)0x4149444c30303030ULL; /* AIDL0000 */

static int aidl_like_send_descriptor_reply(int fd, struct binder_transaction_data *tr) {
    uint8_t reply[512];
    size_t pos = 0;

    /*
     * Android's INTERFACE_TRANSACTION returns the descriptor string directly,
     * not an AIDL method reply with an exception header.
     */
    if (cb_parcel_write_string16_ascii(reply,
                                       sizeof(reply),
                                       &pos,
                                       AIDL_LIKE_DESCRIPTOR) != 0) {
        fprintf(stderr, "aidl-like service: failed to build descriptor reply\n");
        return -1;
    }

    printf("aidl-like service: INTERFACE_TRANSACTION descriptor=%s\n",
           AIDL_LIKE_DESCRIPTOR);
    printf("BINDER_META_INTERFACE_TRANSACTION_OK\n");
    fflush(stdout);

    return aidl_like_send_reply_parcel(fd,
                                       tr->data.ptr.buffer,
                                       reply,
                                       pos,
                                       "aidl-like interface descriptor reply");
}

static int aidl_like_process_echo(int fd, struct binder_transaction_data *tr, struct aidl_like_reader *r) {
    std::string msg;
    std::string out;
    uint8_t reply[1024];
    size_t pos = 0;

    if (aidl_like_read_string16_ascii(r, &msg) != 0) {
        fprintf(stderr, "aidl-like service echo: bad string arg\n");
        return aidl_like_send_exception_reply(fd, tr->data.ptr.buffer, -1, "aidl-like echo bad-arg reply");
    }

    out = "echo:" + msg;

    if (aidl_like_write_no_exception(reply, sizeof(reply), &pos) != 0 ||
        cb_parcel_write_string16_ascii(reply, sizeof(reply), &pos, out.c_str()) != 0) {
        fprintf(stderr, "aidl-like service echo: failed to build reply\n");
        return -1;
    }

    printf("aidl-like service echo msg=%s out=%s\n", msg.c_str(), out.c_str());
    printf("AIDL_LIKE_ECHO_SERVICE_OK\n");
    fflush(stdout);

    return aidl_like_send_reply_parcel(fd, tr->data.ptr.buffer, reply, pos, "aidl-like echo reply");
}

static int aidl_like_process_add(int fd, struct binder_transaction_data *tr, struct aidl_like_reader *r) {
    int32_t a = 0;
    int32_t b = 0;
    int32_t sum = 0;
    uint8_t reply[128];
    size_t pos = 0;

    if (aidl_like_read_i32(r, &a) != 0 ||
        aidl_like_read_i32(r, &b) != 0) {
        fprintf(stderr, "aidl-like service add: bad int args\n");
        return aidl_like_send_exception_reply(fd, tr->data.ptr.buffer, -1, "aidl-like add bad-arg reply");
    }

    sum = a + b;

    if (aidl_like_write_no_exception(reply, sizeof(reply), &pos) != 0 ||
        cb_parcel_write_i32(reply, sizeof(reply), &pos, sum) != 0) {
        fprintf(stderr, "aidl-like service add: failed to build reply\n");
        return -1;
    }

    printf("aidl-like service add %d + %d = %d\n", a, b, sum);
    printf("AIDL_LIKE_ADD_SERVICE_OK\n");
    fflush(stdout);

    return aidl_like_send_reply_parcel(fd, tr->data.ptr.buffer, reply, pos, "aidl-like add reply");
}

static int aidl_like_process_transaction(int fd, struct binder_transaction_data *tr) {
    struct aidl_like_reader r;

    printf("aidl-like service BR_TRANSACTION code=0x%x sender_pid=%d sender_euid=%u data_size=%llu offsets_size=%llu flags=0x%x\n",
           tr->code,
           tr->sender_pid,
           tr->sender_euid,
           (unsigned long long)tr->data_size,
           (unsigned long long)tr->offsets_size,
           tr->flags);

    if (tr->code == AIDL_LIKE_INTERFACE_TRANSACTION)
        return aidl_like_send_descriptor_reply(fd, tr);

    if (tr->code == AIDL_LIKE_PING) {
        printf("aidl-like service: handled PING\n");
        fflush(stdout);
        return cb_send_text_reply(fd, tr->data.ptr.buffer, 0, "pong", "aidl-like ping reply");
    }

    if (!tr->data.ptr.buffer || tr->data_size == 0) {
        fprintf(stderr, "aidl-like service: empty transaction\n");
        return aidl_like_send_exception_reply(fd, tr->data.ptr.buffer, -1, "aidl-like empty reply");
    }

    memset(&r, 0, sizeof(r));
    r.data = (const uint8_t *)(uintptr_t)tr->data.ptr.buffer;
    r.size = (size_t)tr->data_size;
    r.pos = 0;

    if (aidl_like_read_interface_token(&r) != 0) {
        fprintf(stderr, "aidl-like service: interface token failed\n");
        return aidl_like_send_exception_reply(fd, tr->data.ptr.buffer, -1, "aidl-like bad-token reply");
    }

    if (tr->code == AIDL_LIKE_TX_ECHO)
        return aidl_like_process_echo(fd, tr, &r);

    if (tr->code == AIDL_LIKE_TX_ADD)
        return aidl_like_process_add(fd, tr, &r);

    fprintf(stderr, "aidl-like service unknown code=0x%x\n", tr->code);
    return aidl_like_send_exception_reply(fd, tr->data.ptr.buffer, -1, "aidl-like unknown-code reply");
}

static int aidl_like_service_loop(int fd) {
    uint8_t writebuf[64];
    uint8_t readbuf[8192];
    uint8_t *p = writebuf;
    uint32_t cmd = BC_ENTER_LOOPER;
    int first = 1;

    cb_append_u32(&p, cmd);
    printf("aidl-like service enter looper\n");

    for (;;) {
        int n;
        uint8_t *ptr;
        uint8_t *end;

        n = cb_binder_write_read(fd,
                                 first ? writebuf : NULL,
                                 first ? (size_t)(p - writebuf) : 0,
                                 readbuf,
                                 sizeof(readbuf),
                                 first ? "aidl-like service enter looper" : "aidl-like service loop");
        first = 0;

        if (n < 0)
            return -1;

        ptr = readbuf;
        end = readbuf + n;

        while (ptr + sizeof(uint32_t) <= end) {
            uint32_t rcmd;
            memcpy(&rcmd, ptr, sizeof(rcmd));
            ptr += sizeof(rcmd);

            printf("aidl-like service got %s 0x%08x\n", cb_cmd_name(rcmd), rcmd);

            if (rcmd == BR_NOOP || rcmd == BR_TRANSACTION_COMPLETE || rcmd == BR_SPAWN_LOOPER)
                continue;

            if (rcmd == BR_TRANSACTION) {
                struct binder_transaction_data tr;

                if (ptr + sizeof(tr) > end)
                    return -1;

                memcpy(&tr, ptr, sizeof(tr));
                ptr += sizeof(tr);

                if (aidl_like_process_transaction(fd, &tr) != 0)
                    return -1;

                continue;
            }

            if (rcmd == BR_INCREFS || rcmd == BR_ACQUIRE || rcmd == BR_RELEASE || rcmd == BR_DECREFS) {
                if (cb_handle_ref_cmd(fd, rcmd, &ptr, end, "aidl-like service") != 0)
                    return -1;

                continue;
            }

            if (rcmd == BR_DEAD_REPLY || rcmd == BR_FAILED_REPLY) {
                fprintf(stderr, "aidl-like service failed cmd=0x%08x\n", rcmd);
                return 1;
            }

            fprintf(stderr, "aidl-like service unhandled cmd=0x%08x\n", rcmd);
            return -1;
        }
    }
}

int main(int argc, char **argv) {
    const char *service_name = argc > 1 ? argv[1] : "test.android.aidl";
    int fd;

    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);

    fd = cb_binder_open_and_init("aidl-like service");

    if (fd < 0)
        return 1;

    if (cb_register_local_service(fd, service_name, kAidlLikeServicePtr, kAidlLikeServiceCookie) != 0)
        return 1;

    printf("AIDL_LIKE_SERVICE_REGISTERED\n");
    printf("BINDER_META_SERVICE_REGISTERED\n");
    fflush(stdout);

    return aidl_like_service_loop(fd) == 0 ? 0 : 1;
}
