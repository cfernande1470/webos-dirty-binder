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
