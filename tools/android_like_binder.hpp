#ifndef WEBOS_DIRTY_BINDER_ANDROID_LIKE_BINDER_HPP
#define WEBOS_DIRTY_BINDER_ANDROID_LIKE_BINDER_HPP

#include "libbinder_lite.hpp"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace android {

template <typename T>
class sp {
public:
    sp() : ptr_(NULL) {}
    explicit sp(T *ptr) : ptr_(ptr) {}

    sp(const sp &) = delete;
    sp &operator=(const sp &) = delete;

    sp(sp &&other) : ptr_(other.ptr_) {
        other.ptr_ = NULL;
    }

    sp &operator=(sp &&other) {
        if (this != &other) {
            delete ptr_;
            ptr_ = other.ptr_;
            other.ptr_ = NULL;
        }
        return *this;
    }

    ~sp() {
        delete ptr_;
    }

    T *get() const {
        return ptr_;
    }

    T *operator->() const {
        return ptr_;
    }

    explicit operator bool() const {
        return ptr_ != NULL;
    }

private:
    T *ptr_;
};

class String16 {
public:
    explicit String16(const char *value = "")
        : value_(value ? value : "")
    {
    }

    const char *c_str() const {
        return value_;
    }

private:
    const char *value_;
};

class Parcel {
public:
    Parcel() {}

    android_lite::Parcel &lite() {
        return parcel_;
    }

    const android_lite::Parcel &lite() const {
        return parcel_;
    }

    int writeCString(const char *value) {
        return parcel_.writeCString(value);
    }

    int writeInterfaceToken(const char *descriptor) {
        return parcel_.writeInterfaceToken(descriptor);
    }

    int readSidecarTextReply(uint32_t *status, const char **text) const {
        return parcel_.readSidecarTextReply(status, text);
    }

private:
    android_lite::Parcel parcel_;
};

class IBinder {
public:
    IBinder() {}
    explicit IBinder(const android_lite::BpBinder &binder)
        : binder_(binder)
    {
    }

    bool valid() const {
        return binder_.valid();
    }

    uint32_t handle() const {
        return binder_.handle();
    }

    int transact(uint32_t code, const Parcel &data, Parcel *reply) const {
        return binder_.transact(code,
                                data.lite(),
                                reply ? &reply->lite() : NULL);
    }

    const android_lite::BpBinder &lite() const {
        return binder_;
    }

private:
    android_lite::BpBinder binder_;
};

class IServiceManager {
public:
    explicit IServiceManager(android_lite::ServiceManagerProxy proxy)
        : proxy_(proxy)
    {
    }

    sp<IBinder> checkService(const String16 &name) {
        android_lite::BpBinder b = proxy_.checkService(name.c_str());
        if (!b.valid())
            return sp<IBinder>();
        return sp<IBinder>(new IBinder(b));
    }

    sp<IBinder> getService(const String16 &name) {
        android_lite::BpBinder b = proxy_.getService(name.c_str());
        if (!b.valid())
            return sp<IBinder>();
        return sp<IBinder>(new IBinder(b));
    }

    int addService(const String16 &name, const sp<IBinder> &service) {
        if (!service || !service->valid())
            return 1;
        return proxy_.addService(name.c_str(), service->lite());
    }

    bool listServicesContains(const String16 &name) {
        return proxy_.listServicesContains(name.c_str());
    }

private:
    android_lite::ServiceManagerProxy proxy_;
};

class ProcessState {
public:
    static ProcessState &self() {
        static ProcessState state;
        return state;
    }

    android_lite::BinderDriver &driver() {
        return driver_;
    }

private:
    ProcessState() {}

    android_lite::BinderDriver driver_;
};

inline sp<IServiceManager> defaultServiceManager() {
    android_lite::ServiceManagerProxy proxy =
        android_lite::defaultServiceManager(ProcessState::self().driver());
    return sp<IServiceManager>(new IServiceManager(proxy));
}

}  // namespace android

#endif  // WEBOS_DIRTY_BINDER_ANDROID_LIKE_BINDER_HPP
