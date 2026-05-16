#!/usr/bin/env bash
set -uo pipefail

TV_IP="${TV_IP:-192.168.2.121}"
SIDE_DIR="${SIDE_DIR:-/media/internal/android-sidecar}"

ssh root@"$TV_IP" "
set +e
cd '$SIDE_DIR'

killall mini_servicemgr 2>/dev/null || true
killall echo_service 2>/dev/null || true
killall echo_client 2>/dev/null || true

if [ ! -e /dev/binder ]; then
  echo marker-sidecar-death-load > /dev/kmsg
  ./load-binder-tv.sh '$SIDE_DIR/modules/binder.ko'
fi

ls -l /dev/binder

rm -f logs/*.log run/*.pid run/*.exit

echo marker-sidecar-death-sm-server > /dev/kmsg
nohup bin/mini_servicemgr > logs/mini_servicemgr.log 2>&1 &
echo \$! > run/mini_servicemgr.pid
sleep 2

echo marker-sidecar-death-echo-service > /dev/kmsg
nohup bin/echo_service test.echo > logs/echo_service.log 2>&1 &
echo \$! > run/echo_service.pid
sleep 3

echo marker-sidecar-death-client-before > /dev/kmsg
bin/echo_client test.echo 'before service death' > logs/echo_client_before_death.log 2>&1
BEFORE=\$?
echo \$BEFORE > run/echo_client_before_death.exit

echo KILLING_ECHO_SERVICE_PID=\$(cat run/echo_service.pid)
kill \$(cat run/echo_service.pid) 2>/dev/null || true
sleep 3

echo marker-sidecar-death-client-after > /dev/kmsg
bin/echo_client test.echo 'after service death' > logs/echo_client_after_death.log 2>&1
AFTER=\$?
echo \$AFTER > run/echo_client_after_death.exit

echo BEFORE_EXIT=\$BEFORE
echo AFTER_EXIT=\$AFTER

echo === mini_servicemgr ===
cat logs/mini_servicemgr.log
echo === echo_service ===
cat logs/echo_service.log
echo === echo_client_before_death ===
cat logs/echo_client_before_death.log
echo === echo_client_after_death ===
cat logs/echo_client_after_death.log

exit 0
"
