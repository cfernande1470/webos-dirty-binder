# Milestone 26: AIDL-like one-way transactions v0

Goal: prove Android/AIDL-like one-way transaction semantics.

Milestone 25 proved:

    Android IBinder PING_TRANSACTION
    service replies successfully
    normal AIDL-like echo/add still works after ping

Milestone 26 proves:

    client sends AIDL-like notify(seq, payload) using TF_ONE_WAY
    service processes notify without BR_REPLY
    client observes BR_TRANSACTION_COMPLETE
    client then calls sync getCount()
    getCount() validates all one-way notifications were processed

Default:

    ROUNDS=100

Target markers:

    AIDL_LIKE_ONEWAY_SERVICE_REGISTERED
    AIDL_LIKE_ONEWAY_NOTIFY_SENT
    AIDL_LIKE_ONEWAY_NOTIFY_OK
    AIDL_LIKE_ONEWAY_GET_COUNT_OK
    AIDL_LIKE_ONEWAY_CLIENT_SMOKE_OK
    AIDL_LIKE_ONEWAY_SMOKE_TV_OK

Non-goals:

    no FD passing
    no dump(fd)
    no shellCommand(fd)
    no Java framework
    no Android rootfs
    no SELinux policy
