# Milestone: ParcelFileDescriptor-lite v0

Goal: expose FD Bridge as an Android-like Parcel FD API.

Previous milestones:

    Direct BINDER_TYPE_FD is quarantined on LG webOS TV.
    SCM_RIGHTS FD Bridge v0 works.
    Binder remains the control plane.
    UNIX socket SCM_RIGHTS is the FD transport plane.

Goal API:

    parcel_write_fd(fd)
    parcel_read_fd()

Internal transport:

    write_fd:
        create token
        send FD through SCM_RIGHTS
        write token/kind into Binder payload

    read_fd:
        read token/kind from Binder payload
        match token with received SCM_RIGHTS FD
        return local FD

Target markers:

    PARCELFD_LITE_WRITE_FD_OK
    PARCELFD_LITE_TOKEN_ENCODE_OK
    PARCELFD_LITE_SOCKET_SEND_OK
    PARCELFD_LITE_BINDER_CONTROL_OK
    PARCELFD_LITE_READ_FD_OK
    PARCELFD_LITE_PAYLOAD_READ_OK
    PARCELFD_LITE_SMOKE_OK
    PARCELFD_LITE_SMOKE_TV_OK

Non-goals:

    no direct BINDER_TYPE_FD
    no Android rootfs yet
    no SurfaceFlinger
    no zygote
    no PackageManager
