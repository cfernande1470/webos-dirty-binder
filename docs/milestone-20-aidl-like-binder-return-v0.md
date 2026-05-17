# Milestone 20: AIDL-like Binder return object v0

Goal: prove AIDL-like methods can return Binder interface objects.

Milestone 19 proved:

    INTERFACE_TRANSACTION
    descriptor query
    normal AIDL-like calls still work after meta transaction

Milestone 20 proves:

    service method returns a Binder object inside a reply Parcel
    client receives it as BINDER_TYPE_HANDLE
    client acquires returned handle
    client calls returned child Binder interface
    child object replies with AIDL-like exception code + String16 result
    returned handle can be released cleanly

Service:

    test.android.aidl.factory

Interfaces:

    webos.dirtybinder.IBinderFactory
        getChild() -> IReturnedChild

    webos.dirtybinder.IReturnedChild
        echo(String msg) -> String

Target markers:

    AIDL_LIKE_BINDER_RETURN_SERVICE_REGISTERED
    AIDL_LIKE_BINDER_RETURN_CHILD_OBJECT_SENT
    AIDL_LIKE_BINDER_RETURN_HANDLE_OK
    AIDL_LIKE_BINDER_RETURN_CHILD_CALL_OK
    AIDL_LIKE_BINDER_RETURN_CLIENT_SMOKE_OK
    AIDL_LIKE_BINDER_RETURN_SMOKE_TV_OK

Non-goals:

    no generated AIDL
    no Java framework
    no Parcelable
    no zygote
    no Android rootfs
    no SELinux policy
