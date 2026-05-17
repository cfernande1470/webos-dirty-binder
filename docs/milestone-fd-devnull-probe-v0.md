# Milestone: Binder FD devnull probe v0

Goal: test the smallest possible Binder FD transfer.

Previous unsafe-ish test:

    client sent pipe read-end as BINDER_TYPE_FD
    client got BR_FAILED_REPLY
    service never received the FD transaction
    TV may have frozen/rebooted afterwards

This probe removes pipe semantics:

    client opens /dev/null
    client sends /dev/null as BINDER_TYPE_FD
    service only fstat/read-closes it
    service replies OK if the FD arrives

Interpretation:

    /dev/null fails with BR_FAILED_REPLY:
        Binder FD transfer is probably globally rejected/broken on this LG Binder module.

    /dev/null arrives:
        Binder FD transfer exists, but pipe/socket/specific file classes may be rejected or unstable.
