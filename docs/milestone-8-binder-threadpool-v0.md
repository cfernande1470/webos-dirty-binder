# Milestone 8: Binder threadpool v0

Goal: prove Android-style callback dispatch on a Binder looper thread.

Milestone 7 proved:

    client main thread sends local Binder callback object
    service receives callback as handle
    service calls back into client
    client replies

Milestone 8 proves:

    client starts separate Binder looper thread
    client main thread registers callback with TF_ONE_WAY
    service calls callback handle
    Binder delivers callback BR_TRANSACTION to looper thread
    looper thread replies
    main thread observes callback completion marker

Target markers:

    ANDROID_LIKE_THREADPOOL_CLIENT_LOOPER_READY
    ANDROID_LIKE_THREADPOOL_ONEWAY_REGISTER_SENT
    ANDROID_LIKE_THREADPOOL_CALLBACK_THREAD_OK
    ANDROID_LIKE_THREADPOOL_MAIN_OBSERVED_CALLBACK_OK
    ANDROID_LIKE_THREADPOOL_SMOKE_OK

Non-goals:

    no zygote
    no Java framework
    no SELinux
    no Android rootfs
    no binderfs
    no hwbinder/vndbinder
