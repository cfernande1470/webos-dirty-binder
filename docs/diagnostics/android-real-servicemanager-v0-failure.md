# Android real servicemanager v0 failure

Status: Android real `/system/bin/servicemanager` does not work yet.

Observed:

    Android synthetic rootfs works.
    Android linker works.
    /system/bin/toybox works.
    /system/bin/sh works.
    /dev/binder is visible inside chroot.

Failure when testing real Android servicemanager:

    binder: ioctl 40046210 ... returned -22
    binder: ioctl 4018620d ... returned -22

Meaning:

    -22 == EINVAL

Likely causes:

    0x40046210:
        modern Binder ioctl, likely BINDER_ENABLE_ONEWAY_SPAM_DETECTION.
        Can probably be accepted as no-op.

    0x4018620d:
        likely BINDER_SET_CONTEXT_MGR_EXT.
        Android modern servicemanager uses this instead of legacy BINDER_SET_CONTEXT_MGR.

Conclusion:

    Keep mini_servicemgr as the active compatibility shim for now.

Next milestone:

    Binder modern ioctl compat v0:
        accept/no-op harmless modern ioctls
        implement BINDER_SET_CONTEXT_MGR_EXT compatibility
        retry Android real servicemanager smoke
