# Milestone 27: AIDL-like one-way concurrent stress v0

Goal: prove concurrent AIDL-like one-way transaction traffic.

Milestone 26 proved:

    one client
    1000 TF_ONE_WAY notify(...) transactions
    1000 service-side notifications processed
    sync getCount() validates delivery

Milestone 27 proves:

    one ServiceManager
    one one-way AIDL-like service
    N concurrent clients
    each client sends ROUNDS TF_ONE_WAY notify(...) transactions
    service processes CLIENTS * ROUNDS notifications
    each client completes successfully
    no notify BR_REPLY is expected

Default:

    CLIENTS=8
    ROUNDS=250

Target markers:

    AIDL_LIKE_ONEWAY_STRESS_CLIENTS_OK
    AIDL_LIKE_ONEWAY_STRESS_SERVICE_COUNTS_OK
    AIDL_LIKE_ONEWAY_STRESS_SMOKE_TV_OK

Non-goals:

    no FD passing
    no dump(fd)
    no shellCommand(fd)
    no Java framework
    no Android rootfs
    no SELinux policy
