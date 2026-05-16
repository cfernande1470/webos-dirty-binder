#include "libbinder_lite.hpp"

#include <stdio.h>

int main(int argc, char **argv)
{
    const char *service_name = argc >= 2 ? argv[1] : "test.aidl";
    const char *message = argc >= 3 ? argv[2] : "hello from AIDL-lite client";
    char reply[1024];

    android_lite::BinderDriver driver;
    android_lite::ServiceManagerProxy sm =
        android_lite::defaultServiceManager(driver);

    printf("AIDL-lite client defaultServiceManager OK\n");

    if (!sm.listServicesContains(service_name)) {
        fprintf(stderr, "AIDL-lite service not found in listServices: %s\n", service_name);
        return 1;
    }

    android_lite::BpBinder binder = sm.getService(service_name);
    if (!binder.valid()) {
        fprintf(stderr, "AIDL-lite getService failed: %s\n", service_name);
        return 1;
    }

    printf("AIDL-lite getService(%s) handle=%u\n",
           service_name,
           binder.handle());

    android_lite::BpEchoService echo(binder);
    if (!echo.valid()) {
        fprintf(stderr, "AIDL-lite BpEchoService invalid\n");
        return 1;
    }

    if (echo.echoText(message, reply, sizeof(reply)) != 0) {
        fprintf(stderr, "AIDL-lite echoText failed\n");
        return 1;
    }

    printf("AIDL-lite echoText reply=%s\n", reply);
    printf("AIDL_LITE_ECHO_CLIENT_OK\n");
    return 0;
}
