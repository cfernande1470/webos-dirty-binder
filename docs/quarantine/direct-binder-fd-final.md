# Direct Binder FD transfer: quarantined

Status: disabled on LG webOS TV.

Observed with `/dev/null` FD probe:

    client sends BINDER_TYPE_FD
    client receives BR_FAILED_REPLY
    service never receives the FD transaction
    TV may freeze/reboot shortly after

Kernel diagnostics:

    DIRTY_BINDER_FD_DIAG enter_fd_case line=1680
    DIRTY_BINDER_FD_DIAG failed_reply_before line=1719
    binder: transaction failed 29201, size 136-8

Meaning:

    29201 == 0x7211 == BR_FAILED_REPLY

The failure happens inside the Binder FD path before delivery to the service.

Decision:

    Do not run direct BINDER_TYPE_FD probes on the TV.

Replacement architecture:

    Binder control plane:
        tokens
        lifecycle
        callbacks
        service registry
        metadata

    UNIX domain socket FD plane:
        SCM_RIGHTS
        token-matched FD delivery

This preserves Android-like semantics while avoiding the unstable LG Binder FD path.
