#ifndef WEBOS_DIRTY_BINDER_LIBBINDER_LITE_HPP
#define WEBOS_DIRTY_BINDER_LIBBINDER_LITE_HPP

#include <stdint.h>
#include <stddef.h>

namespace android_lite {

class Parcel {
public:
    Parcel();

    void reset();
    int writeBytes(const void *data, size_t size);
    int writeInt32(int32_t value);
    int writeCString(const char *str);
    int writeString16Ascii(const char *str);
    int writeInterfaceToken(const char *descriptor);

    const void *data() const;
    size_t size() const;

    int assign(const void *data, size_t size);

    int readSidecarTextReply(uint32_t *status, const char **text) const;

private:
    static const size_t kCapacity = 4096;

    unsigned char data_[kCapacity];
    size_t size_;
};


class BinderDriver {
public:
    BinderDriver();

    int fd() const;

private:
    int fd_;
};

class BpBinder {
public:
    BpBinder();
    BpBinder(int fd, uint32_t handle);

    bool valid() const;
    uint32_t handle() const;

    int transact(uint32_t code, const Parcel &data, Parcel *reply) const;
    int transactEcho() const;

private:
    int fd_;
    uint32_t handle_;
};

class ServiceManagerProxy {
public:
    explicit ServiceManagerProxy(BinderDriver &driver);

    bool listServicesContains(const char *name);
    BpBinder checkService(const char *name);
    BpBinder getService(const char *name);
    int addService(const char *name, const BpBinder &service);

private:
    BinderDriver &driver_;
};

ServiceManagerProxy defaultServiceManager(BinderDriver &driver);


class BpEchoService {
public:
    explicit BpEchoService(const BpBinder &binder);

    bool valid() const;
    int echo(const char *message, Parcel *reply) const;
    int echoText(const char *message, char *out, size_t out_len) const;

private:
    BpBinder binder_;
};


}  // namespace android_lite

#endif  // WEBOS_DIRTY_BINDER_LIBBINDER_LITE_HPP
