# Milestone 14: AIDL-like callback listener v0

Goal: combine AIDL-like Parcel framing with Binder object callbacks.

Milestone 13 proved:

    stale service handle detection
    client re-resolves service after death
    client continues after service restart

Milestone 14 proves:

    client registers a listener object inside an AIDL-like Parcel
    service receives listener as BINDER_TYPE_HANDLE
    service calls listener using AIDL-like Parcel framing
    listener runs on client Binder looper thread
    listener replies with exception code + String16 payload
    service validates listener reply
    one-way registration avoids main-thread reentrant callback

Service:

    test.android.aidl.callback

Interfaces:

    webos.dirtybinder.IAidlCallbackDemo
        registerListener(IAidlCallbackListener listener)

    webos.dirtybinder.IAidlCallbackListener
        onEvent(String event) -> String

Target markers:

    AIDL_LIKE_CALLBACK_LISTENER_SERVICE_REGISTERED
    AIDL_LIKE_CALLBACK_LISTENER_HANDLE_OK
    AIDL_LIKE_CALLBACK_LISTENER_THREAD_OK
    AIDL_LIKE_CALLBACK_LISTENER_REPLY_OK
    AIDL_LIKE_CALLBACK_LISTENER_ONEWAY_REGISTER_SENT
    AIDL_LIKE_CALLBACK_LISTENER_MAIN_OBSERVED_OK
    AIDL_LIKE_CALLBACK_LISTENER_SMOKE_OK
    AIDL_LIKE_CALLBACK_LISTENER_SMOKE_TV_OK

Non-goals:

    no generated AIDL
    no Java framework
    no zygote
    no Android rootfs
    no SELinux policy
