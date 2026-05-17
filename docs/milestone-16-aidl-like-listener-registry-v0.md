# Milestone 16: AIDL-like listener registry + broadcast v0

Goal: prove Android-like listener registry behavior.

Milestone 15 proved:

    AIDL-like listener passed as Binder object
    service requests death notification
    service calls listener
    service receives listener death

Milestone 16 proves:

    N clients register listener Binder objects
    service stores N listener handles
    service requests death notification per listener
    service broadcasts one AIDL-like callback to all listeners
    each listener replies from a Binder looper thread
    clients exit
    service receives all listener deaths
    service releases all dead listener handles

Default:

    CLIENTS=8

Target markers:

    AIDL_LIKE_LISTENER_REGISTRY_SERVICE_REGISTERED
    AIDL_LIKE_LISTENER_REGISTRY_REGISTER_OK
    AIDL_LIKE_LISTENER_REGISTRY_DEATH_REQUESTED
    AIDL_LIKE_LISTENER_REGISTRY_THREAD_OK
    AIDL_LIKE_LISTENER_REGISTRY_BROADCAST_OK
    AIDL_LIKE_LISTENER_REGISTRY_CLIENT_SMOKE_OK
    AIDL_LIKE_LISTENER_REGISTRY_DEATH_CLEANUP_OK
    AIDL_LIKE_LISTENER_REGISTRY_SMOKE_TV_OK

Non-goals:

    no generated AIDL
    no Java framework
    no system_server
    no zygote
    no Android rootfs
    no SELinux policy
