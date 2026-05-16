#ifndef WEBOS_DIRTY_BINDER_LIBBINDER_LITE_HPP
#define WEBOS_DIRTY_BINDER_LIBBINDER_LITE_HPP

#include <stdint.h>

namespace android_lite {

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

}  // namespace android_lite

#endif  // WEBOS_DIRTY_BINDER_LIBBINDER_LITE_HPP
