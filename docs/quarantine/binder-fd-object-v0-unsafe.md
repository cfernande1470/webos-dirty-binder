# Quarantine: Binder FD object v0

Status: unsafe on LG webOS TV.

Observed behaviour:

    client sends BINDER_TYPE_FD using binder_fd_object-like layout
    client immediately receives BR_FAILED_REPLY
    service does not receive the FD transaction
    TV may freeze/reboot after the test

Important log markers:

    fd object client: sending BINDER_TYPE_FD local_fd=4 object_size=24 offset=112 flags=0
    fd object client got BR_FAILED_REPLY 0x00007211
    ANDROID_LIKE_FD_OBJECT_FAILED_REPLY
    service only received ServiceManager ping transaction

Conclusion:

Do not run BINDER_TYPE_FD tests on the TV for now.

Fallback direction:

    Binder = control plane
    UNIX domain socket + SCM_RIGHTS = FD transport

This is safer and already matches the sidecar architecture goal.
