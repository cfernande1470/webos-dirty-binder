#include "fd_bridge_common.hpp"

static int make_payload_pipe(int pipefd[2]) {
    const char *payload = FD_BRIDGE_PAYLOAD "\n";

    if (pipe(pipefd) != 0) {
        perror("pipe");
        return -1;
    }

    if (write(pipefd[1], payload, strlen(payload)) != (ssize_t)strlen(payload)) {
        perror("write pipe payload");
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    close(pipefd[1]);
    pipefd[1] = -1;

    return 0;
}

int main(int argc, char **argv) {
    const char *service_name = argc > 1 ? argv[1] : FD_BRIDGE_SERVICE_NAME;
    const char *socket_path = argc > 2 ? argv[2] : FD_BRIDGE_SOCKET_PATH;
    int binder_fd;
    uint32_t service_handle = 0;
    int pipefd[2] = {-1, -1};
    uint64_t token;
    struct fd_bridge_control_payload reply;
    int rc = 1;

    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);

    binder_fd = cb_binder_open_and_init("fd bridge client");
    if (binder_fd < 0)
        return 1;

    if (cb_aosp_get_service_handle(binder_fd, service_name, &service_handle) != 0)
        return 1;

    printf("fd bridge client: got service handle=%u\n", service_handle);

    if (make_payload_pipe(pipefd) != 0)
        goto out_release;

    token = fd_bridge_make_token();

    printf("fd bridge client: sending fd via SCM_RIGHTS token=%llu fd=%d socket=%s\n",
           (unsigned long long)token,
           pipefd[0],
           socket_path);

    if (fd_bridge_send_fd(socket_path,
                          token,
                          FD_BRIDGE_KIND_PIPE_READ,
                          "pipe-read-payload",
                          pipefd[0]) != 0) {
        fprintf(stderr, "fd bridge client failed to send SCM_RIGHTS fd\n");
        goto out_close_pipe;
    }

    printf("FD_BRIDGE_CLIENT_SOCKET_SEND_OK token=%llu\n", (unsigned long long)token);

    close(pipefd[0]);
    pipefd[0] = -1;

    memset(&reply, 0, sizeof(reply));

    if (fd_bridge_call_control(binder_fd,
                               service_handle,
                               token,
                               FD_BRIDGE_KIND_PIPE_READ,
                               "binder control for SCM_RIGHTS fd",
                               &reply) != 0) {
        fprintf(stderr, "fd bridge client Binder control failed\n");
        goto out_release;
    }

    printf("fd bridge client reply magic=0x%08x status=%u token=%llu kind=%u text=%s\n",
           reply.magic,
           reply.status,
           (unsigned long long)reply.token,
           reply.kind,
           reply.text);

    if (reply.magic != FD_BRIDGE_MAGIC ||
        reply.status != 0 ||
        reply.token != token) {
        fprintf(stderr, "fd bridge client bad Binder reply\n");
        goto out_release;
    }

    printf("FD_BRIDGE_CLIENT_BINDER_REPLY_OK\n");
    printf("FD_BRIDGE_SMOKE_OK\n");

    rc = 0;

out_close_pipe:
    if (pipefd[0] >= 0)
        close(pipefd[0]);

out_release:
    cb_binder_release_handle(binder_fd, service_handle, "fd bridge client BC_RELEASE service");
    return rc;
}
