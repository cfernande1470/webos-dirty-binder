# Milestone 19: Binder meta transactions v0

Goal: prove Android/libbinder-style Binder meta transactions.

Milestone 18 proved:

    malformed AIDL-like calls return non-zero exception codes
    service survives bad descriptor, unknown code, truncated parcel, bad args
    valid call still works after negative tests

Milestone 19 proves:

    service handles INTERFACE_TRANSACTION
    client can query interface descriptor from Binder object
    descriptor matches expected AIDL-like interface
    normal AIDL-like calls still work after descriptor query

Target markers:

    BINDER_META_SERVICE_REGISTERED
    BINDER_META_INTERFACE_TRANSACTION_OK
    BINDER_META_DESCRIPTOR_OK
    BINDER_META_AIDL_RECOVERY_OK
    BINDER_META_SMOKE_TV_OK

Non-goals:

    no generated AIDL
    no Java framework
    no full libbinder
    no Parcelable
    no SELinux policy
    no Android rootfs
