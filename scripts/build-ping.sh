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
  -o build/binder_ping_static \
  tools/binder_ping.c

file build/binder_ping_static || true
ls -lh build/binder_ping_static
