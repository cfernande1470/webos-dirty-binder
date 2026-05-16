#include "android_like_binder.hpp"
#include "android_like_echo_iface.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <utility>

int main(int argc, char **argv)
{
    const char *service_name = argc >= 2 ? argv[1] : "test.android.service";
    int rounds = argc >= 3 ? atoi(argv[2]) : 10;

    if (rounds <= 0)
        rounds = 1;

    android::sp<android::IServiceManager> sm = android::defaultServiceManager();
    if (!sm) {
        fprintf(stderr, "Android-like lifecycle defaultServiceManager failed\n");
        return 1;
    }

    printf("Android-like lifecycle defaultServiceManager OK\n");

    for (int i = 0; i < rounds; i++) {
        char msg[128];
        char reply[1024];

        snprintf(msg, sizeof(msg), "hello lifecycle round %d", i);

        if (!sm->listServicesContains(android::String16(service_name))) {
            fprintf(stderr,
                    "Android-like lifecycle missing service %s round=%d\n",
                    service_name,
                    i);
            return 1;
        }

        android::sp<android::IBinder> binder =
            sm->getService(android::String16(service_name));

        if (!binder || !binder->valid()) {
            fprintf(stderr,
                    "Android-like lifecycle getService failed round=%d\n",
                    i);
            return 1;
        }

        printf("ANDROID_LIKE_HANDLE_ACQUIRE_OK round=%d handle=%u\n",
               i,
               binder->handle());

        android::sp<IEchoService> echo =
            interface_cast_echo(std::move(binder));

        if (!echo) {
            fprintf(stderr,
                    "Android-like lifecycle interface_cast failed round=%d\n",
                    i);
            return 1;
        }

        if (echo->echoText(msg, reply, sizeof(reply)) != 0) {
            fprintf(stderr,
                    "Android-like lifecycle echoText failed round=%d\n",
                    i);
            return 1;
        }

        printf("Android-like lifecycle reply round=%d text=%s\n",
               i,
               reply);

        if (echo->releaseRemote() != 0) {
            fprintf(stderr,
                    "Android-like lifecycle releaseRemote failed round=%d\n",
                    i);
            return 1;
        }
    }

    printf("ANDROID_LIKE_LIFECYCLE_CLIENT_OK rounds=%d\n", rounds);
    return 0;
}
