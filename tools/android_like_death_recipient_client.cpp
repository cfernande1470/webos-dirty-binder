#include "android_like_binder.hpp"
#include "android_like_echo_iface.hpp"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <utility>

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
        fprintf(stderr, "Android-like death invalid service pid=%ld\n", (long)pid);
        return 1;
    }

    printf("Android-like death killing service pid=%ld\n", (long)pid);

    if (kill(pid, SIGTERM) != 0 && errno != ESRCH) {
        perror("Android-like death SIGTERM");
        return 1;
    }

    for (int i = 0; i < 20; i++) {
        if (!process_alive(pid))
            return 0;
        usleep(100000);
    }

    printf("Android-like death service still alive, sending SIGKILL pid=%ld\n",
           (long)pid);

    if (kill(pid, SIGKILL) != 0 && errno != ESRCH) {
        perror("Android-like death SIGKILL");
        return 1;
    }

    for (int i = 0; i < 20; i++) {
        if (!process_alive(pid))
            return 0;
        usleep(100000);
    }

    fprintf(stderr, "Android-like death service pid=%ld did not die\n", (long)pid);
    return 1;
}

int main(int argc, char **argv)
{
    const char *service_name = argc >= 2 ? argv[1] : "test.android.service";
    pid_t service_pid = argc >= 3 ? (pid_t)atoi(argv[2]) : -1;
    uintptr_t cookie = 0x4445415448524350ULL; /* DEATHRCP-ish */
    char reply[1024];

    android::sp<android::IServiceManager> sm = android::defaultServiceManager();
    if (!sm) {
        fprintf(stderr, "Android-like death defaultServiceManager failed\n");
        return 1;
    }

    printf("Android-like death defaultServiceManager OK\n");

    if (!sm->listServicesContains(android::String16(service_name))) {
        fprintf(stderr, "Android-like death missing service %s\n", service_name);
        return 1;
    }

    android::sp<android::IBinder> binder =
        sm->getService(android::String16(service_name));

    if (!binder || !binder->valid()) {
        fprintf(stderr, "Android-like death getService failed\n");
        return 1;
    }

    printf("ANDROID_LIKE_HANDLE_ACQUIRE_OK death handle=%u\n",
           binder->handle());

    if (binder->linkToDeath(cookie) != 0) {
        fprintf(stderr, "Android-like death linkToDeath failed\n");
        return 1;
    }

    android::sp<IEchoService> echo =
        interface_cast_echo(std::move(binder));

    if (!echo) {
        fprintf(stderr, "Android-like death interface_cast failed\n");
        return 1;
    }

    if (echo->echoText("hello before death notification", reply, sizeof(reply)) != 0) {
        fprintf(stderr, "Android-like death pre-kill echo failed\n");
        return 1;
    }

    printf("Android-like death pre-kill reply=%s\n", reply);

    if (kill_service_process(service_pid) != 0)
        return 1;

    printf("ANDROID_LIKE_SERVICE_KILLED_OK pid=%ld\n", (long)service_pid);

    /*
     * The remote is still owned by BpEchoService, so wait through the same
     * binder fd via releaseRemote's underlying binder API exposed before cast.
     * For v0 we keep the IBinder alive by doing linkToDeath before cast and
     * waiting through a fresh service-manager binder fd is not enough.
     *
     * Therefore BpEchoService exposes waitForRemoteDeath in the next patch.
     */
    if (echo->waitForRemoteDeath(cookie, 10) != 0) {
        fprintf(stderr, "Android-like death waitForRemoteDeath failed\n");
        return 1;
    }

    (void)echo->releaseRemote();

    printf("ANDROID_LIKE_DEATH_RECIPIENT_CLIENT_OK\n");
    return 0;
}
