# Milestone 29: Binder FD probe after instrumentation attempt

Status: **FAILED / QUARANTINED HARD**

Observed:

    BINDER_TYPE_FD transaction:
        type=0x66642a85
        flags=0x00000100
        handle=4
        data_size=112
        offsets_size=8

    client receives:
        BR_FAILED_REPLY

    service receives:
        registration
        ServiceManager PING
        no FD transaction

    system impact:
        webOS UI freezes / becomes unresponsive
        reboot required

Conclusion:

    Binder FD passing is unsafe on the current LG webOS Binder path.
    Do not run FD probes on the main TV by default.
    Continue Android sidecar work without Binder FD passing.

Policy:

    FD passing remains quarantined.
    Future FD work must happen only with a verified instrumented binder.ko,
    watchdog, and preferably a sacrificial/test device.
