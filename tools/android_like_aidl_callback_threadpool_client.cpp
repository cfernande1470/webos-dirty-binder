#include "android_like_aidl_common.hpp"

#include <atomic>
#include <pthread.h>
#include <string>

#define AIDL_CB_SERVICE_DESCRIPTOR "webos.dirtybinder.IAidlCallbackDemo"
#define AIDL_CB_LISTENER_DESCRIPTOR "webos.dirtybinder.IAidlCallbackListener"

#define AIDL_CB_TX_REGISTER_LISTENER 1U
#define AIDL_CB_TX_ON_EVENT 1U

static const binder_uintptr_t kAidlCallbackListenerPtr =
    (binder_uintptr_t)0x4149444c4c53544eULL; /* AIDLLSTN */

static const binder_uintptr_t kAidlCallbackListenerCookie =
    (binder_uintptr_t)0x4149444c4c303030ULL; /* AIDLL000 */

static int g_fd = -1;
static std::atomic<int> g_looper_ready(0);
static std::atomic<int> g_listener_seen(0);
static std::atomic<int> g_listener_ok(0);

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
                "aidl-callback client: bad descriptor got=%s expected=%s\n",
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

static int handle_listener_transaction(int fd, struct binder_transaction_data *tr) {
    struct aidl_like_reader r;
    std::string event;
    std::string ack;

    printf("aidl-callback client listener BR_TRANSACTION code=0x%x sender_pid=%d sender_euid=%u data_size=%llu flags=0x%x\n",
           tr->code,
           tr->sender_pid,
           tr->sender_euid,
           (unsigned long long)tr->data_size,
           tr->flags);

    if (tr->code != AIDL_CB_TX_ON_EVENT) {
        fprintf(stderr, "aidl-callback client listener: unknown code=0x%x\n", tr->code);
        return send_string_reply(fd, tr->data.ptr.buffer, -1, NULL, "aidl-callback client unknown listener reply");
    }

    memset(&r, 0, sizeof(r));
    r.data = (const uint8_t *)(uintptr_t)tr->data.ptr.buffer;
    r.size = (size_t)tr->data_size;
    r.pos = 0;

    if (read_token(&r, AIDL_CB_LISTENER_DESCRIPTOR) != 0 ||
        aidl_like_read_string16_ascii(&r, &event) != 0) {
        fprintf(stderr, "aidl-callback client listener: bad onEvent parcel\n");
        return send_string_reply(fd, tr->data.ptr.buffer, -1, NULL, "aidl-callback client bad listener parcel reply");
    }

    printf("aidl-callback client listener event=%s\n", event.c_str());

    if (event != "aidl-listener-event-0") {
        fprintf(stderr, "aidl-callback client listener: unexpected event=%s\n", event.c_str());
        return send_string_reply(fd, tr->data.ptr.buffer, -1, NULL, "aidl-callback client unexpected event reply");
    }

    ack = "listener-ack:" + event;

    g_listener_seen.store(1);
    g_listener_ok.store(1);

    printf("AIDL_LIKE_CALLBACK_LISTENER_THREAD_OK\n");
    fflush(stdout);

    return send_string_reply(fd,
                             tr->data.ptr.buffer,
                             0,
                             ack.c_str(),
                             "aidl-callback client listener reply");
}

static void *looper_thread_main(void *arg) {
    (void)arg;

    uint8_t writebuf[64];
    uint8_t readbuf[8192];
    uint8_t *p = writebuf;
    uint32_t cmd = BC_ENTER_LOOPER;

    cb_append_u32(&p, cmd);

    if (cb_binder_write_read(g_fd,
                             writebuf,
                             (size_t)(p - writebuf),
                             NULL,
                             0,
                             "aidl-callback client looper enter") < 0) {
        fprintf(stderr, "aidl-callback client looper failed to enter\n");
        return NULL;
    }

    g_looper_ready.store(1);
    printf("AIDL_LIKE_CALLBACK_LISTENER_LOOPER_READY\n");
    fflush(stdout);

    for (;;) {
        int n;
        uint8_t *ptr;
        uint8_t *end;

        n = cb_binder_write_read(g_fd,
                                 NULL,
                                 0,
                                 readbuf,
                                 sizeof(readbuf),
                                 "aidl-callback client looper wait");

        if (n < 0)
            return NULL;

        ptr = readbuf;
        end = readbuf + n;

        while (ptr + sizeof(uint32_t) <= end) {
            uint32_t rcmd;

            memcpy(&rcmd, ptr, sizeof(rcmd));
            ptr += sizeof(rcmd);

            printf("aidl-callback client looper got %s 0x%08x\n", cb_cmd_name(rcmd), rcmd);

            if (rcmd == BR_NOOP || rcmd == BR_TRANSACTION_COMPLETE || rcmd == BR_SPAWN_LOOPER)
                continue;

            if (rcmd == BR_TRANSACTION) {
                struct binder_transaction_data incoming;

                if (ptr + sizeof(incoming) > end)
                    return NULL;

                memcpy(&incoming, ptr, sizeof(incoming));
                ptr += sizeof(incoming);

                if (handle_listener_transaction(g_fd, &incoming) != 0)
                    return NULL;

                continue;
            }

            if (rcmd == BR_INCREFS || rcmd == BR_ACQUIRE || rcmd == BR_RELEASE || rcmd == BR_DECREFS) {
                if (cb_handle_ref_cmd(g_fd, rcmd, &ptr, end, "aidl-callback client looper") != 0)
                    return NULL;

                continue;
            }

            if (rcmd == BR_DEAD_REPLY || rcmd == BR_FAILED_REPLY) {
                fprintf(stderr, "aidl-callback client looper failed cmd=0x%08x\n", rcmd);
                return NULL;
            }

            fprintf(stderr, "aidl-callback client looper unhandled cmd=0x%08x\n", rcmd);
            return NULL;
        }
    }

    return NULL;
}

static int register_listener_oneway(int fd, uint32_t service_handle) {
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
    int transaction_complete = 0;

    if (write_token(parcel, sizeof(parcel), &parcel_size, AIDL_CB_SERVICE_DESCRIPTOR) != 0) {
        fprintf(stderr, "aidl-callback client: failed to build service token\n");
        return -1;
    }

    parcel_size = cb_align8(parcel_size);
    memset(parcel + parcel_size, 0, sizeof(parcel) - parcel_size);

    memset(&obj, 0, sizeof(obj));
    obj.type = BINDER_TYPE_BINDER;
    obj.flags = FLAT_BINDER_FLAG_ACCEPTS_FDS;
    obj.binder = kAidlCallbackListenerPtr;
    obj.cookie = kAidlCallbackListenerCookie;

    offsets[0] = (binder_size_t)parcel_size;
    memcpy(parcel + parcel_size, &obj, sizeof(obj));
    parcel_size += sizeof(obj);
    parcel_size = cb_align8(parcel_size);

    memset(&tr, 0, sizeof(tr));
    tr.target.handle = service_handle;
    tr.code = AIDL_CB_TX_REGISTER_LISTENER;
    tr.flags = TF_ACCEPT_FDS | TF_ONE_WAY;
    tr.data_size = parcel_size;
    tr.offsets_size = sizeof(offsets);
    tr.data.ptr.buffer = (binder_uintptr_t)parcel;
    tr.data.ptr.offsets = (binder_uintptr_t)offsets;

    cmd = BC_TRANSACTION;
    cb_append_u32(&p, cmd);
    cb_append_bytes(&p, &tr, sizeof(tr));

    printf("aidl-callback client main: sending listener object ptr=0x%" PRIx64 " cookie=0x%" PRIx64 "\n",
           (uint64_t)kAidlCallbackListenerPtr,
           (uint64_t)kAidlCallbackListenerCookie);

    while (!transaction_complete) {
        int n;
        uint8_t *ptr;
        uint8_t *end;

        n = cb_binder_write_read(fd,
                                 first ? writebuf : NULL,
                                 first ? (size_t)(p - writebuf) : 0,
                                 readbuf,
                                 sizeof(readbuf),
                                 first ? "aidl-callback client registerListener oneway" : "aidl-callback client main wait");
        first = 0;

        if (n < 0)
            return -1;

        ptr = readbuf;
        end = readbuf + n;

        while (ptr + sizeof(uint32_t) <= end) {
            uint32_t rcmd;

            memcpy(&rcmd, ptr, sizeof(rcmd));
            ptr += sizeof(rcmd);

            printf("aidl-callback client main got %s 0x%08x\n", cb_cmd_name(rcmd), rcmd);

            if (rcmd == BR_NOOP || rcmd == BR_SPAWN_LOOPER)
                continue;

            if (rcmd == BR_TRANSACTION_COMPLETE) {
                transaction_complete = 1;
                printf("AIDL_LIKE_CALLBACK_LISTENER_ONEWAY_REGISTER_SENT\n");
                fflush(stdout);
                continue;
            }

            if (rcmd == BR_TRANSACTION) {
                struct binder_transaction_data incoming;

                if (ptr + sizeof(incoming) > end)
                    return -1;

                memcpy(&incoming, ptr, sizeof(incoming));
                ptr += sizeof(incoming);

                fprintf(stderr,
                        "AIDL_LIKE_CALLBACK_LISTENER_MAIN_GOT_CALLBACK_FAIL code=0x%x\n",
                        incoming.code);

                send_string_reply(fd,
                                  incoming.data.ptr.buffer,
                                  -1,
                                  NULL,
                                  "aidl-callback client main unexpected listener reply");

                return -1;
            }

            if (rcmd == BR_REPLY) {
                struct binder_transaction_data reply;

                if (ptr + sizeof(reply) > end)
                    return -1;

                memcpy(&reply, ptr, sizeof(reply));
                ptr += sizeof(reply);

                cb_binder_free_buffer(fd, reply.data.ptr.buffer, "aidl-callback client free unexpected reply");

                fprintf(stderr, "aidl-callback client: unexpected BR_REPLY for one-way registerListener\n");
                return -1;
            }

            if (rcmd == BR_INCREFS || rcmd == BR_ACQUIRE || rcmd == BR_RELEASE || rcmd == BR_DECREFS) {
                if (cb_handle_ref_cmd(fd, rcmd, &ptr, end, "aidl-callback client main") != 0)
                    return -1;

                continue;
            }

            if (rcmd == BR_DEAD_REPLY || rcmd == BR_FAILED_REPLY) {
                fprintf(stderr, "aidl-callback client main failed cmd=0x%08x\n", rcmd);
                return -1;
            }

            fprintf(stderr, "aidl-callback client main unhandled cmd=0x%08x\n", rcmd);
            return -1;
        }
    }

    for (int i = 0; i < 500; i++) {
        if (g_listener_seen.load() && g_listener_ok.load()) {
            printf("AIDL_LIKE_CALLBACK_LISTENER_MAIN_OBSERVED_OK\n");
            printf("AIDL_LIKE_CALLBACK_LISTENER_SMOKE_OK\n");
            fflush(stdout);
            return 0;
        }

        usleep(10000);
    }

    fprintf(stderr, "aidl-callback client: timeout waiting for listener callback\n");
    return -1;
}

int main(int argc, char **argv) {
    const char *service_name = argc > 1 ? argv[1] : "test.android.aidl.callback";
    pthread_t looper_thread;
    uint32_t service_handle = 0;
    int rc;

    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);

    g_fd = cb_binder_open_and_init("aidl-callback client");

    if (g_fd < 0)
        return 1;

    if (pthread_create(&looper_thread, NULL, looper_thread_main, NULL) != 0) {
        perror("pthread_create");
        return 1;
    }

    for (int i = 0; i < 100; i++) {
        if (g_looper_ready.load())
            break;

        usleep(10000);
    }

    if (!g_looper_ready.load()) {
        fprintf(stderr, "aidl-callback client: looper not ready\n");
        return 1;
    }

    if (cb_aosp_get_service_handle(g_fd, service_name, &service_handle) != 0)
        return 1;

    printf("aidl-callback client main: service handle=%u\n", service_handle);

    rc = register_listener_oneway(g_fd, service_handle);

    cb_binder_release_handle(g_fd, service_handle, "aidl-callback client release service");

    return rc == 0 ? 0 : 1;
}
