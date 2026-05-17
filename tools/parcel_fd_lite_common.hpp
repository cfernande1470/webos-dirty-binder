#ifndef WEBOS_DIRTY_BINDER_PARCEL_FD_LITE_COMMON_HPP
#define WEBOS_DIRTY_BINDER_PARCEL_FD_LITE_COMMON_HPP

#include "fd_bridge_common.hpp"

#define PARCELFD_LITE_SERVICE_NAME "test.android.parcelfd"
#define PARCELFD_LITE_SOCKET_PATH "/media/internal/android-sidecar/run/parcel_fd_lite.sock"

#define PARCELFD_LITE_TRANSACTION 0x50464430U /* PFD0 */
#define PARCELFD_LITE_MAGIC 0x50464c30U       /* PFL0 */
#define PARCELFD_LITE_KIND_PIPE_READ 1U

#define PARCELFD_LITE_PAYLOAD "PARCELFD_LITE_PAYLOAD_OK"

struct parcel_fd_lite_payload {
    uint32_t magic;
    uint32_t status;
    uint64_t token;
    uint32_t kind;
    char text[256];
};

static inline int parcel_fd_lite_write_fd(
    const char *socket_path,
    int fd,
    uint32_t kind,
    const char *label,
    uint64_t *out_token)
{
    uint64_t token;

    if (!out_token)
        return -1;

    token = fd_bridge_make_token();
    *out_token = token;

    printf("PARCELFD_LITE_TOKEN_ENCODE_OK token=%llu kind=%u\n",
           (unsigned long long)token,
           kind);

    if (fd_bridge_send_fd(socket_path, token, kind, label, fd) != 0)
        return -1;

    printf("PARCELFD_LITE_SOCKET_SEND_OK token=%llu\n",
           (unsigned long long)token);

    printf("PARCELFD_LITE_WRITE_FD_OK token=%llu\n",
           (unsigned long long)token);

    return 0;
}

static inline int parcel_fd_lite_send_reply(
    int fd,
    binder_uintptr_t incoming_buffer,
    uint32_t status,
    uint64_t token,
    uint32_t kind,
    const char *text,
    const char *tag)
{
    uint8_t writebuf[2048];
    uint8_t *p = writebuf;
    uint32_t cmd;
    struct binder_transaction_data reply_tr;
    struct parcel_fd_lite_payload reply;

    memset(&reply, 0, sizeof(reply));
    reply.magic = PARCELFD_LITE_MAGIC;
    reply.status = status;
    reply.token = token;
    reply.kind = kind;
    snprintf(reply.text, sizeof(reply.text), "%s", text ? text : "");

    cmd = BC_FREE_BUFFER;
    cb_append_u32(&p, cmd);
    cb_append_bytes(&p, &incoming_buffer, sizeof(incoming_buffer));

    cmd = BC_REPLY;
    cb_append_u32(&p, cmd);

    memset(&reply_tr, 0, sizeof(reply_tr));
    reply_tr.data_size = sizeof(reply);
    reply_tr.offsets_size = 0;
    reply_tr.data.ptr.buffer = (binder_uintptr_t)&reply;
    reply_tr.data.ptr.offsets = 0;

    cb_append_bytes(&p, &reply_tr, sizeof(reply_tr));

    return cb_binder_write_read(fd, writebuf, (size_t)(p - writebuf), NULL, 0, tag) < 0 ? -1 : 0;
}

static inline int parcel_fd_lite_call_binder(
    int fd,
    uint32_t handle,
    uint64_t token,
    uint32_t kind,
    const char *text,
    struct parcel_fd_lite_payload *out_reply)
{
    struct parcel_fd_lite_payload payload;
    uint8_t writebuf[2048];
    uint8_t readbuf[8192];
    uint8_t *p = writebuf;
    uint32_t cmd;
    struct binder_transaction_data tr;
    int first = 1;

    memset(&payload, 0, sizeof(payload));
    payload.magic = PARCELFD_LITE_MAGIC;
    payload.status = 0;
    payload.token = token;
    payload.kind = kind;
    snprintf(payload.text, sizeof(payload.text), "%s", text ? text : "");

    memset(&tr, 0, sizeof(tr));
    tr.target.handle = handle;
    tr.code = PARCELFD_LITE_TRANSACTION;
    tr.flags = TF_ACCEPT_FDS;
    tr.data_size = sizeof(payload);
    tr.offsets_size = 0;
    tr.data.ptr.buffer = (binder_uintptr_t)&payload;
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
                                 first ? "parcelfd lite binder call" : "parcelfd lite binder wait");
        first = 0;

        if (n < 0)
            return -1;

        ptr = readbuf;
        end = readbuf + n;

        while (ptr + sizeof(uint32_t) <= end) {
            uint32_t rcmd;
            memcpy(&rcmd, ptr, sizeof(rcmd));
            ptr += sizeof(rcmd);

            printf("parcelfd lite client got %s 0x%08x\n", cb_cmd_name(rcmd), rcmd);

            if (rcmd == BR_NOOP || rcmd == BR_TRANSACTION_COMPLETE || rcmd == BR_SPAWN_LOOPER)
                continue;

            if (rcmd == BR_REPLY) {
                struct binder_transaction_data reply;

                if (ptr + sizeof(reply) > end)
                    return -1;

                memcpy(&reply, ptr, sizeof(reply));
                ptr += sizeof(reply);

                if (out_reply)
                    memset(out_reply, 0, sizeof(*out_reply));

                if (reply.data.ptr.buffer && reply.data_size >= sizeof(struct parcel_fd_lite_payload)) {
                    struct parcel_fd_lite_payload *rp =
                        (struct parcel_fd_lite_payload *)(uintptr_t)reply.data.ptr.buffer;

                    if (out_reply)
                        memcpy(out_reply, rp, sizeof(*out_reply));
                }

                cb_binder_free_buffer(fd, reply.data.ptr.buffer, "parcelfd lite free binder reply");
                return 0;
            }

            if (rcmd == BR_INCREFS || rcmd == BR_ACQUIRE || rcmd == BR_RELEASE || rcmd == BR_DECREFS) {
                if (cb_handle_ref_cmd(fd, rcmd, &ptr, end, "parcelfd lite client") != 0)
                    return -1;
                continue;
            }

            if (rcmd == BR_DEAD_REPLY || rcmd == BR_FAILED_REPLY) {
                fprintf(stderr, "parcelfd lite binder call failed cmd=0x%08x\n", rcmd);
                return 1;
            }

            fprintf(stderr, "parcelfd lite client unhandled cmd=0x%08x\n", rcmd);
            return -1;
        }
    }
}

#endif
