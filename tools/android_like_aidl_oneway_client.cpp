#include "android_like_aidl_common.hpp"

#include <string>

#define ONEWAY_DESCRIPTOR "webos.dirtybinder.IOnewayDemo"
#define ONEWAY_TX_NOTIFY 1U
#define ONEWAY_TX_GET_COUNT 2U

static int write_token(uint8_t *buf, size_t cap, size_t *pos, const char *descriptor) {
    if (cb_parcel_write_i32(buf, cap, pos, 0) != 0)
        return -1;

    return cb_parcel_write_string16_ascii(buf, cap, pos, descriptor);
}

static int send_notify_oneway(int fd, uint32_t service_handle, int32_t seq, const char *payload) {
    uint8_t parcel[512];
    size_t parcel_size = 0;
    uint8_t writebuf[1024];
    uint8_t readbuf[8192];
    uint8_t *p = writebuf;
    uint32_t cmd;
    struct binder_transaction_data tr;
    int first = 1;

    if (write_token(parcel, sizeof(parcel), &parcel_size, ONEWAY_DESCRIPTOR) != 0 ||
        cb_parcel_write_i32(parcel, sizeof(parcel), &parcel_size, seq) != 0 ||
        cb_parcel_write_string16_ascii(parcel, sizeof(parcel), &parcel_size, payload) != 0)
        return -1;

    memset(&tr, 0, sizeof(tr));
    tr.target.handle = service_handle;
    tr.code = ONEWAY_TX_NOTIFY;
    tr.flags = TF_ACCEPT_FDS | TF_ONE_WAY;
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
                                 first ? "oneway client notify call" : "oneway client notify wait");
        first = 0;

        if (n < 0)
            return -1;

        ptr = readbuf;
        end = readbuf + n;

        while (ptr + sizeof(uint32_t) <= end) {
            uint32_t rcmd;

            memcpy(&rcmd, ptr, sizeof(rcmd));
            ptr += sizeof(rcmd);

            printf("oneway client notify got %s 0x%08x\n", cb_cmd_name(rcmd), rcmd);

            if (rcmd == BR_NOOP || rcmd == BR_SPAWN_LOOPER)
                continue;

            if (rcmd == BR_TRANSACTION_COMPLETE) {
                printf("AIDL_LIKE_ONEWAY_NOTIFY_SENT seq=%d\n", seq);
                fflush(stdout);
                return 0;
            }

            if (rcmd == BR_REPLY) {
                struct binder_transaction_data reply;

                if (ptr + sizeof(reply) > end)
                    return -1;

                memcpy(&reply, ptr, sizeof(reply));
                ptr += sizeof(reply);

                cb_binder_free_buffer(fd,
                                      reply.data.ptr.buffer,
                                      "oneway client free unexpected reply");

                fprintf(stderr, "oneway client: unexpected BR_REPLY for one-way notify\n");
                return -1;
            }

            if (rcmd == BR_INCREFS || rcmd == BR_ACQUIRE || rcmd == BR_RELEASE || rcmd == BR_DECREFS) {
                if (cb_handle_ref_cmd(fd, rcmd, &ptr, end, "oneway client notify") != 0)
                    return -1;

                continue;
            }

            if (rcmd == BR_DEAD_REPLY || rcmd == BR_FAILED_REPLY)
                return -1;

            fprintf(stderr, "oneway client notify unhandled cmd=0x%08x\n", rcmd);
            return -1;
        }
    }
}

static int call_get_count(int fd, uint32_t service_handle, int32_t *out_count) {
    uint8_t parcel[256];
    size_t parcel_size = 0;
    uint8_t writebuf[1024];
    uint8_t readbuf[8192];
    uint8_t *p = writebuf;
    uint32_t cmd;
    struct binder_transaction_data tr;
    int first = 1;

    if (!out_count)
        return -1;

    *out_count = 0;

    if (write_token(parcel, sizeof(parcel), &parcel_size, ONEWAY_DESCRIPTOR) != 0)
        return -1;

    memset(&tr, 0, sizeof(tr));
    tr.target.handle = service_handle;
    tr.code = ONEWAY_TX_GET_COUNT;
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
                                 first ? "oneway client getCount call" : "oneway client getCount wait");
        first = 0;

        if (n < 0)
            return -1;

        ptr = readbuf;
        end = readbuf + n;

        while (ptr + sizeof(uint32_t) <= end) {
            uint32_t rcmd;

            memcpy(&rcmd, ptr, sizeof(rcmd));
            ptr += sizeof(rcmd);

            printf("oneway client getCount got %s 0x%08x\n", cb_cmd_name(rcmd), rcmd);

            if (rcmd == BR_NOOP || rcmd == BR_TRANSACTION_COMPLETE || rcmd == BR_SPAWN_LOOPER)
                continue;

            if (rcmd == BR_REPLY) {
                struct binder_transaction_data reply;
                struct aidl_like_reader r;
                int32_t exception_code = 0;
                int32_t count = 0;

                if (ptr + sizeof(reply) > end)
                    return -1;

                memcpy(&reply, ptr, sizeof(reply));
                ptr += sizeof(reply);

                memset(&r, 0, sizeof(r));
                r.data = (const uint8_t *)(uintptr_t)reply.data.ptr.buffer;
                r.size = (size_t)reply.data_size;
                r.pos = 0;

                if (aidl_like_read_i32(&r, &exception_code) != 0 ||
                    aidl_like_read_i32(&r, &count) != 0) {
                    cb_binder_free_buffer(fd,
                                          reply.data.ptr.buffer,
                                          "oneway client free bad getCount reply");
                    return -1;
                }

                cb_binder_free_buffer(fd,
                                      reply.data.ptr.buffer,
                                      "oneway client free getCount reply");

                if (exception_code != 0) {
                    fprintf(stderr, "oneway client getCount exception=%d\n", exception_code);
                    return -1;
                }

                *out_count = count;
                printf("AIDL_LIKE_ONEWAY_GET_COUNT_OK count=%d\n", count);
                fflush(stdout);
                return 0;
            }

            if (rcmd == BR_INCREFS || rcmd == BR_ACQUIRE || rcmd == BR_RELEASE || rcmd == BR_DECREFS) {
                if (cb_handle_ref_cmd(fd, rcmd, &ptr, end, "oneway client getCount") != 0)
                    return -1;

                continue;
            }

            if (rcmd == BR_DEAD_REPLY || rcmd == BR_FAILED_REPLY)
                return -1;

            fprintf(stderr, "oneway client getCount unhandled cmd=0x%08x\n", rcmd);
            return -1;
        }
    }
}

int main(int argc, char **argv) {
    const char *service_name = argc > 1 ? argv[1] : "test.android.aidl.oneway";
    int rounds = argc > 2 ? atoi(argv[2]) : 100;
    int fd;
    uint32_t service_handle = 0;
    int32_t count = 0;
    int expect_at_least = 0;

    if (argc > 3 && strcmp(argv[3], "--expect-at-least") == 0)
        expect_at_least = 1;

    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);

    fd = cb_binder_open_and_init("oneway client");

    if (fd < 0)
        return 1;

    if (cb_aosp_get_service_handle(fd, service_name, &service_handle) != 0)
        return 1;

    printf("oneway client: service handle=%u rounds=%d\n",
           service_handle,
           rounds);

    for (int i = 0; i < rounds; i++) {
        char payload[128];

        snprintf(payload, sizeof(payload), "oneway-payload-%d", i);

        if (send_notify_oneway(fd, service_handle, i, payload) != 0)
            return 1;
    }

    if (call_get_count(fd, service_handle, &count) != 0)
        return 1;

    if (expect_at_least) {
        if (count < rounds) {
            fprintf(stderr, "oneway client: count too low expected_at_least=%d got=%d\n",
                    rounds,
                    count);
            return 1;
        }
    } else if (count != rounds) {
        fprintf(stderr, "oneway client: count mismatch expected=%d got=%d\n",
                rounds,
                count);
        return 1;
    }

    cb_binder_release_handle(fd, service_handle, "oneway client release service");

    printf("AIDL_LIKE_ONEWAY_CLIENT_SMOKE_OK\n");
    return 0;
}
