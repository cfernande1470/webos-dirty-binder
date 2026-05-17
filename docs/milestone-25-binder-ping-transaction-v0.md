# Milestone 25: Binder PING_TRANSACTION v0

Goal: prove Android/libbinder-style Binder ping transactions.

Milestone 24 was quarantined:

    BINDER_TYPE_FD returned BR_FAILED_REPLY
    the service never received the FD transaction
    the TV rebooted during/after the experiment
    FD passing is now guarded behind BINDER_FD_PASSING_UNSAFE=1

Milestone 25 proves a safe Android Binder meta transaction:

    client resolves AIDL-like service
    client sends Android IBinder PING_TRANSACTION
    service handles 0x5f504e47 ("_PNG")
    service replies successfully
    normal AIDL-like echo/add still works after ping

Transaction codes:

    legacy sidecar ping: 0x50494e47  "PING"
    Android Binder ping: 0x5f504e47  "_PNG"

Target markers:

    BINDER_PING_SERVICE_REGISTERED
    BINDER_PING_TRANSACTION_OK
    BINDER_PING_CLIENT_OK
    BINDER_PING_AIDL_RECOVERY_OK
    BINDER_PING_SMOKE_TV_OK

Non-goals:

    no FD passing
    no dump(fd)
    no shellCommand(fd)
    no Parcelable
    no Java framework
    no Android rootfs
