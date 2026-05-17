# Quarantine: direct Binder FD transfer

Status: unsafe on LG webOS TV.

Direct `BINDER_TYPE_FD` transfer freezes or destabilises the TV.

Observed diagnostics:

    DIRTY_BINDER_FD_DIAG enter_fd_case line=1680
    DIRTY_BINDER_FD_DIAG failed_reply_before line=1719
    binder: transaction failed 29201, size 136-8

Interpretation:

    29201 == 0x7211 == BR_FAILED_REPLY

The service does not receive the FD transaction. The failure happens inside the Binder driver FD path before delivery.

Affected direct-FD probes:

    android_like_fd_object_client/service
    android_like_fd_devnull_client/service
    android_like_fd_passing_client/service

Decision:

    Do not run direct `BINDER_TYPE_FD` probes on the TV.

Recommended path:

    Binder = control plane
    UNIX domain socket + SCM_RIGHTS = FD transport plane

This keeps Android-like control semantics while avoiding the unstable Binder FD path.
