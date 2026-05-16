#!/usr/bin/env bash
set -euo pipefail

TV_IP="${TV_IP:-192.168.2.121}"
SIDE_DIR="${SIDE_DIR:-/home/root/android-sidecar}"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

ssh root@"$TV_IP" "mkdir -p '$SIDE_DIR/bin' '$SIDE_DIR/modules' '$SIDE_DIR/logs' '$SIDE_DIR/run'"

scp build/mini_servicemgr_static root@"$TV_IP":"$SIDE_DIR/bin/mini_servicemgr"
scp build/echo_service_static root@"$TV_IP":"$SIDE_DIR/bin/echo_service"
scp build/echo_client_static root@"$TV_IP":"$SIDE_DIR/bin/echo_client"
scp build/list_services_static root@"$TV_IP":"$SIDE_DIR/bin/list_services"
scp build/aosp_sm_probe_static root@"$TV_IP":"$SIDE_DIR/bin/aosp_sm_probe"
scp build/libbinder_lite_client_static root@"$TV_IP":"$SIDE_DIR/bin/libbinder_lite_client"
scp build/aidl_lite_echo_client_static root@"$TV_IP":"$SIDE_DIR/bin/aidl_lite_echo_client"
scp build/aidl_lite_echo_service_static root@"$TV_IP":"$SIDE_DIR/bin/aidl_lite_echo_service"
scp build/android_like_echo_client_static root@"$TV_IP":"$SIDE_DIR/bin/android_like_echo_client"
scp build/android_like_lifecycle_client_static root@"$TV_IP":"$SIDE_DIR/bin/android_like_lifecycle_client"
scp build/android_like_stale_handle_client_static root@"$TV_IP":"$SIDE_DIR/bin/android_like_stale_handle_client"
scp build/android_like_death_recipient_client_static root@"$TV_IP":"$SIDE_DIR/bin/android_like_death_recipient_client"
scp build/android_like_echo_service_static root@"$TV_IP":"$SIDE_DIR/bin/android_like_echo_service"
scp build/linux-4.4.84/drivers/android/binder.ko root@"$TV_IP":"$SIDE_DIR/modules/binder.ko"
scp scripts/load-binder-tv.sh root@"$TV_IP":"$SIDE_DIR/load-binder-tv.sh"

ssh root@"$TV_IP" "chmod +x '$SIDE_DIR/bin/mini_servicemgr' '$SIDE_DIR/bin/echo_service' '$SIDE_DIR/bin/echo_client' '$SIDE_DIR/bin/list_services' '$SIDE_DIR/bin/aosp_sm_probe' '$SIDE_DIR/bin/libbinder_lite_client' '$SIDE_DIR/bin/aidl_lite_echo_client' '$SIDE_DIR/bin/aidl_lite_echo_service' '$SIDE_DIR/bin/android_like_echo_client' '$SIDE_DIR/bin/android_like_lifecycle_client' '$SIDE_DIR/bin/android_like_stale_handle_client' '$SIDE_DIR/bin/android_like_death_recipient_client' '$SIDE_DIR/bin/android_like_echo_service' '$SIDE_DIR/load-binder-tv.sh'"

echo "Installed sidecar to $TV_IP:$SIDE_DIR"
