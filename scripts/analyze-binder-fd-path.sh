#!/usr/bin/env bash
set -euo pipefail

OUT="${OUT:-docs/artifacts/binder-fd-dissect/binder-fd-source-audit.txt}"

mkdir -p "$(dirname "$OUT")"

{
  echo "== binder fd source audit =="
  date || true

  echo
  echo "== locate binder source =="
  find build linux* -path '*drivers/android/binder.c' -print 2>/dev/null || true
  find build linux* -path '*drivers/android/binder_alloc.c' -print 2>/dev/null || true
  find build linux* -path '*include*uapi*android*binder.h' -print 2>/dev/null || true

  BINDER_C="$(find build linux* -path '*drivers/android/binder.c' -print 2>/dev/null | head -n 1 || true)"
  BINDER_H="$(find build linux* -path '*include*uapi*android*binder.h' -print 2>/dev/null | head -n 1 || true)"

  echo
  echo "BINDER_C=$BINDER_C"
  echo "BINDER_H=$BINDER_H"

  if [ -n "$BINDER_H" ]; then
    echo
    echo "== binder.h fd/object definitions =="
    grep -nE 'BINDER_TYPE_FD|BINDER_TYPE_BINDER|FLAT_BINDER_FLAG_ACCEPTS_FDS|flat_binder_object|binder_fd_object|binder_buffer_object|BINDER_TYPE_FDA' "$BINDER_H" || true
  fi

  if [ -n "$BINDER_C" ]; then
    echo
    echo "== fd transfer symbols =="
    grep -nE 'binder_translate_fd|transfer_file|accept_fds|BINDER_TYPE_FD|BR_FAILED_REPLY|return_error|security_binder_transfer_file|task_get_unused_fd|fget|fdget|binder_transaction' "$BINDER_C" || true

    echo
    echo "== context around binder_translate_fd / transfer fd =="
    grep -nE 'binder_translate_fd|security_binder_transfer_file|accept_fds|BINDER_TYPE_FD|BR_FAILED_REPLY' "$BINDER_C" | cut -d: -f1 | sort -n | uniq | while read -r line; do
      start=$((line - 25))
      [ "$start" -lt 1 ] && start=1
      end=$((line + 35))
      echo
      echo "----- $BINDER_C:$start-$end -----"
      sed -n "${start},${end}p" "$BINDER_C"
    done
  fi

  echo
  echo "BINDER_FD_SOURCE_AUDIT_OK"
} | tee "$OUT"
