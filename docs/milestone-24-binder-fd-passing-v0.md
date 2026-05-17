# Milestone 24: Binder FD passing v0

Status: **QUARANTINED**

Goal was to prove file descriptor passing through Binder:

    client creates a pipe
    client writes payload into pipe
    client passes read-fd through Binder as BINDER_TYPE_FD
    service receives translated local fd
    service reads payload
    service replies

Observed result on LG webOS Binder 4.4 sidecar:

    client sends BINDER_TYPE_FD
    kernel returns BR_FAILED_REPLY
    service never receives the FD transaction
    TV reboot was observed during/after the experiment

Evidence:

    client emitted BINDER_FD_OBJECT_SENT
    client received BR_FAILED_REPLY
    service emitted BINDER_FD_SERVICE_REGISTERED
    service only received ServiceManager PING
    service did not receive FD transaction

Interpretation:

    FD passing is not proven safe on this Binder driver/build.
    It may be unsupported, disabled, ABI-mismatched, or triggering a kernel-side fault.
    Do not run this smoke by default.

Policy:

    run-binder-fd-passing-tv.sh is guarded.
    It will not send BINDER_TYPE_FD unless explicitly enabled with:

        BINDER_FD_PASSING_UNSAFE=1

Target marker for quarantine:

    BINDER_FD_PASSING_QUARANTINED

Next safe direction:

    continue Android compatibility milestones that do not pass file descriptors
    revisit FD passing only after collecting kernel crash logs / pstore / dmesg
