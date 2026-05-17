# Android real servicemanager name/policy result

Result after Binder modern ioctl compatibility:

    Binder modern ioctls work:
        DIRTY_BINDER_IOCTL_COMPAT_V0 oneway spam detection noop
        DIRTY_BINDER_IOCTL_COMPAT_V0 set context mgr ext

    Android real servicemanager starts and stays alive.

    But addService/getService does not produce a usable registered service.

Observed:

    tested service names:
        test.android.parcelfd
        activity
        package
        media.metrics
        gpu
        surfaceflinger

    all return null handle on getService.

    Android `service list` reports only:
        manager: [android.os.IServiceManager]

Interpretation:

    The blocker moved from Binder ioctl compatibility to Android servicemanager
    policy/protocol compatibility.

Decision:

    Keep mini_servicemgr as the compatibility shim for now.
    Next: test Android real client tools against mini_servicemgr.
