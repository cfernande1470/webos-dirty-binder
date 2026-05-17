# Milestone: Android userspace preflight v1

Goal: validate that an Android-like userspace process can run from a rootfs-style directory while webOS remains the host.

This is not a full Android install.

Architecture:

    webOS host:
        owns compositor
        owns drivers
        owns input/audio/video
        loads Binder sidecar
        runs mini_servicemgr
        runs ParcelFileDescriptor-lite service

    Android-like rootfs:
        /media/internal/android-rootfs
        static test binary inside /bin
        /dev/binder device node
        /run/parcel_fd_lite.sock UNIX socket
        no zygote
        no SurfaceFlinger
        no PackageManager
        no SELinux policy

Test flow:

    host creates rootfs skeleton
    host exposes /dev/binder inside rootfs
    host starts mini_servicemgr
    host starts parcel_fd_lite_service with socket inside rootfs/run
    host runs android_userspace_preflight_v1 from chroot when possible
    preflight process opens /dev/binder
    preflight process gets test.android.parcelfd from ServiceManager
    preflight process sends FD using ParcelFileDescriptor-lite
    service reads FD payload
    preflight receives Binder reply

Target markers:

    ANDROID_USERSPACE_PREFLIGHT_V1_STARTED
    ANDROID_USERSPACE_PREFLIGHT_V1_BINDER_HANDLE_OK
    ANDROID_USERSPACE_PREFLIGHT_V1_PARCELFD_WRITE_OK
    ANDROID_USERSPACE_PREFLIGHT_V1_BINDER_REPLY_OK
    ANDROID_USERSPACE_PREFLIGHT_V1_SMOKE_OK
    ANDROID_USERSPACE_PREFLIGHT_V1_TV_OK

Non-goals:

    no Android rootfs package yet
    no zygote
    no app_process
    no SurfaceFlinger
    no hwbinder/vndbinder
    no HALs
    no direct BINDER_TYPE_FD
