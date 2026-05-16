#include "android_like_binder.hpp"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int process_alive(pid_t pid)
{
    if (pid <= 0)
        return 0;

    if (kill(pid, 0) == 0)
        return 1;

    return errno != ESRCH;
}

static int kill_service_process(pid_t pid)
{
    if (pid <= 0) {
        fprintf(stderr, "Android-like unlink-death invalid service pid=%ld\n", (long)pid);
        return 1;
    }

    printf("Android-like unlink-death killing service pid=%ld\n", (long)pid);

    if (kill(pid, SIGTERM) != 0 && errno != ESRCH) {
        perror("Android-like unlink-death SIGTERM");
        return 1;
    }

    for (int i = 0; i < 20; i++) {
        if (!process_alive(pid))
            return 0;
        usleep(100000);
    }

    printf("Android-like unlink-death service still alive, sending SIGKILL pid=%ld\n",
           (long)pid);

    if (kill(pid, SIGKILL) != 0 && errno != ESRCH) {
        perror("Android-like unlink-death SIGKILL");
        return 1;
    }

    for (int i = 0; i < 20; i++) {
        if (!process_alive(pid))
            return 0;
        usleep(100000);
    }

    fprintf(stderr,
            "Android-like unlink-death service pid=%ld did not die\n",
            (long)pid);
    return 1;
}

int main(int argc, char **argv)
{
    const char *service_name = argc >= 2 ? argv[1] : "test.android.service";
    pid_t service_pid = argc >= 3 ? (pid_t)atoi(argv[2]) : -1;
    uintptr_t cookie = 0x554e4c494e4b4454ULL; /* UNLINKDT-ish */

    android::sp<android::IServiceManager> sm = android::defaultServiceManager();
    if (!sm) {
        fprintf(stderr, "Android-like unlink-death defaultServiceManager failed\n");
        return 1;
    }

    printf("Android-like unlink-death defaultServiceManager OK\n");

    if (!sm->listServicesContains(android::String16(service_name))) {
        fprintf(stderr,
                "Android-like unlink-death missing service %s\n",
                service_name);
        return 1;
    }

    android::sp<android::IBinder> binder =
        sm->getService(android::String16(service_name));

    if (!binder || !binder->valid()) {
        fprintf(stderr, "Android-like unlink-death getService failed\n");
        return 1;
    }

    printf("ANDROID_LIKE_HANDLE_ACQUIRE_OK unlink-death handle=%u\n",
           binder->handle());

    if (binder->linkToDeath(cookie) != 0) {
        fprintf(stderr, "Android-like unlink-death linkToDeath failed\n");
        return 1;
    }

    if (binder->unlinkToDeath(cookie) != 0) {
        fprintf(stderr, "Android-like unlink-death unlinkToDeath failed\n");
        return 1;
    }

    if (binder->waitForClearDeathNotification(cookie, 10) != 0) {
        fprintf(stderr,
                "Android-like unlink-death waitForClearDeathNotification failed\n");
        return 1;
    }

    if (kill_service_process(service_pid) != 0)
        return 1;

    printf("ANDROID_LIKE_SERVICE_KILLED_OK pid=%ld\n", (long)service_pid);

    if (binder->waitForNoDeathNotification(cookie, 2) != 0) {
        fprintf(stderr,
                "Android-like unlink-death got death notification after unlink\n");
        return 1;
    }

    if (binder->releaseHandle() != 0) {
        fprintf(stderr, "Android-like unlink-death releaseHandle failed\n");
        return 1;
    }

    printf("ANDROID_LIKE_UNLINK_DEATH_RECIPIENT_CLIENT_OK\n");
    return 0;
}
