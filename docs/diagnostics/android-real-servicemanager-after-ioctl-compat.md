# Android real servicemanager after ioctl compat

Status:

    Modern Binder ioctl compatibility works.

Confirmed dmesg markers:

    DIRTY_BINDER_IOCTL_COMPAT_V0 oneway spam detection noop enable=1
    DIRTY_BINDER_IOCTL_COMPAT_V0 set context mgr ext type=0x0 flags=0x0

Remaining issue:

    Android real servicemanager stays alive.
    ParcelFD-lite service attempts addService.
    Client getService returns a null handle.

Interpretation:

    The issue moved from Binder ioctl compatibility to Android ServiceManager policy/protocol compatibility.

Likely causes:

    service name not present in Android service_contexts
    addService returns an error that libbinder-lite does not parse yet
    SELinux/service_manager policy denies add/find
    modern ServiceManager reply format differs from mini_servicemgr assumptions

Next diagnostic:

    Try several service names:
        test.android.parcelfd
        activity
        package
        media.metrics
        gpu
        surfaceflinger

    Capture:
        servicemanager stdout/stderr
        ParcelFD-lite addService/getService logs
        dmesg Binder tail
        Android service_contexts snippets
