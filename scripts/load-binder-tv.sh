#!/bin/sh
set -eu

MOD="${1:-/tmp/binder-dirty.ko}"

addr() {
  awk -v s="$1" '$3 == s { print "0x"$1; exit }' /proc/kallsyms
}

need_addr() {
  name="$1"
  value="$(addr "$name")"
  if [ -z "$value" ]; then
    echo "ERROR: missing symbol address for $name"
    exit 1
  fi
  echo "$value"
}

if [ ! -f "$MOD" ]; then
  echo "ERROR: module not found: $MOD"
  exit 1
fi

if grep -q '^binder ' /proc/modules; then
  echo "binder already loaded:"
  grep '^binder ' /proc/modules
  grep binder /proc/misc || true
  ls -l /dev/binder /dev/hwbinder /dev/vndbinder 2>&1 || true
  exit 0
fi

SYM_ZAP_PAGE_RANGE="$(need_addr zap_page_range)"
SYM_PUT_FILES_STRUCT="$(need_addr put_files_struct)"
SYM_GET_VM_AREA="$(need_addr get_vm_area)"
SYM___FD_INSTALL="$(need_addr __fd_install)"
SYM___CLOSE_FD="$(need_addr __close_fd)"
SYM_MAP_KERNEL_RANGE_NOFLUSH="$(need_addr map_kernel_range_noflush)"
SYM___LOCK_TASK_SIGHAND="$(need_addr __lock_task_sighand)"
SYM_GET_FILES_STRUCT="$(need_addr get_files_struct)"
SYM___ALLOC_FD="$(need_addr __alloc_fd)"

echo "Loading $MOD"

insmod "$MOD" \
  sym_zap_page_range="$SYM_ZAP_PAGE_RANGE" \
  sym_put_files_struct="$SYM_PUT_FILES_STRUCT" \
  sym_get_vm_area="$SYM_GET_VM_AREA" \
  sym___fd_install="$SYM___FD_INSTALL" \
  sym___close_fd="$SYM___CLOSE_FD" \
  sym_map_kernel_range_noflush="$SYM_MAP_KERNEL_RANGE_NOFLUSH" \
  sym___lock_task_sighand="$SYM___LOCK_TASK_SIGHAND" \
  sym_get_files_struct="$SYM_GET_FILES_STRUCT" \
  sym___alloc_fd="$SYM___ALLOC_FD"

echo "Loaded:"
grep '^binder ' /proc/modules || true
grep binder /proc/misc || true
ls -l /dev/binder /dev/hwbinder /dev/vndbinder 2>&1 || true
