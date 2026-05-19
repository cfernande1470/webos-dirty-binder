#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

TV_IP="${TV_IP:-192.168.2.121}"
ROUNDS="${ROUNDS:-16}"
FD_STAGE_TIMEOUT="${FD_STAGE_TIMEOUT:-40}"

if [ "${NO_BUILD:-0}" != "1" ]; then
  KCFLAGS="${KCFLAGS:-} -Wno-error -Wno-error=unused-variable -Wno-error=unused-function" ./scripts/build-module.sh
fi

FD_DEBUG_STAGE=7 TV_IP="$TV_IP" ./scripts/reload-build-binder-tv.sh

ROUNDS="$ROUNDS" \
NO_BUILD=1 \
TV_IP="$TV_IP" \
FD_DEBUG_STAGE=7 \
FD_STAGE_TIMEOUT="$FD_STAGE_TIMEOUT" \
./scripts/run-binder-fd-stage-tv.sh
