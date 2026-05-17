#include "android_like_aidl_common.hpp"

#include <string>

#define FACTORY_DESCRIPTOR "webos.dirtybinder.IBinderFactory"
#define CHILD_DESCRIPTOR "webos.dirtybinder.IReturnedChild"

#define FACTORY_TX_GET_CHILD 0x47434844U
#define CHILD_TX_ECHO 0x43484543U

static int write_token(uint8_t *buf, size_t cap, size_t *pos, const char *descriptor) {
    if (cb_parcel_write_i32(buf, cap, pos, 0) != 0)
        return -1;

    return cb_parcel_write_string16_ascii(buf, cap, pos, descriptor);
}

static int get_child_handle(int fd, uint32_t factory_handle, uint32_t *out_child_handle) {
    uint8_t parcel[512];
    size_t parcel_size = 0;
    uint8_t writebuf[1024];
    uint8_t readbuf[8192];
    uint8_t *p = writebuf;
    uint32_t cmd;
    struct binder_transaction_data tr;
    int first = 1;

    if (!out_child_handle)
        return -1;

    *out_child_handle = 0;

    if (write_token(parcel, sizeof(parcel), &parcel_size, FACTORY_DESCRIPTOR) != 0)
        return -1;

    memset(&tr, 0, sizeof(tr));
    tr.target.handle = factory_handle;
    tr.code = FACTORY_TX_GET_CHILD;
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
                                 first ? "binder-return client getChild call" : "binder-return client getChild wait");
        first = 0;

        if (n < 0)
            return -1;

        ptr = readbuf;
        end = readbuf + n;

        while (ptr + sizeof(uint32_t) <= end) {
            uint32_t rcmd;

            memcpy(&rcmd, ptr, sizeof(rcmd));
            ptr += sizeof(rcmd);

            printf("binder-return client getChild got %s 0x%08x\n", cb_cmd_name(rcmd), rcmd);

            if (rcmd == BR_NOOP || rcmd == BR_TRANSACTION_COMPLETE || rcmd == BR_SPAWN_LOOPER)
                continue;

            if (rcmd == BR_REPLY) {
                struct binder_transaction_data reply;
                struct aidl_like_reader r;
                int32_t exception_code = 0;
                uint32_t child_handle = 0;

                if (ptr + sizeof(reply) > end)
                    return -1;

                memcpy(&reply, ptr, sizeof(reply));
                ptr += sizeof(reply);

                memset(&r, 0, sizeof(r));
                r.data = (const uint8_t *)(uintptr_t)reply.data.ptr.buffer;
                r.size = (size_t)reply.data_size;
                r.pos = 0;

                if (aidl_like_read_i32(&r, &exception_code) != 0) {
                    cb_binder_free_buffer(fd, reply.data.ptr.buffer, "binder-return free bad getChild reply");
                    return -1;
                }

                if (exception_code != 0) {
                    fprintf(stderr, "binder-return client getChild exception=%d\n", exception_code);
                    cb_binder_free_buffer(fd, reply.data.ptr.buffer, "binder-return free exception getChild reply");
                    return -1;
                }

                child_handle = cb_first_handle_from_transaction(&reply);

                if (!child_handle) {
                    fprintf(stderr, "binder-return client: getChild returned no Binder handle\n");
                    cb_binder_free_buffer(fd, reply.data.ptr.buffer, "binder-return free no-handle reply");
                    return -1;
                }

                if (cb_binder_acquire_handle(fd,
                                             child_handle,
                                             "binder-return client acquire child handle") != 0) {
                    cb_binder_free_buffer(fd, reply.data.ptr.buffer, "binder-return free acquire-failed reply");
                    return -1;
                }

                cb_binder_free_buffer(fd, reply.data.ptr.buffer, "binder-return free getChild reply");

                *out_child_handle = child_handle;

                printf("AIDL_LIKE_BINDER_RETURN_HANDLE_OK handle=%u\n", child_handle);
                fflush(stdout);

                return 0;
            }

            if (rcmd == BR_INCREFS || rcmd == BR_ACQUIRE || rcmd == BR_RELEASE || rcmd == BR_DECREFS) {
                if (cb_handle_ref_cmd(fd, rcmd, &ptr, end, "binder-return client getChild") != 0)
                    return -1;

                continue;
            }

            if (rcmd == BR_DEAD_REPLY || rcmd == BR_FAILED_REPLY)
                return -1;

            fprintf(stderr, "binder-return client getChild unhandled cmd=0x%08x\n", rcmd);
            return -1;
        }
    }
}

static int call_child_echo(int fd, uint32_t child_handle, const char *msg, std::string *out) {
    uint8_t parcel[512];
    size_t parcel_size = 0;
    uint8_t writebuf[1024];
    uint8_t readbuf[8192];
    uint8_t *p = writebuf;
    uint32_t cmd;
    struct binder_transaction_data tr;
    int first = 1;

    if (!out)
        return -1;

    out->clear();

    if (write_token(parcel, sizeof(parcel), &parcel_size, CHILD_DESCRIPTOR) != 0 ||
        cb_parcel_write_string16_ascii(parcel, sizeof(parcel), &parcel_size, msg) != 0)
        return -1;

    memset(&tr, 0, sizeof(tr));
    tr.target.handle = child_handle;
    tr.code = CHILD_TX_ECHO;
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
                                 first ? "binder-return client child echo call" : "binder-return client child echo wait");
        first = 0;

        if (n < 0)
            return -1;

        ptr = readbuf;
        end = readbuf + n;

        while (ptr + sizeof(uint32_t) <= end) {
            uint32_t rcmd;

            memcpy(&rcmd, ptr, sizeof(rcmd));
            ptr += sizeof(rcmd);

            printf("binder-return client child got %s 0x%08x\n", cb_cmd_name(rcmd), rcmd);

            if (rcmd == BR_NOOP || rcmd == BR_TRANSACTION_COMPLETE || rcmd == BR_SPAWN_LOOPER)
                continue;

            if (rcmd == BR_REPLY) {
                struct binder_transaction_data reply;
                struct aidl_like_reader r;
                int32_t exception_code = 0;
                std::string result;

                if (ptr + sizeof(reply) > end)
                    return -1;

                memcpy(&reply, ptr, sizeof(reply));
                ptr += sizeof(reply);

                memset(&r, 0, sizeof(r));
                r.data = (const uint8_t *)(uintptr_t)reply.data.ptr.buffer;
                r.size = (size_t)reply.data_size;
                r.pos = 0;

                if (aidl_like_read_i32(&r, &exception_code) != 0) {
                    cb_binder_free_buffer(fd, reply.data.ptr.buffer, "binder-return free bad child reply");
                    return -1;
                }

                if (exception_code != 0) {
                    fprintf(stderr, "binder-return client child exception=%d\n", exception_code);
                    cb_binder_free_buffer(fd, reply.data.ptr.buffer, "binder-return free child exception reply");
                    return -1;
                }

                if (aidl_like_read_string16_ascii(&r, &result) != 0) {
                    cb_binder_free_buffer(fd, reply.data.ptr.buffer, "binder-return free bad child string");
                    return -1;
                }

                cb_binder_free_buffer(fd, reply.data.ptr.buffer, "binder-return free child reply");

                *out = result;
                return 0;
            }

            if (rcmd == BR_INCREFS || rcmd == BR_ACQUIRE || rcmd == BR_RELEASE || rcmd == BR_DECREFS) {
                if (cb_handle_ref_cmd(fd, rcmd, &ptr, end, "binder-return client child") != 0)
                    return -1;

                continue;
            }

            if (rcmd == BR_DEAD_REPLY || rcmd == BR_FAILED_REPLY)
                return -1;

            fprintf(stderr, "binder-return client child unhandled cmd=0x%08x\n", rcmd);
            return -1;
        }
    }
}

int main(int argc, char **argv) {
    const char *service_name = argc > 1 ? argv[1] : "test.android.aidl.factory";
    int fd;
    uint32_t factory_handle = 0;
    uint32_t child_handle = 0;
    std::string reply;

    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);

    fd = cb_binder_open_and_init("binder-return client");

    if (fd < 0)
        return 1;

    if (cb_aosp_get_service_handle(fd, service_name, &factory_handle) != 0)
        return 1;

    printf("binder-return client: factory handle=%u\n", factory_handle);

    if (get_child_handle(fd, factory_handle, &child_handle) != 0)
        return 1;

    if (call_child_echo(fd, child_handle, "hello-child", &reply) != 0)
        return 1;

    printf("binder-return client child reply=%s\n", reply.c_str());

    if (reply != "child-echo:hello-child") {
        fprintf(stderr, "binder-return client bad child reply=%s\n", reply.c_str());
        return 1;
    }

    printf("AIDL_LIKE_BINDER_RETURN_CHILD_CALL_OK\n");

    cb_binder_release_handle(fd, child_handle, "binder-return client release child");

    /*
     * The returned Binder handle carries both strong and weak refs.
     * BC_RELEASE drops the strong ref; BC_DECREFS drops the weak ref so the
     * service can observe both BR_RELEASE and BR_DECREFS for lifecycle tests.
     */
    cb_binder_send_handle_cmd(fd,
                              BC_DECREFS,
                              child_handle,
                              "binder-return client decrefs child");

    cb_binder_release_handle(fd, factory_handle, "binder-return client release factory");

    printf("AIDL_LIKE_BINDER_RETURN_CLIENT_SMOKE_OK\n");
    return 0;
}
