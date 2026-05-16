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
        fprintf(stderr, "Android-like stale invalid service pid=%ld\n", (long)pid);
        return 1;
    }

    printf("Android-like stale killing service pid=%ld\n", (long)pid);

    if (kill(pid, SIGTERM) != 0 && errno != ESRCH) {
        perror("Android-like stale SIGTERM");
        return 1;
    }

    for (int i = 0; i < 20; i++) {
        if (!process_alive(pid))
            return 0;
        usleep(100000);
    }

    printf("Android-like stale service still alive, sending SIGKILL pid=%ld\n",
           (long)pid);

    if (kill(pid, SIGKILL) != 0 && errno != ESRCH) {
        perror("Android-like stale SIGKILL");
        return 1;
    }

    for (int i = 0; i < 20; i++) {
        if (!process_alive(pid))
            return 0;
        usleep(100000);
    }

    fprintf(stderr,
            "Android-like stale service pid=%ld did not die\n",
            (long)pid);
    return 1;
}

static void timeout_handler(int sig)
{
    (void)sig;
    fprintf(stderr, "Android-like stale transact timeout\n");
    _exit(124);
}

int main(int argc, char **argv)
{
    const char *service_name = argc >= 2 ? argv[1] : "test.android.service";
    pid_t service_pid = argc >= 3 ? (pid_t)atoi(argv[2]) : -1;

    char reply[1024];

    android::sp<android::IServiceManager> sm = android::defaultServiceManager();
    if (!sm) {
        fprintf(stderr, "Android-like stale defaultServiceManager failed\n");
        return 1;
    }

    printf("Android-like stale defaultServiceManager OK\n");

    if (!sm->listServicesContains(android::String16(service_name))) {
        fprintf(stderr,
                "Android-like stale missing service before kill: %s\n",
                service_name);
        return 1;
    }

    android::sp<android::IBinder> binder =
        sm->getService(android::String16(service_name));

    if (!binder || !binder->valid()) {
        fprintf(stderr, "Android-like stale getService failed before kill\n");
        return 1;
    }

    printf("ANDROID_LIKE_HANDLE_ACQUIRE_OK stale handle=%u\n",
           binder->handle());

    android::sp<IEchoService> echo =
        interface_cast_echo(std::move(binder));

    if (!echo) {
        fprintf(stderr, "Android-like stale interface_cast failed\n");
        return 1;
    }

    if (echo->echoText("hello before service death", reply, sizeof(reply)) != 0) {
        fprintf(stderr, "Android-like stale pre-kill echo failed\n");
        return 1;
    }

    printf("Android-like stale pre-kill reply=%s\n", reply);

    if (kill_service_process(service_pid) != 0)
        return 1;

    printf("ANDROID_LIKE_SERVICE_KILLED_OK pid=%ld\n", (long)service_pid);

    /*
     * The old remote handle should not be usable after the serving process dies.
     * We arm a timeout so a driver/sidecar regression cannot hang the smoke.
     */
    signal(SIGALRM, timeout_handler);
    alarm(10);

    memset(reply, 0, sizeof(reply));
    int rc = echo->echoText("hello through stale handle", reply, sizeof(reply));

    alarm(0);

    if (rc == 0) {
        fprintf(stderr,
                "Android-like stale handle unexpectedly succeeded reply=%s\n",
                reply);
        return 1;
    }

    printf("ANDROID_LIKE_STALE_HANDLE_DETECTED_OK rc=%d\n", rc);

    /*
     * Releasing a dead/stale remote is best-effort. The important invariant is
     * that the stale transact failed cleanly and the process stayed alive.
     */
    (void)echo->releaseRemote();

    printf("ANDROID_LIKE_STALE_HANDLE_CLIENT_OK\n");
    return 0;
}
