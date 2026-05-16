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
