#!/usr/bin/env bash
# kepyur on the phone, over wireless adb (the adapter occupies the USB-C port). Run inside nix-shell.
#
#   scripts/phone.sh pair IP:PAIRPORT CODE   once: Developer options > Wireless debugging > Pair with code
#   scripts/phone.sh connect IP:PORT         each session (the port shown on the Wireless debugging screen)
#   scripts/phone.sh install                 build + install the debug APK
#   scripts/phone.sh usbtest                 40 MS/s 60 s + 20 MS/s 60 s stream test, then scan/watch; pulls logs
#   scripts/phone.sh run                     start normally (scan, list, watch)
#   scripts/phone.sh logs [NAME]             pull kepyur.log + logcat into logs/<date>-NAME/
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
PKG=com.kepyur.app
APP_LOG=/sdcard/Android/data/$PKG/files/kepyur.log

# Wireless debugging often lists the phone twice (mDNS name + ip:port): pick one unless ANDROID_SERIAL says
if [ -z "${ANDROID_SERIAL:-}" ] && [ "${1:-}" != pair ] && [ "${1:-}" != connect ]; then
  ANDROID_SERIAL=$(adb devices | awk 'NR > 1 && $2 == "device" { print $1 }' | grep -m1 ':' || \
                   adb devices | awk 'NR > 1 && $2 == "device" { print $1; exit }')
  [ -n "$ANDROID_SERIAL" ] || { echo "no phone: scripts/phone.sh connect IP:PORT" >&2; exit 1; }
  export ANDROID_SERIAL
fi

pull_logs() {
  local dir="$ROOT/logs/$(date +%Y%m%d-%H%M%S)-${1:-session}"
  mkdir -p "$dir"
  adb shell cat "$APP_LOG" > "$dir/kepyur.log" 2>/dev/null || true
  adb logcat -d -v time > "$dir/logcat.txt" || true
  adb shell getprop ro.product.model > "$dir/device.txt" || true
  adb shell dumpsys usb > "$dir/dumpsys-usb.txt" 2>/dev/null || true
  echo "logs -> $dir"
  grep -E "usbtest|test: result|usb3 switch|radio:|open failed|error" "$dir/kepyur.log" | tail -30 || true
}

case "${1:-}" in
  pair) adb pair "$2" "$3" ;;
  connect) adb connect "$2"; adb devices ;;
  install) "$ROOT/scripts/build-android.sh" && adb install -r "$ROOT/android/app/build/outputs/apk/debug/app-debug.apk" ;;
  usbtest)
    adb shell rm -f "$APP_LOG" || true
    adb logcat -c || true
    adb shell am force-stop $PKG
    adb shell am start -n $PKG/.MainActivity --es mode usbtest
    echo "Running the USB test (~2.5 min). If the phone asks to allow kepyur to use the adapter, tap OK"
    echo "(tick 'always' if offered). Keep the VTX powered on R4 if you can: decoding starts after the test."
    for i in $(seq 1 40); do
      sleep 5
      line=$(adb shell cat "$APP_LOG" 2>/dev/null | grep -E "test: result|usbtest: all done|open failed|error:" | tail -1 || true)
      [ -n "$line" ] && echo "  $line"
      adb shell cat "$APP_LOG" 2>/dev/null | grep -q "usbtest: all done" && { sleep 20; break; }
    done
    pull_logs usbtest
    ;;
  run)
    adb shell am force-stop $PKG
    adb shell am start -n $PKG/.MainActivity
    ;;
  logs) pull_logs "${2:-session}" ;;
  *) sed -n '2,10p' "$0"; exit 2 ;;
esac
