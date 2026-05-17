#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/android/binder.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#endif

#ifndef __NR_memfd_create
#if defined(__aarch64__)
#define __NR_memfd_create 279
#else
#define __NR_memfd_create 319
#endif
#endif

static int mkdir_p(const char *path) {
    char tmp[512];
    size_t len;

    if (!path || !*path)
        return -1;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);

    if (len == 0 || len >= sizeof(tmp))
        return -1;

    if (tmp[len - 1] == '/')
        tmp[len - 1] = 0;

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0755);
            *p = '/';
        }
    }

    if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
        return -1;

    return 0;
}

static void rm_file_if_exists(const char *path) {
    unlink(path);
}

static int test_binder(void) {
    int fd;
    struct binder_version ver;

    fd = open("/dev/binder", O_RDWR | O_CLOEXEC);

    if (fd < 0) {
        printf("ANDROID_PREFLIGHT_BINDER_DEVICE_FAIL errno=%d (%s)\n", errno, strerror(errno));
        return -1;
    }

    printf("ANDROID_PREFLIGHT_BINDER_DEVICE_OK\n");

    memset(&ver, 0, sizeof(ver));

    if (ioctl(fd, BINDER_VERSION, &ver) != 0) {
        printf("ANDROID_PREFLIGHT_BINDER_VERSION_FAIL errno=%d (%s)\n", errno, strerror(errno));
        close(fd);
        return -1;
    }

    printf("ANDROID_PREFLIGHT_BINDER_VERSION_OK protocol=%d\n", ver.protocol_version);

    close(fd);
    return 0;
}

static void test_binder_devices(void) {
    if (access("/dev/hwbinder", F_OK) == 0)
        printf("ANDROID_PREFLIGHT_HWBINDER_PRESENT\n");
    else
        printf("ANDROID_PREFLIGHT_HWBINDER_MISSING\n");

    if (access("/dev/vndbinder", F_OK) == 0)
        printf("ANDROID_PREFLIGHT_VNDBINDER_PRESENT\n");
    else
        printf("ANDROID_PREFLIGHT_VNDBINDER_MISSING\n");
}

static int test_memfd(void) {
    int fd;
    const char *msg = "android-preflight-memfd";
    char buf[64];

    fd = (int)syscall(__NR_memfd_create, "android-preflight", MFD_CLOEXEC);

    if (fd < 0) {
        printf("ANDROID_PREFLIGHT_MEMFD_FAIL errno=%d (%s)\n", errno, strerror(errno));
        return -1;
    }

    if (write(fd, msg, strlen(msg)) != (ssize_t)strlen(msg)) {
        printf("ANDROID_PREFLIGHT_MEMFD_FAIL write errno=%d (%s)\n", errno, strerror(errno));
        close(fd);
        return -1;
    }

    if (lseek(fd, 0, SEEK_SET) < 0) {
        printf("ANDROID_PREFLIGHT_MEMFD_FAIL lseek errno=%d (%s)\n", errno, strerror(errno));
        close(fd);
        return -1;
    }

    memset(buf, 0, sizeof(buf));

    if (read(fd, buf, sizeof(buf) - 1) <= 0) {
        printf("ANDROID_PREFLIGHT_MEMFD_FAIL read errno=%d (%s)\n", errno, strerror(errno));
        close(fd);
        return -1;
    }

    close(fd);

    printf("ANDROID_PREFLIGHT_MEMFD_OK data=%s\n", buf);
    return 0;
}

static int test_eventfd(void) {
    int fd;
    uint64_t one = 1;
    uint64_t got = 0;

    fd = eventfd(0, EFD_CLOEXEC);

    if (fd < 0) {
        printf("ANDROID_PREFLIGHT_EVENTFD_FAIL errno=%d (%s)\n", errno, strerror(errno));
        return -1;
    }

    if (write(fd, &one, sizeof(one)) != (ssize_t)sizeof(one) ||
        read(fd, &got, sizeof(got)) != (ssize_t)sizeof(got) ||
        got != one) {
        printf("ANDROID_PREFLIGHT_EVENTFD_FAIL io got=%" PRIu64 " errno=%d (%s)\n",
               got,
               errno,
               strerror(errno));
        close(fd);
        return -1;
    }

    close(fd);

    printf("ANDROID_PREFLIGHT_EVENTFD_OK\n");
    return 0;
}

static int test_signalfd(void) {
    sigset_t mask;
    int fd;

    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);

    fd = signalfd(-1, &mask, SFD_CLOEXEC);

    if (fd < 0) {
        printf("ANDROID_PREFLIGHT_SIGNALFD_FAIL errno=%d (%s)\n", errno, strerror(errno));
        return -1;
    }

    close(fd);

    printf("ANDROID_PREFLIGHT_SIGNALFD_OK\n");
    return 0;
}

static int test_epoll(void) {
    int fd;

    fd = epoll_create1(EPOLL_CLOEXEC);

    if (fd < 0) {
        printf("ANDROID_PREFLIGHT_EPOLL_FAIL errno=%d (%s)\n", errno, strerror(errno));
        return -1;
    }

    close(fd);

    printf("ANDROID_PREFLIGHT_EPOLL_OK\n");
    return 0;
}

static int test_mount_one(const char *type, const char *target, const char *data, const char *marker) {
    if (mkdir_p(target) != 0) {
        printf("%s_FAIL mkdir errno=%d (%s)\n", marker, errno, strerror(errno));
        return -1;
    }

    if (mount(type, target, type, 0, data) != 0) {
        printf("%s_FAIL mount errno=%d (%s)\n", marker, errno, strerror(errno));
        return -1;
    }

    printf("%s_OK\n", marker);

    if (umount2(target, MNT_DETACH) != 0) {
        printf("%s_UMOUNT_WARN errno=%d (%s)\n", marker, errno, strerror(errno));
    }

    return 0;
}

static int test_tmpfs_mount(const char *base) {
    char target[512];
    char probe[512];
    int fd;

    snprintf(target, sizeof(target), "%s/tmpfs", base);

    if (mkdir_p(target) != 0) {
        printf("ANDROID_PREFLIGHT_TMPFS_MOUNT_FAIL mkdir errno=%d (%s)\n", errno, strerror(errno));
        return -1;
    }

    if (mount("tmpfs", target, "tmpfs", 0, "size=1m,mode=755") != 0) {
        printf("ANDROID_PREFLIGHT_TMPFS_MOUNT_FAIL mount errno=%d (%s)\n", errno, strerror(errno));
        return -1;
    }

    snprintf(probe, sizeof(probe), "%s/probe.txt", target);
    fd = open(probe, O_CREAT | O_WRONLY | O_CLOEXEC, 0644);

    if (fd >= 0) {
        write(fd, "ok\n", 3);
        close(fd);
        rm_file_if_exists(probe);
    }

    printf("ANDROID_PREFLIGHT_TMPFS_MOUNT_OK\n");

    if (umount2(target, MNT_DETACH) != 0)
        printf("ANDROID_PREFLIGHT_TMPFS_UMOUNT_WARN errno=%d (%s)\n", errno, strerror(errno));

    return 0;
}

static int test_mount_namespace(void) {
    pid_t pid;
    int status;

    pid = fork();

    if (pid < 0) {
        printf("ANDROID_PREFLIGHT_MOUNT_NS_FAIL fork errno=%d (%s)\n", errno, strerror(errno));
        return -1;
    }

    if (pid == 0) {
        if (unshare(CLONE_NEWNS) != 0) {
            printf("ANDROID_PREFLIGHT_MOUNT_NS_FAIL errno=%d (%s)\n", errno, strerror(errno));
            _exit(2);
        }

        if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) != 0) {
            printf("ANDROID_PREFLIGHT_MOUNT_NS_PRIVATE_WARN errno=%d (%s)\n", errno, strerror(errno));
        }

        printf("ANDROID_PREFLIGHT_MOUNT_NS_OK\n");
        _exit(0);
    }

    if (waitpid(pid, &status, 0) < 0) {
        printf("ANDROID_PREFLIGHT_MOUNT_NS_FAIL wait errno=%d (%s)\n", errno, strerror(errno));
        return -1;
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        return -1;

    return 0;
}

int main(int argc, char **argv) {
    const char *base = argc > 1 ? argv[1] : "/media/internal/android-sidecar/preflight-mounts";
    int required_fail = 0;

    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);

    printf("ANDROID_PREFLIGHT_BEGIN base=%s\n", base);

    if (test_binder() != 0)
        required_fail = 1;

    test_binder_devices();

    if (test_memfd() != 0)
        required_fail = 1;

    if (test_eventfd() != 0)
        required_fail = 1;

    if (test_signalfd() != 0)
        required_fail = 1;

    if (test_epoll() != 0)
        required_fail = 1;

    mkdir_p(base);

    if (test_tmpfs_mount(base) != 0)
        required_fail = 1;

    {
        char proc_target[512];
        snprintf(proc_target, sizeof(proc_target), "%s/proc", base);

        if (test_mount_one("proc",
                           proc_target,
                           "",
                           "ANDROID_PREFLIGHT_PROC_MOUNT") != 0)
            required_fail = 1;
    }

    {
        char devpts_target[512];
        snprintf(devpts_target, sizeof(devpts_target), "%s/devpts", base);

        if (test_mount_one("devpts",
                           devpts_target,
                           "newinstance,ptmxmode=0666,mode=0620",
                           "ANDROID_PREFLIGHT_DEVPTS_MOUNT") != 0)
            required_fail = 1;
    }

    if (test_mount_namespace() != 0)
        required_fail = 1;

    if (required_fail) {
        printf("ANDROID_PREFLIGHT_FAIL\n");
        return 1;
    }

    printf("ANDROID_PREFLIGHT_OK\n");
    return 0;
}
