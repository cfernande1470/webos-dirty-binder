# Milestone 13: AIDL-like stale handle recovery v0

Goal: prove Android-like client recovery from a stale Binder service handle.

Milestone 12 proved:

    service death notification
    service re-registration
    new clients can resolve the new service handle

Milestone 13 proves:

    one long-lived client resolves a service handle
    service dies
    client tries old handle
    Binder returns dead/failed reply
    client releases old handle
    service re-registers under same name
    client re-resolves service
    client calls successfully using the new handle

Target markers:

    AIDL_LIKE_STALE_INITIAL_OK
    AIDL_LIKE_STALE_READY_TO_KILL
    AIDL_LIKE_STALE_HANDLE_DEAD_REPLY_OK
    AIDL_LIKE_STALE_RERESOLVE_OK
    AIDL_LIKE_STALE_AFTER_RECOVERY_OK
    AIDL_LIKE_STALE_SMOKE_OK
    AIDL_LIKE_STALE_SMOKE_TV_OK

Non-goals:

    no Java framework
    no Android system_server
    no zygote
    no init.rc supervision
    no SELinux policy
