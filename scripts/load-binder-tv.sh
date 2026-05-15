#!/bin/sh
set -eu

MOD="${1:-/tmp/binder-dirty.ko}"

addr() {
  awk -v s="$1" '$3 == s { print "0x"$1; exit }' /proc/kallsyms
}

if grep -q '^binder ' /proc/modules; then
  echo "binder already loaded:"
  grep '^binder ' /proc/modules
  ls -l /dev/binder 2>/dev/null || true
  exit 0
fi

echo "Loading $MOD"

insmod "$MOD" \
  sym_zap_page_range="$(addr zap_page_range)" \
  sym_put_files_struct="$(addr put_files_struct)" \
  sym_get_vm_area="$(addr get_vm_area)" \
  sym___fd_install="$(addr __fd_install)" \
  sym___close_fd="$(addr __close_fd)" \
  sym_map_kernel_range_noflush="$(addr map_kernel_range_noflush)" \
  sym___lock_task_sighand="$(addr __lock_task_sighand)" \
  sym_get_files_struct="$(addr get_files_struct)" \
  sym___alloc_fd="$(addr __alloc_fd)"

echo "Loaded:"
grep '^binder ' /proc/modules || true
grep binder /proc/misc || true
ls -l /dev/binder /dev/hwbinder /dev/vndbinder 2>&1 || true
