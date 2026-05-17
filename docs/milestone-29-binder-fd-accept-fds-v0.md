# Milestone 29: Binder FD object format probe v0

Status: unsafe probe, one round only.

Previous FD result:

    BINDER_TYPE_FD with obj.flags=0 returned BR_FAILED_REPLY
    service never received transaction
    TV reboot was observed around the experiment
    milestone 24 was quarantined

New probe:

    BINDER_TYPE_FD
    obj.flags = FLAT_BINDER_FLAG_ACCEPTS_FDS
    obj.handle = fd
    obj.cookie = 0
    ROUNDS=1 only

Target markers:

    BINDER_FD_SERVICE_REGISTERED
    BINDER_FD_OBJECT_SENT
    BINDER_FD_RECEIVED_OK
    BINDER_FD_READ_OK
    BINDER_FD_CLIENT_ROUND_OK
    BINDER_FD_CLIENT_SMOKE_OK
    BINDER_FD_SMOKE_TV_OK
