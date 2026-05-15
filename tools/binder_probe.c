#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>

struct binder_version {
    int32_t protocol_version;
};

#define BINDER_VERSION _IOWR('b', 9, struct binder_version)
#define BINDER_SET_MAX_THREADS _IOW('b', 5, uint32_t)

int main(void) {
    int fd = open("/dev/binder", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "open /dev/binder failed: %s\n", strerror(errno));
        return 1;
    }

    struct binder_version ver;
    ver.protocol_version = 0;

    if (ioctl(fd, BINDER_VERSION, &ver) < 0) {
        fprintf(stderr, "BINDER_VERSION failed: %s\n", strerror(errno));
        close(fd);
        return 2;
    }

    printf("BINDER_VERSION protocol_version=%d\n", ver.protocol_version);

    uint32_t max_threads = 1;
    if (ioctl(fd, BINDER_SET_MAX_THREADS, &max_threads) < 0) {
        fprintf(stderr, "BINDER_SET_MAX_THREADS failed: %s\n", strerror(errno));
    } else {
        printf("BINDER_SET_MAX_THREADS ok\n");
    }

    close(fd);
    return 0;
}
