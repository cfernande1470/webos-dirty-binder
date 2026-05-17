# Milestone 23: AIDL-like unique returned Binder objects lifecycle v0

Goal: prove per-call returned Binder object lifecycle with unique local Binder nodes.

Milestone 22 proved:

    returned Binder object lifecycle is observable
    BR_INCREFS / BR_ACQUIRE / BR_RELEASE / BR_DECREFS are delivered
    singleton returned object refs may be coalesced by Binder

Milestone 23 proves:

    getChild() returns a unique Binder object per client
    each returned object has unique ptr/cookie identity
    N clients receive N different child handles
    each client calls child.echo(...)
    each client releases strong and weak refs
    service observes exact release/decrefs cleanup for N unique child objects

Default:

    CLIENTS=16

Target markers:

    AIDL_LIKE_BINDER_RETURN_UNIQUE_MODE
    AIDL_LIKE_BINDER_RETURN_CHILD_OBJECT_SENT
    AIDL_LIKE_BINDER_RETURN_CHILD_CALL_OK
    AIDL_LIKE_BINDER_RETURN_CHILD_RELEASE
    AIDL_LIKE_BINDER_RETURN_CHILD_DECREFS
    AIDL_LIKE_BINDER_RETURN_UNIQUE_CHILD_LIFECYCLE_EXACT_OK
    AIDL_LIKE_BINDER_RETURN_UNIQUE_LIFECYCLE_SMOKE_TV_OK

Non-goals:

    no generated AIDL
    no Java framework
    no Parcelable
    no zygote
    no Android rootfs
    no SELinux policy
