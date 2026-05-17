#include "android_like_aidl_common.hpp"

#include <string>

static int call_interface_transaction(int fd, uint32_t handle, std::string *out_descriptor) {
    uint8_t writebuf[1024];
    uint8_t readbuf[8192];
    uint8_t *p = writebuf;
    uint32_t cmd;
    struct binder_transaction_data tr;
    int first = 1;

    if (!out_descriptor)
        return -1;

    out_descriptor->clear();

    memset(&tr, 0, sizeof(tr));
    tr.target.handle = handle;
    tr.code = AIDL_LIKE_INTERFACE_TRANSACTION;
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
                                 first ? "binder-meta interface call" : "binder-meta interface wait");
        first = 0;

        if (n < 0)
            return -1;

        ptr = readbuf;
        end = readbuf + n;

        while (ptr + sizeof(uint32_t) <= end) {
            uint32_t rcmd;

            memcpy(&rcmd, ptr, sizeof(rcmd));
            ptr += sizeof(rcmd);

            printf("binder-meta client got %s 0x%08x\n", cb_cmd_name(rcmd), rcmd);

            if (rcmd == BR_NOOP || rcmd == BR_TRANSACTION_COMPLETE || rcmd == BR_SPAWN_LOOPER)
                continue;

            if (rcmd == BR_REPLY) {
                struct binder_transaction_data reply;
                struct aidl_like_reader r;
                std::string descriptor;

                if (ptr + sizeof(reply) > end)
                    return -1;

                memcpy(&reply, ptr, sizeof(reply));
                ptr += sizeof(reply);

                memset(&r, 0, sizeof(r));
                r.data = (const uint8_t *)(uintptr_t)reply.data.ptr.buffer;
                r.size = (size_t)reply.data_size;
                r.pos = 0;

                if (aidl_like_read_string16_ascii(&r, &descriptor) != 0) {
                    cb_binder_free_buffer(fd,
                                          reply.data.ptr.buffer,
                                          "binder-meta free bad descriptor reply");
                    return -1;
                }

                cb_binder_free_buffer(fd,
                                      reply.data.ptr.buffer,
                                      "binder-meta free descriptor reply");

                *out_descriptor = descriptor;
                return 0;
            }

            if (rcmd == BR_INCREFS || rcmd == BR_ACQUIRE || rcmd == BR_RELEASE || rcmd == BR_DECREFS) {
                if (cb_handle_ref_cmd(fd, rcmd, &ptr, end, "binder-meta client") != 0)
                    return -1;

                continue;
            }

            if (rcmd == BR_DEAD_REPLY || rcmd == BR_FAILED_REPLY) {
                fprintf(stderr, "binder-meta client dead/failed cmd=0x%08x\n", rcmd);
                return -1;
            }

            fprintf(stderr, "binder-meta client unhandled cmd=0x%08x\n", rcmd);
            return -1;
        }
    }
}

int main(int argc, char **argv) {
    const char *service_name = argc > 1 ? argv[1] : "test.android.aidl.meta";
    int fd;
    uint32_t service_handle = 0;
    std::string descriptor;

    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);

    fd = cb_binder_open_and_init("binder-meta client");

    if (fd < 0)
        return 1;

    if (cb_aosp_get_service_handle(fd, service_name, &service_handle) != 0)
        return 1;

    printf("binder-meta client: service handle=%u\n", service_handle);

    if (call_interface_transaction(fd, service_handle, &descriptor) != 0)
        return 1;

    printf("binder-meta client descriptor=%s\n", descriptor.c_str());

    if (descriptor != AIDL_LIKE_DESCRIPTOR) {
        fprintf(stderr,
                "binder-meta client descriptor mismatch expected=%s got=%s\n",
                AIDL_LIKE_DESCRIPTOR,
                descriptor.c_str());
        return 1;
    }

    printf("BINDER_META_DESCRIPTOR_OK\n");

    cb_binder_release_handle(fd, service_handle, "binder-meta client release service");

    printf("BINDER_META_INTERFACE_TRANSACTION_OK\n");
    return 0;
}
