# Milestone 7: Binder callbacks v0

Goal: prove reverse Binder calls on LG webOS.

Flow:

    client process owns local callback Binder object
    client calls service.registerCallback(callback)
    service receives callback as Binder handle
    service calls callback.onEchoEvent(...)
    client receives BR_TRANSACTION
    client replies
    service receives BR_REPLY
    smoke test validates lifecycle

Target markers:

    ANDROID_LIKE_CALLBACK_REGISTER_OK
    ANDROID_LIKE_CALLBACK_HANDLE_OK
    ANDROID_LIKE_CALLBACK_TRANSACTION_OK
    ANDROID_LIKE_CALLBACK_REPLY_OK
    ANDROID_LIKE_CALLBACK_SMOKE_OK

Non-goals:

    no full Android userspace
    no zygote
    no PackageManager
    no SELinux policy
    no Java framework
    no persistent system partition writes
