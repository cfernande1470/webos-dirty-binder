#include "fd_bridge_common.hpp"

static const binder_uintptr_t kFdBridgeServicePtr =
    (binder_uintptr_t)0x4644425249444745ULL; /* FDBRIDGE */

static const binder_uintptr_t kFdBridgeServiceCookie =
    (binder_uintptr_t)0x4644425230303030ULL; /* FDBR0000 */

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t g_token = 0;
static uint32_t g_kind = 0;
static int g_fd = -1;
static char g_label[128];

static void store_fd(uint64_t token, uint32_t kind, int fd, const char *label) {
    pthread_mutex_lock(&g_lock);

    if (g_fd >= 0)
        close(g_fd);

    g_token = token;
    g_kind = kind;
    g_fd = fd;
    memset(g_label, 0, sizeof(g_label));
    snprintf(g_label, sizeof(g_label), "%s", label ? label : "");

    pthread_mutex_unlock(&g_lock);
}

static int take_fd(uint64_t token, int *out_fd, uint32_t *out_kind, char *out_label, size_t out_label_len) {
    for (int i = 0; i < 200; i++) {
        pthread_mutex_lock(&g_lock);

        if (g_fd >= 0 && g_token == token) {
            if (out_fd)
                *out_fd = g_fd;
            if (out_kind)
                *out_kind = g_kind;
            if (out_label && out_label_len)
                snprintf(out_label, out_label_len, "%s", g_label);

            g_fd = -1;
            g_token = 0;
            g_kind = 0;
            memset(g_label, 0, sizeof(g_label));

            pthread_mutex_unlock(&g_lock);
            return 0;
        }

        pthread_mutex_unlock(&g_lock);
        usleep(10000);
    }

    return -1;
}

static void *socket_thread_main(void *arg) {
    const char *path = (const char *)arg;
    int listener = fd_bridge_create_listener(path);

    if (listener < 0)
        return NULL;

    printf("FD_BRIDGE_SERVICE_SOCKET_READY path=%s\n", path);
    fflush(stdout);

    for (;;) {
        int s;
        int received_fd = -1;
        struct fd_bridge_socket_msg smsg;

        s = accept(listener, NULL, NULL);
        if (s < 0) {
            perror("accept fd bridge");
            continue;
        }

        if (fd_bridge_recv_fd(s, &smsg, &received_fd) == 0) {
            printf("fd bridge socket received token=%llu kind=%u fd=%d label=%s\n",
                   (unsigned long long)smsg.token,
                   smsg.kind,
                   received_fd,
                   smsg.label);

            store_fd(smsg.token, smsg.kind, received_fd, smsg.label);
        } else if (received_fd >= 0) {
            close(received_fd);
        }

        close(s);
    }

    return NULL;
}

static int process_control(int binder_fd, struct binder_transaction_data *tr) {
    struct fd_bridge_control_payload *payload = NULL;
    int received_fd = -1;
    uint32_t received_kind = 0;
    char received_label[128];
    char buf[256];
    ssize_t n;

    printf("fd bridge service BR_TRANSACTION code=0x%x sender_pid=%d sender_euid=%u data_size=%llu offsets_size=%llu\n",
           tr->code,
           tr->sender_pid,
           tr->sender_euid,
           (unsigned long long)tr->data_size,
           (unsigned long long)tr->offsets_size);

    if (tr->code != FD_BRIDGE_CONTROL_TRANSACTION) {
        return fd_bridge_send_control_reply(binder_fd, tr->data.ptr.buffer, 1, 0, 0,
                                            "unknown fd bridge transaction",
                                            "fd bridge unknown reply");
    }

    if (tr->data.ptr.buffer && tr->data_size >= sizeof(struct fd_bridge_control_payload))
        payload = (struct fd_bridge_control_payload *)(uintptr_t)tr->data.ptr.buffer;

    if (!payload || payload->magic != FD_BRIDGE_MAGIC) {
        return fd_bridge_send_control_reply(binder_fd, tr->data.ptr.buffer, 1, 0, 0,
                                            "bad fd bridge payload",
                                            "fd bridge bad payload reply");
    }

    printf("FD_BRIDGE_BINDER_CONTROL_OK token=%llu kind=%u text=%s\n",
           (unsigned long long)payload->token,
           payload->kind,
           payload->text);

    memset(received_label, 0, sizeof(received_label));

    if (take_fd(payload->token, &received_fd, &received_kind, received_label, sizeof(received_label)) != 0) {
        fprintf(stderr, "fd bridge service missing fd for token=%llu\n",
                (unsigned long long)payload->token);

        return fd_bridge_send_control_reply(binder_fd, tr->data.ptr.buffer, 1,
                                            payload->token, payload->kind,
                                            "missing SCM_RIGHTS fd",
                                            "fd bridge missing fd reply");
    }

    printf("FD_BRIDGE_SERVICE_GOT_FD token=%llu fd=%d kind=%u label=%s\n",
           (unsigned long long)payload->token,
           received_fd,
           received_kind,
           received_label);

    memset(buf, 0, sizeof(buf));
    n = read(received_fd, buf, sizeof(buf) - 1);
    close(received_fd);

    if (n < 0) {
        fprintf(stderr, "fd bridge service read failed errno=%d (%s)\n", errno, strerror(errno));

        return fd_bridge_send_control_reply(binder_fd, tr->data.ptr.buffer, 1,
                                            payload->token, payload->kind,
                                            "read failed",
                                            "fd bridge read failed reply");
    }

    printf("fd bridge service read n=%zd text=%s\n", n, buf);

    if (strstr(buf, FD_BRIDGE_PAYLOAD) == NULL) {
        fprintf(stderr, "fd bridge service payload mismatch\n");

        return fd_bridge_send_control_reply(binder_fd, tr->data.ptr.buffer, 1,
                                            payload->token, payload->kind,
                                            "payload mismatch",
                                            "fd bridge mismatch reply");
    }

    printf("FD_BRIDGE_SERVICE_READ_OK\n");

    return fd_bridge_send_control_reply(binder_fd, tr->data.ptr.buffer, 0,
                                        payload->token, payload->kind,
                                        "fd bridge received and read",
                                        "fd bridge ok reply");
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
                                 first ? "fd bridge service enter looper" : "fd bridge service loop");
        first = 0;

        if (n < 0)
            return -1;

        ptr = readbuf;
        end = readbuf + n;

        while (ptr + sizeof(uint32_t) <= end) {
            uint32_t rcmd;
            memcpy(&rcmd, ptr, sizeof(rcmd));
            ptr += sizeof(rcmd);

            printf("fd bridge service got %s 0x%08x\n", cb_cmd_name(rcmd), rcmd);

            if (rcmd == BR_NOOP || rcmd == BR_TRANSACTION_COMPLETE || rcmd == BR_SPAWN_LOOPER)
                continue;

            if (rcmd == BR_TRANSACTION) {
                struct binder_transaction_data tr;

                if (ptr + sizeof(tr) > end)
                    return -1;

                memcpy(&tr, ptr, sizeof(tr));
                ptr += sizeof(tr);

                if (process_control(fd, &tr) != 0)
                    return -1;

                continue;
            }

            if (rcmd == BR_INCREFS || rcmd == BR_ACQUIRE || rcmd == BR_RELEASE || rcmd == BR_DECREFS) {
                if (cb_handle_ref_cmd(fd, rcmd, &ptr, end, "fd bridge service") != 0)
                    return -1;
                continue;
            }

            if (rcmd == BR_DEAD_REPLY || rcmd == BR_FAILED_REPLY) {
                fprintf(stderr, "fd bridge service failed cmd=0x%08x\n", rcmd);
                return 1;
            }

            fprintf(stderr, "fd bridge service unhandled cmd=0x%08x\n", rcmd);
            return -1;
        }
    }
}

int main(int argc, char **argv) {
    const char *service_name = argc > 1 ? argv[1] : FD_BRIDGE_SERVICE_NAME;
    const char *socket_path = argc > 2 ? argv[2] : FD_BRIDGE_SOCKET_PATH;
    pthread_t socket_thread;
    int fd;

    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);

    if (pthread_create(&socket_thread, NULL, socket_thread_main, (void *)socket_path) != 0) {
        perror("pthread_create socket thread");
        return 1;
    }

    pthread_detach(socket_thread);
    usleep(200000);

    fd = cb_binder_open_and_init("fd bridge service");
    if (fd < 0)
        return 1;

    if (cb_register_local_service(fd, service_name, kFdBridgeServicePtr, kFdBridgeServiceCookie) != 0)
        return 1;

    printf("FD_BRIDGE_SERVICE_REGISTERED name=%s socket=%s\n", service_name, socket_path);
    fflush(stdout);

    return service_loop(fd) == 0 ? 0 : 1;
}
