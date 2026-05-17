#include "android_like_fd_devnull_common.hpp"

static int send_devnull_fd_transaction(int fd, uint32_t service_handle) {
    int local_fd = -1;
    uint8_t parcel[1024];
    size_t parcel_size = 0;
    binder_size_t offsets[1];
    struct devnull_binder_fd_object fdobj;
    uint8_t writebuf[2048];
    uint8_t readbuf[8192];
    uint8_t *p = writebuf;
    uint32_t cmd;
    struct binder_transaction_data tr;
    int first = 1;

    local_fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
    if (local_fd < 0) {
        perror("open /dev/null");
        return -1;
    }

    if (cb_parcel_write_i32(parcel, sizeof(parcel), &parcel_size, 0) != 0 ||
        cb_parcel_write_string16_ascii(parcel, sizeof(parcel), &parcel_size, ANDROID_LIKE_FD_DEVNULL_DESCRIPTOR) != 0 ||
        cb_parcel_write_string16_ascii(parcel, sizeof(parcel), &parcel_size, "send devnull fd") != 0) {
        fprintf(stderr, "fd devnull client: failed to build Parcel\n");
        close(local_fd);
        return -1;
    }

    parcel_size = cb_align8(parcel_size);
    memset(parcel + parcel_size, 0, sizeof(parcel) - parcel_size);

    memset(&fdobj, 0, sizeof(fdobj));
    fdobj.type = BINDER_TYPE_FD;
    fdobj.pad_flags = 0;
    fdobj.fd = (uint32_t)local_fd;
    fdobj.cookie = 0;

    offsets[0] = (binder_size_t)parcel_size;
    memcpy(parcel + parcel_size, &fdobj, sizeof(fdobj));
    parcel_size += sizeof(fdobj);
    parcel_size = cb_align8(parcel_size);

    memset(&tr, 0, sizeof(tr));
    tr.target.handle = service_handle;
    tr.code = ANDROID_LIKE_FD_DEVNULL_SEND;
    tr.flags = TF_ACCEPT_FDS;
    tr.data_size = parcel_size;
    tr.offsets_size = sizeof(offsets);
    tr.data.ptr.buffer = (binder_uintptr_t)parcel;
    tr.data.ptr.offsets = (binder_uintptr_t)offsets;

    printf("fd devnull client: sending /dev/null BINDER_TYPE_FD local_fd=%d object_size=%zu offset=%llu data_size=%llu offsets_size=%llu\n",
           local_fd,
           sizeof(fdobj),
           (unsigned long long)offsets[0],
           (unsigned long long)tr.data_size,
           (unsigned long long)tr.offsets_size);

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
                                 first ? "fd devnull client send fd" : "fd devnull client wait");
        first = 0;

        if (n < 0) {
            close(local_fd);
            return -1;
        }

        ptr = readbuf;
        end = readbuf + n;

        while (ptr + sizeof(uint32_t) <= end) {
            uint32_t rcmd;
            memcpy(&rcmd, ptr, sizeof(rcmd));
            ptr += sizeof(rcmd);

            printf("fd devnull client got %s 0x%08x\n", cb_cmd_name(rcmd), rcmd);

            if (rcmd == BR_NOOP || rcmd == BR_TRANSACTION_COMPLETE || rcmd == BR_SPAWN_LOOPER)
                continue;

            if (rcmd == BR_REPLY) {
                struct binder_transaction_data reply;
                struct callback_text_payload *rp = NULL;

                if (ptr + sizeof(reply) > end) {
                    close(local_fd);
                    return -1;
                }

                memcpy(&reply, ptr, sizeof(reply));
                ptr += sizeof(reply);

                if (reply.data.ptr.buffer && reply.data_size >= sizeof(struct callback_text_payload))
                    rp = (struct callback_text_payload *)(uintptr_t)reply.data.ptr.buffer;

                if (!rp || rp->magic != ANDROID_LIKE_CALLBACK_MAGIC || rp->status != 0) {
                    fprintf(stderr, "fd devnull client bad reply\n");
                    cb_binder_free_buffer(fd, reply.data.ptr.buffer, "fd devnull client free bad reply");
                    close(local_fd);
                    return -1;
                }

                printf("fd devnull client final reply text=%s\n", rp->text);
                cb_binder_free_buffer(fd, reply.data.ptr.buffer, "fd devnull client free reply");

                close(local_fd);

                printf("ANDROID_LIKE_FD_DEVNULL_CLIENT_REPLY_OK\n");
                printf("ANDROID_LIKE_FD_DEVNULL_SMOKE_OK\n");
                return 0;
            }

            if (rcmd == BR_INCREFS || rcmd == BR_ACQUIRE || rcmd == BR_RELEASE || rcmd == BR_DECREFS) {
                if (cb_handle_ref_cmd(fd, rcmd, &ptr, end, "fd devnull client") != 0) {
                    close(local_fd);
                    return -1;
                }
                continue;
            }

            if (rcmd == BR_DEAD_REPLY || rcmd == BR_FAILED_REPLY) {
                fprintf(stderr, "ANDROID_LIKE_FD_DEVNULL_FAILED_REPLY cmd=0x%08x\n", rcmd);
                close(local_fd);
                return 2;
            }

            fprintf(stderr, "fd devnull client unhandled cmd=0x%08x\n", rcmd);
            close(local_fd);
            return -1;
        }
    }
}

int main(int argc, char **argv) {
    const char *service_name = argc > 1 ? argv[1] : ANDROID_LIKE_FD_DEVNULL_SERVICE;
    int fd;
    uint32_t service_handle = 0;
    int rc;

    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);

    fd = cb_binder_open_and_init("fd devnull client");
    if (fd < 0)
        return 1;

    if (cb_aosp_get_service_handle(fd, service_name, &service_handle) != 0)
        return 1;

    printf("fd devnull client: got service handle=%u\n", service_handle);

    rc = send_devnull_fd_transaction(fd, service_handle);

    cb_binder_release_handle(fd, service_handle, "fd devnull client BC_RELEASE service");

    return rc == 0 ? 0 : 1;
}
