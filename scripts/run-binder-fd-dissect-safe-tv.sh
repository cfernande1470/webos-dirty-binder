#!/usr/bin/env bash
set -euo pipefail

TV_IP="${TV_IP:-192.168.2.121}"
SIDE_DIR="${SIDE_DIR:-/tmp/android-usb/android-sidecar}"

ssh root@"$TV_IP" "SIDE_DIR='$SIDE_DIR' sh -s" <<'TVSH'
set -eu

cd "$SIDE_DIR"

if [ ! -e bin/fd_scm_rights_preflight ]; then
  echo "ERROR: missing $SIDE_DIR/bin/fd_scm_rights_preflight"
  exit 1
fi

chmod +x bin/fd_scm_rights_preflight

mkdir -p logs

rm -f logs/fd_scm_rights_preflight.log

{
  echo "== fd dissect safe tv =="
  date || true
  uptime || true

  echo
  echo "== run SCM_RIGHTS fd preflight =="
  bin/fd_scm_rights_preflight
} | tee logs/fd_scm_rights_preflight.log

grep -q 'FD_SCM_RIGHTS_SEND_OK' logs/fd_scm_rights_preflight.log
grep -q 'FD_SCM_RIGHTS_RECV_OK' logs/fd_scm_rights_preflight.log
grep -q 'FD_SCM_RIGHTS_READ_OK' logs/fd_scm_rights_preflight.log
grep -q 'FD_SCM_RIGHTS_PREFLIGHT_OK' logs/fd_scm_rights_preflight.log

echo "BINDER_FD_DISSECT_SAFE_SMOKE_TV_OK"
TVSH
