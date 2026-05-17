#include "android_like_fd_devnull_common.hpp"

static const binder_uintptr_t kFdDevNullServicePtr =
    (binder_uintptr_t)0x46444e554c4c5356ULL; /* FDNULLSV */

static const binder_uintptr_t kFdDevNullServiceCookie =
    (binder_uintptr_t)0x46444e554c4c3030ULL; /* FDNULL00 */

static int process_fd_devnull_transaction(int binder_fd, struct binder_transaction_data *tr) {
    int received_fd = -1;
    struct stat st;
    char one;
    ssize_t n;

    printf("fd devnull service BR_TRANSACTION code=0x%x sender_pid=%d sender_euid=%u data_size=%llu offsets_size=%llu\n",
           tr->code,
           tr->sender_pid,
           tr->sender_euid,
           (unsigned long long)tr->data_size,
           (unsigned long long)tr->offsets_size);

    if (tr->code != ANDROID_LIKE_FD_DEVNULL_SEND) {
        fprintf(stderr, "fd devnull service unknown code=0x%x\n", tr->code);
        cb_send_text_reply(binder_fd, tr->data.ptr.buffer, 1, "unknown devnull code", "fd devnull unknown reply");
        return 0;
    }

    if (devnull_extract_fd_from_transaction(tr, &received_fd) != 0 || received_fd < 0) {
        cb_send_text_reply(binder_fd, tr->data.ptr.buffer, 1, "missing devnull fd", "fd devnull missing reply");
        return 0;
    }

    printf("ANDROID_LIKE_FD_DEVNULL_SERVICE_GOT_FD fd=%d\n", received_fd);

    memset(&st, 0, sizeof(st));
    if (fstat(received_fd, &st) != 0) {
        fprintf(stderr, "fd devnull service fstat failed errno=%d (%s)\n", errno, strerror(errno));
        close(received_fd);
        cb_send_text_reply(binder_fd, tr->data.ptr.buffer, 1, "devnull fstat failed", "fd devnull fstat failed reply");
        return 0;
    }

    printf("fd devnull service fstat mode=0%o rdev=%llu\n",
           (unsigned int)st.st_mode,
           (unsigned long long)st.st_rdev);

    n = read(received_fd, &one, 1);
    printf("fd devnull service read returned=%zd errno=%d (%s)\n", n, errno, strerror(errno));

    close(received_fd);

    printf("ANDROID_LIKE_FD_DEVNULL_SERVICE_FSTAT_OK\n");

    cb_send_text_reply(binder_fd, tr->data.ptr.buffer, 0, "devnull fd received", "fd devnull ok reply");
    return 0;
}

static int fd_devnull_service_loop(int fd) {
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
                                 first ? "fd devnull service enter looper" : "fd devnull service loop");
        first = 0;

        if (n < 0)
            return -1;

        ptr = readbuf;
        end = readbuf + n;

        while (ptr + sizeof(uint32_t) <= end) {
            uint32_t rcmd;
            memcpy(&rcmd, ptr, sizeof(rcmd));
            ptr += sizeof(rcmd);

            printf("fd devnull service got %s 0x%08x\n", cb_cmd_name(rcmd), rcmd);

            if (rcmd == BR_NOOP || rcmd == BR_TRANSACTION_COMPLETE || rcmd == BR_SPAWN_LOOPER)
                continue;

            if (rcmd == BR_TRANSACTION) {
                struct binder_transaction_data tr;

                if (ptr + sizeof(tr) > end)
                    return -1;

                memcpy(&tr, ptr, sizeof(tr));
                ptr += sizeof(tr);

                if (process_fd_devnull_transaction(fd, &tr) != 0)
                    return -1;

                continue;
            }

            if (rcmd == BR_INCREFS || rcmd == BR_ACQUIRE || rcmd == BR_RELEASE || rcmd == BR_DECREFS) {
                if (cb_handle_ref_cmd(fd, rcmd, &ptr, end, "fd devnull service") != 0)
                    return -1;
                continue;
            }

            if (rcmd == BR_DEAD_REPLY || rcmd == BR_FAILED_REPLY) {
                fprintf(stderr, "fd devnull service failed cmd=0x%08x\n", rcmd);
                return 1;
            }

            fprintf(stderr, "fd devnull service unhandled cmd=0x%08x\n", rcmd);
            return -1;
        }
    }
}

int main(int argc, char **argv) {
    const char *service_name = argc > 1 ? argv[1] : ANDROID_LIKE_FD_DEVNULL_SERVICE;
    int fd;

    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);

    fd = cb_binder_open_and_init("fd devnull service");
    if (fd < 0)
        return 1;

    if (cb_register_local_service(fd, service_name, kFdDevNullServicePtr, kFdDevNullServiceCookie) != 0)
        return 1;

    printf("ANDROID_LIKE_FD_DEVNULL_SERVICE_REGISTERED\n");
    fflush(stdout);

    return fd_devnull_service_loop(fd) == 0 ? 0 : 1;
}
