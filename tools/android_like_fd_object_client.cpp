#include "android_like_fd_object_common.hpp"

static int send_fd_object_transaction(int fd, uint32_t service_handle) {
    int pipefd[2] = {-1, -1};
    uint8_t parcel[1024];
    size_t parcel_size = 0;
    binder_size_t offsets[1];
    struct dirty_binder_fd_object fdobj;
    uint8_t writebuf[2048];
    uint8_t readbuf[8192];
    uint8_t *p = writebuf;
    uint32_t cmd;
    struct binder_transaction_data tr;
    int first = 1;
    const char *payload = ANDROID_LIKE_FD_OBJECT_PAYLOAD "\n";

    if (pipe(pipefd) != 0) {
        perror("pipe");
        return -1;
    }

    if (write(pipefd[1], payload, strlen(payload)) != (ssize_t)strlen(payload)) {
        perror("write pipe payload");
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    close(pipefd[1]);
    pipefd[1] = -1;

    if (cb_parcel_write_i32(parcel, sizeof(parcel), &parcel_size, 0) != 0 ||
        cb_parcel_write_string16_ascii(parcel, sizeof(parcel), &parcel_size, ANDROID_LIKE_FD_OBJECT_DESCRIPTOR) != 0 ||
        cb_parcel_write_string16_ascii(parcel, sizeof(parcel), &parcel_size, "send fd object") != 0) {
        fprintf(stderr, "fd object client: failed to build Parcel\n");
        close(pipefd[0]);
        return -1;
    }

    parcel_size = cb_align8(parcel_size);
    memset(parcel + parcel_size, 0, sizeof(parcel) - parcel_size);

    memset(&fdobj, 0, sizeof(fdobj));
    fdobj.type = BINDER_TYPE_FD;
    fdobj.pad_flags = 0;
    fdobj.fd = (uint32_t)pipefd[0];
    fdobj.cookie = 0;

    offsets[0] = (binder_size_t)parcel_size;
    memcpy(parcel + parcel_size, &fdobj, sizeof(fdobj));
    parcel_size += sizeof(fdobj);
    parcel_size = cb_align8(parcel_size);

    memset(&tr, 0, sizeof(tr));
    tr.target.handle = service_handle;
    tr.code = ANDROID_LIKE_FD_OBJECT_SEND_FD;
    tr.flags = TF_ACCEPT_FDS;
    tr.data_size = parcel_size;
    tr.offsets_size = sizeof(offsets);
    tr.data.ptr.buffer = (binder_uintptr_t)parcel;
    tr.data.ptr.offsets = (binder_uintptr_t)offsets;

    printf("fd object client: sending BINDER_TYPE_FD local_fd=%d object_size=%zu offset=%llu flags=0\n",
           pipefd[0],
           sizeof(fdobj),
           (unsigned long long)offsets[0]);

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
                                 first ? "fd object client send fd" : "fd object client wait");
        first = 0;

        if (n < 0) {
            close(pipefd[0]);
            return -1;
        }

        ptr = readbuf;
        end = readbuf + n;

        while (ptr + sizeof(uint32_t) <= end) {
            uint32_t rcmd;
            memcpy(&rcmd, ptr, sizeof(rcmd));
            ptr += sizeof(rcmd);

            printf("fd object client got %s 0x%08x\n", cb_cmd_name(rcmd), rcmd);

            if (rcmd == BR_NOOP || rcmd == BR_TRANSACTION_COMPLETE || rcmd == BR_SPAWN_LOOPER)
                continue;

            if (rcmd == BR_REPLY) {
                struct binder_transaction_data reply;
                struct callback_text_payload *rp = NULL;

                if (ptr + sizeof(reply) > end) {
                    close(pipefd[0]);
                    return -1;
                }

                memcpy(&reply, ptr, sizeof(reply));
                ptr += sizeof(reply);

                if (reply.data.ptr.buffer && reply.data_size >= sizeof(struct callback_text_payload))
                    rp = (struct callback_text_payload *)(uintptr_t)reply.data.ptr.buffer;

                if (!rp || rp->magic != ANDROID_LIKE_CALLBACK_MAGIC || rp->status != 0) {
                    fprintf(stderr, "fd object client bad reply\n");
                    cb_binder_free_buffer(fd, reply.data.ptr.buffer, "fd object client free bad reply");
                    close(pipefd[0]);
                    return -1;
                }

                printf("fd object client final reply text=%s\n", rp->text);
                cb_binder_free_buffer(fd, reply.data.ptr.buffer, "fd object client free reply");

                close(pipefd[0]);

                printf("ANDROID_LIKE_FD_OBJECT_CLIENT_REPLY_OK\n");
                printf("ANDROID_LIKE_FD_OBJECT_SMOKE_OK\n");
                return 0;
            }

            if (rcmd == BR_INCREFS || rcmd == BR_ACQUIRE || rcmd == BR_RELEASE || rcmd == BR_DECREFS) {
                if (cb_handle_ref_cmd(fd, rcmd, &ptr, end, "fd object client") != 0) {
                    close(pipefd[0]);
                    return -1;
                }
                continue;
            }

            if (rcmd == BR_DEAD_REPLY || rcmd == BR_FAILED_REPLY) {
                fprintf(stderr, "ANDROID_LIKE_FD_OBJECT_FAILED_REPLY cmd=0x%08x\n", rcmd);
                close(pipefd[0]);
                return 2;
            }

            fprintf(stderr, "fd object client unhandled cmd=0x%08x\n", rcmd);
            close(pipefd[0]);
            return -1;
        }
    }
}

int main(int argc, char **argv) {
    const char *service_name = argc > 1 ? argv[1] : ANDROID_LIKE_FD_OBJECT_SERVICE;
    int fd;
    uint32_t service_handle = 0;
    int rc;

    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);

    fd = cb_binder_open_and_init("fd object client");
    if (fd < 0)
        return 1;

    if (cb_aosp_get_service_handle(fd, service_name, &service_handle) != 0)
        return 1;

    printf("fd object client: got service handle=%u\n", service_handle);

    rc = send_fd_object_transaction(fd, service_handle);

    cb_binder_release_handle(fd, service_handle, "fd object client BC_RELEASE service");

    return rc == 0 ? 0 : 1;
}
