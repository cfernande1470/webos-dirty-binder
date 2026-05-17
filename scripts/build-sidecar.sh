#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

mkdir -p build

CC="${CC:-}"
if [ -z "$CC" ]; then
  if command -v aarch64-linux-gnu-gcc >/dev/null 2>&1; then
    CC=aarch64-linux-gnu-gcc
  else
    CC=gcc
  fi
fi

echo "Using CC=$CC"

"$CC" -static -O2 -Wall -Wextra \
  -Ibuild/linux-4.4.84/arch/arm64/include/uapi \
  -Ibuild/linux-4.4.84/arch/arm64/include/generated/uapi \
  -Ibuild/linux-4.4.84/include/uapi \
  -Ibuild/linux-4.4.84/include/generated/uapi \
  -Ibuild/linux-4.4.84/include \
  -o build/sidecar_binder_static \
  tools/sidecar_binder.c

cp build/sidecar_binder_static build/mini_servicemgr_static
cp build/sidecar_binder_static build/echo_service_static
cp build/sidecar_binder_static build/echo_client_static
cp build/sidecar_binder_static build/list_services_static

file build/sidecar_binder_static || true
ls -lh build/sidecar_binder_static build/mini_servicemgr_static build/echo_service_static build/echo_client_static build/list_services_static

mkdir -p build/uapi-compat/linux
cat > build/uapi-compat/linux/compiler.h <<'EOF'
#ifndef _UAPI_COMPAT_LINUX_COMPILER_H
#define _UAPI_COMPAT_LINUX_COMPILER_H

#define __user
#define __kernel
#define __safe
#define __force
#define __nocast
#define __iomem
#define __chk_user_ptr(x) ((void)0)
#define __chk_io_ptr(x) ((void)0)
#define __bitwise
#define __bitwise__
#define __must_check
#ifndef __always_inline
#define __always_inline inline
#endif
#define __printf(a, b)
#define __aligned(x) __attribute__((aligned(x)))

#endif
EOF

echo "== build aosp_sm_probe =="
AOSP_CC="${CC:-aarch64-linux-gnu-gcc}"
AOSP_UAPI="-Ibuild/uapi-compat -Ibuild/linux-4.4.84/arch/arm64/include/uapi -Ibuild/linux-4.4.84/arch/arm64/include/generated/uapi -Ibuild/linux-4.4.84/include/uapi -Ibuild/linux-4.4.84/include/generated/uapi"
"$AOSP_CC" -O2 -static -Wall -Wextra $AOSP_UAPI -o build/aosp_sm_probe_static tools/aosp_sm_probe.c
file build/aosp_sm_probe_static
ls -lh build/aosp_sm_probe_static

echo "== build libbinder_lite_client =="
LITE_CXX="${CXX:-aarch64-linux-gnu-g++}"
LITE_UAPI="-Ibuild/uapi-compat -Ibuild/linux-4.4.84/arch/arm64/include/uapi -Ibuild/linux-4.4.84/arch/arm64/include/generated/uapi -Ibuild/linux-4.4.84/include/uapi -Ibuild/linux-4.4.84/include/generated/uapi"
"$LITE_CXX" -O2 -static -Wall -Wextra $LITE_UAPI -Itools -o build/libbinder_lite_client_static tools/libbinder_lite.cpp tools/libbinder_lite_client.cpp
file build/libbinder_lite_client_static
ls -lh build/libbinder_lite_client_static

echo "== build aidl_lite_echo_client =="
"$LITE_CXX" -O2 -static -Wall -Wextra $LITE_UAPI -Itools -o build/aidl_lite_echo_client_static tools/libbinder_lite.cpp tools/aidl_lite_echo_client.cpp
file build/aidl_lite_echo_client_static
ls -lh build/aidl_lite_echo_client_static

echo "== build aidl_lite_echo_service =="
"$LITE_CXX" -O2 -static -Wall -Wextra $LITE_UAPI -Itools -o build/aidl_lite_echo_service_static tools/aidl_lite_echo_service.cpp
file build/aidl_lite_echo_service_static
ls -lh build/aidl_lite_echo_service_static

echo "== build android_like_echo_client =="
"$LITE_CXX" -O2 -static -Wall -Wextra $LITE_UAPI -Itools -o build/android_like_echo_client_static tools/libbinder_lite.cpp tools/android_like_echo_client.cpp
file build/android_like_echo_client_static
ls -lh build/android_like_echo_client_static

echo "== build android_like_unlink_death_client =="
"$LITE_CXX" -O2 -static -Wall -Wextra $LITE_UAPI -Itools   -o build/android_like_unlink_death_client_static   tools/libbinder_lite.cpp tools/android_like_unlink_death_client.cpp
file build/android_like_unlink_death_client_static
ls -lh build/android_like_unlink_death_client_static

echo "== build android_like_death_recipient_client =="
"$LITE_CXX" -O2 -static -Wall -Wextra $LITE_UAPI -Itools   -o build/android_like_death_recipient_client_static   tools/libbinder_lite.cpp tools/android_like_death_recipient_client.cpp
file build/android_like_death_recipient_client_static
ls -lh build/android_like_death_recipient_client_static

echo "== build android_like_stale_handle_client =="
"$LITE_CXX" -O2 -static -Wall -Wextra $LITE_UAPI -Itools   -o build/android_like_stale_handle_client_static   tools/libbinder_lite.cpp tools/android_like_stale_handle_client.cpp
file build/android_like_stale_handle_client_static
ls -lh build/android_like_stale_handle_client_static

echo "== build android_like_lifecycle_client =="
"$LITE_CXX" -O2 -static -Wall -Wextra $LITE_UAPI -Itools   -o build/android_like_lifecycle_client_static   tools/libbinder_lite.cpp tools/android_like_lifecycle_client.cpp
file build/android_like_lifecycle_client_static
ls -lh build/android_like_lifecycle_client_static

echo "== build android_like_echo_service =="
"$LITE_CXX" -O2 -static -Wall -Wextra $LITE_UAPI -Itools -o build/android_like_echo_service_static tools/android_like_echo_service.cpp
file build/android_like_echo_service_static
ls -lh build/android_like_echo_service_static

echo "== build android_like_callback_service =="
"$LITE_CXX" -O2 -static -Wall -Wextra $LITE_UAPI -Itools -o build/android_like_callback_service_static tools/android_like_callback_service.cpp
file build/android_like_callback_service_static
ls -lh build/android_like_callback_service_static

echo "== build android_like_callback_client =="
"$LITE_CXX" -O2 -static -Wall -Wextra $LITE_UAPI -Itools -o build/android_like_callback_client_static tools/android_like_callback_client.cpp
file build/android_like_callback_client_static
ls -lh build/android_like_callback_client_static

echo "== build android_like_callback_threadpool_client =="
"$LITE_CXX" -O2 -static -Wall -Wextra $LITE_UAPI -Itools -pthread -o build/android_like_callback_threadpool_client_static tools/android_like_callback_threadpool_client.cpp
file build/android_like_callback_threadpool_client_static
ls -lh build/android_like_callback_threadpool_client_static

echo "== build android_like_aidl_service =="
"$LITE_CXX" -O2 -static -Wall -Wextra $LITE_UAPI -Itools -o build/android_like_aidl_service_static tools/android_like_aidl_service.cpp
file build/android_like_aidl_service_static
ls -lh build/android_like_aidl_service_static

echo "== build android_like_aidl_client =="
"$LITE_CXX" -O2 -static -Wall -Wextra $LITE_UAPI -Itools -o build/android_like_aidl_client_static tools/android_like_aidl_client.cpp
file build/android_like_aidl_client_static
ls -lh build/android_like_aidl_client_static

echo "== build android_like_aidl_stale_handle_client =="
"$LITE_CXX" -O2 -static -Wall -Wextra $LITE_UAPI -Itools -o build/android_like_aidl_stale_handle_client_static tools/android_like_aidl_stale_handle_client.cpp
file build/android_like_aidl_stale_handle_client_static
ls -lh build/android_like_aidl_stale_handle_client_static

echo "== build android_like_aidl_callback_service =="
"$LITE_CXX" -O2 -static -Wall -Wextra $LITE_UAPI -Itools -o build/android_like_aidl_callback_service_static tools/android_like_aidl_callback_service.cpp
file build/android_like_aidl_callback_service_static
ls -lh build/android_like_aidl_callback_service_static

echo "== build android_like_aidl_callback_threadpool_client =="
"$LITE_CXX" -O2 -static -Wall -Wextra $LITE_UAPI -Itools -pthread -o build/android_like_aidl_callback_threadpool_client_static tools/android_like_aidl_callback_threadpool_client.cpp
file build/android_like_aidl_callback_threadpool_client_static
ls -lh build/android_like_aidl_callback_threadpool_client_static

echo "== build android_like_aidl_listener_registry_service =="
"$LITE_CXX" -O2 -static -Wall -Wextra $LITE_UAPI -Itools -o build/android_like_aidl_listener_registry_service_static tools/android_like_aidl_listener_registry_service.cpp
file build/android_like_aidl_listener_registry_service_static
ls -lh build/android_like_aidl_listener_registry_service_static

echo "== build android_like_aidl_listener_registry_client =="
"$LITE_CXX" -O2 -static -Wall -Wextra $LITE_UAPI -Itools -pthread -o build/android_like_aidl_listener_registry_client_static tools/android_like_aidl_listener_registry_client.cpp
file build/android_like_aidl_listener_registry_client_static
ls -lh build/android_like_aidl_listener_registry_client_static

echo "== build android_like_aidl_negative_client =="
"$LITE_CXX" -O2 -static -Wall -Wextra $LITE_UAPI -Itools -o build/android_like_aidl_negative_client_static tools/android_like_aidl_negative_client.cpp
file build/android_like_aidl_negative_client_static
ls -lh build/android_like_aidl_negative_client_static

echo "== build android_like_binder_meta_client =="
"$LITE_CXX" -O2 -static -Wall -Wextra $LITE_UAPI -Itools -o build/android_like_binder_meta_client_static tools/android_like_binder_meta_client.cpp
file build/android_like_binder_meta_client_static
ls -lh build/android_like_binder_meta_client_static

echo "== build android_like_aidl_binder_return_service =="
"$LITE_CXX" -O2 -static -Wall -Wextra $LITE_UAPI -Itools -o build/android_like_aidl_binder_return_service_static tools/android_like_aidl_binder_return_service.cpp
file build/android_like_aidl_binder_return_service_static
ls -lh build/android_like_aidl_binder_return_service_static

echo "== build android_like_aidl_binder_return_client =="
"$LITE_CXX" -O2 -static -Wall -Wextra $LITE_UAPI -Itools -o build/android_like_aidl_binder_return_client_static tools/android_like_aidl_binder_return_client.cpp
file build/android_like_aidl_binder_return_client_static
ls -lh build/android_like_aidl_binder_return_client_static

echo "== build android_like_fd_passing_service =="
"$LITE_CXX" -O2 -static -Wall -Wextra $LITE_UAPI -Itools -o build/android_like_fd_passing_service_static tools/android_like_fd_passing_service.cpp
file build/android_like_fd_passing_service_static
ls -lh build/android_like_fd_passing_service_static

echo "== build android_like_fd_passing_client =="
"$LITE_CXX" -O2 -static -Wall -Wextra $LITE_UAPI -Itools -o build/android_like_fd_passing_client_static tools/android_like_fd_passing_client.cpp
file build/android_like_fd_passing_client_static
ls -lh build/android_like_fd_passing_client_static

echo "== build android_like_binder_ping_client =="
"$LITE_CXX" -O2 -static -Wall -Wextra $LITE_UAPI -Itools -o build/android_like_binder_ping_client_static tools/android_like_binder_ping_client.cpp
file build/android_like_binder_ping_client_static
ls -lh build/android_like_binder_ping_client_static

echo "== build android_like_aidl_oneway_service =="
"$LITE_CXX" -O2 -static -Wall -Wextra $LITE_UAPI -Itools -o build/android_like_aidl_oneway_service_static tools/android_like_aidl_oneway_service.cpp
file build/android_like_aidl_oneway_service_static
ls -lh build/android_like_aidl_oneway_service_static

echo "== build android_like_aidl_oneway_client =="
"$LITE_CXX" -O2 -static -Wall -Wextra $LITE_UAPI -Itools -o build/android_like_aidl_oneway_client_static tools/android_like_aidl_oneway_client.cpp
file build/android_like_aidl_oneway_client_static
ls -lh build/android_like_aidl_oneway_client_static

echo "== build android_userspace_preflight =="
"$LITE_CXX" -O2 -static -Wall -Wextra $LITE_UAPI -Itools -o build/android_userspace_preflight_static tools/android_userspace_preflight.cpp
file build/android_userspace_preflight_static
ls -lh build/android_userspace_preflight_static

echo "== build fd_scm_rights_preflight =="
"$LITE_CXX" -O2 -static -Wall -Wextra -o build/fd_scm_rights_preflight_static tools/fd_scm_rights_preflight.cpp
file build/fd_scm_rights_preflight_static
ls -lh build/fd_scm_rights_preflight_static

echo "== build android_like_fd_object_service =="
"$LITE_CXX" -O2 -static -Wall -Wextra $LITE_UAPI -Itools -o build/android_like_fd_object_service_static tools/android_like_fd_object_service.cpp
file build/android_like_fd_object_service_static
ls -lh build/android_like_fd_object_service_static

echo "== build android_like_fd_object_client =="
"$LITE_CXX" -O2 -static -Wall -Wextra $LITE_UAPI -Itools -o build/android_like_fd_object_client_static tools/android_like_fd_object_client.cpp
file build/android_like_fd_object_client_static
ls -lh build/android_like_fd_object_client_static

echo "== build android_like_fd_devnull_service =="
"$LITE_CXX" -O2 -static -Wall -Wextra $LITE_UAPI -Itools -o build/android_like_fd_devnull_service_static tools/android_like_fd_devnull_service.cpp
file build/android_like_fd_devnull_service_static
ls -lh build/android_like_fd_devnull_service_static

echo "== build android_like_fd_devnull_client =="
"$LITE_CXX" -O2 -static -Wall -Wextra $LITE_UAPI -Itools -o build/android_like_fd_devnull_client_static tools/android_like_fd_devnull_client.cpp
file build/android_like_fd_devnull_client_static
ls -lh build/android_like_fd_devnull_client_static

echo "== build fd_bridge_service =="
"$LITE_CXX" -O2 -static -Wall -Wextra $LITE_UAPI -Itools -pthread -o build/fd_bridge_service_static tools/fd_bridge_service.cpp
file build/fd_bridge_service_static
ls -lh build/fd_bridge_service_static

echo "== build fd_bridge_client =="
"$LITE_CXX" -O2 -static -Wall -Wextra $LITE_UAPI -Itools -pthread -o build/fd_bridge_client_static tools/fd_bridge_client.cpp
file build/fd_bridge_client_static
ls -lh build/fd_bridge_client_static

echo "== build parcel_fd_lite_service =="
"$LITE_CXX" -O2 -static -Wall -Wextra $LITE_UAPI -Itools -pthread -o build/parcel_fd_lite_service_static tools/parcel_fd_lite_service.cpp
file build/parcel_fd_lite_service_static
ls -lh build/parcel_fd_lite_service_static

echo "== build parcel_fd_lite_client =="
"$LITE_CXX" -O2 -static -Wall -Wextra $LITE_UAPI -Itools -pthread -o build/parcel_fd_lite_client_static tools/parcel_fd_lite_client.cpp
file build/parcel_fd_lite_client_static
ls -lh build/parcel_fd_lite_client_static

echo "== build android_userspace_preflight_v1 =="
"$LITE_CXX" -O2 -static -Wall -Wextra $LITE_UAPI -Itools -pthread -o build/android_userspace_preflight_v1_static tools/android_userspace_preflight_v1.cpp
file build/android_userspace_preflight_v1_static
ls -lh build/android_userspace_preflight_v1_static
