#!/usr/bin/env bash
# Android debug APK -> android/app/build/outputs/apk/debug/app-debug.apk. Run inside nix-shell.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT/android" && gradle --no-daemon -q assembleDebug
ls -la "$ROOT/android/app/build/outputs/apk/debug/app-debug.apk"
