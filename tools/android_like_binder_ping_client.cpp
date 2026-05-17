#include "android_like_aidl_common.hpp"

static int call_android_ping_transaction(int fd, uint32_t handle) {
    uint8_t writebuf[1024];
    uint8_t readbuf[8192];
    uint8_t *p = writebuf;
    uint32_t cmd;
    struct binder_transaction_data tr;
    int first = 1;

    memset(&tr, 0, sizeof(tr));
    tr.target.handle = handle;
    tr.code = AIDL_LIKE_ANDROID_PING_TRANSACTION;
    tr.flags = TF_ACCEPT_FDS;
    tr.data_size = 0;
    tr.offsets_size = 0;
    tr.data.ptr.buffer = 0;
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
                                 first ? "binder-ping client call" : "binder-ping client wait");
        first = 0;

        if (n < 0)
            return -1;

        ptr = readbuf;
        end = readbuf + n;

        while (ptr + sizeof(uint32_t) <= end) {
            uint32_t rcmd;

            memcpy(&rcmd, ptr, sizeof(rcmd));
            ptr += sizeof(rcmd);

            printf("binder-ping client got %s 0x%08x\n", cb_cmd_name(rcmd), rcmd);

            if (rcmd == BR_NOOP || rcmd == BR_TRANSACTION_COMPLETE || rcmd == BR_SPAWN_LOOPER)
                continue;

            if (rcmd == BR_REPLY) {
                struct binder_transaction_data reply;
                struct aidl_like_reader r;
                int32_t status = -1;

                if (ptr + sizeof(reply) > end)
                    return -1;

                memcpy(&reply, ptr, sizeof(reply));
                ptr += sizeof(reply);

                memset(&r, 0, sizeof(r));
                r.data = (const uint8_t *)(uintptr_t)reply.data.ptr.buffer;
                r.size = (size_t)reply.data_size;
                r.pos = 0;

                if (aidl_like_read_i32(&r, &status) != 0) {
                    cb_binder_free_buffer(fd,
                                          reply.data.ptr.buffer,
                                          "binder-ping free bad reply");
                    return -1;
                }

                cb_binder_free_buffer(fd,
                                      reply.data.ptr.buffer,
                                      "binder-ping free reply");

                if (status != 0) {
                    fprintf(stderr, "binder-ping client: status=%d\n", status);
                    return -1;
                }

                printf("BINDER_PING_CLIENT_OK\n");
                return 0;
            }

            if (rcmd == BR_INCREFS || rcmd == BR_ACQUIRE || rcmd == BR_RELEASE || rcmd == BR_DECREFS) {
                if (cb_handle_ref_cmd(fd, rcmd, &ptr, end, "binder-ping client") != 0)
                    return -1;

                continue;
            }

            if (rcmd == BR_DEAD_REPLY || rcmd == BR_FAILED_REPLY) {
                fprintf(stderr, "binder-ping client dead/failed cmd=0x%08x\n", rcmd);
                return -1;
            }

            fprintf(stderr, "binder-ping client unhandled cmd=0x%08x\n", rcmd);
            return -1;
        }
    }
}

int main(int argc, char **argv) {
    const char *service_name = argc > 1 ? argv[1] : "test.android.aidl.ping";
    int fd;
    uint32_t service_handle = 0;

    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);

    fd = cb_binder_open_and_init("binder-ping client");

    if (fd < 0)
        return 1;

    if (cb_aosp_get_service_handle(fd, service_name, &service_handle) != 0)
        return 1;

    printf("binder-ping client: service handle=%u\n", service_handle);

    if (call_android_ping_transaction(fd, service_handle) != 0)
        return 1;

    cb_binder_release_handle(fd, service_handle, "binder-ping client release service");

    return 0;
}
