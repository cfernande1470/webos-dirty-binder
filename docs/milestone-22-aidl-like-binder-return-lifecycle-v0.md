# Milestone 22: AIDL-like Binder return object lifecycle v0

Goal: prove returned Binder objects have observable Binder ref lifecycle.

Milestone 21 proved:

    factory service returns Binder child objects
    32 concurrent clients receive child handles
    clients call child.echo(...)
    all clients succeed

Milestone 22 proves:

    service observes BR_INCREFS / BR_ACQUIRE for returned child object
    service observes BR_RELEASE when clients release returned child handles
    service confirms returned singleton object lifecycle cleanup after clients finish
    normal child calls still work during lifecycle tracking

Default:

    CLIENTS=16

Target markers:

    AIDL_LIKE_BINDER_RETURN_LIFECYCLE_SERVICE_REGISTERED
    AIDL_LIKE_BINDER_RETURN_CHILD_OBJECT_SENT
    AIDL_LIKE_BINDER_RETURN_CHILD_CALL_OK
    AIDL_LIKE_BINDER_RETURN_CHILD_INCREFS
    AIDL_LIKE_BINDER_RETURN_CHILD_ACQUIRE
    AIDL_LIKE_BINDER_RETURN_CHILD_RELEASE
    AIDL_LIKE_BINDER_RETURN_CHILD_DECREFS optional
    AIDL_LIKE_BINDER_RETURN_CHILD_LIFECYCLE_RELEASE_OK
    AIDL_LIKE_BINDER_RETURN_LIFECYCLE_SMOKE_TV_OK

Non-goals:

    no Java framework
    no generated AIDL
    no Parcelable
    no Android rootfs
    no SELinux policy
