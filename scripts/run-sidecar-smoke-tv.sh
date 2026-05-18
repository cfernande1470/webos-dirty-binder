#!/usr/bin/env bash
set -euo pipefail

TV_IP="${TV_IP:-192.168.2.121}"
SIDE_DIR="${SIDE_DIR:-/tmp/android-usb/android-sidecar}"

ssh root@"$TV_IP" "
set -e
cd '$SIDE_DIR'

killall mini_servicemgr 2>/dev/null || true
killall echo_service 2>/dev/null || true
killall echo_client 2>/dev/null || true

if [ ! -e /dev/binder ]; then
  echo marker-sidecar-load > /dev/kmsg
  ./load-binder-tv.sh '$SIDE_DIR/modules/binder.ko'
fi

ls -l /dev/binder

rm -f logs/*.log run/*.pid

echo marker-sidecar-sm-server > /dev/kmsg
nohup bin/mini_servicemgr > logs/mini_servicemgr.log 2>&1 &
echo \$! > run/mini_servicemgr.pid
sleep 2

echo marker-sidecar-echo-service > /dev/kmsg
nohup bin/echo_service test.echo > logs/echo_service.log 2>&1 &
echo \$! > run/echo_service.pid
sleep 3

echo marker-sidecar-echo-client > /dev/kmsg
bin/echo_client test.echo 'hello from sidecar smoke' > logs/echo_client.log 2>&1
echo \$? > run/echo_client.exit

echo CLIENT_EXIT=\$(cat run/echo_client.exit)
echo === mini_servicemgr ===
cat logs/mini_servicemgr.log
echo === echo_service ===
cat logs/echo_service.log
echo === echo_client ===
cat logs/echo_client.log
"
