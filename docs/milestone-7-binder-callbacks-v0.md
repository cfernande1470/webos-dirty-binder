# Milestone 7: Binder callbacks v0

Goal: prove reverse Binder calls on LG webOS.

Flow:

    client owns local callback Binder object
    client calls service.registerCallback(callback)
    service receives callback as BINDER_TYPE_HANDLE
    service transacts back into callback
    client receives BR_TRANSACTION while waiting for original BR_REPLY
    client replies
    service receives callback BR_REPLY
    service replies to original client call

Target markers:

    ANDROID_LIKE_CALLBACK_SERVICE_REGISTERED
    ANDROID_LIKE_CALLBACK_REGISTER_OK
    ANDROID_LIKE_CALLBACK_HANDLE_OK
    ANDROID_LIKE_CALLBACK_TRANSACTION_OK
    ANDROID_LIKE_CALLBACK_REPLY_OK
    ANDROID_LIKE_CALLBACK_SMOKE_OK

Non-goals:

    no zygote
    no Java framework
    no SELinux policy
    no Android rootfs
    no partition writes
