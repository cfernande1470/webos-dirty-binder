#include "android_like_aidl_common.hpp"

#include <string>

#define NEGATIVE_UNKNOWN_CODE 0x7fffff01U

static int negative_call_raw(
    int fd,
    uint32_t handle,
    uint32_t code,
    const uint8_t *parcel,
    size_t parcel_size,
    int32_t *out_exception,
    std::string *out_string,
    int32_t *out_i32,
    const char *tag)
{
    uint8_t writebuf[2048];
    uint8_t readbuf[8192];
    uint8_t *p = writebuf;
    uint32_t cmd;
    struct binder_transaction_data tr;
    int first = 1;

    if (out_exception)
        *out_exception = 0;

    if (out_string)
        out_string->clear();

    if (out_i32)
        *out_i32 = 0;

    memset(&tr, 0, sizeof(tr));
    tr.target.handle = handle;
    tr.code = code;
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
                                 first ? tag : "negative client wait");
        first = 0;

        if (n < 0)
            return -1;

        ptr = readbuf;
        end = readbuf + n;

        while (ptr + sizeof(uint32_t) <= end) {
            uint32_t rcmd;

            memcpy(&rcmd, ptr, sizeof(rcmd));
            ptr += sizeof(rcmd);

            printf("%s got %s 0x%08x\n", tag, cb_cmd_name(rcmd), rcmd);

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
                    cb_binder_free_buffer(fd, reply.data.ptr.buffer, "negative client free missing exception reply");
                    return -1;
                }

                if (out_exception)
                    *out_exception = exception_code;

                if (exception_code == 0 && out_string) {
                    if (aidl_like_read_string16_ascii(&r, out_string) != 0) {
                        cb_binder_free_buffer(fd, reply.data.ptr.buffer, "negative client free bad string reply");
                        return -1;
                    }
                }

                if (exception_code == 0 && out_i32) {
                    if (aidl_like_read_i32(&r, out_i32) != 0) {
                        cb_binder_free_buffer(fd, reply.data.ptr.buffer, "negative client free bad int reply");
                        return -1;
                    }
                }

                cb_binder_free_buffer(fd, reply.data.ptr.buffer, "negative client free reply");
                return 0;
            }

            if (rcmd == BR_INCREFS || rcmd == BR_ACQUIRE || rcmd == BR_RELEASE || rcmd == BR_DECREFS) {
                if (cb_handle_ref_cmd(fd, rcmd, &ptr, end, tag) != 0)
                    return -1;

                continue;
            }

            if (rcmd == BR_DEAD_REPLY || rcmd == BR_FAILED_REPLY) {
                fprintf(stderr, "%s dead/failed cmd=0x%08x\n", tag, rcmd);
                return -1;
            }

            fprintf(stderr, "%s unhandled cmd=0x%08x\n", tag, rcmd);
            return -1;
        }
    }
}

static int build_token_parcel(
    uint8_t *parcel,
    size_t cap,
    size_t *pos,
    const char *descriptor)
{
    if (cb_parcel_write_i32(parcel, cap, pos, 0) != 0)
        return -1;

    return cb_parcel_write_string16_ascii(parcel, cap, pos, descriptor);
}

static int expect_nonzero_exception(
    int fd,
    uint32_t handle,
    uint32_t code,
    const uint8_t *parcel,
    size_t parcel_size,
    const char *marker,
    const char *tag)
{
    int32_t exception_code = 0;

    if (negative_call_raw(fd,
                          handle,
                          code,
                          parcel,
                          parcel_size,
                          &exception_code,
                          NULL,
                          NULL,
                          tag) != 0)
        return -1;

    printf("%s exception_code=%d\n", tag, exception_code);

    if (exception_code == 0) {
        fprintf(stderr, "%s expected non-zero exception\n", tag);
        return -1;
    }

    printf("%s\n", marker);
    fflush(stdout);
    return 0;
}

int main(int argc, char **argv) {
    const char *service_name = argc > 1 ? argv[1] : "test.android.aidl.negative";
    int fd;
    uint32_t service_handle = 0;

    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);

    fd = cb_binder_open_and_init("negative client");

    if (fd < 0)
        return 1;

    if (cb_aosp_get_service_handle(fd, service_name, &service_handle) != 0)
        return 1;

    printf("negative client: got service handle=%u\n", service_handle);

    {
        uint8_t parcel[512];
        size_t pos = 0;

        if (build_token_parcel(parcel, sizeof(parcel), &pos, "wrong.descriptor.IDemo") != 0 ||
            cb_parcel_write_string16_ascii(parcel, sizeof(parcel), &pos, "bad-token") != 0)
            return 1;

        if (expect_nonzero_exception(fd,
                                     service_handle,
                                     AIDL_LIKE_TX_ECHO,
                                     parcel,
                                     pos,
                                     "AIDL_LIKE_NEGATIVE_BAD_DESCRIPTOR_OK",
                                     "negative bad descriptor") != 0)
            return 1;
    }

    {
        uint8_t parcel[512];
        size_t pos = 0;

        if (aidl_like_write_interface_token(parcel, sizeof(parcel), &pos) != 0)
            return 1;

        if (expect_nonzero_exception(fd,
                                     service_handle,
                                     NEGATIVE_UNKNOWN_CODE,
                                     parcel,
                                     pos,
                                     "AIDL_LIKE_NEGATIVE_UNKNOWN_CODE_OK",
                                     "negative unknown code") != 0)
            return 1;
    }

    {
        uint8_t parcel[4];
        size_t pos = 0;

        if (cb_parcel_write_i32(parcel, sizeof(parcel), &pos, 0) != 0)
            return 1;

        if (expect_nonzero_exception(fd,
                                     service_handle,
                                     AIDL_LIKE_TX_ECHO,
                                     parcel,
                                     pos,
                                     "AIDL_LIKE_NEGATIVE_TRUNCATED_PARCEL_OK",
                                     "negative truncated parcel") != 0)
            return 1;
    }

    {
        uint8_t parcel[512];
        size_t pos = 0;

        if (aidl_like_write_interface_token(parcel, sizeof(parcel), &pos) != 0)
            return 1;

        /* add(a,b) with missing int args */
        if (expect_nonzero_exception(fd,
                                     service_handle,
                                     AIDL_LIKE_TX_ADD,
                                     parcel,
                                     pos,
                                     "AIDL_LIKE_NEGATIVE_BAD_ARGS_OK",
                                     "negative bad args") != 0)
            return 1;
    }

    {
        uint8_t parcel[512];
        size_t pos = 0;
        int32_t exception_code = 0;
        std::string reply;

        if (aidl_like_write_interface_token(parcel, sizeof(parcel), &pos) != 0 ||
            cb_parcel_write_string16_ascii(parcel, sizeof(parcel), &pos, "after-negative") != 0)
            return 1;

        if (negative_call_raw(fd,
                              service_handle,
                              AIDL_LIKE_TX_ECHO,
                              parcel,
                              pos,
                              &exception_code,
                              &reply,
                              NULL,
                              "negative valid echo after errors") != 0)
            return 1;

        if (exception_code != 0 || reply != "echo:after-negative") {
            fprintf(stderr,
                    "negative client: valid recovery call failed ex=%d reply=%s\n",
                    exception_code,
                    reply.c_str());
            return 1;
        }

        printf("AIDL_LIKE_NEGATIVE_RECOVERY_VALID_CALL_OK\n");
    }

    cb_binder_release_handle(fd, service_handle, "negative client release service");

    printf("AIDL_LIKE_NEGATIVE_CLIENT_SMOKE_OK\n");
    return 0;
}
