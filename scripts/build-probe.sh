#!/bin/sh
set -eu

cc="${CC:-gcc}"

mkdir -p build
"$cc" -O2 -static -o build/binder_probe_static tools/binder_probe.c

file build/binder_probe_static
