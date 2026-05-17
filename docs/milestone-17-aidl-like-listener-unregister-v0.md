# Milestone 17: AIDL-like listener unregister v0

Goal: prove explicit listener unregister, not only death cleanup.

Milestone 16 proved:

    N clients register Binder listeners
    service stores listener handles
    service requests death notification per listener
    service broadcasts to all listeners
    service cleans all listeners on process death

Milestone 17 proves:

    N clients register Binder listeners
    service requests death notification per listener
    service broadcasts to all listeners
    each client explicitly unregisters after broadcast
    service clears death notification
    service releases listener handle
    all clients exit successfully

Default:

    CLIENTS=8

Target markers:

    AIDL_LIKE_LISTENER_UNREGISTER_REQUEST_SENT
    AIDL_LIKE_LISTENER_UNREGISTER_OK
    AIDL_LIKE_LISTENER_UNREGISTER_REPLY_OK
    AIDL_LIKE_LISTENER_UNREGISTER_CLIENT_SMOKE_OK
    AIDL_LIKE_LISTENER_UNREGISTER_ALL_OK
    AIDL_LIKE_LISTENER_UNREGISTER_SMOKE_TV_OK

Non-goals:

    no generated AIDL
    no Java framework
    no system_server
    no zygote
    no Android rootfs
    no SELinux policy
