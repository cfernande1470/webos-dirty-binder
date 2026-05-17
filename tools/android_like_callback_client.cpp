#include "android_like_callback_common.hpp"

static const binder_uintptr_t kClientCallbackPtr =
    (binder_uintptr_t)0x434c49454e544342ULL; /* CLIENTCB */
static const binder_uintptr_t kClientCallbackCookie =
    (binder_uintptr_t)0x434c49454e543030ULL; /* CLIENT00 */

static int client_send_callback_reply(int fd, struct binder_transaction_data *tr) {
    struct callback_text_payload *payload = NULL;

    printf("callback client BR_TRANSACTION code=0x%x sender_pid=%d sender_euid=%u data_size=%llu\n",
           tr->code,
           tr->sender_pid,
           tr->sender_euid,
           (unsigned long long)tr->data_size);

    if (tr->code != ANDROID_LIKE_CALLBACK_ON_EVENT) {
        fprintf(stderr, "callback client unknown callback code=0x%x\n", tr->code);
        return cb_send_text_reply(fd, tr->data.ptr.buffer, 1, "unknown client callback code", "callback client unknown reply");
    }

    if (tr->data.ptr.buffer && tr->data_size >= sizeof(struct callback_text_payload))
        payload = (struct callback_text_payload *)(uintptr_t)tr->data.ptr.buffer;

    if (!payload || payload->magic != ANDROID_LIKE_CALLBACK_MAGIC) {
        fprintf(stderr, "callback client bad callback payload\n");
        return cb_send_text_reply(fd, tr->data.ptr.buffer, 1, "bad callback payload", "callback client bad-payload reply");
    }

    printf("callback client event text=%s\n", payload->text);
    printf("ANDROID_LIKE_CALLBACK_TRANSACTION_OK\n");

    return cb_send_text_reply(fd,
                              tr->data.ptr.buffer,
                              0,
                              "client callback ack",
                              "callback client event reply");
}

static int client_register_callback(int fd, uint32_t service_handle) {
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

    if (cb_parcel_write_i32(parcel, sizeof(parcel), &parcel_size, 0) != 0 ||
        cb_parcel_write_string16_ascii(parcel, sizeof(parcel), &parcel_size, ANDROID_LIKE_CALLBACK_DESCRIPTOR) != 0 ||
        cb_parcel_write_string16_ascii(parcel, sizeof(parcel), &parcel_size, "register client callback") != 0) {
        fprintf(stderr, "callback client: failed to build register Parcel\n");
        return -1;
    }

    parcel_size = cb_align8(parcel_size);
    memset(parcel + parcel_size, 0, sizeof(parcel) - parcel_size);

    memset(&obj, 0, sizeof(obj));
    obj.type = BINDER_TYPE_BINDER;
    obj.flags = FLAT_BINDER_FLAG_ACCEPTS_FDS;
    obj.binder = kClientCallbackPtr;
    obj.cookie = kClientCallbackCookie;

    offsets[0] = (binder_size_t)parcel_size;
    memcpy(parcel + parcel_size, &obj, sizeof(obj));
    parcel_size += sizeof(obj);
    parcel_size = cb_align8(parcel_size);

    memset(&tr, 0, sizeof(tr));
    tr.target.handle = service_handle;
    tr.code = ANDROID_LIKE_CALLBACK_REGISTER;
    tr.flags = TF_ACCEPT_FDS;
    tr.data_size = parcel_size;
    tr.offsets_size = sizeof(offsets);
    tr.data.ptr.buffer = (binder_uintptr_t)parcel;
    tr.data.ptr.offsets = (binder_uintptr_t)offsets;

    cmd = BC_TRANSACTION;
    cb_append_u32(&p, cmd);
    cb_append_bytes(&p, &tr, sizeof(tr));

    printf("callback client: sending local callback object ptr=0x%" PRIx64 " cookie=0x%" PRIx64 "\n",
           (uint64_t)kClientCallbackPtr,
           (uint64_t)kClientCallbackCookie);

    for (;;) {
        int n;
        uint8_t *ptr;
        uint8_t *end;

        n = cb_binder_write_read(fd,
                                 first ? writebuf : NULL,
                                 first ? (size_t)(p - writebuf) : 0,
                                 readbuf,
                                 sizeof(readbuf),
                                 first ? "callback client register call" : "callback client wait");
        first = 0;

        if (n < 0)
            return -1;

        ptr = readbuf;
        end = readbuf + n;

        while (ptr + sizeof(uint32_t) <= end) {
            uint32_t rcmd;
            memcpy(&rcmd, ptr, sizeof(rcmd));
            ptr += sizeof(rcmd);

            printf("callback client got %s 0x%08x\n", cb_cmd_name(rcmd), rcmd);

            if (rcmd == BR_NOOP || rcmd == BR_TRANSACTION_COMPLETE || rcmd == BR_SPAWN_LOOPER)
                continue;

            if (rcmd == BR_TRANSACTION) {
                struct binder_transaction_data incoming;

                if (ptr + sizeof(incoming) > end)
                    return -1;

                memcpy(&incoming, ptr, sizeof(incoming));
                ptr += sizeof(incoming);

                if (client_send_callback_reply(fd, &incoming) != 0)
                    return -1;

                continue;
            }

            if (rcmd == BR_REPLY) {
                struct binder_transaction_data reply;
                struct callback_text_payload *rp = NULL;

                if (ptr + sizeof(reply) > end)
                    return -1;

                memcpy(&reply, ptr, sizeof(reply));
                ptr += sizeof(reply);

                if (reply.data.ptr.buffer && reply.data_size >= sizeof(struct callback_text_payload))
                    rp = (struct callback_text_payload *)(uintptr_t)reply.data.ptr.buffer;

                if (!rp || rp->magic != ANDROID_LIKE_CALLBACK_MAGIC || rp->status != 0) {
                    fprintf(stderr, "callback client bad final reply\n");
                    cb_binder_free_buffer(fd, reply.data.ptr.buffer, "callback client free bad final reply");
                    return -1;
                }

                printf("callback client final reply text=%s\n", rp->text);
                cb_binder_free_buffer(fd, reply.data.ptr.buffer, "callback client free final reply");

                printf("ANDROID_LIKE_CALLBACK_REGISTER_OK\n");
                printf("ANDROID_LIKE_CALLBACK_SMOKE_OK\n");
                return 0;
            }

            if (rcmd == BR_INCREFS || rcmd == BR_ACQUIRE || rcmd == BR_RELEASE || rcmd == BR_DECREFS) {
                if (cb_handle_ref_cmd(fd, rcmd, &ptr, end, "callback client") != 0)
                    return -1;
                continue;
            }

            if (rcmd == BR_DEAD_REPLY || rcmd == BR_FAILED_REPLY) {
                fprintf(stderr, "callback client failed cmd=0x%08x\n", rcmd);
                return 1;
            }

            fprintf(stderr, "callback client unhandled cmd=0x%08x\n", rcmd);
            return -1;
        }
    }
}

int main(int argc, char **argv) {
    const char *service_name = argc > 1 ? argv[1] : "test.android.callback";
    int fd;
    uint32_t service_handle = 0;
    int rc;

    fd = cb_binder_open_and_init("callback client");
    if (fd < 0)
        return 1;

    if (cb_aosp_get_service_handle(fd, service_name, &service_handle) != 0)
        return 1;

    printf("callback client: got service handle=%u\n", service_handle);

    rc = client_register_callback(fd, service_handle);

    cb_binder_release_handle(fd, service_handle, "callback client BC_RELEASE service");

    return rc == 0 ? 0 : 1;
}
