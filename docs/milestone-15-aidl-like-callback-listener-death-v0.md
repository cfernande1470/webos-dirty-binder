# Milestone 15: AIDL-like callback listener death v0

Goal: prove Android-like listener death detection.

Milestone 14 proved:

    AIDL-like registerListener(listener)
    listener Binder object passed inside Parcel
    service calls listener
    listener runs on client Binder looper thread
    listener replies with AIDL-like no-exception + String16 ack

Milestone 15 proves:

    service requests death notification for listener handle
    service keeps listener handle alive after callback
    client process exits
    service receives BR_DEAD_BINDER for listener
    service sends BC_DEAD_BINDER_DONE
    service releases listener handle

Target markers:

    AIDL_LIKE_CALLBACK_LISTENER_DEATH_MODE
    AIDL_LIKE_CALLBACK_LISTENER_DEATH_REQUESTED
    AIDL_LIKE_CALLBACK_LISTENER_THREAD_OK
    AIDL_LIKE_CALLBACK_LISTENER_REPLY_OK
    AIDL_LIKE_CALLBACK_LISTENER_DEATH_OK
    AIDL_LIKE_CALLBACK_LISTENER_DEATH_SMOKE_TV_OK

Non-goals:

    no generated AIDL
    no Java framework
    no zygote
    no Android rootfs
    no SELinux policy
