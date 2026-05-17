# Milestone 29: Binder FD probe after ACCEPT_FDS

Status: **FAILED / QUARANTINED**

Observed:

    BINDER_TYPE_FD transaction with flags=0x100
    client receives BR_FAILED_REPLY
    service never receives FD transaction
    webOS UI became unstable/frozen
    SSH recovery/reboot was needed

Conclusion:

    Binder FD passing is not proven safe on this LG webOS Binder 4.4 path.
    Do not run FD probes manually without watchdog + debug capture.

Next diagnostics:

    inspect binder debugfs logs if available
    verify non-Binder FD passing via SCM_RIGHTS
    audit service node accept_fds in local Binder object registration
    test alternate BINDER_TYPE_FD encodings only under timeout/watchdog
