#include "android_like_aidl_common.hpp"

#include <string>

#define AIDL_CB_SERVICE_DESCRIPTOR "webos.dirtybinder.IAidlCallbackDemo"
#define AIDL_CB_LISTENER_DESCRIPTOR "webos.dirtybinder.IAidlCallbackListener"

#define AIDL_CB_TX_REGISTER_LISTENER 1U
#define AIDL_CB_TX_ON_EVENT 1U
#define AIDL_CB_PING 0x50494e47U

static const binder_uintptr_t kAidlCallbackServicePtr =
    (binder_uintptr_t)0x4149444c43425356ULL; /* AIDLCBSV */

static const binder_uintptr_t kAidlCallbackServiceCookie =
    (binder_uintptr_t)0x4149444c43423030ULL; /* AIDLCB00 */

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
                "aidl-callback service: bad descriptor got=%s expected=%s\n",
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

static int call_listener_on_event(
    int fd,
    uint32_t listener_handle,
    const char *event_text,
    std::string *out_ack)
{
    uint8_t parcel[1024];
    size_t parcel_size = 0;
    uint8_t writebuf[2048];
    uint8_t readbuf[8192];
    uint8_t *p = writebuf;
    uint32_t cmd;
    struct binder_transaction_data tr;
    int first = 1;

    if (out_ack)
        out_ack->clear();

    if (write_token(parcel, sizeof(parcel), &parcel_size, AIDL_CB_LISTENER_DESCRIPTOR) != 0 ||
        cb_parcel_write_string16_ascii(parcel, sizeof(parcel), &parcel_size, event_text) != 0) {
        fprintf(stderr, "aidl-callback service: failed to build listener parcel\n");
        return -1;
    }

    memset(&tr, 0, sizeof(tr));
    tr.target.handle = listener_handle;
    tr.code = AIDL_CB_TX_ON_EVENT;
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
                                 first ? "aidl-callback service call listener" : "aidl-callback service wait listener");
        first = 0;

        if (n < 0)
            return -1;

        ptr = readbuf;
        end = readbuf + n;

        while (ptr + sizeof(uint32_t) <= end) {
            uint32_t rcmd;

            memcpy(&rcmd, ptr, sizeof(rcmd));
            ptr += sizeof(rcmd);

            printf("aidl-callback service listener call got %s 0x%08x\n", cb_cmd_name(rcmd), rcmd);

            if (rcmd == BR_NOOP || rcmd == BR_TRANSACTION_COMPLETE || rcmd == BR_SPAWN_LOOPER)
                continue;

            if (rcmd == BR_REPLY) {
                struct binder_transaction_data reply;
                struct aidl_like_reader r;
                int32_t exception_code = 0;
                std::string ack;

                if (ptr + sizeof(reply) > end)
                    return -1;

                memcpy(&reply, ptr, sizeof(reply));
                ptr += sizeof(reply);

                memset(&r, 0, sizeof(r));
                r.data = (const uint8_t *)(uintptr_t)reply.data.ptr.buffer;
                r.size = (size_t)reply.data_size;
                r.pos = 0;

                if (aidl_like_read_i32(&r, &exception_code) != 0) {
                    fprintf(stderr, "aidl-callback service: listener reply missing exception code\n");
                    cb_binder_free_buffer(fd, reply.data.ptr.buffer, "aidl-callback service free bad listener reply");
                    return -1;
                }

                if (exception_code != 0) {
                    fprintf(stderr, "aidl-callback service: listener exception=%d\n", exception_code);
                    cb_binder_free_buffer(fd, reply.data.ptr.buffer, "aidl-callback service free exception listener reply");
                    return -1;
                }

                if (aidl_like_read_string16_ascii(&r, &ack) != 0) {
                    fprintf(stderr, "aidl-callback service: listener reply missing ack string\n");
                    cb_binder_free_buffer(fd, reply.data.ptr.buffer, "aidl-callback service free bad ack listener reply");
                    return -1;
                }

                cb_binder_free_buffer(fd, reply.data.ptr.buffer, "aidl-callback service free listener reply");

                if (out_ack)
                    *out_ack = ack;

                return 0;
            }

            if (rcmd == BR_INCREFS || rcmd == BR_ACQUIRE || rcmd == BR_RELEASE || rcmd == BR_DECREFS) {
                if (cb_handle_ref_cmd(fd, rcmd, &ptr, end, "aidl-callback service listener call") != 0)
                    return -1;

                continue;
            }

            if (rcmd == BR_DEAD_REPLY || rcmd == BR_FAILED_REPLY) {
                fprintf(stderr, "aidl-callback service: listener dead/failed cmd=0x%08x\n", rcmd);
                return -1;
            }

            fprintf(stderr, "aidl-callback service: unhandled listener cmd=0x%08x\n", rcmd);
            return -1;
        }
    }
}

static int process_register_listener(int fd, struct binder_transaction_data *tr) {
    struct aidl_like_reader r;
    uint32_t listener_handle = 0;
    std::string ack;
    const char *event_text = "aidl-listener-event-0";

    memset(&r, 0, sizeof(r));
    r.data = (const uint8_t *)(uintptr_t)tr->data.ptr.buffer;
    r.size = (size_t)tr->data_size;
    r.pos = 0;

    if (read_token(&r, AIDL_CB_SERVICE_DESCRIPTOR) != 0) {
        fprintf(stderr, "aidl-callback service: bad registerListener token\n");

        if (tr->flags & TF_ONE_WAY) {
            cb_binder_free_buffer(fd, tr->data.ptr.buffer, "aidl-callback service free bad oneway");
            return -1;
        }

        return send_string_reply(fd, tr->data.ptr.buffer, -1, NULL, "aidl-callback service bad-token reply");
    }

    listener_handle = cb_first_handle_from_transaction(tr);

    if (!listener_handle) {
        fprintf(stderr, "aidl-callback service: missing listener handle\n");

        if (tr->flags & TF_ONE_WAY) {
            cb_binder_free_buffer(fd, tr->data.ptr.buffer, "aidl-callback service free missing-handle oneway");
            return -1;
        }

        return send_string_reply(fd, tr->data.ptr.buffer, -1, NULL, "aidl-callback service missing-handle reply");
    }

    printf("aidl-callback service: listener handle=%u\n", listener_handle);
    printf("AIDL_LIKE_CALLBACK_LISTENER_HANDLE_OK\n");
    fflush(stdout);

    if (cb_binder_acquire_handle(fd, listener_handle, "aidl-callback service acquire listener") != 0)
        return -1;

    if (call_listener_on_event(fd, listener_handle, event_text, &ack) != 0) {
        cb_binder_release_handle(fd, listener_handle, "aidl-callback service release listener after failed call");
        return -1;
    }

    printf("aidl-callback service: listener ack=%s\n", ack.c_str());

    if (ack != "listener-ack:aidl-listener-event-0") {
        fprintf(stderr, "aidl-callback service: bad listener ack=%s\n", ack.c_str());
        cb_binder_release_handle(fd, listener_handle, "aidl-callback service release listener after bad ack");
        return -1;
    }

    printf("AIDL_LIKE_CALLBACK_LISTENER_REPLY_OK\n");
    fflush(stdout);

    if (cb_binder_release_handle(fd, listener_handle, "aidl-callback service release listener") != 0)
        return -1;

    if (tr->flags & TF_ONE_WAY) {
        if (cb_binder_free_buffer(fd,
                                  tr->data.ptr.buffer,
                                  "aidl-callback service free oneway registerListener") != 0)
            return -1;

        printf("aidl-callback service: one-way registerListener complete\n");
        fflush(stdout);
        return 0;
    }

    return send_string_reply(fd,
                             tr->data.ptr.buffer,
                             0,
                             "listener registered",
                             "aidl-callback service registerListener reply");
}

static int process_transaction(int fd, struct binder_transaction_data *tr) {
    printf("aidl-callback service BR_TRANSACTION code=0x%x sender_pid=%d sender_euid=%u data_size=%llu offsets_size=%llu flags=0x%x\n",
           tr->code,
           tr->sender_pid,
           tr->sender_euid,
           (unsigned long long)tr->data_size,
           (unsigned long long)tr->offsets_size,
           tr->flags);

    if (tr->code == AIDL_CB_PING) {
        printf("aidl-callback service: handled PING\n");
        fflush(stdout);
        return cb_send_text_reply(fd, tr->data.ptr.buffer, 0, "pong", "aidl-callback ping reply");
    }

    if (tr->code == AIDL_CB_TX_REGISTER_LISTENER)
        return process_register_listener(fd, tr);

    fprintf(stderr, "aidl-callback service: unknown code=0x%x\n", tr->code);

    if (tr->flags & TF_ONE_WAY) {
        cb_binder_free_buffer(fd, tr->data.ptr.buffer, "aidl-callback service free unknown oneway");
        return -1;
    }

    return send_string_reply(fd, tr->data.ptr.buffer, -1, NULL, "aidl-callback service unknown reply");
}

static int service_loop(int fd) {
    uint8_t writebuf[64];
    uint8_t readbuf[8192];
    uint8_t *p = writebuf;
    uint32_t cmd = BC_ENTER_LOOPER;
    int first = 1;

    cb_append_u32(&p, cmd);
    printf("aidl-callback service enter looper\n");

    for (;;) {
        int n;
        uint8_t *ptr;
        uint8_t *end;

        n = cb_binder_write_read(fd,
                                 first ? writebuf : NULL,
                                 first ? (size_t)(p - writebuf) : 0,
                                 readbuf,
                                 sizeof(readbuf),
                                 first ? "aidl-callback service enter looper" : "aidl-callback service loop");
        first = 0;

        if (n < 0)
            return -1;

        ptr = readbuf;
        end = readbuf + n;

        while (ptr + sizeof(uint32_t) <= end) {
            uint32_t rcmd;

            memcpy(&rcmd, ptr, sizeof(rcmd));
            ptr += sizeof(rcmd);

            printf("aidl-callback service got %s 0x%08x\n", cb_cmd_name(rcmd), rcmd);

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
                if (cb_handle_ref_cmd(fd, rcmd, &ptr, end, "aidl-callback service") != 0)
                    return -1;

                continue;
            }

            if (rcmd == BR_DEAD_REPLY || rcmd == BR_FAILED_REPLY) {
                fprintf(stderr, "aidl-callback service failed cmd=0x%08x\n", rcmd);
                return 1;
            }

            fprintf(stderr, "aidl-callback service unhandled cmd=0x%08x\n", rcmd);
            return -1;
        }
    }
}

int main(int argc, char **argv) {
    const char *service_name = argc > 1 ? argv[1] : "test.android.aidl.callback";
    int fd;

    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);

    fd = cb_binder_open_and_init("aidl-callback service");

    if (fd < 0)
        return 1;

    if (cb_register_local_service(fd, service_name, kAidlCallbackServicePtr, kAidlCallbackServiceCookie) != 0)
        return 1;

    printf("AIDL_LIKE_CALLBACK_LISTENER_SERVICE_REGISTERED\n");
    fflush(stdout);

    return service_loop(fd) == 0 ? 0 : 1;
}
