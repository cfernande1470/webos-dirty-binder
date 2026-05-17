# Milestone: FD bridge v0

Goal: replace unsafe direct Binder FD passing with a safe side channel.

Architecture:

    Binder:
        control message
        token
        metadata
        lifecycle

    UNIX domain socket:
        SCM_RIGHTS
        token + FD

Flow:

    client gets Binder service handle
    client creates pipe with known payload
    client sends pipe read-end over UNIX socket using SCM_RIGHTS
    client sends Binder control transaction containing the same token
    service matches token
    service reads FD payload
    service replies through Binder

Target markers:

    FD_BRIDGE_SERVICE_SOCKET_READY
    FD_BRIDGE_SERVICE_REGISTERED
    FD_BRIDGE_CLIENT_SOCKET_SEND_OK
    FD_BRIDGE_BINDER_CONTROL_OK
    FD_BRIDGE_SERVICE_GOT_FD
    FD_BRIDGE_SERVICE_READ_OK
    FD_BRIDGE_CLIENT_BINDER_REPLY_OK
    FD_BRIDGE_SMOKE_OK
    FD_BRIDGE_SMOKE_TV_OK

Non-goals:

    no BINDER_TYPE_FD
    no kernel FD transfer
    no direct Binder FD probes
