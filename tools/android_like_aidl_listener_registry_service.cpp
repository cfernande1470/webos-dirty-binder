#include "android_like_aidl_common.hpp"

#include <string>
#include <vector>

#define REGISTRY_SERVICE_DESCRIPTOR "webos.dirtybinder.IListenerRegistry"
#define REGISTRY_LISTENER_DESCRIPTOR "webos.dirtybinder.IRegistryListener"

#define REGISTRY_TX_REGISTER_LISTENER 1U
#define REGISTRY_TX_UNREGISTER_LISTENER 2U
#define REGISTRY_TX_ON_BROADCAST 1U
#define REGISTRY_PING 0x50494e47U

#define BC_REQUEST_DEATH_NOTIFICATION_RAW_4_4 0x400c630eU
#define BC_CLEAR_DEATH_NOTIFICATION_RAW_4_4 0x400c630fU
#define BC_DEAD_BINDER_DONE_RAW_4_4 0x40086310U
#define BR_DEAD_BINDER_RAW_4_4 0x8008720fU
#define BR_CLEAR_DEATH_NOTIFICATION_DONE_RAW_4_4 0x80087210U

static const binder_uintptr_t kRegistryServicePtr =
    (binder_uintptr_t)0x5245475352563030ULL; /* REGSRV00 */

static const binder_uintptr_t kRegistryServiceCookie =
    (binder_uintptr_t)0x524547434b303030ULL; /* REGCK000 */

struct listener_entry {
    uint32_t id;
    uint32_t handle;
    binder_uintptr_t cookie;
    int alive;
    int broadcast_ok;
    int death_ok;
};

static std::vector<listener_entry> g_listeners;
static int g_expected_listeners = 8;
static int g_broadcast_done = 0;
static int g_death_count = 0;
static int g_unregister_count = 0;

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
                "registry service: bad descriptor got=%s expected=%s\n",
                descriptor.c_str(),
                expected);
        return -1;
    }

    return 0;
}

static int request_listener_death_notification(int fd, uint32_t handle, binder_uintptr_t cookie) {
    uint8_t writebuf[64];
    uint8_t *p = writebuf;
    uint32_t cmd = BC_REQUEST_DEATH_NOTIFICATION_RAW_4_4;

    cb_append_u32(&p, cmd);
    cb_append_u32(&p, handle);
    cb_append_bytes(&p, &cookie, sizeof(cookie));

    printf("registry service: request death handle=%u cookie=0x%" PRIx64 " write_size=%zu\n",
           handle,
           (uint64_t)cookie,
           (size_t)(p - writebuf));

    return cb_binder_write_read(fd,
                                writebuf,
                                (size_t)(p - writebuf),
                                NULL,
                                0,
                                "registry service request listener death") < 0 ? -1 : 0;
}

static int clear_listener_death_notification(int fd, uint32_t handle, binder_uintptr_t cookie) {
    uint8_t writebuf[64];
    uint8_t *p = writebuf;
    uint32_t cmd = BC_CLEAR_DEATH_NOTIFICATION_RAW_4_4;

    cb_append_u32(&p, cmd);
    cb_append_u32(&p, handle);
    cb_append_bytes(&p, &cookie, sizeof(cookie));

    printf("registry service: clear death handle=%u cookie=0x%" PRIx64 " write_size=%zu\n",
           handle,
           (uint64_t)cookie,
           (size_t)(p - writebuf));

    return cb_binder_write_read(fd,
                                writebuf,
                                (size_t)(p - writebuf),
                                NULL,
                                0,
                                "registry service clear listener death") < 0 ? -1 : 0;
}

static int send_dead_binder_done(int fd, binder_uintptr_t cookie) {
    uint8_t writebuf[64];
    uint8_t *p = writebuf;
    uint32_t cmd = BC_DEAD_BINDER_DONE_RAW_4_4;

    cb_append_u32(&p, cmd);
    cb_append_bytes(&p, &cookie, sizeof(cookie));

    printf("registry service: BC_DEAD_BINDER_DONE cookie=0x%" PRIx64 "\n",
           (uint64_t)cookie);

    return cb_binder_write_read(fd,
                                writebuf,
                                (size_t)(p - writebuf),
                                NULL,
                                0,
                                "registry service dead binder done") < 0 ? -1 : 0;
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

static int call_listener_broadcast(
    int fd,
    listener_entry *listener,
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

    if (write_token(parcel, sizeof(parcel), &parcel_size, REGISTRY_LISTENER_DESCRIPTOR) != 0 ||
        cb_parcel_write_i32(parcel, sizeof(parcel), &parcel_size, (int32_t)listener->id) != 0 ||
        cb_parcel_write_string16_ascii(parcel, sizeof(parcel), &parcel_size, event_text) != 0) {
        fprintf(stderr, "registry service: failed to build broadcast parcel\n");
        return -1;
    }

    memset(&tr, 0, sizeof(tr));
    tr.target.handle = listener->handle;
    tr.code = REGISTRY_TX_ON_BROADCAST;
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
                                 first ? "registry service call listener" : "registry service wait listener");
        first = 0;

        if (n < 0)
            return -1;

        ptr = readbuf;
        end = readbuf + n;

        while (ptr + sizeof(uint32_t) <= end) {
            uint32_t rcmd;

            memcpy(&rcmd, ptr, sizeof(rcmd));
            ptr += sizeof(rcmd);

            printf("registry service listener call got %s 0x%08x\n", cb_cmd_name(rcmd), rcmd);

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
                    cb_binder_free_buffer(fd, reply.data.ptr.buffer, "registry service free bad listener reply");
                    return -1;
                }

                if (exception_code != 0) {
                    fprintf(stderr, "registry service: listener id=%u exception=%d\n",
                            listener->id,
                            exception_code);
                    cb_binder_free_buffer(fd, reply.data.ptr.buffer, "registry service free exception listener reply");
                    return -1;
                }

                if (aidl_like_read_string16_ascii(&r, &ack) != 0) {
                    cb_binder_free_buffer(fd, reply.data.ptr.buffer, "registry service free bad ack listener reply");
                    return -1;
                }

                cb_binder_free_buffer(fd, reply.data.ptr.buffer, "registry service free listener reply");

                if (out_ack)
                    *out_ack = ack;

                return 0;
            }

            if (rcmd == BR_INCREFS || rcmd == BR_ACQUIRE || rcmd == BR_RELEASE || rcmd == BR_DECREFS) {
                if (cb_handle_ref_cmd(fd, rcmd, &ptr, end, "registry service listener call") != 0)
                    return -1;

                continue;
            }

            if (rcmd == BR_DEAD_REPLY || rcmd == BR_FAILED_REPLY) {
                fprintf(stderr, "registry service: listener id=%u dead/failed cmd=0x%08x\n",
                        listener->id,
                        rcmd);
                return -1;
            }

            fprintf(stderr, "registry service: unhandled listener cmd=0x%08x\n", rcmd);
            return -1;
        }
    }
}

static int maybe_broadcast(int fd) {
    int ok_count = 0;

    if (g_broadcast_done)
        return 0;

    if ((int)g_listeners.size() < g_expected_listeners)
        return 0;

    printf("registry service: starting broadcast to %d listeners\n", g_expected_listeners);
    fflush(stdout);

    for (size_t i = 0; i < g_listeners.size(); i++) {
        char event[128];
        char expected_ack[192];
        std::string ack;

        if (!g_listeners[i].alive)
            continue;

        snprintf(event,
                 sizeof(event),
                 "registry-event-client-%u",
                 g_listeners[i].id);

        snprintf(expected_ack,
                 sizeof(expected_ack),
                 "registry-ack:%s",
                 event);

        if (call_listener_broadcast(fd, &g_listeners[i], event, &ack) != 0)
            return -1;

        printf("registry service: listener id=%u ack=%s\n",
               g_listeners[i].id,
               ack.c_str());

        if (ack != expected_ack) {
            fprintf(stderr,
                    "registry service: bad ack id=%u expected=%s got=%s\n",
                    g_listeners[i].id,
                    expected_ack,
                    ack.c_str());
            return -1;
        }

        g_listeners[i].broadcast_ok = 1;
        ok_count++;
    }

    if (ok_count != g_expected_listeners) {
        fprintf(stderr,
                "registry service: broadcast ok_count=%d expected=%d\n",
                ok_count,
                g_expected_listeners);
        return -1;
    }

    g_broadcast_done = 1;

    printf("AIDL_LIKE_LISTENER_REGISTRY_BROADCAST_OK\n");
    fflush(stdout);

    return 0;
}

static int process_register_listener(int fd, struct binder_transaction_data *tr) {
    struct aidl_like_reader r;
    uint32_t handle = 0;
    int32_t client_id = 0;
    listener_entry e;

    memset(&r, 0, sizeof(r));
    r.data = (const uint8_t *)(uintptr_t)tr->data.ptr.buffer;
    r.size = (size_t)tr->data_size;
    r.pos = 0;

    if (read_token(&r, REGISTRY_SERVICE_DESCRIPTOR) != 0) {
        if (tr->flags & TF_ONE_WAY) {
            cb_binder_free_buffer(fd, tr->data.ptr.buffer, "registry service free bad oneway");
            return -1;
        }

        return send_string_reply(fd, tr->data.ptr.buffer, -1, NULL, "registry service bad-token reply");
    }

    if (aidl_like_read_i32(&r, &client_id) != 0) {
        if (tr->flags & TF_ONE_WAY) {
            cb_binder_free_buffer(fd, tr->data.ptr.buffer, "registry service free bad-id oneway");
            return -1;
        }

        return send_string_reply(fd, tr->data.ptr.buffer, -1, NULL, "registry service bad-id reply");
    }

    handle = cb_first_handle_from_transaction(tr);

    if (!handle) {
        if (tr->flags & TF_ONE_WAY) {
            cb_binder_free_buffer(fd, tr->data.ptr.buffer, "registry service free missing-handle oneway");
            return -1;
        }

        return send_string_reply(fd, tr->data.ptr.buffer, -1, NULL, "registry service missing-handle reply");
    }

    if (cb_binder_acquire_handle(fd, handle, "registry service acquire listener") != 0)
        return -1;

    memset(&e, 0, sizeof(e));
    e.id = (uint32_t)client_id;
    e.handle = handle;
    e.cookie = ((binder_uintptr_t)0x5245474445414400ULL) | ((binder_uintptr_t)e.id & 0xffU);
    e.alive = 1;
    e.broadcast_ok = 0;
    e.death_ok = 0;

    if (request_listener_death_notification(fd, e.handle, e.cookie) != 0) {
        cb_binder_release_handle(fd, e.handle, "registry service release listener after death request failed");
        return -1;
    }

    printf("AIDL_LIKE_LISTENER_REGISTRY_DEATH_REQUESTED id=%u\n", e.id);
    printf("AIDL_LIKE_LISTENER_REGISTRY_REGISTER_OK id=%u handle=%u\n", e.id, e.handle);
    fflush(stdout);

    g_listeners.push_back(e);

    if (tr->flags & TF_ONE_WAY) {
        if (cb_binder_free_buffer(fd,
                                  tr->data.ptr.buffer,
                                  "registry service free oneway registerListener") != 0)
            return -1;
    } else {
        if (send_string_reply(fd,
                              tr->data.ptr.buffer,
                              0,
                              "listener registered",
                              "registry service registerListener reply") != 0)
            return -1;
    }

    return maybe_broadcast(fd);
}

static int process_unregister_listener(int fd, struct binder_transaction_data *tr) {
    struct aidl_like_reader r;
    int32_t client_id = 0;
    listener_entry *found = NULL;

    memset(&r, 0, sizeof(r));
    r.data = (const uint8_t *)(uintptr_t)tr->data.ptr.buffer;
    r.size = (size_t)tr->data_size;
    r.pos = 0;

    if (read_token(&r, REGISTRY_SERVICE_DESCRIPTOR) != 0) {
        return send_string_reply(fd,
                                 tr->data.ptr.buffer,
                                 -1,
                                 NULL,
                                 "registry service unregister bad-token reply");
    }

    if (aidl_like_read_i32(&r, &client_id) != 0) {
        return send_string_reply(fd,
                                 tr->data.ptr.buffer,
                                 -1,
                                 NULL,
                                 "registry service unregister bad-id reply");
    }

    for (size_t i = 0; i < g_listeners.size(); i++) {
        if (g_listeners[i].id == (uint32_t)client_id && g_listeners[i].alive) {
            found = &g_listeners[i];
            break;
        }
    }

    if (!found) {
        fprintf(stderr, "registry service: unregister missing/already-dead id=%d\n", client_id);
        return send_string_reply(fd,
                                 tr->data.ptr.buffer,
                                 -1,
                                 NULL,
                                 "registry service unregister missing reply");
    }

    if (clear_listener_death_notification(fd, found->handle, found->cookie) != 0) {
        fprintf(stderr, "registry service: clear death failed id=%u\n", found->id);
        return send_string_reply(fd,
                                 tr->data.ptr.buffer,
                                 -1,
                                 NULL,
                                 "registry service unregister clear-death failed reply");
    }

    if (cb_binder_release_handle(fd,
                                 found->handle,
                                 "registry service release unregistered listener") != 0) {
        return send_string_reply(fd,
                                 tr->data.ptr.buffer,
                                 -1,
                                 NULL,
                                 "registry service unregister release failed reply");
    }

    found->alive = 0;
    g_unregister_count++;

    printf("AIDL_LIKE_LISTENER_UNREGISTER_OK id=%u unregister_count=%d/%d\n",
           found->id,
           g_unregister_count,
           g_expected_listeners);
    fflush(stdout);

    if (tr->flags & TF_ONE_WAY) {
        if (cb_binder_free_buffer(fd,
                                  tr->data.ptr.buffer,
                                  "registry service free oneway unregister") != 0)
            return -1;
    } else {
        if (send_string_reply(fd,
                              tr->data.ptr.buffer,
                              0,
                              "listener unregistered",
                              "registry service unregister reply") != 0)
            return -1;
    }

    if (g_unregister_count >= g_expected_listeners) {
        printf("AIDL_LIKE_LISTENER_UNREGISTER_ALL_OK\n");
        fflush(stdout);
    }

    return 0;
}

static int process_transaction(int fd, struct binder_transaction_data *tr) {
    printf("registry service BR_TRANSACTION code=0x%x sender_pid=%d sender_euid=%u data_size=%llu offsets_size=%llu flags=0x%x\n",
           tr->code,
           tr->sender_pid,
           tr->sender_euid,
           (unsigned long long)tr->data_size,
           (unsigned long long)tr->offsets_size,
           tr->flags);

    if (tr->code == REGISTRY_PING) {
        return cb_send_text_reply(fd, tr->data.ptr.buffer, 0, "pong", "registry service ping reply");
    }

    if (tr->code == REGISTRY_TX_REGISTER_LISTENER)
        return process_register_listener(fd, tr);

    if (tr->code == REGISTRY_TX_UNREGISTER_LISTENER)
        return process_unregister_listener(fd, tr);

    if (tr->flags & TF_ONE_WAY) {
        cb_binder_free_buffer(fd, tr->data.ptr.buffer, "registry service free unknown oneway");
        return -1;
    }

    return send_string_reply(fd, tr->data.ptr.buffer, -1, NULL, "registry service unknown reply");
}

static int handle_dead_binder(int fd, binder_uintptr_t cookie) {
    printf("registry service: BR_DEAD_BINDER cookie=0x%" PRIx64 "\n",
           (uint64_t)cookie);

    if (send_dead_binder_done(fd, cookie) != 0)
        return -1;

    for (size_t i = 0; i < g_listeners.size(); i++) {
        if (g_listeners[i].cookie == cookie && g_listeners[i].alive) {
            g_listeners[i].alive = 0;
            g_listeners[i].death_ok = 1;
            g_death_count++;

            cb_binder_release_handle(fd,
                                     g_listeners[i].handle,
                                     "registry service release dead listener");

            printf("registry service: listener death id=%u death_count=%d/%d\n",
                   g_listeners[i].id,
                   g_death_count,
                   g_expected_listeners);
            fflush(stdout);

            break;
        }
    }

    if (g_broadcast_done && g_death_count >= g_expected_listeners) {
        printf("AIDL_LIKE_LISTENER_REGISTRY_DEATH_CLEANUP_OK\n");
        fflush(stdout);
    }

    return 0;
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
                                 first ? "registry service enter looper" : "registry service loop");
        first = 0;

        if (n < 0)
            return -1;

        ptr = readbuf;
        end = readbuf + n;

        while (ptr + sizeof(uint32_t) <= end) {
            uint32_t rcmd;

            memcpy(&rcmd, ptr, sizeof(rcmd));
            ptr += sizeof(rcmd);

            printf("registry service got %s 0x%08x\n", cb_cmd_name(rcmd), rcmd);

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

            if (rcmd == BR_CLEAR_DEATH_NOTIFICATION_DONE_RAW_4_4) {
                binder_uintptr_t cookie = 0;

                if (ptr + sizeof(cookie) > end)
                    return -1;

                memcpy(&cookie, ptr, sizeof(cookie));
                ptr += sizeof(cookie);

                printf("registry service: BR_CLEAR_DEATH_NOTIFICATION_DONE cookie=0x%" PRIx64 "\n",
                       (uint64_t)cookie);
                fflush(stdout);
                continue;
            }

            if (rcmd == BR_DEAD_BINDER_RAW_4_4) {
                binder_uintptr_t cookie = 0;

                if (ptr + sizeof(cookie) > end)
                    return -1;

                memcpy(&cookie, ptr, sizeof(cookie));
                ptr += sizeof(cookie);

                if (handle_dead_binder(fd, cookie) != 0)
                    return -1;

                continue;
            }

            if (rcmd == BR_INCREFS || rcmd == BR_ACQUIRE || rcmd == BR_RELEASE || rcmd == BR_DECREFS) {
                if (cb_handle_ref_cmd(fd, rcmd, &ptr, end, "registry service") != 0)
                    return -1;

                continue;
            }

            if (rcmd == BR_DEAD_REPLY || rcmd == BR_FAILED_REPLY) {
                fprintf(stderr, "registry service failed cmd=0x%08x\n", rcmd);
                return 1;
            }

            fprintf(stderr, "registry service unhandled cmd=0x%08x\n", rcmd);
            return -1;
        }
    }
}

int main(int argc, char **argv) {
    const char *service_name = argc > 1 ? argv[1] : "test.android.aidl.registry";
    int fd;

    if (argc > 2)
        g_expected_listeners = atoi(argv[2]);

    if (g_expected_listeners <= 0)
        g_expected_listeners = 1;

    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);

    fd = cb_binder_open_and_init("registry service");

    if (fd < 0)
        return 1;

    if (cb_register_local_service(fd, service_name, kRegistryServicePtr, kRegistryServiceCookie) != 0)
        return 1;

    printf("AIDL_LIKE_LISTENER_REGISTRY_SERVICE_REGISTERED expected=%d\n",
           g_expected_listeners);
    fflush(stdout);

    return service_loop(fd) == 0 ? 0 : 1;
}
