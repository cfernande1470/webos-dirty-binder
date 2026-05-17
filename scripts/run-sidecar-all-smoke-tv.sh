#!/usr/bin/env bash
set -euo pipefail

TV_IP="${TV_IP:-192.168.2.121}"
SIDE_DIR="${SIDE_DIR:-/media/internal/android-sidecar}"
CLIENTS="${CLIENTS:-8}"
ROUNDS="${ROUNDS:-10}"

export TV_IP SIDE_DIR CLIENTS ROUNDS

echo "== sidecar basic smoke =="
./scripts/run-sidecar-smoke-tv.sh

echo "== sidecar listServices smoke =="
./scripts/run-sidecar-list-smoke-tv.sh

echo "== sidecar death smoke =="
./scripts/run-sidecar-death-smoke-tv.sh

echo "== sidecar multiservice smoke =="
./scripts/run-sidecar-multiservice-smoke-tv.sh

echo "== sidecar stress smoke =="
./scripts/run-sidecar-stress-smoke-tv.sh

echo "== sidecar rebind smoke =="
./scripts/run-sidecar-rebind-smoke-tv.sh

echo "== sidecar context-manager restart smoke =="
./scripts/run-sidecar-context-restart-smoke-tv.sh

echo "== sidecar duplicate registration smoke =="
./scripts/run-sidecar-duplicate-smoke-tv.sh

echo "== AOSP ServiceManager compatibility smoke =="
./scripts/run-aosp-sm-compat-smoke-tv.sh

echo "== libbinder-lite client smoke =="
./scripts/run-libbinder-lite-client-smoke-tv.sh

echo "== Android-like API smoke =="
./scripts/run-android-like-api-smoke-tv.sh

echo "== Android-like service smoke =="
./scripts/run-android-like-service-smoke-tv.sh

echo "== Android-like lifecycle smoke =="
./scripts/run-android-like-lifecycle-smoke-tv.sh

echo "== Android-like restart recovery smoke =="
./scripts/run-android-like-restart-recovery-smoke-tv.sh

echo "== Android-like death recipient smoke =="
./scripts/run-android-like-death-recipient-smoke-tv.sh

echo "== Android-like unlink death smoke =="
./scripts/run-android-like-unlink-death-smoke-tv.sh

echo "== Android-like concurrent lifecycle smoke =="
./scripts/run-android-like-concurrent-lifecycle-smoke-tv.sh

echo "== Android-like callback smoke =="

./scripts/run-android-like-callback-smoke-tv.sh

echo "BINDER_CALLBACK_V0_OK"

echo "== Android-like callback stress smoke =="

CLIENTS="${CLIENTS:-8}" ./scripts/run-android-like-callback-stress-tv.sh

echo "BINDER_CALLBACK_STRESS_V0_OK"


echo "BINDER_LIFECYCLE_V0_OK"
echo "BINDER_DEATH_NOTIFICATION_V0_OK"
echo "ALL_SIDECAR_SMOKE_OK"


echo "== Android-like AIDL Parcel smoke =="

ROUNDS="${ROUNDS:-16}" ./scripts/run-android-like-aidl-smoke-tv.sh

echo "AIDL_LIKE_PARCEL_V0_OK"


echo "== Android-like AIDL concurrent stress smoke =="

CLIENTS="${CLIENTS:-16}" ROUNDS="${ROUNDS:-50}" ./scripts/run-android-like-aidl-stress-tv.sh

echo "AIDL_LIKE_STRESS_V0_OK"


echo "== Android-like AIDL service recovery smoke =="

CYCLES="${CYCLES:-5}" ROUNDS="${ROUNDS:-10}" ./scripts/run-android-like-aidl-recovery-tv.sh

echo "AIDL_LIKE_RECOVERY_V0_OK"


echo "== Android-like AIDL stale handle recovery smoke =="

CLIENT_SLEEP="${CLIENT_SLEEP:-8}" ./scripts/run-android-like-aidl-stale-handle-tv.sh

echo "AIDL_LIKE_STALE_HANDLE_V0_OK"


echo "== Android-like AIDL callback listener smoke =="

./scripts/run-android-like-aidl-callback-listener-tv.sh

echo "AIDL_LIKE_CALLBACK_LISTENER_V0_OK"


echo "== Android-like AIDL callback listener death smoke =="

./scripts/run-android-like-aidl-callback-listener-death-tv.sh

echo "AIDL_LIKE_CALLBACK_LISTENER_DEATH_V0_OK"
