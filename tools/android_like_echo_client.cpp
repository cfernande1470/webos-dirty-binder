#include "android_like_binder.hpp"
#include "android_like_echo_iface.hpp"

#include <stdio.h>
#include <utility>

int main(int argc, char **argv)
{
    const char *service_name = argc >= 2 ? argv[1] : "test.android.like";
    const char *message = argc >= 3 ? argv[2] : "hello from Android-like API client";
    char reply[1024];

    android::sp<android::IServiceManager> sm =
        android::defaultServiceManager();

    if (!sm) {
        fprintf(stderr, "Android-like defaultServiceManager failed\n");
        return 1;
    }

    printf("Android-like defaultServiceManager OK\n");

    if (!sm->listServicesContains(android::String16(service_name))) {
        fprintf(stderr, "Android-like listServices missing %s\n", service_name);
        return 1;
    }

    android::sp<android::IBinder> binder =
        sm->getService(android::String16(service_name));

    if (!binder || !binder->valid()) {
        fprintf(stderr, "Android-like getService failed: %s\n", service_name);
        return 1;
    }

    printf("Android-like getService(%s) handle=%u\n",
           service_name,
           binder->handle());

    android::sp<IEchoService> echo =
        interface_cast_echo(std::move(binder));

    if (!echo) {
        fprintf(stderr, "Android-like interface_cast<IEchoService> failed\n");
        return 1;
    }

    if (echo->echoText(message, reply, sizeof(reply)) != 0) {
        fprintf(stderr, "Android-like echoText failed\n");
        return 1;
    }

    printf("Android-like echoText reply=%s\n", reply);
    printf("ANDROID_LIKE_AIDL_WIRE_OK\n");
    printf("ANDROID_LIKE_INTERFACE_CONTRACT_OK\n");
    printf("ANDROID_LIKE_API_CLIENT_OK\n");
    return 0;
}
