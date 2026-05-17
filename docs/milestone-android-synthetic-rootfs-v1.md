# Milestone: Android synthetic rootfs v1

Goal: run basic Android binaries from Waydroid-style system/vendor images using a synthetic chroot root.

Findings:

    system.img layout:
        /system/bin
        /system/lib64
        /system/apex

    vendor.img layout:
        /bin
        /lib64
        /etc
        /build.prop

Problem:

    Android binaries use linker paths through /apex.
    A naive chroot without /apex fails with:
        No such file or directory

Synthetic rootfs layout:

    /system  -> bind mount system_raw/system
    /vendor  -> bind mount vendor_raw
    /apex    -> bind mount system_raw/system/apex
    /dev     -> minimal dev nodes, including binder
    /proc    -> procfs
    /sys     -> sysfs if possible
    /data    -> writable USB-backed data dir
    /cache   -> writable USB-backed cache dir
    /tmp     -> writable tmp dir

Target markers:

    ANDROID_SYNTH_ROOTFS_SYSTEM_RAW_MOUNT_OK
    ANDROID_SYNTH_ROOTFS_VENDOR_RAW_MOUNT_OK
    ANDROID_SYNTH_ROOTFS_BIND_SYSTEM_OK
    ANDROID_SYNTH_ROOTFS_BIND_VENDOR_OK
    ANDROID_SYNTH_ROOTFS_BIND_APEX_OK
    ANDROID_SYNTH_ROOTFS_TOYBOX_OK
    ANDROID_SYNTH_ROOTFS_SH_OK
    ANDROID_SYNTH_ROOTFS_GETPROP_ATTEMPTED
    ANDROID_SYNTH_ROOTFS_V1_OK

Non-goals:

    no zygote
    no SurfaceFlinger
    no hwservicemanager
    no hwbinder/vndbinder yet
    no Android init boot
