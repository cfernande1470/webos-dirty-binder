#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static int send_fd(int sock, int fd_to_send) {
    char data = 'F';
    struct iovec iov;
    struct msghdr msg;
    char cmsgbuf[CMSG_SPACE(sizeof(int))];
    struct cmsghdr *cmsg;

    memset(&iov, 0, sizeof(iov));
    iov.iov_base = &data;
    iov.iov_len = sizeof(data);

    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = cmsgbuf;
    msg.msg_controllen = sizeof(cmsgbuf);

    cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));

    memcpy(CMSG_DATA(cmsg), &fd_to_send, sizeof(int));
    msg.msg_controllen = cmsg->cmsg_len;

    if (sendmsg(sock, &msg, 0) < 0) {
        perror("sendmsg SCM_RIGHTS");
        return -1;
    }

    printf("FD_SCM_RIGHTS_SEND_OK fd=%d\n", fd_to_send);
    fflush(stdout);
    return 0;
}

static int recv_fd(int sock) {
    char data = 0;
    struct iovec iov;
    struct msghdr msg;
    char cmsgbuf[CMSG_SPACE(sizeof(int))];
    struct cmsghdr *cmsg;
    int received_fd = -1;

    memset(&iov, 0, sizeof(iov));
    iov.iov_base = &data;
    iov.iov_len = sizeof(data);

    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = cmsgbuf;
    msg.msg_controllen = sizeof(cmsgbuf);

    if (recvmsg(sock, &msg, 0) < 0) {
        perror("recvmsg SCM_RIGHTS");
        return -1;
    }

    cmsg = CMSG_FIRSTHDR(&msg);

    if (!cmsg ||
        cmsg->cmsg_level != SOL_SOCKET ||
        cmsg->cmsg_type != SCM_RIGHTS ||
        cmsg->cmsg_len < CMSG_LEN(sizeof(int))) {
        fprintf(stderr, "missing SCM_RIGHTS fd\n");
        return -1;
    }

    memcpy(&received_fd, CMSG_DATA(cmsg), sizeof(int));

    if (received_fd < 0) {
        fprintf(stderr, "received bad fd=%d\n", received_fd);
        return -1;
    }

    printf("FD_SCM_RIGHTS_RECV_OK fd=%d\n", received_fd);
    fflush(stdout);
    return received_fd;
}

int main(void) {
    int sv[2];
    int pipefd[2];
    pid_t pid;
    int status;
    const char *payload = "fd-scm-rights-payload";
    char buf[128];

    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);

    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv) != 0) {
        perror("socketpair");
        return 1;
    }

    if (pipe(pipefd) != 0) {
        perror("pipe");
        return 1;
    }

    if (write(pipefd[1], payload, strlen(payload)) != (ssize_t)strlen(payload)) {
        perror("write pipe");
        return 1;
    }

    close(pipefd[1]);

    pid = fork();

    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        int rfd;
        ssize_t n;

        close(sv[0]);
        close(pipefd[0]);

        rfd = recv_fd(sv[1]);

        if (rfd < 0)
            _exit(2);

        memset(buf, 0, sizeof(buf));
        n = read(rfd, buf, sizeof(buf) - 1);

        if (n < 0) {
            perror("child read received fd");
            close(rfd);
            _exit(3);
        }

        close(rfd);

        printf("FD_SCM_RIGHTS_READ_OK bytes=%zd data=%s\n", n, buf);
        fflush(stdout);

        if (strcmp(buf, payload) != 0) {
            fprintf(stderr, "payload mismatch expected=%s got=%s\n", payload, buf);
            _exit(4);
        }

        _exit(0);
    }

    close(sv[1]);

    if (send_fd(sv[0], pipefd[0]) != 0)
        return 1;

    close(pipefd[0]);
    close(sv[0]);

    if (waitpid(pid, &status, 0) < 0) {
        perror("waitpid");
        return 1;
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "child failed status=0x%x\n", status);
        return 1;
    }

    printf("FD_SCM_RIGHTS_PREFLIGHT_OK\n");
    return 0;
}
