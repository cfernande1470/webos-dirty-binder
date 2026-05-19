#!/usr/bin/env bash
set -euo pipefail

TV_IP="${TV_IP:-192.168.2.121}"
SIDE_DIR="${SIDE_DIR:-/tmp/android-usb/android-sidecar}"
FD_DEBUG_STAGE="${FD_DEBUG_STAGE:-0}"
FD_STAGE_TIMEOUT="${FD_STAGE_TIMEOUT:-25}"

case "$FD_DEBUG_STAGE" in
  0|1|2|3|4|5|6|7) ;;
  *) echo "ERROR: usa FD_DEBUG_STAGE=0..7; stage 7 es FD real controlado"; exit 2 ;;
esac

if [ "${NO_BUILD:-0}" != "1" ]; then
  echo "== clean old module objects =="
  find build -type f \( -name '*.o' -o -name '*.ko' -o -name '*.mod' -o -name '*.mod.c' -o -name '*.cmd' -o -name 'modules.order' \) -delete 2>/dev/null || true

  echo "== build staged binder.ko =="
  KCFLAGS="${KCFLAGS:-} -Wno-error -Wno-error=unused-variable -Wno-error=unused-function" \
    ./scripts/build-module.sh 2>&1 | tee /tmp/binder-fd-stage-build.log
fi

KO=""
for c in \
  build/linux-4.4.84/drivers/android/binder.ko \
  build/linux-4.4.84/drivers/staging/android/binder.ko \
  artifacts/binder.ko \
  artifacts/binder-dirty-lgc1-o20-4.4.84-229.1.kavir.2.ko
do
  [ -f "$c" ] && KO="$c" && break
done

[ -n "$KO" ] || {
  echo "ERROR: no binder.ko found"
  exit 1
}

echo "== verify module param in $KO =="
modinfo -p "$KO" 2>/dev/null | grep fd_debug_stage || {
  echo "ERROR: binder.ko has no fd_debug_stage param"
  exit 1
}

echo "== load/reuse binder fd_debug_stage=$FD_DEBUG_STAGE =="
TV_IP="$TV_IP" FD_DEBUG_STAGE="$FD_DEBUG_STAGE" scripts/load-binder-fd-stage-tv.sh

echo "== deploy sidecar =="
TV_IP="$TV_IP" SIDE_DIR="$SIDE_DIR" scripts/deploy-fd-stage-sidecar-tv.sh

echo "== clear old stage dmesg window marker by noting current tail =="
ssh root@"$TV_IP" "dmesg 2>/dev/null | tail -5 || true" >/dev/null || true

echo "== run staged direct-FD probe =="
set +e
env \
  TV_IP="$TV_IP" \
  SIDE_DIR="$SIDE_DIR" \
  BINDER_FD_STAGE_ALLOW=1 \
  BINDER_FD_PASSING_UNSAFE=1 \
  timeout "$FD_STAGE_TIMEOUT" scripts/run-binder-fd-passing-tv.sh
rc=$?
set -e

echo "== probe rc=$rc =="

DMESG="$(ssh root@"$TV_IP" "dmesg 2>/dev/null | grep -Ei 'DIRTY_BINDER_FD_STAGE|binder|oops|panic|watchdog|fatal' | tail -180 || true")"
echo "$DMESG"

if [ "$rc" = 124 ]; then
  echo "FAIL_TIMEOUT stage=$FD_DEBUG_STAGE"
  exit 124
fi

if [ "$rc" != 0 ]; then
  if printf '%s\n' "$DMESG" | grep -q "DIRTY_BINDER_FD_STAGE stage=$FD_DEBUG_STAGE"; then
    echo "BINDER_FD_STAGE_SAFE_FAILURE_OK stage=$FD_DEBUG_STAGE rc=$rc"
    exit 0
  fi

  echo "FAIL_NOT_A_STAGE_RESULT stage=$FD_DEBUG_STAGE rc=$rc"
  echo "El probe falló, pero no apareció el marker DIRTY_BINDER_FD_STAGE del kernel."
  echo "Eso es fallo de setup/userland, no resultado Binder FD."
  exit "$rc"
fi

if [ "$FD_DEBUG_STAGE" = "7" ]; then
  echo "BINDER_FD_STAGE7_REAL_FD_OK"
  exit 0
fi

echo "WARN_UNEXPECTED_SUCCESS stage=$FD_DEBUG_STAGE"
exit 3
