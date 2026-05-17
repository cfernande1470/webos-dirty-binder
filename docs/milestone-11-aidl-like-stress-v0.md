# Milestone 11: AIDL-like concurrent stress v0

Goal: prove concurrent Android/AIDL-like Parcel traffic over Binder.

Milestone 10 proved:

    one client
    one service
    AIDL-like interface token
    exception code
    String16-style echo
    int32 add
    100 sequential rounds

Milestone 11 proves:

    one ServiceManager
    one AIDL-like service
    N concurrent clients
    each client performs ROUNDS echo/add calls
    service survives concurrent Parcel traffic
    all clients exit successfully

Default:

    CLIENTS=16
    ROUNDS=50

Target markers:

    AIDL_LIKE_STRESS_CLIENTS_OK
    AIDL_LIKE_STRESS_SERVICE_COUNTS_OK
    AIDL_LIKE_STRESS_SMOKE_TV_OK

Non-goals:

    no generated AIDL
    no Java framework
    no zygote
    no Android rootfs
    no SELinux policy
