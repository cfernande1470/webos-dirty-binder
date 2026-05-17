#include "android_like_aidl_common.hpp"

#include <string>

#define FD_PASS_DESCRIPTOR "webos.dirtybinder.IFdPassing"
#define FD_PASS_TX_SEND_FD 0x46445053U /* FDPS */

static int write_token(uint8_t *buf, size_t cap, size_t *pos, const char *descriptor) {
    if (cb_parcel_write_i32(buf, cap, pos, 0) != 0)
        return -1;

    return cb_parcel_write_string16_ascii(buf, cap, pos, descriptor);
}

static int call_send_fd(
    int binder_fd,
    uint32_t service_handle,
    const char *label,
    const char *payload,
    std::string *out)
{
    int pipefd[2];
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

    if (!out)
        return -1;

    out->clear();

    if (pipe(pipefd) != 0) {
        perror("pipe");
        return -1;
    }

    if (write(pipefd[1], payload, strlen(payload)) < 0) {
        perror("write pipe");
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    close(pipefd[1]);

    if (write_token(parcel, sizeof(parcel), &parcel_size, FD_PASS_DESCRIPTOR) != 0 ||
        cb_parcel_write_string16_ascii(parcel, sizeof(parcel), &parcel_size, label) != 0) {
        close(pipefd[0]);
        return -1;
    }

    parcel_size = cb_align8(parcel_size);
    memset(parcel + parcel_size, 0, sizeof(parcel) - parcel_size);

    memset(&obj, 0, sizeof(obj));
    obj.type = BINDER_TYPE_FD;

    /*
     * Some older Binder userspaces set FLAT_BINDER_FLAG_ACCEPTS_FDS even on
     * BINDER_TYPE_FD objects. The previous obj.flags=0 variant returned
     * BR_FAILED_REPLY before the service received the transaction.
     */
    obj.flags = FLAT_BINDER_FLAG_ACCEPTS_FDS;
    obj.handle = (uint32_t)pipefd[0];
    obj.cookie = 0;

    offsets[0] = (binder_size_t)parcel_size;
    memcpy(parcel + parcel_size, &obj, sizeof(obj));
    parcel_size += sizeof(obj);
    parcel_size = cb_align8(parcel_size);

    memset(&tr, 0, sizeof(tr));
    tr.target.handle = service_handle;
    tr.code = FD_PASS_TX_SEND_FD;
    tr.flags = TF_ACCEPT_FDS;
    tr.data_size = parcel_size;
    tr.offsets_size = sizeof(offsets);
    tr.data.ptr.buffer = (binder_uintptr_t)parcel;
    tr.data.ptr.offsets = (binder_uintptr_t)offsets;

    cmd = BC_TRANSACTION;
    cb_append_u32(&p, cmd);
    cb_append_bytes(&p, &tr, sizeof(tr));

    printf("fd-passing client object: sizeof(flat_binder_object)=%zu offset=%llu type=0x%08x flags=0x%08x handle=%u cookie=0x%" PRIx64 " data_size=%zu offsets_size=%zu\n",
           sizeof(obj),
           (unsigned long long)offsets[0],
           obj.type,
           obj.flags,
           obj.handle,
           (uint64_t)obj.cookie,
           parcel_size,
           sizeof(offsets));

    printf("BINDER_FD_OBJECT_SENT label=%s payload=%s fd=%d\n",
           label,
           payload,
           pipefd[0]);
    fflush(stdout);

    for (;;) {
        int n;
        uint8_t *ptr;
        uint8_t *end;

        n = cb_binder_write_read(binder_fd,
                                 first ? writebuf : NULL,
                                 first ? (size_t)(p - writebuf) : 0,
                                 readbuf,
                                 sizeof(readbuf),
                                 first ? "fd-passing client sendFd call" : "fd-passing client sendFd wait");
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

            printf("fd-passing client got %s 0x%08x\n", cb_cmd_name(rcmd), rcmd);

            if (rcmd == BR_NOOP || rcmd == BR_TRANSACTION_COMPLETE || rcmd == BR_SPAWN_LOOPER)
                continue;

            if (rcmd == BR_REPLY) {
                struct binder_transaction_data reply;
                struct aidl_like_reader r;
                int32_t exception_code = 0;
                std::string result;

                if (ptr + sizeof(reply) > end) {
                    close(pipefd[0]);
                    return -1;
                }

                memcpy(&reply, ptr, sizeof(reply));
                ptr += sizeof(reply);

                memset(&r, 0, sizeof(r));
                r.data = (const uint8_t *)(uintptr_t)reply.data.ptr.buffer;
                r.size = (size_t)reply.data_size;
                r.pos = 0;

                if (aidl_like_read_i32(&r, &exception_code) != 0) {
                    cb_binder_free_buffer(binder_fd, reply.data.ptr.buffer, "fd-passing client free bad reply");
                    close(pipefd[0]);
                    return -1;
                }

                if (exception_code != 0) {
                    fprintf(stderr, "fd-passing client exception=%d\n", exception_code);
                    cb_binder_free_buffer(binder_fd, reply.data.ptr.buffer, "fd-passing client free exception reply");
                    close(pipefd[0]);
                    return -1;
                }

                if (aidl_like_read_string16_ascii(&r, &result) != 0) {
                    cb_binder_free_buffer(binder_fd, reply.data.ptr.buffer, "fd-passing client free bad string");
                    close(pipefd[0]);
                    return -1;
                }

                cb_binder_free_buffer(binder_fd, reply.data.ptr.buffer, "fd-passing client free reply");
                close(pipefd[0]);

                *out = result;
                return 0;
            }

            if (rcmd == BR_INCREFS || rcmd == BR_ACQUIRE || rcmd == BR_RELEASE || rcmd == BR_DECREFS) {
                if (cb_handle_ref_cmd(binder_fd, rcmd, &ptr, end, "fd-passing client") != 0) {
                    close(pipefd[0]);
                    return -1;
                }

                continue;
            }

            if (rcmd == BR_DEAD_REPLY || rcmd == BR_FAILED_REPLY) {
                close(pipefd[0]);
                return -1;
            }

            fprintf(stderr, "fd-passing client unhandled cmd=0x%08x\n", rcmd);
            close(pipefd[0]);
            return -1;
        }
    }
}

int main(int argc, char **argv) {
    const char *service_name = argc > 1 ? argv[1] : "test.android.fd";
    int rounds = argc > 2 ? atoi(argv[2]) : 16;
    int fd;
    uint32_t service_handle = 0;

    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);

    fd = cb_binder_open_and_init("fd-passing client");

    if (fd < 0)
        return 1;

    if (cb_aosp_get_service_handle(fd, service_name, &service_handle) != 0)
        return 1;

    printf("fd-passing client: service handle=%u rounds=%d\n",
           service_handle,
           rounds);

    for (int i = 0; i < rounds; i++) {
        char label[64];
        char payload[128];
        char expected[256];
        std::string reply;

        snprintf(label, sizeof(label), "round-%d", i);
        snprintf(payload, sizeof(payload), "payload-%d-from-client", i);
        snprintf(expected, sizeof(expected), "fd-ok:%s:%s", label, payload);

        if (call_send_fd(fd, service_handle, label, payload, &reply) != 0)
            return 1;

        printf("fd-passing client reply=%s\n", reply.c_str());

        if (reply != expected) {
            fprintf(stderr,
                    "fd-passing client mismatch expected=%s got=%s\n",
                    expected,
                    reply.c_str());
            return 1;
        }

        printf("BINDER_FD_CLIENT_ROUND_OK round=%d\n", i);
        fflush(stdout);
    }

    cb_binder_release_handle(fd, service_handle, "fd-passing client release service");

    printf("BINDER_FD_CLIENT_SMOKE_OK\n");
    return 0;
}
