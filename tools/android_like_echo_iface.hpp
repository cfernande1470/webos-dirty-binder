#ifndef WEBOS_DIRTY_BINDER_ANDROID_LIKE_ECHO_IFACE_HPP
#define WEBOS_DIRTY_BINDER_ANDROID_LIKE_ECHO_IFACE_HPP

#include "android_like_binder.hpp"

#include <stdio.h>
#include <utility>

#define ANDROID_LIKE_ECHO_DESCRIPTOR "webos.dirtybinder.IEchoService"
#define ANDROID_LIKE_TRANSACTION_ECHO_TEXT 1U

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

    int echoText(const char *message, char *out, size_t out_len) override
    {
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

        if (data.writeInterfaceToken(ANDROID_LIKE_ECHO_DESCRIPTOR) != 0)
            return -1;

        if (data.writeCString(message ? message : "") != 0)
            return -1;

        if (remote_->transact(ANDROID_LIKE_TRANSACTION_ECHO_TEXT, data, &reply) != 0)
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

static inline android::sp<IEchoService>
interface_cast_echo(android::sp<android::IBinder> binder)
{
    if (!binder || !binder->valid())
        return android::sp<IEchoService>();

    return android::sp<IEchoService>(new BpEchoService(std::move(binder)));
}

#endif  // WEBOS_DIRTY_BINDER_ANDROID_LIKE_ECHO_IFACE_HPP
