# FD autopsy v0

Purpose: diagnose Binder FD failure without sending a new BINDER_TYPE_FD transaction.

Known unsafe symptom:

    fd object client sends BINDER_TYPE_FD
    client receives BR_FAILED_REPLY
    service never receives the FD transaction
    TV may freeze/reboot

This autopsy collects:

    binder module identity
    kernel config
    debugfs Binder state/logs
    pstore crash logs
    dmesg crash/security/binder lines

Next safe hypotheses:

    H1: target node is not actually accept_fds=true
    H2: LG binder driver rejects all FD transfer
    H3: security_binder_transfer_file or equivalent hook rejects the file
    H4: old Binder UAPI expects legacy flat_binder_object semantics
    H5: pipe FDs specifically trigger a bad path; test /dev/null next
