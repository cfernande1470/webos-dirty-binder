#include "android_like_fd_object_common.hpp"

static const binder_uintptr_t kFdObjectServicePtr =
    (binder_uintptr_t)0x46444f424a535256ULL; /* FDOBJSRV */

static const binder_uintptr_t kFdObjectServiceCookie =
    (binder_uintptr_t)0x46444f424a303030ULL; /* FDOBJ000 */

static int process_fd_object_transaction(int fd, struct binder_transaction_data *tr) {
    int received_fd = -1;
    char buf[256];
    ssize_t n;

    printf("fd object service BR_TRANSACTION code=0x%x sender_pid=%d sender_euid=%u data_size=%llu offsets_size=%llu\n",
           tr->code,
           tr->sender_pid,
           tr->sender_euid,
           (unsigned long long)tr->data_size,
           (unsigned long long)tr->offsets_size);

    if (tr->code != ANDROID_LIKE_FD_OBJECT_SEND_FD) {
        fprintf(stderr, "fd object service unknown code=0x%x\n", tr->code);
        cb_send_text_reply(fd, tr->data.ptr.buffer, 1, "unknown fd object code", "fd object service unknown reply");
        return 0;
    }

    if (fdobj_extract_fd_from_transaction(tr, &received_fd) != 0 || received_fd < 0) {
        cb_send_text_reply(fd, tr->data.ptr.buffer, 1, "missing fd object", "fd object service missing fd reply");
        return 0;
    }

    printf("ANDROID_LIKE_FD_OBJECT_SERVICE_GOT_FD fd=%d\n", received_fd);

    memset(buf, 0, sizeof(buf));
    n = read(received_fd, buf, sizeof(buf) - 1);

    if (n < 0) {
        fprintf(stderr, "fd object service read failed errno=%d (%s)\n", errno, strerror(errno));
        close(received_fd);
        cb_send_text_reply(fd, tr->data.ptr.buffer, 1, "fd read failed", "fd object service read failed reply");
        return 0;
    }

    close(received_fd);

    printf("fd object service read n=%zd text=%s\n", n, buf);

    if (strstr(buf, ANDROID_LIKE_FD_OBJECT_PAYLOAD) == NULL) {
        fprintf(stderr, "fd object service payload mismatch\n");
        cb_send_text_reply(fd, tr->data.ptr.buffer, 1, "fd payload mismatch", "fd object service mismatch reply");
        return 0;
    }

    printf("ANDROID_LIKE_FD_OBJECT_SERVICE_READ_OK\n");

    cb_send_text_reply(fd, tr->data.ptr.buffer, 0, "fd object received and readable", "fd object service ok reply");
    return 0;
}

static int fd_object_service_loop(int fd) {
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
                                 first ? "fd object service enter looper" : "fd object service loop");
        first = 0;

        if (n < 0)
            return -1;

        ptr = readbuf;
        end = readbuf + n;

        while (ptr + sizeof(uint32_t) <= end) {
            uint32_t rcmd;
            memcpy(&rcmd, ptr, sizeof(rcmd));
            ptr += sizeof(rcmd);

            printf("fd object service got %s 0x%08x\n", cb_cmd_name(rcmd), rcmd);

            if (rcmd == BR_NOOP || rcmd == BR_TRANSACTION_COMPLETE || rcmd == BR_SPAWN_LOOPER)
                continue;

            if (rcmd == BR_TRANSACTION) {
                struct binder_transaction_data tr;

                if (ptr + sizeof(tr) > end)
                    return -1;

                memcpy(&tr, ptr, sizeof(tr));
                ptr += sizeof(tr);

                if (process_fd_object_transaction(fd, &tr) != 0)
                    return -1;

                continue;
            }

            if (rcmd == BR_INCREFS || rcmd == BR_ACQUIRE || rcmd == BR_RELEASE || rcmd == BR_DECREFS) {
                if (cb_handle_ref_cmd(fd, rcmd, &ptr, end, "fd object service") != 0)
                    return -1;
                continue;
            }

            if (rcmd == BR_DEAD_REPLY || rcmd == BR_FAILED_REPLY) {
                fprintf(stderr, "fd object service failed cmd=0x%08x\n", rcmd);
                return 1;
            }

            fprintf(stderr, "fd object service unhandled cmd=0x%08x\n", rcmd);
            return -1;
        }
    }
}

int main(int argc, char **argv) {
    const char *service_name = argc > 1 ? argv[1] : ANDROID_LIKE_FD_OBJECT_SERVICE;
    int fd;

    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);

    fd = cb_binder_open_and_init("fd object service");
    if (fd < 0)
        return 1;

    if (cb_register_local_service(fd, service_name, kFdObjectServicePtr, kFdObjectServiceCookie) != 0)
        return 1;

    printf("ANDROID_LIKE_FD_OBJECT_SERVICE_REGISTERED\n");
    fflush(stdout);

    return fd_object_service_loop(fd) == 0 ? 0 : 1;
}
