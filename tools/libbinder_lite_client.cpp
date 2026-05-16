#include "libbinder_lite.hpp"

#include <stdio.h>

int main(int argc, char **argv)
{
    const char *name = argc >= 2 ? argv[1] : "test.aosp";
    const char *alias = "test.aosp.alias";

    android_lite::BinderDriver driver;
    android_lite::ServiceManagerProxy sm =
        android_lite::defaultServiceManager(driver);

    printf("libbinder-lite API defaultServiceManager OK\n");

    if (!sm.listServicesContains(name))
        return 1;

    android_lite::BpBinder checked = sm.checkService(name);
    if (!checked.valid()) {
        fprintf(stderr, "libbinder-lite API checkService returned invalid binder\n");
        return 1;
    }

    printf("libbinder-lite API checkService got handle=%u\n", checked.handle());

    if (checked.transactEcho() != 0)
        return 1;

    printf("LIBBINDER_LITE_CHECK_SERVICE_OK\n");

    android_lite::BpBinder got = sm.getService(name);
    if (!got.valid()) {
        fprintf(stderr, "libbinder-lite API getService returned invalid binder\n");
        return 1;
    }

    printf("libbinder-lite API getService got handle=%u\n", got.handle());

    if (got.transactEcho() != 0)
        return 1;

    printf("LIBBINDER_LITE_GET_SERVICE_OK\n");

    if (sm.addService(alias, got) != 0)
        return 1;

    printf("LIBBINDER_LITE_ADD_SERVICE_OK\n");

    if (!sm.listServicesContains(alias))
        return 1;

    android_lite::BpBinder aliasBinder = sm.checkService(alias);
    if (!aliasBinder.valid()) {
        fprintf(stderr, "libbinder-lite API alias checkService returned invalid binder\n");
        return 1;
    }

    printf("libbinder-lite API alias checkService got handle=%u\n",
           aliasBinder.handle());

    if (aliasBinder.transactEcho() != 0)
        return 1;

    printf("LIBBINDER_LITE_ALIAS_SERVICE_OK\n");
    printf("LIBBINDER_LITE_API_CLIENT_OK\n");
    printf("LIBBINDER_LITE_CLIENT_OK\n");
    return 0;
}
