# Milestone: Android rootfs inspect v1

Goal: inspect the USB-backed Android/Waydroid-style images and test whether basic Android userspace binaries can run from chroot.

Previous state:

    Android rootfs preflight v1 passed.
    USB Android staging is available.
    system.img and vendor.img were downloaded/extracted to USB-backed android-images.
    webOS remains the host.
    Binder sidecar remains external to Android rootfs.

This milestone does:

    mount system.img read-only at /media/internal/android-rootfs/system
    mount vendor.img read-only at /media/internal/android-rootfs/vendor
    create minimal /dev entries
    create /proc, /sys, /run, /tmp, /data
    run simple chroot probes:
        /system/bin/linker64 existence
        /system/bin/toybox if available
        /system/bin/sh if available
        /system/bin/getprop if available
    list key Android bins and libraries

This milestone does NOT:

    start zygote
    start servicemanager from Android
    start SurfaceFlinger
    start hwservicemanager
    use hwbinder/vndbinder
    use direct BINDER_TYPE_FD
