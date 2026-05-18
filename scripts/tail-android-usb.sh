#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
[ -f "$ROOT/configs/android-usb.env" ] && . "$ROOT/configs/android-usb.env"

TV_IP="${TV_IP:-192.168.2.121}"
ANDROID_USB_MOUNT="${ANDROID_USB_MOUNT:-/tmp/android-usb}"
ANDROID_SIDE_DIR="${ANDROID_SIDE_DIR:-$ANDROID_USB_MOUNT/android-sidecar}"
LOG="${ANDROID_SIDE_DIR}/logs/android-usb-install.log"

ssh -t root@"$TV_IP" "LOG='$LOG'; echo 'Tailing: '\$LOG; while [ ! -f \"\$LOG\" ]; do echo 'Waiting for log...'; sleep 2; done; tail -n 160 -F \"\$LOG\""
