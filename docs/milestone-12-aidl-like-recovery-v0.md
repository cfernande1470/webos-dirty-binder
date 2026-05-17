# Milestone 12: AIDL-like service death/recovery v0

Goal: prove Android-like service death and re-registration recovery.

Milestone 11 proved:

    32 concurrent AIDL-like clients
    100 rounds each
    3200 echo transactions
    3200 add transactions
    6400 client-side no-exception replies

Milestone 12 proves:

    one ServiceManager
    one AIDL-like service
    client resolves service handle
    client performs AIDL-like calls
    service process dies
    ServiceManager receives death notification
    service re-registers under the same name
    client resolves the new handle
    AIDL-like calls work again
    repeated recovery cycles pass

Default:

    CYCLES=5
    ROUNDS=10

Target markers:

    AIDL_LIKE_RECOVERY_INITIAL_OK
    AIDL_LIKE_RECOVERY_DEATH_DETECTED
    AIDL_LIKE_RECOVERY_REREGISTER_OK
    AIDL_LIKE_RECOVERY_CLIENT_OK
    AIDL_LIKE_RECOVERY_SMOKE_TV_OK

Non-goals:

    no zygote
    no Android framework restart manager
    no init.rc service supervision
    no SELinux policy
    no Android rootfs
