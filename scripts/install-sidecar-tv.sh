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
scp build/android_like_unlink_death_client_static root@"$TV_IP":"$SIDE_DIR/bin/android_like_unlink_death_client"
scp build/android_like_echo_service_static root@"$TV_IP":"$SIDE_DIR/bin/android_like_echo_service"
scp build/android_like_callback_service_static root@"$TV_IP":"$SIDE_DIR/bin/android_like_callback_service"
scp build/android_like_callback_client_static root@"$TV_IP":"$SIDE_DIR/bin/android_like_callback_client"

scp build/android_like_callback_threadpool_client_static root@"$TV_IP":"$SIDE_DIR/bin/android_like_callback_threadpool_client"
scp build/android_like_aidl_service_static root@"$TV_IP":"$SIDE_DIR/bin/android_like_aidl_service"
scp build/android_like_aidl_client_static root@"$TV_IP":"$SIDE_DIR/bin/android_like_aidl_client"

scp build/android_like_aidl_stale_handle_client_static root@"$TV_IP":"$SIDE_DIR/bin/android_like_aidl_stale_handle_client"
scp build/android_like_aidl_callback_service_static root@"$TV_IP":"$SIDE_DIR/bin/android_like_aidl_callback_service"
scp build/android_like_aidl_callback_threadpool_client_static root@"$TV_IP":"$SIDE_DIR/bin/android_like_aidl_callback_threadpool_client"

scp build/android_like_aidl_listener_registry_service_static root@"$TV_IP":"$SIDE_DIR/bin/android_like_aidl_listener_registry_service"
scp build/android_like_aidl_listener_registry_client_static root@"$TV_IP":"$SIDE_DIR/bin/android_like_aidl_listener_registry_client"

scp build/android_like_aidl_negative_client_static root@"$TV_IP":"$SIDE_DIR/bin/android_like_aidl_negative_client"
scp build/android_like_binder_meta_client_static root@"$TV_IP":"$SIDE_DIR/bin/android_like_binder_meta_client"
scp build/android_like_aidl_binder_return_service_static root@"$TV_IP":"$SIDE_DIR/bin/android_like_aidl_binder_return_service"
scp build/android_like_aidl_binder_return_client_static root@"$TV_IP":"$SIDE_DIR/bin/android_like_aidl_binder_return_client"

scp build/android_like_fd_passing_service_static root@"$TV_IP":"$SIDE_DIR/bin/android_like_fd_passing_service"
scp build/android_like_fd_passing_client_static root@"$TV_IP":"$SIDE_DIR/bin/android_like_fd_passing_client"

scp build/android_like_binder_ping_client_static root@"$TV_IP":"$SIDE_DIR/bin/android_like_binder_ping_client"
scp build/android_like_aidl_oneway_service_static root@"$TV_IP":"$SIDE_DIR/bin/android_like_aidl_oneway_service"
scp build/android_like_aidl_oneway_client_static root@"$TV_IP":"$SIDE_DIR/bin/android_like_aidl_oneway_client"

scp build/android_userspace_preflight_static root@"$TV_IP":"$SIDE_DIR/bin/android_userspace_preflight"
scp build/fd_scm_rights_preflight_static root@"$TV_IP":"$SIDE_DIR/bin/fd_scm_rights_preflight"

scp build/android_like_fd_devnull_service_static root@"$TV_IP":"$SIDE_DIR/bin/android_like_fd_devnull_service"
scp build/android_like_fd_devnull_client_static root@"$TV_IP":"$SIDE_DIR/bin/android_like_fd_devnull_client"

scp build/linux-4.4.84/drivers/android/binder.ko root@"$TV_IP":"$SIDE_DIR/modules/binder.ko"
scp scripts/load-binder-tv.sh root@"$TV_IP":"$SIDE_DIR/load-binder-tv.sh"

ssh root@"$TV_IP" "chmod +x '$SIDE_DIR/bin/mini_servicemgr' '$SIDE_DIR/bin/echo_service' '$SIDE_DIR/bin/echo_client' '$SIDE_DIR/bin/list_services' '$SIDE_DIR/bin/aosp_sm_probe' '$SIDE_DIR/bin/libbinder_lite_client' '$SIDE_DIR/bin/aidl_lite_echo_client' '$SIDE_DIR/bin/aidl_lite_echo_service' '$SIDE_DIR/bin/android_like_echo_client' '$SIDE_DIR/bin/android_like_lifecycle_client' '$SIDE_DIR/bin/android_like_stale_handle_client' '$SIDE_DIR/bin/android_like_death_recipient_client' '$SIDE_DIR/bin/android_like_unlink_death_client' '$SIDE_DIR/bin/android_like_echo_service' '$SIDE_DIR/bin/android_like_callback_service' '$SIDE_DIR/bin/android_like_callback_client' '$SIDE_DIR/bin/android_like_callback_threadpool_client' '$SIDE_DIR/bin/android_like_aidl_service' '$SIDE_DIR/bin/android_like_aidl_client' '$SIDE_DIR/bin/android_like_aidl_stale_handle_client' '$SIDE_DIR/bin/android_like_aidl_callback_service' '$SIDE_DIR/bin/android_like_aidl_callback_threadpool_client' '$SIDE_DIR/bin/android_like_aidl_listener_registry_service' '$SIDE_DIR/bin/android_like_aidl_listener_registry_client' '$SIDE_DIR/bin/android_like_aidl_negative_client' '$SIDE_DIR/bin/android_like_binder_meta_client' '$SIDE_DIR/bin/android_like_aidl_binder_return_service' '$SIDE_DIR/bin/android_like_aidl_binder_return_client' '$SIDE_DIR/bin/android_like_fd_passing_service' '$SIDE_DIR/bin/android_like_fd_passing_client' '$SIDE_DIR/bin/android_like_binder_ping_client' '$SIDE_DIR/bin/android_like_aidl_oneway_service' '$SIDE_DIR/bin/android_like_aidl_oneway_client' '$SIDE_DIR/bin/android_userspace_preflight' '$SIDE_DIR/bin/fd_scm_rights_preflight' '$SIDE_DIR/bin/android_like_fd_devnull_service' '$SIDE_DIR/bin/android_like_fd_devnull_client' '$SIDE_DIR/load-binder-tv.sh'"

echo "Installed sidecar to $TV_IP:$SIDE_DIR"
