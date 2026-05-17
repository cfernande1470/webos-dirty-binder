# Milestone: Binder FD object v0

Goal: isolate Binder FD passing using a real `BINDER_TYPE_FD` object with `pad_flags = 0`.

Hypothesis:

Previous FD tests may have failed before delivery because the FD object was encoded with the wrong object layout or polluted flags.

This milestone tests:

    client opens pipe
    client writes known payload
    client sends read end through Binder as BINDER_TYPE_FD
    service receives translated FD
    service reads payload from received FD
    service replies OK

Target markers:

    ANDROID_LIKE_FD_OBJECT_SERVICE_REGISTERED
    ANDROID_LIKE_FD_OBJECT_SERVICE_GOT_FD
    ANDROID_LIKE_FD_OBJECT_SERVICE_READ_OK
    ANDROID_LIKE_FD_OBJECT_CLIENT_REPLY_OK
    ANDROID_LIKE_FD_OBJECT_SMOKE_OK
    ANDROID_LIKE_FD_OBJECT_SMOKE_TV_OK

If this still returns BR_FAILED_REPLY, fallback plan is Binder control plane + UNIX socket SCM_RIGHTS FD side channel.
