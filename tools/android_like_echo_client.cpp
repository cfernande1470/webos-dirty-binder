#include "android_like_binder.hpp"

#include <stdio.h>
#include <utility>

#define SC_CODE_ECHO 0x4543484fU

class IEchoService {
public:
    virtual ~IEchoService() {}
    virtual int echoText(const char *message, char *out, size_t out_len) = 0;
};

class BpEchoService : public IEchoService {
public:
    explicit BpEchoService(android::sp<android::IBinder> remote)
        : remote_(std::move(remote))
    {
    }

    int echoText(const char *message, char *out, size_t out_len) override {
        android::Parcel data;
        android::Parcel reply;
        uint32_t status = 0xffffffffU;
        const char *text = NULL;

        if (!remote_ || !remote_->valid()) {
            fprintf(stderr, "Android-like BpEchoService invalid remote\n");
            return 1;
        }

        if (out && out_len)
            out[0] = '\0';

        if (data.writeCString(message ? message : "") != 0)
            return -1;

        if (remote_->transact(SC_CODE_ECHO, data, &reply) != 0)
            return 1;

        if (reply.readSidecarTextReply(&status, &text) != 0)
            return -1;

        printf("Android-like echo reply status=%u text=%s\n",
               status,
               text ? text : "(null)");

        if (status != 0)
            return 1;

        if (out && out_len)
            snprintf(out, out_len, "%s", text ? text : "");

        return 0;
    }

private:
    android::sp<android::IBinder> remote_;
};

static android::sp<IEchoService> interface_cast_echo(android::sp<android::IBinder> binder)
{
    if (!binder || !binder->valid())
        return android::sp<IEchoService>();

    return android::sp<IEchoService>(new BpEchoService(std::move(binder)));
}

int main(int argc, char **argv)
{
    const char *service_name = argc >= 2 ? argv[1] : "test.aidl.service";
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
    printf("ANDROID_LIKE_API_CLIENT_OK\n");
    return 0;
}
