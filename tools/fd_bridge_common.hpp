#ifndef WEBOS_DIRTY_BINDER_FD_BRIDGE_COMMON_HPP
#define WEBOS_DIRTY_BINDER_FD_BRIDGE_COMMON_HPP

#include "android_like_callback_common.hpp"

#include <fcntl.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>

#define FD_BRIDGE_SERVICE_NAME "test.android.fdbridge"
#define FD_BRIDGE_SOCKET_PATH "/media/internal/android-sidecar/run/fd_bridge.sock"

#define FD_BRIDGE_CONTROL_TRANSACTION 0x46444252U
#define FD_BRIDGE_MAGIC 0x46444230U
#define FD_BRIDGE_KIND_PIPE_READ 1U

#define FD_BRIDGE_PAYLOAD "SCM_RIGHTS_FD_BRIDGE_PAYLOAD_OK"

struct fd_bridge_socket_msg {
    uint64_t token;
    uint32_t kind;
    uint32_t reserved;
    char label[128];
};

struct fd_bridge_control_payload {
    uint32_t magic;
    uint32_t status;
    uint64_t token;
    uint32_t kind;
    char text[256];
};

static inline uint64_t fd_bridge_make_token(void) {
    struct timespec ts;
    uint64_t token;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    token = ((uint64_t)getpid() << 32) ^
            ((uint64_t)ts.tv_sec << 16) ^
            (uint64_t)ts.tv_nsec;

    if (!token)
        token = 1;

    return token;
}

static inline int fd_bridge_connect_socket(const char *path) {
    int s;
    struct sockaddr_un addr;

    s = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (s < 0) {
        perror("socket AF_UNIX");
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;

    if (strlen(path) >= sizeof(addr.sun_path)) {
        fprintf(stderr, "fd bridge: socket path too long: %s\n", path);
        close(s);
        return -1;
    }

    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);

    for (int i = 0; i < 80; i++) {
        if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) == 0)
            return s;

        usleep(50000);
    }

    perror("connect fd bridge socket");
    close(s);
    return -1;
}

static inline int fd_bridge_create_listener(const char *path) {
    int s;
    struct sockaddr_un addr;

    unlink(path);

    s = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (s < 0) {
        perror("socket listener");
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;

    if (strlen(path) >= sizeof(addr.sun_path)) {
        fprintf(stderr, "fd bridge: listener path too long: %s\n", path);
        close(s);
        return -1;
    }

    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        perror("bind fd bridge socket");
        close(s);
        return -1;
    }

    chmod(path, 0600);

    if (listen(s, 16) != 0) {
        perror("listen fd bridge socket");
        close(s);
        return -1;
    }

    return s;
}

static inline int fd_bridge_send_fd(const char *path, uint64_t token, uint32_t kind, const char *label, int send_fd) {
    int s;
    struct fd_bridge_socket_msg smsg;
    struct msghdr msg;
    struct iovec iov;
    char cbuf[CMSG_SPACE(sizeof(int))];
    struct cmsghdr *cmsg;
    ssize_t n;

    s = fd_bridge_connect_socket(path);
    if (s < 0)
        return -1;

    memset(&smsg, 0, sizeof(smsg));
    smsg.token = token;
    smsg.kind = kind;
    snprintf(smsg.label, sizeof(smsg.label), "%s", label ? label : "");

    memset(&msg, 0, sizeof(msg));
    memset(cbuf, 0, sizeof(cbuf));

    iov.iov_base = &smsg;
    iov.iov_len = sizeof(smsg);

    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = cbuf;
    msg.msg_controllen = sizeof(cbuf);

    cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    memcpy(CMSG_DATA(cmsg), &send_fd, sizeof(send_fd));

    msg.msg_controllen = CMSG_SPACE(sizeof(int));

    n = sendmsg(s, &msg, 0);
    close(s);

    if (n != (ssize_t)sizeof(smsg)) {
        fprintf(stderr, "fd bridge: sendmsg failed n=%zd expected=%zu errno=%d (%s)\n",
                n, sizeof(smsg), errno, strerror(errno));
        return -1;
    }

    return 0;
}

static inline int fd_bridge_recv_fd(int s, struct fd_bridge_socket_msg *out_msg, int *out_fd) {
    struct msghdr msg;
    struct iovec iov;
    char cbuf[CMSG_SPACE(sizeof(int))];
    struct cmsghdr *cmsg;
    ssize_t n;

    if (!out_msg || !out_fd)
        return -1;

    *out_fd = -1;
    memset(out_msg, 0, sizeof(*out_msg));

    memset(&msg, 0, sizeof(msg));
    memset(cbuf, 0, sizeof(cbuf));

    iov.iov_base = out_msg;
    iov.iov_len = sizeof(*out_msg);

    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = cbuf;
    msg.msg_controllen = sizeof(cbuf);

    n = recvmsg(s, &msg, 0);
    if (n != (ssize_t)sizeof(*out_msg)) {
        fprintf(stderr, "fd bridge: recvmsg failed n=%zd expected=%zu errno=%d (%s)\n",
                n, sizeof(*out_msg), errno, strerror(errno));
        return -1;
    }

    for (cmsg = CMSG_FIRSTHDR(&msg); cmsg; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
        if (cmsg->cmsg_level == SOL_SOCKET &&
            cmsg->cmsg_type == SCM_RIGHTS &&
            cmsg->cmsg_len >= CMSG_LEN(sizeof(int))) {
            memcpy(out_fd, CMSG_DATA(cmsg), sizeof(int));
            break;
        }
    }

    if (*out_fd < 0) {
        fprintf(stderr, "fd bridge: no SCM_RIGHTS fd received\n");
        return -1;
    }

    return 0;
}

static inline int fd_bridge_send_control_reply(
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
    struct fd_bridge_control_payload reply;

    memset(&reply, 0, sizeof(reply));
    reply.magic = FD_BRIDGE_MAGIC;
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


static inline int fd_bridge_call_control(
    int fd,
    uint32_t handle,
    uint64_t token,
    uint32_t kind,
    const char *text,
    struct fd_bridge_control_payload *out_reply)
{
    struct fd_bridge_control_payload payload;
    uint8_t writebuf[2048];
    uint8_t readbuf[8192];
    uint8_t *p = writebuf;
    uint32_t cmd;
    struct binder_transaction_data tr;
    int first = 1;

    memset(&payload, 0, sizeof(payload));
    payload.magic = FD_BRIDGE_MAGIC;
    payload.status = 0;
    payload.token = token;
    payload.kind = kind;
    snprintf(payload.text, sizeof(payload.text), "%s", text ? text : "");

    memset(&tr, 0, sizeof(tr));
    tr.target.handle = handle;
    tr.code = FD_BRIDGE_CONTROL_TRANSACTION;
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
                                 first ? "fd bridge binder control call" : "fd bridge binder control wait");
        first = 0;

        if (n < 0)
            return -1;

        ptr = readbuf;
        end = readbuf + n;

        while (ptr + sizeof(uint32_t) <= end) {
            uint32_t rcmd;
            memcpy(&rcmd, ptr, sizeof(rcmd));
            ptr += sizeof(rcmd);

            printf("fd bridge client got %s 0x%08x\n", cb_cmd_name(rcmd), rcmd);

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

                if (reply.data.ptr.buffer && reply.data_size >= sizeof(struct fd_bridge_control_payload)) {
                    struct fd_bridge_control_payload *rp =
                        (struct fd_bridge_control_payload *)(uintptr_t)reply.data.ptr.buffer;

                    if (out_reply)
                        memcpy(out_reply, rp, sizeof(*out_reply));
                }

                cb_binder_free_buffer(fd, reply.data.ptr.buffer, "fd bridge free binder reply");
                return 0;
            }

            if (rcmd == BR_INCREFS || rcmd == BR_ACQUIRE || rcmd == BR_RELEASE || rcmd == BR_DECREFS) {
                if (cb_handle_ref_cmd(fd, rcmd, &ptr, end, "fd bridge client") != 0)
                    return -1;
                continue;
            }

            if (rcmd == BR_DEAD_REPLY || rcmd == BR_FAILED_REPLY) {
                fprintf(stderr, "fd bridge binder control failed cmd=0x%08x\n", rcmd);
                return 1;
            }

            fprintf(stderr, "fd bridge client unhandled cmd=0x%08x\n", rcmd);
            return -1;
        }
    }
}


#endif
