# Milestone 9: Binder callback stress v0

Goal: prove the sidecar Binder stack survives multiple concurrent Android-like clients.

Milestone 7 proved:

    synchronous callback roundtrip

Milestone 8 proved:

    callback dispatch on Binder looper thread using TF_ONE_WAY registration

Milestone 9 proves:

    one ServiceManager
    one registered callback service
    N concurrent client processes
    each client owns a local Binder callback object
    each client registers with TF_ONE_WAY
    service calls each callback handle
    each client looper thread replies
    all clients exit successfully

Target markers:

    ANDROID_LIKE_STRESS_CLIENTS_OK
    ANDROID_LIKE_STRESS_SERVICE_COUNTS_OK
    ANDROID_LIKE_STRESS_SMOKE_TV_OK

Default:

    CLIENTS=8

Non-goals:

    no Android rootfs
    no zygote
    no Java framework
    no SELinux policy
    no hwbinder/vndbinder
