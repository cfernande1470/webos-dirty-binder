# Milestone: Android real servicemanager v0

Goal: test Android's real `/system/bin/servicemanager` from the synthetic rootfs.

Previous milestone:

    synthetic Android rootfs works
    /system, /vendor and /apex are mounted
    Android linker runs
    /system/bin/toybox runs
    /system/bin/sh runs
    getprop runs
    /dev/binder is visible

This milestone tests:

    stop mini_servicemgr
    start real Android /system/bin/servicemanager inside chroot
    register a sidecar Binder service against it
    query that service
    call ParcelFD-lite through it

This does NOT start:

    zygote
    SurfaceFlinger
    system_server
    hwservicemanager
    vndservicemanager
    Android init

Target markers:

    ANDROID_REAL_SM_STARTED
    ANDROID_REAL_SM_PROCESS_ALIVE
    ANDROID_REAL_SM_PARCELFD_SERVICE_REGISTER_ATTEMPT
    ANDROID_REAL_SM_PARCELFD_CLIENT_ATTEMPT
    ANDROID_REAL_SM_SMOKE_OK

Possible outcomes:

    servicemanager starts and accepts our service:
        real Android ServiceManager compatibility is promising.

    servicemanager starts but rejects addService/getService:
        keep mini_servicemgr as compatibility shim for now.

    servicemanager fails to start:
        inspect missing linkerconfig/properties/SELinux assumptions.
