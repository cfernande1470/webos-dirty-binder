#include "android_like_aidl_common.hpp"

#include <string>

#define ONEWAY_DESCRIPTOR "webos.dirtybinder.IOnewayDemo"
#define ONEWAY_TX_NOTIFY 1U
#define ONEWAY_TX_GET_COUNT 2U
#define ONEWAY_PING 0x50494e47U

static const binder_uintptr_t kOnewayServicePtr =
    (binder_uintptr_t)0x4f4e455741595356ULL; /* ONEWAYSV */

static const binder_uintptr_t kOnewayServiceCookie =
    (binder_uintptr_t)0x4f4e4557434b3030ULL; /* ONEWCK00 */

static int g_notify_count = 0;
static int g_last_seq = -1;

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
                "oneway service: bad descriptor got=%s expected=%s\n",
                descriptor.c_str(),
                expected);
        return -1;
    }

    return 0;
}

static int send_i32_reply(
    int fd,
    binder_uintptr_t incoming_buffer,
    int32_t exception_code,
    int32_t value,
    const char *tag)
{
    uint8_t reply[128];
    size_t pos = 0;

    if (cb_parcel_write_i32(reply, sizeof(reply), &pos, exception_code) != 0)
        return -1;

    if (exception_code == 0) {
        if (cb_parcel_write_i32(reply, sizeof(reply), &pos, value) != 0)
            return -1;
    }

    return aidl_like_send_reply_parcel(fd, incoming_buffer, reply, pos, tag);
}

static int process_notify(int fd, struct binder_transaction_data *tr) {
    struct aidl_like_reader r;
    int32_t seq = -1;
    std::string payload;

    memset(&r, 0, sizeof(r));
    r.data = (const uint8_t *)(uintptr_t)tr->data.ptr.buffer;
    r.size = (size_t)tr->data_size;
    r.pos = 0;

    if (read_token(&r, ONEWAY_DESCRIPTOR) != 0 ||
        aidl_like_read_i32(&r, &seq) != 0 ||
        aidl_like_read_string16_ascii(&r, &payload) != 0) {
        fprintf(stderr, "oneway service: bad notify parcel\n");

        if (tr->flags & TF_ONE_WAY) {
            cb_binder_free_buffer(fd, tr->data.ptr.buffer, "oneway service free bad notify");
            return -1;
        }

        return send_i32_reply(fd,
                              tr->data.ptr.buffer,
                              -1,
                              0,
                              "oneway service bad notify reply");
    }

    g_notify_count++;
    g_last_seq = seq;

    printf("oneway service notify seq=%d payload=%s count=%d flags=0x%x\n",
           seq,
           payload.c_str(),
           g_notify_count,
           tr->flags);
    printf("AIDL_LIKE_ONEWAY_NOTIFY_OK seq=%d count=%d\n", seq, g_notify_count);
    fflush(stdout);

    if (tr->flags & TF_ONE_WAY) {
        return cb_binder_free_buffer(fd,
                                     tr->data.ptr.buffer,
                                     "oneway service free notify oneway");
    }

    return send_i32_reply(fd,
                          tr->data.ptr.buffer,
                          0,
                          g_notify_count,
                          "oneway service notify sync reply");
}

static int process_get_count(int fd, struct binder_transaction_data *tr) {
    struct aidl_like_reader r;

    memset(&r, 0, sizeof(r));
    r.data = (const uint8_t *)(uintptr_t)tr->data.ptr.buffer;
    r.size = (size_t)tr->data_size;
    r.pos = 0;

    if (read_token(&r, ONEWAY_DESCRIPTOR) != 0) {
        return send_i32_reply(fd,
                              tr->data.ptr.buffer,
                              -1,
                              0,
                              "oneway service bad getCount reply");
    }

    printf("oneway service getCount count=%d last_seq=%d\n",
           g_notify_count,
           g_last_seq);
    fflush(stdout);

    return send_i32_reply(fd,
                          tr->data.ptr.buffer,
                          0,
                          g_notify_count,
                          "oneway service getCount reply");
}

static int process_transaction(int fd, struct binder_transaction_data *tr) {
    printf("oneway service BR_TRANSACTION code=0x%x data_size=%llu offsets_size=%llu flags=0x%x\n",
           tr->code,
           (unsigned long long)tr->data_size,
           (unsigned long long)tr->offsets_size,
           tr->flags);

    if (tr->code == ONEWAY_PING) {
        return cb_send_text_reply(fd,
                                  tr->data.ptr.buffer,
                                  0,
                                  "pong",
                                  "oneway service ping reply");
    }

    if (tr->code == ONEWAY_TX_NOTIFY)
        return process_notify(fd, tr);

    if (tr->code == ONEWAY_TX_GET_COUNT)
        return process_get_count(fd, tr);

    return send_i32_reply(fd,
                          tr->data.ptr.buffer,
                          -1,
                          0,
                          "oneway service unknown reply");
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
                                 first ? "oneway service enter looper" : "oneway service loop");
        first = 0;

        if (n < 0)
            return -1;

        ptr = readbuf;
        end = readbuf + n;

        while (ptr + sizeof(uint32_t) <= end) {
            uint32_t rcmd;

            memcpy(&rcmd, ptr, sizeof(rcmd));
            ptr += sizeof(rcmd);

            printf("oneway service got %s 0x%08x\n", cb_cmd_name(rcmd), rcmd);

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
                if (cb_handle_ref_cmd(fd, rcmd, &ptr, end, "oneway service") != 0)
                    return -1;

                continue;
            }

            if (rcmd == BR_DEAD_REPLY || rcmd == BR_FAILED_REPLY) {
                fprintf(stderr, "oneway service failed cmd=0x%08x\n", rcmd);
                return 1;
            }

            fprintf(stderr, "oneway service unhandled cmd=0x%08x\n", rcmd);
            return -1;
        }
    }
}

int main(int argc, char **argv) {
    const char *service_name = argc > 1 ? argv[1] : "test.android.aidl.oneway";
    int fd;

    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);

    fd = cb_binder_open_and_init("oneway service");

    if (fd < 0)
        return 1;

    if (cb_register_local_service(fd,
                                  service_name,
                                  kOnewayServicePtr,
                                  kOnewayServiceCookie) != 0)
        return 1;

    printf("AIDL_LIKE_ONEWAY_SERVICE_REGISTERED\n");
    fflush(stdout);

    return service_loop(fd) == 0 ? 0 : 1;
}
