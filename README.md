# kepyur

Android analog FPV receiver for spectating. A TP-Link Archer T4U v3 (RTL8812BU) plugged into the phone
streams 40 MS/s raw IQ through [wifi-sdr](https://github.com/snokvist/wifi-sdr) (fork:
`gilankpam/wifi-sdr`, branch `kepyur-android`) and [devourer](https://github.com/OpenIPC/devourer); kepyur
scans the 5.8 GHz bands for analog video, lists the pilots, and decodes the picked one in colour (NTSC and PAL; grey when the colour burst is too weak).

The latest APK from `main` is the rolling **main** pre-release (tag `main-latest`), rebuilt by CI
(`.github/workflows/android.yml`) on every push to `main`. It is a debug build signed with CI's key: if a
locally built kepyur is installed, uninstall it first (`adb uninstall com.kepyur.app`).

## Layout

- `native/dsp/` — video decoder (FM discriminator, sync, fields), spectrum + pilot finding. No hardware.
- `native/radio/` — the adapter (wifi-sdr `Receiver`) + a DSP thread: scan, watch, stream test.
- `native/jni/` — the app's native side; frames go straight to the video Surface.
- `native/tools/` — desktop: `kp_replay` (recordings), `kp_live` (adapter on this PC), `kp_sim` (kp_live
  against a recording played back as a tunable adapter).
- `android/` — the app (Kotlin, plain Views). `scripts/` — build and phone helpers.

## Build

Everything comes from `shell.nix`:

```sh
git clone https://github.com/gilankpam/kepyur && cd kepyur
git submodule update --init third_party/libusb third_party/wifi-sdr
git -C third_party/wifi-sdr submodule update --init external/devourer
nix-shell
scripts/build-desktop.sh      # build/desktop/kp_replay, kp_live, kp_sim
scripts/build-android.sh      # android/app/build/outputs/apk/debug/app-debug.apk
```

## On the phone

The adapter takes the USB-C port, so adb runs over Wi-Fi (Developer options → Wireless debugging):

```sh
scripts/phone.sh pair 192.168.1.23:37000 123456   # once ("Pair device with pairing code")
scripts/phone.sh connect 192.168.1.23:41000       # the port on the Wireless debugging screen
scripts/phone.sh install
scripts/phone.sh usbtest    # 60 s at 40 MS/s + 60 s at 20 MS/s, then scan/watch; logs -> logs/
scripts/phone.sh run        # normal start
scripts/phone.sh logs NAME  # pull kepyur.log + logcat
```

## Desktop bench

```sh
build/desktop/kp_replay ../vtx-spike/rec/demo.cs16            # lock numbers
build/desktop/kp_replay ../vtx-spike/rec/r4.cs16 --cnr 8      # with noise added (CNR in an 18 MHz channel)
build/desktop/kp_replay ../vtx-spike/rec/r4.cs16 --frames | ffplay -f rawvideo -pixel_format rgb0 -video_size 720x480 -i -
KP_SIM_FILE=../vtx-spike/rec/r4.cs16 build/desktop/kp_sim --scan   # scan -> watch without hardware
build/desktop/kp_live --scan --frames | ffplay -f rawvideo -pixel_format rgb0 -video_size 720x480 -framerate 60 -fflags nobuffer -i -
```

Measured on the spike's recordings (R4 NTSC VTX on the bench): 96.5 % of lines locked, no lock losses
over 10 s; the picture holds to about 5 dB CNR (it gets snowy, it doesn't drop).

License: GPL-2.0 (wifi-sdr is GPL-2.0-only).
