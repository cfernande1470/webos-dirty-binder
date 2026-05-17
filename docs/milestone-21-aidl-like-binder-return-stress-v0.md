# Milestone 21: AIDL-like Binder return object stress v0

Goal: prove returned Binder interface objects survive concurrent client use.

Milestone 20 proved:

    service method returns a Binder object inside a reply Parcel
    client receives BINDER_TYPE_HANDLE
    client calls the returned child Binder interface
    child object replies with AIDL-like exception code + String16 result

Milestone 21 proves:

    one factory service
    N concurrent clients
    each client calls getChild()
    each client receives a returned child Binder handle
    each client calls child.echo(...)
    service handles all returned-object transactions
    all returned handles are released cleanly

Default:

    CLIENTS=16

Target markers:

    AIDL_LIKE_BINDER_RETURN_STRESS_CLIENTS_OK
    AIDL_LIKE_BINDER_RETURN_STRESS_SERVICE_COUNTS_OK
    AIDL_LIKE_BINDER_RETURN_STRESS_SMOKE_TV_OK

Non-goals:

    no generated AIDL
    no Java framework
    no Parcelable
    no zygote
    no Android rootfs
    no SELinux policy
