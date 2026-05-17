# Milestone 29b: Binder FD dissect v0

Goal: dissect Binder FD failure without sending another Binder FD transaction.

Previous result:

    BINDER_TYPE_FD
    sizeof(flat_binder_object)=24
    offset=88
    type=0x66642a85
    flags=0x00000100
    handle=4
    data_size=112
    offsets_size=8
    client receives BR_FAILED_REPLY
    service never receives transaction
    webOS UI became unstable

Safe dissection:

    1. verify generic Linux FD passing with socketpair + SCM_RIGHTS
    2. inspect local binder.c FD transfer path
    3. locate exact BR_FAILED_REPLY branches around binder_translate_fd / accept_fds
    4. do not send BINDER_TYPE_FD in this milestone

Target markers:

    FD_SCM_RIGHTS_SEND_OK
    FD_SCM_RIGHTS_RECV_OK
    FD_SCM_RIGHTS_READ_OK
    FD_SCM_RIGHTS_PREFLIGHT_OK
    BINDER_FD_SOURCE_AUDIT_OK
    BINDER_FD_DISSECT_SAFE_SMOKE_TV_OK
