#include "android_like_callback_common.hpp"

#include <atomic>
#include <pthread.h>

static const binder_uintptr_t kThreadpoolClientCallbackPtr =
    (binder_uintptr_t)0x5450434c4e544342ULL; /* TPCLNTCB */

static const binder_uintptr_t kThreadpoolClientCallbackCookie =
    (binder_uintptr_t)0x5450434c30303030ULL; /* TPCL0000 */

static int g_fd = -1;
static std::atomic<int> g_callback_seen(0);
static std::atomic<int> g_callback_ok(0);
static std::atomic<int> g_looper_ready(0);

static int threadpool_client_send_callback_reply(int fd, struct binder_transaction_data *tr) {
    struct callback_text_payload *payload = NULL;

    printf("threadpool client callback thread BR_TRANSACTION code=0x%x sender_pid=%d sender_euid=%u data_size=%llu\n",
           tr->code,
           tr->sender_pid,
           tr->sender_euid,
           (unsigned long long)tr->data_size);

    if (tr->code != ANDROID_LIKE_CALLBACK_ON_EVENT) {
        fprintf(stderr, "threadpool client callback thread unknown callback code=0x%x\n", tr->code);
        return cb_send_text_reply(fd, tr->data.ptr.buffer, 1, "unknown threadpool callback code", "threadpool callback unknown reply");
    }

    if (tr->data.ptr.buffer && tr->data_size >= sizeof(struct callback_text_payload))
        payload = (struct callback_text_payload *)(uintptr_t)tr->data.ptr.buffer;

    if (!payload || payload->magic != ANDROID_LIKE_CALLBACK_MAGIC) {
        fprintf(stderr, "threadpool client callback thread bad payload\n");
        return cb_send_text_reply(fd, tr->data.ptr.buffer, 1, "bad threadpool callback payload", "threadpool callback bad-payload reply");
    }

    printf("threadpool client callback thread event text=%s\n", payload->text);

    g_callback_seen.store(1);
    g_callback_ok.store(1);

    printf("ANDROID_LIKE_THREADPOOL_CALLBACK_THREAD_OK\n");
    fflush(stdout);

    return cb_send_text_reply(fd,
                              tr->data.ptr.buffer,
                              0,
                              "threadpool callback ack",
                              "threadpool callback event reply");
}

static void *threadpool_client_looper(void *arg) {
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
                             "threadpool client looper enter") < 0) {
        fprintf(stderr, "threadpool client looper failed to enter\n");
        return NULL;
    }

    g_looper_ready.store(1);
    printf("ANDROID_LIKE_THREADPOOL_CLIENT_LOOPER_READY\n");
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
                                 "threadpool client looper wait");

        if (n < 0)
            return NULL;

        ptr = readbuf;
        end = readbuf + n;

        while (ptr + sizeof(uint32_t) <= end) {
            uint32_t rcmd;
            memcpy(&rcmd, ptr, sizeof(rcmd));
            ptr += sizeof(rcmd);

            printf("threadpool client looper got %s 0x%08x\n", cb_cmd_name(rcmd), rcmd);

            if (rcmd == BR_NOOP || rcmd == BR_TRANSACTION_COMPLETE || rcmd == BR_SPAWN_LOOPER)
                continue;

            if (rcmd == BR_TRANSACTION) {
                struct binder_transaction_data incoming;

                if (ptr + sizeof(incoming) > end)
                    return NULL;

                memcpy(&incoming, ptr, sizeof(incoming));
                ptr += sizeof(incoming);

                if (threadpool_client_send_callback_reply(g_fd, &incoming) != 0)
                    return NULL;

                continue;
            }

            if (rcmd == BR_INCREFS || rcmd == BR_ACQUIRE || rcmd == BR_RELEASE || rcmd == BR_DECREFS) {
                if (cb_handle_ref_cmd(g_fd, rcmd, &ptr, end, "threadpool client looper") != 0)
                    return NULL;

                continue;
            }

            if (rcmd == BR_DEAD_REPLY || rcmd == BR_FAILED_REPLY) {
                fprintf(stderr, "threadpool client looper failed cmd=0x%08x\n", rcmd);
                return NULL;
            }

            fprintf(stderr, "threadpool client looper unhandled cmd=0x%08x\n", rcmd);
            return NULL;
        }
    }

    return NULL;
}

static int threadpool_client_register_callback(int fd, uint32_t service_handle) {
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

    if (cb_parcel_write_i32(parcel, sizeof(parcel), &parcel_size, 0) != 0 ||
        cb_parcel_write_string16_ascii(parcel, sizeof(parcel), &parcel_size, ANDROID_LIKE_CALLBACK_DESCRIPTOR) != 0 ||
        cb_parcel_write_string16_ascii(parcel, sizeof(parcel), &parcel_size, "register threadpool client callback one-way") != 0) {
        fprintf(stderr, "threadpool client: failed to build register Parcel\n");
        return -1;
    }

    parcel_size = cb_align8(parcel_size);
    memset(parcel + parcel_size, 0, sizeof(parcel) - parcel_size);

    memset(&obj, 0, sizeof(obj));
    obj.type = BINDER_TYPE_BINDER;
    obj.flags = FLAT_BINDER_FLAG_ACCEPTS_FDS;
    obj.binder = kThreadpoolClientCallbackPtr;
    obj.cookie = kThreadpoolClientCallbackCookie;

    offsets[0] = (binder_size_t)parcel_size;
    memcpy(parcel + parcel_size, &obj, sizeof(obj));
    parcel_size += sizeof(obj);
    parcel_size = cb_align8(parcel_size);

    memset(&tr, 0, sizeof(tr));
    tr.target.handle = service_handle;
    tr.code = ANDROID_LIKE_CALLBACK_REGISTER;
    tr.flags = TF_ACCEPT_FDS | TF_ONE_WAY;
    tr.data_size = parcel_size;
    tr.offsets_size = sizeof(offsets);
    tr.data.ptr.buffer = (binder_uintptr_t)parcel;
    tr.data.ptr.offsets = (binder_uintptr_t)offsets;

    cmd = BC_TRANSACTION;
    cb_append_u32(&p, cmd);
    cb_append_bytes(&p, &tr, sizeof(tr));

    printf("threadpool client main: sending ONE_WAY local callback object ptr=0x%" PRIx64 " cookie=0x%" PRIx64 "\n",
           (uint64_t)kThreadpoolClientCallbackPtr,
           (uint64_t)kThreadpoolClientCallbackCookie);

    while (!transaction_complete) {
        int n;
        uint8_t *ptr;
        uint8_t *end;

        n = cb_binder_write_read(fd,
                                 first ? writebuf : NULL,
                                 first ? (size_t)(p - writebuf) : 0,
                                 readbuf,
                                 sizeof(readbuf),
                                 first ? "threadpool client main oneway register call" : "threadpool client main oneway wait");
        first = 0;

        if (n < 0)
            return -1;

        ptr = readbuf;
        end = readbuf + n;

        while (ptr + sizeof(uint32_t) <= end) {
            uint32_t rcmd;
            memcpy(&rcmd, ptr, sizeof(rcmd));
            ptr += sizeof(rcmd);

            printf("threadpool client main got %s 0x%08x\n", cb_cmd_name(rcmd), rcmd);

            if (rcmd == BR_NOOP || rcmd == BR_SPAWN_LOOPER)
                continue;

            if (rcmd == BR_TRANSACTION_COMPLETE) {
                transaction_complete = 1;
                printf("ANDROID_LIKE_THREADPOOL_ONEWAY_REGISTER_SENT\n");
                fflush(stdout);
                continue;
            }

            if (rcmd == BR_TRANSACTION) {
                struct binder_transaction_data incoming;

                if (ptr + sizeof(incoming) > end)
                    return -1;

                memcpy(&incoming, ptr, sizeof(incoming));
                ptr += sizeof(incoming);

                fprintf(stderr, "ANDROID_LIKE_THREADPOOL_MAIN_GOT_CALLBACK_FAIL code=0x%x\n", incoming.code);

                cb_send_text_reply(fd,
                                   incoming.data.ptr.buffer,
                                   1,
                                   "main thread unexpectedly got callback",
                                   "threadpool main unexpected callback reply");

                return -1;
            }

            if (rcmd == BR_REPLY) {
                struct binder_transaction_data reply;

                if (ptr + sizeof(reply) > end)
                    return -1;

                memcpy(&reply, ptr, sizeof(reply));
                ptr += sizeof(reply);

                cb_binder_free_buffer(fd, reply.data.ptr.buffer, "threadpool client main unexpected reply free");

                fprintf(stderr, "threadpool client main: unexpected BR_REPLY for one-way register\n");
                return -1;
            }

            if (rcmd == BR_INCREFS || rcmd == BR_ACQUIRE || rcmd == BR_RELEASE || rcmd == BR_DECREFS) {
                if (cb_handle_ref_cmd(fd, rcmd, &ptr, end, "threadpool client main") != 0)
                    return -1;

                continue;
            }

            if (rcmd == BR_DEAD_REPLY || rcmd == BR_FAILED_REPLY) {
                fprintf(stderr, "threadpool client main failed cmd=0x%08x\n", rcmd);
                return 1;
            }

            fprintf(stderr, "threadpool client main unhandled cmd=0x%08x\n", rcmd);
            return -1;
        }
    }

    for (int i = 0; i < 500; i++) {
        if (g_callback_seen.load() && g_callback_ok.load()) {
            printf("ANDROID_LIKE_THREADPOOL_MAIN_OBSERVED_CALLBACK_OK\n");
            printf("ANDROID_LIKE_THREADPOOL_SMOKE_OK\n");
            fflush(stdout);
            return 0;
        }

        usleep(10000);
    }

    fprintf(stderr, "threadpool client main: timeout waiting for callback looper marker\n");
    return -1;
}

int main(int argc, char **argv) {
    const char *service_name = argc > 1 ? argv[1] : "test.android.callback";
    pthread_t looper_thread;
    uint32_t service_handle = 0;
    int rc;

    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);

    g_fd = cb_binder_open_and_init("threadpool client");

    if (g_fd < 0)
        return 1;

    if (pthread_create(&looper_thread, NULL, threadpool_client_looper, NULL) != 0) {
        perror("pthread_create");
        return 1;
    }

    for (int i = 0; i < 100; i++) {
        if (g_looper_ready.load())
            break;

        usleep(10000);
    }

    if (!g_looper_ready.load()) {
        fprintf(stderr, "threadpool client looper did not become ready\n");
        return 1;
    }

    if (cb_aosp_get_service_handle(g_fd, service_name, &service_handle) != 0)
        return 1;

    printf("threadpool client main: got service handle=%u\n", service_handle);

    rc = threadpool_client_register_callback(g_fd, service_handle);

    cb_binder_release_handle(g_fd, service_handle, "threadpool client main BC_RELEASE service");

    return rc == 0 ? 0 : 1;
}
