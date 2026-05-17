# Milestone 18: AIDL-like negative/error semantics v0

Goal: prove Android/AIDL-like error behavior for malformed or invalid transactions.

Milestone 17 proved:

    listener registry
    broadcast to N listeners
    explicit unregister
    clear death notification
    all listeners cleaned without relying on process death

Milestone 18 proves:

    bad interface descriptor returns non-zero exception code
    unknown transaction code returns non-zero exception code
    truncated Parcel returns non-zero exception code
    malformed args return non-zero exception code
    service survives all negative tests
    valid call still works after negative tests

Target markers:

    AIDL_LIKE_NEGATIVE_SERVICE_REGISTERED
    AIDL_LIKE_NEGATIVE_BAD_DESCRIPTOR_OK
    AIDL_LIKE_NEGATIVE_UNKNOWN_CODE_OK
    AIDL_LIKE_NEGATIVE_TRUNCATED_PARCEL_OK
    AIDL_LIKE_NEGATIVE_BAD_ARGS_OK
    AIDL_LIKE_NEGATIVE_RECOVERY_VALID_CALL_OK
    AIDL_LIKE_NEGATIVE_CLIENT_SMOKE_OK
    AIDL_LIKE_NEGATIVE_SMOKE_TV_OK

Non-goals:

    no Java exceptions
    no generated AIDL
    no Parcelable support
    no SELinux policy
    no Android rootfs
