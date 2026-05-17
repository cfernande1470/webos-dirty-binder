#include "android_like_aidl_common.hpp"

#include <atomic>
#include <pthread.h>
#include <string>

#define REGISTRY_SERVICE_DESCRIPTOR "webos.dirtybinder.IListenerRegistry"
#define REGISTRY_LISTENER_DESCRIPTOR "webos.dirtybinder.IRegistryListener"

#define REGISTRY_TX_REGISTER_LISTENER 1U
#define REGISTRY_TX_UNREGISTER_LISTENER 2U
#define REGISTRY_TX_ON_BROADCAST 1U

static int g_client_id = 0;
static int g_fd = -1;
static std::atomic<int> g_looper_ready(0);
static std::atomic<int> g_callback_seen(0);
static std::atomic<int> g_callback_ok(0);

static binder_uintptr_t listener_ptr_for_id(int id) {
    return ((binder_uintptr_t)0x5245474c53540000ULL) | ((binder_uintptr_t)id & 0xffffU);
}

static binder_uintptr_t listener_cookie_for_id(int id) {
    return ((binder_uintptr_t)0x524547434c4e0000ULL) | ((binder_uintptr_t)id & 0xffffU);
}

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
                "registry client: bad descriptor got=%s expected=%s\n",
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

static int handle_broadcast_transaction(int fd, struct binder_transaction_data *tr) {
    struct aidl_like_reader r;
    int32_t target_id = 0;
    std::string event;
    std::string expected_event;
    std::string ack;

    printf("registry client listener BR_TRANSACTION code=0x%x sender_pid=%d sender_euid=%u data_size=%llu flags=0x%x\n",
           tr->code,
           tr->sender_pid,
           tr->sender_euid,
           (unsigned long long)tr->data_size,
           tr->flags);

    if (tr->code != REGISTRY_TX_ON_BROADCAST) {
        return send_string_reply(fd, tr->data.ptr.buffer, -1, NULL, "registry client unknown listener reply");
    }

    memset(&r, 0, sizeof(r));
    r.data = (const uint8_t *)(uintptr_t)tr->data.ptr.buffer;
    r.size = (size_t)tr->data_size;
    r.pos = 0;

    if (read_token(&r, REGISTRY_LISTENER_DESCRIPTOR) != 0 ||
        aidl_like_read_i32(&r, &target_id) != 0 ||
        aidl_like_read_string16_ascii(&r, &event) != 0) {
        return send_string_reply(fd, tr->data.ptr.buffer, -1, NULL, "registry client bad listener parcel reply");
    }

    expected_event = "registry-event-client-" + std::to_string(g_client_id);

    if (target_id != g_client_id || event != expected_event) {
        fprintf(stderr,
                "registry client: unexpected broadcast target=%d self=%d event=%s expected=%s\n",
                target_id,
                g_client_id,
                event.c_str(),
                expected_event.c_str());
        return send_string_reply(fd, tr->data.ptr.buffer, -1, NULL, "registry client unexpected broadcast reply");
    }

    ack = "registry-ack:" + event;

    g_callback_seen.store(1);
    g_callback_ok.store(1);

    printf("AIDL_LIKE_LISTENER_REGISTRY_THREAD_OK id=%d\n", g_client_id);
    fflush(stdout);

    return send_string_reply(fd,
                             tr->data.ptr.buffer,
                             0,
                             ack.c_str(),
                             "registry client listener reply");
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
                             "registry client looper enter") < 0) {
        return NULL;
    }

    g_looper_ready.store(1);
    printf("AIDL_LIKE_LISTENER_REGISTRY_LOOPER_READY id=%d\n", g_client_id);
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
                                 "registry client looper wait");

        if (n < 0)
            return NULL;

        ptr = readbuf;
        end = readbuf + n;

        while (ptr + sizeof(uint32_t) <= end) {
            uint32_t rcmd;

            memcpy(&rcmd, ptr, sizeof(rcmd));
            ptr += sizeof(rcmd);

            printf("registry client looper got %s 0x%08x\n", cb_cmd_name(rcmd), rcmd);

            if (rcmd == BR_NOOP || rcmd == BR_TRANSACTION_COMPLETE || rcmd == BR_SPAWN_LOOPER)
                continue;

            if (rcmd == BR_TRANSACTION) {
                struct binder_transaction_data incoming;

                if (ptr + sizeof(incoming) > end)
                    return NULL;

                memcpy(&incoming, ptr, sizeof(incoming));
                ptr += sizeof(incoming);

                if (handle_broadcast_transaction(g_fd, &incoming) != 0)
                    return NULL;

                continue;
            }

            if (rcmd == BR_INCREFS || rcmd == BR_ACQUIRE || rcmd == BR_RELEASE || rcmd == BR_DECREFS) {
                if (cb_handle_ref_cmd(g_fd, rcmd, &ptr, end, "registry client looper") != 0)
                    return NULL;

                continue;
            }

            if (rcmd == BR_DEAD_REPLY || rcmd == BR_FAILED_REPLY)
                return NULL;

            fprintf(stderr, "registry client looper unhandled cmd=0x%08x\n", rcmd);
            return NULL;
        }
    }

    return NULL;
}

static int unregister_listener_sync(int fd, uint32_t service_handle) {
    uint8_t parcel[512];
    size_t parcel_size = 0;
    uint8_t writebuf[1024];
    uint8_t readbuf[8192];
    uint8_t *p = writebuf;
    uint32_t cmd;
    struct binder_transaction_data tr;
    int first = 1;

    if (write_token(parcel, sizeof(parcel), &parcel_size, REGISTRY_SERVICE_DESCRIPTOR) != 0 ||
        cb_parcel_write_i32(parcel, sizeof(parcel), &parcel_size, (int32_t)g_client_id) != 0) {
        fprintf(stderr, "registry client: failed to build unregister request id=%d\n", g_client_id);
        return -1;
    }

    memset(&tr, 0, sizeof(tr));
    tr.target.handle = service_handle;
    tr.code = REGISTRY_TX_UNREGISTER_LISTENER;
    tr.flags = TF_ACCEPT_FDS;
    tr.data_size = parcel_size;
    tr.offsets_size = 0;
    tr.data.ptr.buffer = (binder_uintptr_t)parcel;
    tr.data.ptr.offsets = 0;

    cmd = BC_TRANSACTION;
    cb_append_u32(&p, cmd);
    cb_append_bytes(&p, &tr, sizeof(tr));

    printf("AIDL_LIKE_LISTENER_UNREGISTER_REQUEST_SENT id=%d\n", g_client_id);
    fflush(stdout);

    for (;;) {
        int n;
        uint8_t *ptr;
        uint8_t *end;

        n = cb_binder_write_read(fd,
                                 first ? writebuf : NULL,
                                 first ? (size_t)(p - writebuf) : 0,
                                 readbuf,
                                 sizeof(readbuf),
                                 first ? "registry client unregister call" : "registry client unregister wait");
        first = 0;

        if (n < 0)
            return -1;

        ptr = readbuf;
        end = readbuf + n;

        while (ptr + sizeof(uint32_t) <= end) {
            uint32_t rcmd;

            memcpy(&rcmd, ptr, sizeof(rcmd));
            ptr += sizeof(rcmd);

            printf("registry client unregister got %s 0x%08x\n", cb_cmd_name(rcmd), rcmd);

            if (rcmd == BR_NOOP || rcmd == BR_TRANSACTION_COMPLETE || rcmd == BR_SPAWN_LOOPER)
                continue;

            if (rcmd == BR_REPLY) {
                struct binder_transaction_data reply;
                struct aidl_like_reader r;
                int32_t exception_code = 0;
                std::string msg;

                if (ptr + sizeof(reply) > end)
                    return -1;

                memcpy(&reply, ptr, sizeof(reply));
                ptr += sizeof(reply);

                memset(&r, 0, sizeof(r));
                r.data = (const uint8_t *)(uintptr_t)reply.data.ptr.buffer;
                r.size = (size_t)reply.data_size;
                r.pos = 0;

                if (aidl_like_read_i32(&r, &exception_code) != 0) {
                    cb_binder_free_buffer(fd, reply.data.ptr.buffer, "registry client unregister free bad reply");
                    return -1;
                }

                if (exception_code != 0) {
                    fprintf(stderr, "registry client: unregister exception=%d id=%d\n",
                            exception_code,
                            g_client_id);
                    cb_binder_free_buffer(fd, reply.data.ptr.buffer, "registry client unregister free exception reply");
                    return -1;
                }

                if (aidl_like_read_string16_ascii(&r, &msg) != 0) {
                    cb_binder_free_buffer(fd, reply.data.ptr.buffer, "registry client unregister free bad string reply");
                    return -1;
                }

                cb_binder_free_buffer(fd, reply.data.ptr.buffer, "registry client unregister free reply");

                printf("AIDL_LIKE_LISTENER_UNREGISTER_REPLY_OK id=%d msg=%s\n",
                       g_client_id,
                       msg.c_str());
                fflush(stdout);
                return 0;
            }

            if (rcmd == BR_INCREFS || rcmd == BR_ACQUIRE || rcmd == BR_RELEASE || rcmd == BR_DECREFS) {
                if (cb_handle_ref_cmd(fd, rcmd, &ptr, end, "registry client unregister") != 0)
                    return -1;

                continue;
            }

            if (rcmd == BR_DEAD_REPLY || rcmd == BR_FAILED_REPLY)
                return -1;

            fprintf(stderr, "registry client unregister unhandled cmd=0x%08x\n", rcmd);
            return -1;
        }
    }
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

    if (write_token(parcel, sizeof(parcel), &parcel_size, REGISTRY_SERVICE_DESCRIPTOR) != 0 ||
        cb_parcel_write_i32(parcel, sizeof(parcel), &parcel_size, (int32_t)g_client_id) != 0) {
        return -1;
    }

    parcel_size = cb_align8(parcel_size);
    memset(parcel + parcel_size, 0, sizeof(parcel) - parcel_size);

    memset(&obj, 0, sizeof(obj));
    obj.type = BINDER_TYPE_BINDER;
    obj.flags = FLAT_BINDER_FLAG_ACCEPTS_FDS;
    obj.binder = listener_ptr_for_id(g_client_id);
    obj.cookie = listener_cookie_for_id(g_client_id);

    offsets[0] = (binder_size_t)parcel_size;
    memcpy(parcel + parcel_size, &obj, sizeof(obj));
    parcel_size += sizeof(obj);
    parcel_size = cb_align8(parcel_size);

    memset(&tr, 0, sizeof(tr));
    tr.target.handle = service_handle;
    tr.code = REGISTRY_TX_REGISTER_LISTENER;
    tr.flags = TF_ACCEPT_FDS | TF_ONE_WAY;
    tr.data_size = parcel_size;
    tr.offsets_size = sizeof(offsets);
    tr.data.ptr.buffer = (binder_uintptr_t)parcel;
    tr.data.ptr.offsets = (binder_uintptr_t)offsets;

    cmd = BC_TRANSACTION;
    cb_append_u32(&p, cmd);
    cb_append_bytes(&p, &tr, sizeof(tr));

    printf("registry client main: id=%d listener ptr=0x%" PRIx64 " cookie=0x%" PRIx64 "\n",
           g_client_id,
           (uint64_t)obj.binder,
           (uint64_t)obj.cookie);

    while (!transaction_complete) {
        int n;
        uint8_t *ptr;
        uint8_t *end;

        n = cb_binder_write_read(fd,
                                 first ? writebuf : NULL,
                                 first ? (size_t)(p - writebuf) : 0,
                                 readbuf,
                                 sizeof(readbuf),
                                 first ? "registry client register oneway" : "registry client main wait");
        first = 0;

        if (n < 0)
            return -1;

        ptr = readbuf;
        end = readbuf + n;

        while (ptr + sizeof(uint32_t) <= end) {
            uint32_t rcmd;

            memcpy(&rcmd, ptr, sizeof(rcmd));
            ptr += sizeof(rcmd);

            printf("registry client main got %s 0x%08x\n", cb_cmd_name(rcmd), rcmd);

            if (rcmd == BR_NOOP || rcmd == BR_SPAWN_LOOPER)
                continue;

            if (rcmd == BR_TRANSACTION_COMPLETE) {
                transaction_complete = 1;
                printf("AIDL_LIKE_LISTENER_REGISTRY_REGISTER_SENT id=%d\n", g_client_id);
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
                        "AIDL_LIKE_LISTENER_REGISTRY_MAIN_GOT_CALLBACK_FAIL id=%d code=0x%x\n",
                        g_client_id,
                        incoming.code);

                send_string_reply(fd,
                                  incoming.data.ptr.buffer,
                                  -1,
                                  NULL,
                                  "registry client main unexpected callback reply");

                return -1;
            }

            if (rcmd == BR_INCREFS || rcmd == BR_ACQUIRE || rcmd == BR_RELEASE || rcmd == BR_DECREFS) {
                if (cb_handle_ref_cmd(fd, rcmd, &ptr, end, "registry client main") != 0)
                    return -1;

                continue;
            }

            if (rcmd == BR_DEAD_REPLY || rcmd == BR_FAILED_REPLY)
                return -1;

            fprintf(stderr, "registry client main unhandled cmd=0x%08x\n", rcmd);
            return -1;
        }
    }

    for (int i = 0; i < 1000; i++) {
        if (g_callback_seen.load() && g_callback_ok.load()) {
            printf("AIDL_LIKE_LISTENER_REGISTRY_CLIENT_BROADCAST_OK id=%d\n", g_client_id);
            printf("AIDL_LIKE_LISTENER_REGISTRY_CLIENT_SMOKE_OK id=%d\n", g_client_id);
            fflush(stdout);
            return 0;
        }

        usleep(10000);
    }

    fprintf(stderr, "registry client: timeout waiting for broadcast id=%d\n", g_client_id);
    return -1;
}

int main(int argc, char **argv) {
    const char *service_name = argc > 1 ? argv[1] : "test.android.aidl.registry";
    pthread_t looper_thread;
    uint32_t service_handle = 0;
    int rc;
    int unregister_mode = 0;

    if (argc > 2)
        g_client_id = atoi(argv[2]);

    if (argc > 3 && strcmp(argv[3], "--unregister") == 0)
        unregister_mode = 1;

    if (g_client_id <= 0)
        g_client_id = 1;

    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);

    g_fd = cb_binder_open_and_init("registry client");

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

    if (!g_looper_ready.load())
        return 1;

    if (cb_aosp_get_service_handle(g_fd, service_name, &service_handle) != 0)
        return 1;

    printf("registry client main: id=%d service handle=%u\n", g_client_id, service_handle);

    rc = register_listener_oneway(g_fd, service_handle);

    if (rc == 0 && unregister_mode) {
        if (unregister_listener_sync(g_fd, service_handle) != 0)
            rc = 1;
        else {
            printf("AIDL_LIKE_LISTENER_UNREGISTER_CLIENT_SMOKE_OK id=%d\n", g_client_id);
            fflush(stdout);
        }
    }

    cb_binder_release_handle(g_fd, service_handle, "registry client release service");

    return rc == 0 ? 0 : 1;
}
