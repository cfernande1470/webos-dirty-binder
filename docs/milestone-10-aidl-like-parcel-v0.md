# Milestone 10: AIDL-like Parcel v0

Goal: move from custom Binder payloads to Android/AIDL-like Parcel framing.

Milestone 7 proved:

    synchronous Binder callbacks

Milestone 8 proved:

    callback dispatch on Binder looper thread

Milestone 9 proved:

    32 concurrent callback clients

Milestone 10 proves:

    Android-like interface descriptor token
    request Parcel parsing
    reply Parcel with exception code
    String16-style ASCII transport
    int32 transport
    multiple transactions against one service

Service:

    test.android.aidl

Methods:

    echo(String msg) -> String
    add(int32 a, int32 b) -> int32

Target markers:

    AIDL_LIKE_SERVICE_REGISTERED
    AIDL_LIKE_ECHO_OK
    AIDL_LIKE_ADD_OK
    AIDL_LIKE_EXCEPTION_CODE_OK
    AIDL_LIKE_CLIENT_SMOKE_OK
    AIDL_LIKE_SMOKE_TV_OK

Non-goals:

    no generated AIDL
    no Java framework
    no zygote
    no Android rootfs
    no SELinux policy
