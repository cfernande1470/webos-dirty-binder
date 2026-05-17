#include "android_like_aidl_common.hpp"

#include <string>

#define AIDL_LIKE_CALL_OK 0
#define AIDL_LIKE_CALL_ERR 1
#define AIDL_LIKE_CALL_DEAD 2

static int aidl_like_call_echo_status(
    int fd,
    uint32_t handle,
    const char *msg,
    std::string *out)
{
    uint8_t parcel[1024];
    size_t parcel_size = 0;
    uint8_t writebuf[2048];
    uint8_t readbuf[8192];
    uint8_t *p = writebuf;
    uint32_t cmd;
    struct binder_transaction_data tr;
    int first = 1;

    if (out)
        out->clear();

    if (aidl_like_write_interface_token(parcel, sizeof(parcel), &parcel_size) != 0 ||
        cb_parcel_write_string16_ascii(parcel, sizeof(parcel), &parcel_size, msg) != 0) {
        fprintf(stderr, "stale client: failed to build echo request\n");
        return AIDL_LIKE_CALL_ERR;
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
                                 first ? "stale client echo call" : "stale client echo wait");
        first = 0;

        if (n < 0)
            return AIDL_LIKE_CALL_ERR;

        ptr = readbuf;
        end = readbuf + n;

        while (ptr + sizeof(uint32_t) <= end) {
            uint32_t rcmd;

            memcpy(&rcmd, ptr, sizeof(rcmd));
            ptr += sizeof(rcmd);

            printf("stale client echo got %s 0x%08x\n", cb_cmd_name(rcmd), rcmd);

            if (rcmd == BR_NOOP || rcmd == BR_TRANSACTION_COMPLETE || rcmd == BR_SPAWN_LOOPER)
                continue;

            if (rcmd == BR_DEAD_REPLY || rcmd == BR_FAILED_REPLY) {
                printf("stale client: old handle returned %s\n", cb_cmd_name(rcmd));
                return AIDL_LIKE_CALL_DEAD;
            }

            if (rcmd == BR_REPLY) {
                struct binder_transaction_data reply;
                struct aidl_like_reader r;
                int32_t exception_code = 0;
                std::string tmp;

                if (ptr + sizeof(reply) > end)
                    return AIDL_LIKE_CALL_ERR;

                memcpy(&reply, ptr, sizeof(reply));
                ptr += sizeof(reply);

                memset(&r, 0, sizeof(r));
                r.data = (const uint8_t *)(uintptr_t)reply.data.ptr.buffer;
                r.size = (size_t)reply.data_size;
                r.pos = 0;

                if (aidl_like_read_i32(&r, &exception_code) != 0) {
                    fprintf(stderr, "stale client: missing exception code\n");
                    cb_binder_free_buffer(fd, reply.data.ptr.buffer, "stale client free bad reply");
                    return AIDL_LIKE_CALL_ERR;
                }

                if (exception_code != 0) {
                    fprintf(stderr, "stale client: exception_code=%d\n", exception_code);
                    cb_binder_free_buffer(fd, reply.data.ptr.buffer, "stale client free exception reply");
                    return AIDL_LIKE_CALL_ERR;
                }

                if (aidl_like_read_string16_ascii(&r, &tmp) != 0) {
                    fprintf(stderr, "stale client: bad string reply\n");
                    cb_binder_free_buffer(fd, reply.data.ptr.buffer, "stale client free bad string reply");
                    return AIDL_LIKE_CALL_ERR;
                }

                cb_binder_free_buffer(fd, reply.data.ptr.buffer, "stale client free reply");

                if (out)
                    *out = tmp;

                return AIDL_LIKE_CALL_OK;
            }

            if (rcmd == BR_INCREFS || rcmd == BR_ACQUIRE || rcmd == BR_RELEASE || rcmd == BR_DECREFS) {
                if (cb_handle_ref_cmd(fd, rcmd, &ptr, end, "stale client") != 0)
                    return AIDL_LIKE_CALL_ERR;

                continue;
            }

            fprintf(stderr, "stale client: unhandled cmd=0x%08x\n", rcmd);
            return AIDL_LIKE_CALL_ERR;
        }
    }
}

static int get_service_with_retry(int fd, const char *service_name, uint32_t *out_handle) {
    int i;

    for (i = 0; i < 60; i++) {
        uint32_t h = 0;
        int rc = cb_aosp_get_service_handle(fd, service_name, &h);

        if (rc == 0 && h != 0) {
            *out_handle = h;
            return 0;
        }

        usleep(250000);
    }

    return -1;
}

int main(int argc, char **argv) {
    const char *service_name = argc > 1 ? argv[1] : "test.android.aidl.stale";
    int sleep_before_old_handle = argc > 2 ? atoi(argv[2]) : 8;
    int fd;
    uint32_t old_handle = 0;
    uint32_t new_handle = 0;
    std::string reply;
    int rc;

    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);

    fd = cb_binder_open_and_init("stale client");

    if (fd < 0)
        return 1;

    if (get_service_with_retry(fd, service_name, &old_handle) != 0) {
        fprintf(stderr, "stale client: failed to resolve initial service\n");
        return 1;
    }

    printf("stale client: initial handle=%u\n", old_handle);

    rc = aidl_like_call_echo_status(fd, old_handle, "before-death", &reply);

    if (rc != AIDL_LIKE_CALL_OK || reply != "echo:before-death") {
        fprintf(stderr, "stale client: initial echo failed rc=%d reply=%s\n", rc, reply.c_str());
        return 1;
    }

    printf("AIDL_LIKE_STALE_INITIAL_OK\n");
    printf("AIDL_LIKE_STALE_READY_TO_KILL\n");
    fflush(stdout);

    sleep((unsigned int)sleep_before_old_handle);

    reply.clear();
    rc = aidl_like_call_echo_status(fd, old_handle, "old-handle-after-death", &reply);

    if (rc != AIDL_LIKE_CALL_DEAD) {
        fprintf(stderr,
                "stale client: expected dead/failed reply on old handle, got rc=%d reply=%s\n",
                rc,
                reply.c_str());
        return 1;
    }

    printf("AIDL_LIKE_STALE_HANDLE_DEAD_REPLY_OK\n");

    cb_binder_release_handle(fd, old_handle, "stale client release old handle");

    if (get_service_with_retry(fd, service_name, &new_handle) != 0) {
        fprintf(stderr, "stale client: failed to re-resolve service\n");
        return 1;
    }

    printf("stale client: new handle=%u\n", new_handle);

    if (new_handle == old_handle) {
        fprintf(stderr, "stale client: warning: new handle equals old handle=%u\n", new_handle);
    }

    printf("AIDL_LIKE_STALE_RERESOLVE_OK\n");

    reply.clear();
    rc = aidl_like_call_echo_status(fd, new_handle, "after-recovery", &reply);

    if (rc != AIDL_LIKE_CALL_OK || reply != "echo:after-recovery") {
        fprintf(stderr, "stale client: after-recovery echo failed rc=%d reply=%s\n", rc, reply.c_str());
        return 1;
    }

    printf("AIDL_LIKE_STALE_AFTER_RECOVERY_OK\n");

    cb_binder_release_handle(fd, new_handle, "stale client release new handle");

    printf("AIDL_LIKE_STALE_SMOKE_OK\n");
    return 0;
}
