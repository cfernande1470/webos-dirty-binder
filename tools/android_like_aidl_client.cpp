#include "android_like_aidl_common.hpp"

static int aidl_like_call_echo(int fd, uint32_t handle, const char *msg, std::string *out) {
    uint8_t parcel[1024];
    size_t parcel_size = 0;
    uint8_t writebuf[2048];
    uint8_t readbuf[8192];
    uint8_t *p = writebuf;
    uint32_t cmd;
    struct binder_transaction_data tr;
    int first = 1;

    if (!out)
        return -1;

    out->clear();

    if (aidl_like_write_interface_token(parcel, sizeof(parcel), &parcel_size) != 0 ||
        cb_parcel_write_string16_ascii(parcel, sizeof(parcel), &parcel_size, msg) != 0) {
        fprintf(stderr, "aidl-like client echo: failed to build request\n");
        return -1;
    }

    memset(&tr, 0, sizeof(tr));
    tr.target.handle = handle;
    tr.code = AIDL_LIKE_TX_ECHO;
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
                                 first ? "aidl-like client echo call" : "aidl-like client echo wait");
        first = 0;

        if (n < 0)
            return -1;

        ptr = readbuf;
        end = readbuf + n;

        while (ptr + sizeof(uint32_t) <= end) {
            uint32_t rcmd;
            memcpy(&rcmd, ptr, sizeof(rcmd));
            ptr += sizeof(rcmd);

            printf("aidl-like client echo got %s 0x%08x\n", cb_cmd_name(rcmd), rcmd);

            if (rcmd == BR_NOOP || rcmd == BR_TRANSACTION_COMPLETE || rcmd == BR_SPAWN_LOOPER)
                continue;

            if (rcmd == BR_REPLY) {
                struct binder_transaction_data reply;
                struct aidl_like_reader r;
                int32_t exception_code = 0;

                if (ptr + sizeof(reply) > end)
                    return -1;

                memcpy(&reply, ptr, sizeof(reply));
                ptr += sizeof(reply);

                memset(&r, 0, sizeof(r));
                r.data = (const uint8_t *)(uintptr_t)reply.data.ptr.buffer;
                r.size = (size_t)reply.data_size;
                r.pos = 0;

                if (aidl_like_read_i32(&r, &exception_code) != 0) {
                    fprintf(stderr, "aidl-like client echo: missing exception code\n");
                    cb_binder_free_buffer(fd, reply.data.ptr.buffer, "aidl-like client echo free bad reply");
                    return -1;
                }

                if (exception_code != 0) {
                    fprintf(stderr, "aidl-like client echo: exception_code=%d\n", exception_code);
                    cb_binder_free_buffer(fd, reply.data.ptr.buffer, "aidl-like client echo free exception reply");
                    return -1;
                }

                printf("AIDL_LIKE_EXCEPTION_CODE_OK\n");

                if (aidl_like_read_string16_ascii(&r, out) != 0) {
                    fprintf(stderr, "aidl-like client echo: bad string reply\n");
                    cb_binder_free_buffer(fd, reply.data.ptr.buffer, "aidl-like client echo free bad string reply");
                    return -1;
                }

                cb_binder_free_buffer(fd, reply.data.ptr.buffer, "aidl-like client echo free reply");
                return 0;
            }

            if (rcmd == BR_INCREFS || rcmd == BR_ACQUIRE || rcmd == BR_RELEASE || rcmd == BR_DECREFS) {
                if (cb_handle_ref_cmd(fd, rcmd, &ptr, end, "aidl-like client echo") != 0)
                    return -1;

                continue;
            }

            if (rcmd == BR_DEAD_REPLY || rcmd == BR_FAILED_REPLY) {
                fprintf(stderr, "aidl-like client echo failed cmd=0x%08x\n", rcmd);
                return 1;
            }

            fprintf(stderr, "aidl-like client echo unhandled cmd=0x%08x\n", rcmd);
            return -1;
        }
    }
}

static int aidl_like_call_add(int fd, uint32_t handle, int32_t a, int32_t b, int32_t *out_sum) {
    uint8_t parcel[1024];
    size_t parcel_size = 0;
    uint8_t writebuf[2048];
    uint8_t readbuf[8192];
    uint8_t *p = writebuf;
    uint32_t cmd;
    struct binder_transaction_data tr;
    int first = 1;

    if (!out_sum)
        return -1;

    *out_sum = 0;

    if (aidl_like_write_interface_token(parcel, sizeof(parcel), &parcel_size) != 0 ||
        cb_parcel_write_i32(parcel, sizeof(parcel), &parcel_size, a) != 0 ||
        cb_parcel_write_i32(parcel, sizeof(parcel), &parcel_size, b) != 0) {
        fprintf(stderr, "aidl-like client add: failed to build request\n");
        return -1;
    }

    memset(&tr, 0, sizeof(tr));
    tr.target.handle = handle;
    tr.code = AIDL_LIKE_TX_ADD;
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
                                 first ? "aidl-like client add call" : "aidl-like client add wait");
        first = 0;

        if (n < 0)
            return -1;

        ptr = readbuf;
        end = readbuf + n;

        while (ptr + sizeof(uint32_t) <= end) {
            uint32_t rcmd;
            memcpy(&rcmd, ptr, sizeof(rcmd));
            ptr += sizeof(rcmd);

            printf("aidl-like client add got %s 0x%08x\n", cb_cmd_name(rcmd), rcmd);

            if (rcmd == BR_NOOP || rcmd == BR_TRANSACTION_COMPLETE || rcmd == BR_SPAWN_LOOPER)
                continue;

            if (rcmd == BR_REPLY) {
                struct binder_transaction_data reply;
                struct aidl_like_reader r;
                int32_t exception_code = 0;

                if (ptr + sizeof(reply) > end)
                    return -1;

                memcpy(&reply, ptr, sizeof(reply));
                ptr += sizeof(reply);

                memset(&r, 0, sizeof(r));
                r.data = (const uint8_t *)(uintptr_t)reply.data.ptr.buffer;
                r.size = (size_t)reply.data_size;
                r.pos = 0;

                if (aidl_like_read_i32(&r, &exception_code) != 0) {
                    fprintf(stderr, "aidl-like client add: missing exception code\n");
                    cb_binder_free_buffer(fd, reply.data.ptr.buffer, "aidl-like client add free bad reply");
                    return -1;
                }

                if (exception_code != 0) {
                    fprintf(stderr, "aidl-like client add: exception_code=%d\n", exception_code);
                    cb_binder_free_buffer(fd, reply.data.ptr.buffer, "aidl-like client add free exception reply");
                    return -1;
                }

                printf("AIDL_LIKE_EXCEPTION_CODE_OK\n");

                if (aidl_like_read_i32(&r, out_sum) != 0) {
                    fprintf(stderr, "aidl-like client add: bad int reply\n");
                    cb_binder_free_buffer(fd, reply.data.ptr.buffer, "aidl-like client add free bad int reply");
                    return -1;
                }

                cb_binder_free_buffer(fd, reply.data.ptr.buffer, "aidl-like client add free reply");
                return 0;
            }

            if (rcmd == BR_INCREFS || rcmd == BR_ACQUIRE || rcmd == BR_RELEASE || rcmd == BR_DECREFS) {
                if (cb_handle_ref_cmd(fd, rcmd, &ptr, end, "aidl-like client add") != 0)
                    return -1;

                continue;
            }

            if (rcmd == BR_DEAD_REPLY || rcmd == BR_FAILED_REPLY) {
                fprintf(stderr, "aidl-like client add failed cmd=0x%08x\n", rcmd);
                return 1;
            }

            fprintf(stderr, "aidl-like client add unhandled cmd=0x%08x\n", rcmd);
            return -1;
        }
    }
}

int main(int argc, char **argv) {
    const char *service_name = argc > 1 ? argv[1] : "test.android.aidl";
    int rounds = argc > 2 ? atoi(argv[2]) : 16;
    int fd;
    uint32_t service_handle = 0;

    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);

    fd = cb_binder_open_and_init("aidl-like client");

    if (fd < 0)
        return 1;

    if (cb_aosp_get_service_handle(fd, service_name, &service_handle) != 0)
        return 1;

    printf("aidl-like client: got service handle=%u rounds=%d\n", service_handle, rounds);

    for (int i = 0; i < rounds; i++) {
        char msg[128];
        char expected[160];
        std::string echo_reply;
        int32_t sum = 0;

        snprintf(msg, sizeof(msg), "hello-%d", i);
        snprintf(expected, sizeof(expected), "echo:%s", msg);

        if (aidl_like_call_echo(fd, service_handle, msg, &echo_reply) != 0)
            return 1;

        printf("aidl-like client echo reply=%s\n", echo_reply.c_str());

        if (echo_reply != expected) {
            fprintf(stderr, "aidl-like client echo mismatch expected=%s got=%s\n",
                    expected,
                    echo_reply.c_str());
            return 1;
        }

        printf("AIDL_LIKE_ECHO_OK\n");

        if (aidl_like_call_add(fd, service_handle, i, i + 7, &sum) != 0)
            return 1;

        printf("aidl-like client add reply=%d\n", sum);

        if (sum != (i + i + 7)) {
            fprintf(stderr, "aidl-like client add mismatch expected=%d got=%d\n",
                    i + i + 7,
                    sum);
            return 1;
        }

        printf("AIDL_LIKE_ADD_OK\n");
    }

    cb_binder_release_handle(fd, service_handle, "aidl-like client BC_RELEASE service");

    printf("AIDL_LIKE_CLIENT_SMOKE_OK\n");
    return 0;
}
