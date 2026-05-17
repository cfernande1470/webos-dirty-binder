#include "parcel_fd_lite_common.hpp"

static int make_preflight_pipe(int pipefd[2]) {
    const char *payload = PARCELFD_LITE_PAYLOAD "\n";

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
    const char *service_name = argc > 1 ? argv[1] : PARCELFD_LITE_SERVICE_NAME;
    const char *socket_path = argc > 2 ? argv[2] : "/run/parcel_fd_lite.sock";

    int binder_fd;
    uint32_t service_handle = 0;
    int pipefd[2] = {-1, -1};
    uint64_t token = 0;
    struct parcel_fd_lite_payload reply;
    int rc = 1;

    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);

    printf("ANDROID_USERSPACE_PREFLIGHT_V1_STARTED service=%s socket=%s\n",
           service_name,
           socket_path);

    binder_fd = cb_binder_open_and_init("android userspace preflight v1");
    if (binder_fd < 0)
        return 1;

    if (cb_aosp_get_service_handle(binder_fd, service_name, &service_handle) != 0)
        return 1;

    printf("ANDROID_USERSPACE_PREFLIGHT_V1_BINDER_HANDLE_OK handle=%u\n",
           service_handle);

    if (make_preflight_pipe(pipefd) != 0)
        goto out_release;

    if (parcel_fd_lite_write_fd(socket_path,
                                pipefd[0],
                                PARCELFD_LITE_KIND_PIPE_READ,
                                "android-userspace-preflight-v1-pipe",
                                &token) != 0) {
        fprintf(stderr, "android userspace preflight: parcel_fd_lite_write_fd failed\n");
        goto out_close_pipe;
    }

    printf("ANDROID_USERSPACE_PREFLIGHT_V1_PARCELFD_WRITE_OK token=%llu\n",
           (unsigned long long)token);

    close(pipefd[0]);
    pipefd[0] = -1;

    memset(&reply, 0, sizeof(reply));

    if (parcel_fd_lite_call_binder(binder_fd,
                                   service_handle,
                                   token,
                                   PARCELFD_LITE_KIND_PIPE_READ,
                                   "Android userspace preflight v1 ParcelFD token",
                                   &reply) != 0) {
        fprintf(stderr, "android userspace preflight: Binder control failed\n");
        goto out_release;
    }

    printf("android userspace preflight reply magic=0x%08x status=%u token=%llu kind=%u text=%s\n",
           reply.magic,
           reply.status,
           (unsigned long long)reply.token,
           reply.kind,
           reply.text);

    if (reply.magic != PARCELFD_LITE_MAGIC ||
        reply.status != 0 ||
        reply.token != token) {
        fprintf(stderr, "android userspace preflight: bad Binder reply\n");
        goto out_release;
    }

    printf("ANDROID_USERSPACE_PREFLIGHT_V1_BINDER_REPLY_OK\n");
    printf("ANDROID_USERSPACE_PREFLIGHT_V1_SMOKE_OK\n");

    rc = 0;

out_close_pipe:
    if (pipefd[0] >= 0)
        close(pipefd[0]);

out_release:
    cb_binder_release_handle(binder_fd, service_handle, "android userspace preflight BC_RELEASE service");
    return rc;
}
