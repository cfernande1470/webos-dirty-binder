#include "android_like_callback_common.hpp"

static const binder_uintptr_t kCallbackServicePtr =
    (binder_uintptr_t)0x43414c4c4241434bULL; /* CALLBACK */
static const binder_uintptr_t kCallbackServiceCookie =
    (binder_uintptr_t)0x5345525643424b30ULL; /* SERVCBK0 */

static int process_register_callback(int fd, struct binder_transaction_data *tr) {
    uint32_t callback_handle;
    struct callback_text_payload callback_reply;

    printf("callback service BR_TRANSACTION code=0x%x sender_pid=%d sender_euid=%u data_size=%llu offsets_size=%llu\n",
           tr->code,
           tr->sender_pid,
           tr->sender_euid,
           (unsigned long long)tr->data_size,
           (unsigned long long)tr->offsets_size);

    callback_handle = cb_first_handle_from_transaction(tr);

    if (!callback_handle) {
        fprintf(stderr, "callback service: no callback handle in transaction\n");
        cb_send_text_reply(fd, tr->data.ptr.buffer, 1, "missing callback handle", "callback service missing-handle reply");
        return 0;
    }

    printf("callback service: received callback handle=%u\n", callback_handle);
    printf("ANDROID_LIKE_CALLBACK_HANDLE_OK\n");
    fflush(stdout);

    if (cb_binder_acquire_handle(fd, callback_handle, "callback service BC_ACQUIRE callback") != 0) {
        cb_send_text_reply(fd, tr->data.ptr.buffer, 1, "failed to acquire callback", "callback service acquire-fail reply");
        return -1;
    }

    memset(&callback_reply, 0, sizeof(callback_reply));

    if (cb_call_text_handle(fd,
                            callback_handle,
                            ANDROID_LIKE_CALLBACK_ON_EVENT,
                            "event from service to client callback",
                            &callback_reply,
                            "callback service -> client callback transact") != 0) {
        cb_binder_release_handle(fd, callback_handle, "callback service BC_RELEASE callback after failed call");
        cb_send_text_reply(fd, tr->data.ptr.buffer, 1, "callback transact failed", "callback service transact-fail reply");
        return -1;
    }

    printf("callback service: callback reply magic=0x%08x status=%u text=%s\n",
           callback_reply.magic,
           callback_reply.status,
           callback_reply.text);

    if (callback_reply.magic != ANDROID_LIKE_CALLBACK_MAGIC || callback_reply.status != 0) {
        cb_binder_release_handle(fd, callback_handle, "callback service BC_RELEASE callback after bad reply");
        cb_send_text_reply(fd, tr->data.ptr.buffer, 1, "bad callback reply", "callback service bad-reply reply");
        return -1;
    }

    printf("ANDROID_LIKE_CALLBACK_REPLY_OK\n");
    fflush(stdout);

    if (cb_binder_release_handle(fd, callback_handle, "callback service BC_RELEASE callback") != 0)
        return -1;

    if (cb_send_text_reply(fd,
                           tr->data.ptr.buffer,
                           0,
                           "callback registered and invoked",
                           "callback service register reply") != 0)
        return -1;

    printf("ANDROID_LIKE_CALLBACK_SERVICE_OK\n");
    fflush(stdout);
    return 0;
}

static int callback_service_loop(int fd) {
    uint8_t writebuf[64];
    uint8_t readbuf[8192];
    uint8_t *p = writebuf;
    uint32_t cmd = BC_ENTER_LOOPER;
    int first = 1;

    cb_append_u32(&p, cmd);
    printf("callback service enter looper\n");

    for (;;) {
        int n;
        uint8_t *ptr;
        uint8_t *end;

        n = cb_binder_write_read(fd,
                                 first ? writebuf : NULL,
                                 first ? (size_t)(p - writebuf) : 0,
                                 readbuf,
                                 sizeof(readbuf),
                                 first ? "callback service enter looper" : "callback service loop");
        first = 0;

        if (n < 0)
            return -1;

        ptr = readbuf;
        end = readbuf + n;

        while (ptr + sizeof(uint32_t) <= end) {
            uint32_t rcmd;
            memcpy(&rcmd, ptr, sizeof(rcmd));
            ptr += sizeof(rcmd);

            printf("callback service got %s 0x%08x\n", cb_cmd_name(rcmd), rcmd);

            if (rcmd == BR_NOOP || rcmd == BR_TRANSACTION_COMPLETE || rcmd == BR_SPAWN_LOOPER)
                continue;

            if (rcmd == BR_TRANSACTION) {
                struct binder_transaction_data tr;

                if (ptr + sizeof(tr) > end)
                    return -1;

                memcpy(&tr, ptr, sizeof(tr));
                ptr += sizeof(tr);

                if (tr.code == ANDROID_LIKE_CALLBACK_REGISTER) {
                    if (process_register_callback(fd, &tr) != 0)
                        return -1;
                    continue;
                }

                if (tr.code == 0x50494e47U) { /* PING */
                    printf("callback service: handled PING\n");
                    fflush(stdout);
                    cb_send_text_reply(fd, tr.data.ptr.buffer, 0, "pong", "callback service ping reply");
                    continue;
                }

                fprintf(stderr, "callback service unknown code=0x%x\n", tr.code);
                cb_send_text_reply(fd, tr.data.ptr.buffer, 1, "unknown callback service code", "callback service unknown-code reply");
                continue;
            }

            if (rcmd == BR_INCREFS || rcmd == BR_ACQUIRE || rcmd == BR_RELEASE || rcmd == BR_DECREFS) {
                if (cb_handle_ref_cmd(fd, rcmd, &ptr, end, "callback service") != 0)
                    return -1;
                continue;
            }

            if (rcmd == BR_DEAD_REPLY || rcmd == BR_FAILED_REPLY) {
                fprintf(stderr, "callback service failed cmd=0x%08x\n", rcmd);
                return 1;
            }

            fprintf(stderr, "callback service unhandled cmd=0x%08x\n", rcmd);
            return -1;
        }
    }
}

int main(int argc, char **argv) {
    const char *service_name = argc > 1 ? argv[1] : "test.android.callback";
    int fd;

    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);

    fd = cb_binder_open_and_init("callback service");
    if (fd < 0)
        return 1;

    if (cb_register_local_service(fd, service_name, kCallbackServicePtr, kCallbackServiceCookie) != 0)
        return 1;

    printf("ANDROID_LIKE_CALLBACK_SERVICE_REGISTERED\n");
    fflush(stdout);

    return callback_service_loop(fd) == 0 ? 0 : 1;
}
