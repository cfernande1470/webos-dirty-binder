#ifndef WEBOS_DIRTY_BINDER_ANDROID_LIKE_ECHO_IFACE_HPP
#define WEBOS_DIRTY_BINDER_ANDROID_LIKE_ECHO_IFACE_HPP

#include "android_like_binder.hpp"

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <utility>

#define ANDROID_LIKE_ECHO_DESCRIPTOR "webos.dirtybinder.IEchoService"
#define ANDROID_LIKE_TRANSACTION_ECHO_TEXT 1U

namespace android_like_echo_wire {

struct ParcelView {
    const unsigned char *data;
    size_t size;
    size_t pos;
};

static inline size_t align4(size_t n)
{
    return (n + 3U) & ~3U;
}

static inline int readI32(ParcelView *v, int32_t *out)
{
    if (!v || !out || v->pos + sizeof(*out) > v->size)
        return -1;

    memcpy(out, v->data + v->pos, sizeof(*out));
    v->pos += sizeof(*out);
    return 0;
}

static inline int readString16Ascii(ParcelView *v, char *out, size_t out_len)
{
    int32_t len;
    size_t bytes;
    size_t padded;
    size_t i;

    if (!v || !out || out_len == 0)
        return -1;

    out[0] = '\0';

    if (readI32(v, &len) != 0)
        return -1;

    if (len < 0)
        return 0;

    bytes = ((size_t)len + 1U) * 2U;
    padded = align4(bytes);

    if (v->pos + padded > v->size)
        return -1;

    for (i = 0; i < (size_t)len && i + 1 < out_len; i++) {
        unsigned char lo = v->data[v->pos + i * 2U];
        unsigned char hi = v->data[v->pos + i * 2U + 1U];

        out[i] = hi == 0 ? (char)lo : '?';
    }

    out[i < out_len ? i : out_len - 1] = '\0';
    v->pos += padded;
    return 0;
}

static inline int writeEchoRequest(android::Parcel *data, const char *message)
{
    if (!data)
        return -1;

    if (data->writeInterfaceToken(ANDROID_LIKE_ECHO_DESCRIPTOR) != 0)
        return -1;

    if (data->writeCString(message ? message : "") != 0)
        return -1;

    return 0;
}

static inline int parseEchoRequest(const void *data,
                                   size_t size,
                                   const char **message_out,
                                   char *descriptor,
                                   size_t descriptor_len)
{
    ParcelView v;
    int32_t strict_policy;

    if (!data || !message_out || !descriptor || descriptor_len == 0)
        return -1;

    *message_out = "";

    v.data = (const unsigned char *)data;
    v.size = size;
    v.pos = 0;

    if (readI32(&v, &strict_policy) != 0)
        return -1;

    if (readString16Ascii(&v, descriptor, descriptor_len) != 0)
        return -1;

    if (strcmp(descriptor, ANDROID_LIKE_ECHO_DESCRIPTOR) != 0)
        return -1;

    if (v.pos >= v.size)
        return -1;

    *message_out = (const char *)(v.data + v.pos);
    return 0;
}

static inline int readEchoReply(android::Parcel *reply,
                                uint32_t *status,
                                const char **text)
{
    if (!reply || !status || !text)
        return -1;

    return reply->readSidecarTextReply(status, text);
}

} // namespace android_like_echo_wire

class IEchoService {
public:
    virtual ~IEchoService() {}
    virtual int echoText(const char *message, char *out, size_t out_len) = 0;
    virtual int releaseRemote() { return 0; }
    virtual int waitForRemoteDeath(uintptr_t cookie, int timeout_sec)
    {
        (void)cookie;
        (void)timeout_sec;
        return -1;
    }
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

        if (android_like_echo_wire::writeEchoRequest(&data, message) != 0)
            return -1;

        if (remote_->transact(ANDROID_LIKE_TRANSACTION_ECHO_TEXT, data, &reply) != 0)
            return 1;

        if (android_like_echo_wire::readEchoReply(&reply, &status, &text) != 0)
            return -1;

        printf("ANDROID_LIKE_ECHO_WIRE_HELPERS_OK\n");

        printf("Android-like echo reply status=%u text=%s\n",
               status,
               text ? text : "(null)");

        if (status != 0)
            return 1;

        if (out && out_len)
            snprintf(out, out_len, "%s", text ? text : "");

        return 0;
    }

    int releaseRemote() override
    {
        if (!remote_) {
            fprintf(stderr, "Android-like BpEchoService releaseRemote invalid remote\n");
            return -1;
        }

        return remote_->releaseHandle();
    }

    int waitForRemoteDeath(uintptr_t cookie, int timeout_sec) override
    {
        if (!remote_) {
            fprintf(stderr, "Android-like BpEchoService waitForRemoteDeath invalid remote\n");
            return -1;
        }

        return remote_->waitForDeathNotification(cookie, timeout_sec);
    }

private:
    android::sp<android::IBinder> remote_;
};

class BnEchoService : public IEchoService {
public:
    int handleTransaction(uint32_t code,
                          const void *data,
                          size_t size,
                          char *out,
                          size_t out_len)
    {
        const char *message = NULL;
        char descriptor[128];

        if (code != ANDROID_LIKE_TRANSACTION_ECHO_TEXT) {
            fprintf(stderr,
                    "Android-like BnEchoService unknown transaction code=0x%x\n",
                    code);
            return 1;
        }

        if (android_like_echo_wire::parseEchoRequest(data,
                                                size,
                                                &message,
                                                descriptor,
                                                sizeof(descriptor)) != 0) {
            fprintf(stderr,
                    "Android-like BnEchoService bad interface token or parcel\n");
            return 1;
        }

        printf("Android-like BnEchoService descriptor=%s message=%s\n",
               descriptor,
               message ? message : "");

        if (echoText(message ? message : "", out, out_len) != 0)
            return 1;

        printf("ANDROID_LIKE_BN_ECHO_TRANSACTION_OK\n");
        return 0;
    }

private:
};

static inline android::sp<IEchoService>
interface_cast_echo(android::sp<android::IBinder> binder)
{
    if (!binder || !binder->valid())
        return android::sp<IEchoService>();

    return android::sp<IEchoService>(new BpEchoService(std::move(binder)));
}

#endif  // WEBOS_DIRTY_BINDER_ANDROID_LIKE_ECHO_IFACE_HPP
