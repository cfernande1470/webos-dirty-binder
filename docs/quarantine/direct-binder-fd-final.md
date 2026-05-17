# Direct Binder FD transfer: quarantined

Status: disabled on LG webOS TV.

Direct `BINDER_TYPE_FD` transfer currently fails before delivery to the service.

Observed diagnostics:

    DIRTY_BINDER_FD_DIAG enter_fd_case line=1680
    DIRTY_BINDER_FD_DIAG failed_reply_before line=1719
    binder: transaction failed 29201, size 136-8

Meaning:

    29201 == 0x7211 == BR_FAILED_REPLY

The service does not receive the real FD transaction. The failure happens inside the Binder FD path before delivery.

Decision:

    Do not run direct BINDER_TYPE_FD probes on the TV.

Replacement architecture:

    Binder = control plane
    UNIX domain socket + SCM_RIGHTS = FD transport plane
