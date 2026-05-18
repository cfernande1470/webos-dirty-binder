#!/usr/bin/env bash
set -euo pipefail

TV_IP="${TV_IP:-192.168.2.121}"
SIDE_DIR="${SIDE_DIR:-/tmp/android-usb/android-sidecar}"
CLIENTS="${CLIENTS:-6}"
ROUNDS="${ROUNDS:-10}"

export TV_IP SIDE_DIR CLIENTS ROUNDS

echo "== git =="
git status --short
git rev-parse --short HEAD

echo "== build module =="
./scripts/build-module.sh

echo "== build probe =="
./scripts/build-probe.sh

echo "== build ping =="
./scripts/build-ping.sh

echo "== build sidecar =="
./scripts/build-sidecar.sh

echo "== install sidecar =="
./scripts/install-sidecar-tv.sh

echo "== binder ping smoke =="
TV="root@$TV_IP"
scp build/binder_ping_static "$TV:/tmp/binder_ping"
scp build/binder_probe_static "$TV:/tmp/binder_probe"
scp artifacts/binder-dirty-lgc1-o20-4.4.84-229.1.kavir.2.ko "$TV:/tmp/binder.ko"
scp scripts/load-binder-tv.sh "$TV:/tmp/load-binder-tv.sh"

ssh "$TV" "sh -s" <<'TVSH'
set -eu

chmod +x /tmp/binder_ping /tmp/binder_probe /tmp/load-binder-tv.sh

if [ ! -e /dev/binder ]; then
  /tmp/load-binder-tv.sh /tmp/binder.ko
fi

/tmp/binder_probe

rm -f /tmp/binder_ping_server.log /tmp/binder_ping_server.pid
/tmp/binder_ping server > /tmp/binder_ping_server.log 2>&1 &
echo "$!" > /tmp/binder_ping_server.pid
sleep 1

set +e
/tmp/binder_ping client
rc="$?"
set -e

kill "$(cat /tmp/binder_ping_server.pid)" 2>/dev/null || true

cat /tmp/binder_ping_server.log || true

if [ "$rc" -ne 0 ]; then
  echo "FAIL: binder_ping client rc=$rc"
  exit "$rc"
fi
TVSH

echo "== sidecar smoke suite =="
./scripts/run-sidecar-all-smoke-tv.sh

echo "QUICK_CHECK_TV_OK"
