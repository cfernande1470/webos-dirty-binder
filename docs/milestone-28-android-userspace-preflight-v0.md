# Milestone 28: Android userspace preflight v0

Goal: decide whether an Android userspace/rootfs sidecar is viable on LG webOS without flashing or replacing webOS.

Milestone 27 proved:

    16 concurrent clients
    1000 TF_ONE_WAY notify(...) each
    16000 service-side one-way transactions processed

Milestone 24 remains quarantined:

    BINDER_TYPE_FD returned BR_FAILED_REPLY
    TV reboot was observed
    FD passing must not be used by default

Milestone 28 proves/checks:

    /dev/binder exists and responds to BINDER_VERSION
    /dev/hwbinder and /dev/vndbinder status
    memfd_create availability
    eventfd/signalfd/epoll availability
    tmpfs mount in sidecar area
    proc mount in sidecar area
    devpts mount in sidecar area
    mount namespace unshare in child process
    current kernel/filesystem/cgroup capabilities are captured

Target markers:

    ANDROID_PREFLIGHT_BINDER_DEVICE_OK
    ANDROID_PREFLIGHT_BINDER_VERSION_OK
    ANDROID_PREFLIGHT_MEMFD_OK
    ANDROID_PREFLIGHT_EVENTFD_OK
    ANDROID_PREFLIGHT_SIGNALFD_OK
    ANDROID_PREFLIGHT_EPOLL_OK
    ANDROID_PREFLIGHT_TMPFS_MOUNT_OK
    ANDROID_PREFLIGHT_PROC_MOUNT_OK
    ANDROID_PREFLIGHT_DEVPTS_MOUNT_OK
    ANDROID_PREFLIGHT_MOUNT_NS_OK
    ANDROID_USERSPACE_PREFLIGHT_SMOKE_TV_OK

Non-goals:

    no Android framework
    no zygote
    no FD passing
    no flashing
    no partition writes
    no bootloader changes
