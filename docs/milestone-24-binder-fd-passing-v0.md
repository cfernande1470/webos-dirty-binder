# Milestone 24: Binder FD passing v0

Goal: prove file descriptor passing through Binder.

Milestone 23 proved:

    getChild() returns unique Binder objects
    each object has exact BR_RELEASE / BR_DECREFS lifecycle
    32 clients passed with exact object cleanup

Milestone 24 proves:

    client creates a pipe
    client writes payload into pipe
    client passes read-fd through Binder as BINDER_TYPE_FD
    service receives translated local fd
    service reads payload from received fd
    service replies with AIDL-like exception code + String16 result
    client validates result
    repeated FD passing works

Default:

    ROUNDS=16

Target markers:

    BINDER_FD_SERVICE_REGISTERED
    BINDER_FD_OBJECT_SENT
    BINDER_FD_RECEIVED_OK
    BINDER_FD_READ_OK
    BINDER_FD_CLIENT_ROUND_OK
    BINDER_FD_CLIENT_SMOKE_OK
    BINDER_FD_SMOKE_TV_OK

Non-goals:

    no ashmem
    no memfd
    no Parcelable
    no Java framework
    no Android rootfs
    no SELinux policy
