# kepyur — design

Android analog FPV receiver (VRX) for spectating, built on an RTL8812BU Wi-Fi adapter used as an SDR.
2026-11-09.

## Intent

The owner can already watch an analog 5.8 GHz FPV quad on a Linux desktop. `../vtx-spike` (throwaway)
proved that an RTL8812BU (TP-Link Archer T4U v3, `2357:0115`) on USB 3, streaming 40 MS/s raw IQ through
[wifi-sdr](https://github.com/snokvist/wifi-sdr) (owner's fork: `github.com/gilankpam/wifi-sdr`, chip
bring-up by [devourer](https://github.com/OpenIPC/devourer)), carries enough of the FM video signal to
decode a stable black-and-white picture. kepyur takes that same stack to an Android phone, so the owner
can sit next to a pilot at the field, plug the adapter into a phone, and watch the pilot's video.

The first version is for **spectating**: someone else flies, the owner watches. Nobody steers from this
picture.

## Goals (first version)

- Runs on **one phone: the owner's vivo X200 Pro** (Dimensity 9400, USB-C at USB 3.2 Gen 1 — believed,
  not yet verified on the device). Other phones are not a goal yet.
- **The phone finds the pilots.** The app scans the 5.8 GHz band on its own and lists analog video
  signals it finds; the owner never types a channel.
- **List, tap to pick.** Several pilots can be in the air; the owner picks one from a list.
- **Stable black-and-white video** of the picked pilot.

**Done means (owner's words):** at the field, the scan finds the quad and shows stable video.

*Builder's call:* "stable" is taken to mean: once locked on a quad, the picture holds for a whole
flight (3–5 min) at full field rate, with no re-acquire loops while the signal is good; when the signal
drops (quad behind a tree) and comes back, the picture recovers within about a second without the owner
touching anything.

## Non-goals (first version)

- **A range target.** The owner set none: the adapter's own 5 GHz antennas, whatever VTX power the pilot
  uses, and the picture is as good as that signal allows. No weak-signal heroics (external LNA, special
  antennas, threshold-extension demodulators) unless the field test shows the basic decoder falls over at
  ordinary distances. The decoder should still degrade gracefully (noisy picture, not lost lock) as the
  signal weakens.
- **Battery life and long sessions.** Owner: "if it dies it dies". No power-saving work; the thermal check
  is only that the phone doesn't throttle the DSP into dropping banks within a flight or two.
- Colour. Black and white is fine for now (owner). The chroma subcarrier is filtered out, as in the spike.
- Flying from the phone / low-latency FPV. Latency of a few frames is acceptable; robustness wins over
  latency whenever they conflict.
- Goggles or HDMI output, DVR/recording, audio subcarrier, OSD, diversity (the chip's path B), other
  adapters (8812CU/EU run at USB 2 rates only, too slow for video), other phones, Play Store release.
- Digital FPV systems (DJI, Walksnail, HDZero, OpenIPC). They show up in the band; the scanner must not
  list them as pilots.

## Anti-goals

- A picture that keeps blanking and re-acquiring while the signal is fine. The spike's live run did this
  (`vtx-spike/live.log`: 248 "lost lock, re-acquiring" in one session, plus a run where the bank queue
  overflowed and dropped 33 538 banks). Shipping that behaviour is a failure even if every feature exists.
- The app silently jumping to a different pilot. If the picked signal fades, kepyur keeps trying on that
  pilot; the owner decides when to switch.

## Constraints

- **USB 3 is mandatory.** wifi-sdr streams continuously only at 2.5–5 MS/s on USB 2; an FM video signal
  occupies ~6 MHz (measured, spike). The video path runs at 40 MS/s (`rate_sel = 1`, `wide = true`,
  ~316 Mbit/s on the wire). If the phone's port, cable or OTG adapter ends up at USB 2, the app must say so
  plainly, not show a broken picture.
- **The adapter cold-plugs at USB 2.** `wsdr::request_usb3()` (wifi-sdr `src/usbmode.h`) flips it to
  SuperSpeed with register writes; it then re-enumerates. On Android that is a new `UsbDevice`, so USB
  permission is asked again.
- **The host USB controller matters.** On the owner's desktop one AMD xHCI wedged large C2H packets and
  another ran perfectly (wifi-sdr README). Nobody knows yet how the X200 Pro's controller behaves. This is
  the project's biggest unknown and is proved first (see Build order).
- **No device scanning on Android.** wifi-sdr's `Session::open` (`src/session.cpp`) and `request_usb3`
  find the adapter with `libusb_get_device_list` + `libusb_open`, which an unrooted Android app cannot do.
  The app gets the device from `UsbManager`, opens a `UsbDeviceConnection`, and hands its file descriptor
  to native code, which wraps it with `libusb_wrap_sys_device` (libusb with
  `LIBUSB_OPTION_NO_DEVICE_DISCOVERY`). OpenIPC's PixelPilot Android app runs devourer on Android this
  way; use it as the reference. wifi-sdr needs an fd-based open path (and the same for `request_usb3`);
  make those changes in the owner's fork (`gilankpam/wifi-sdr`), keeping the desktop path working.
  devourer's `claim_interface_then_reset` does a USB reset; devourer already has a wrapped-fd path that
  can skip the reset — use it, and check early that bring-up works without the reset on Android.
- **Building wifi-sdr needs MIPS cross binutils** (`mipsel-linux-gnu-as/ld/objcopy`): it assembles the
  on-chip streamer `src/fw/stream8822b.S` into a C header at build time. The generated headers are
  host-independent, so the Android build can use the same CMake step.
- **All build dependencies come from a `shell.nix` in the repo** (owner's decision): Android SDK and NDK
  (nixpkgs `androidenv`), JDK, Gradle, CMake, the MIPS binutils, libusb, and the desktop test tooling. The
  host has no `adb`/Java/Gradle on its PATH today.
- **License:** wifi-sdr is GPL-2.0-only and is linked in, so kepyur is GPL-2.0 too.
- **CPU budget.** Every sample at 40 MS/s goes through mix, halfband decimate, FM discriminator, filters
  and sync tracking. The spike's scalar C++ keeps up on the desktop (queue 0). On the phone expect to need
  NEON (or careful auto-vectorisation) and a dedicated big-core thread; the Receiver's sink thread must only
  copy (wifi-sdr rule: a stall there becomes a chip-side fault).

## What the owner decided

- Target the vivo X200 Pro first.
- The adapter plugs **directly** into the phone's USB-C port (OTG adapter or cable), no hub. The phone
  powers it. Bus power is a must-prove item in build step 1, not just a risk.
- The owner runs everything that needs hardware: the phone, the adapter, the VTX/quad, field tests, and
  new IQ recordings. The builder prepares scripted runs and reads back logs and recordings.
- Spectating only; no flying from the phone yet.
- The phone scans and finds pilots; a list, tap to pick.
- Screen layout **B** (mockup: `.superpowers/brainstorm/*/content/flow.html`): video always fills the
  screen (landscape). On launch the app scans; the strongest pilot found starts playing behind a pilot list
  docked on the right. Tapping a row tunes to that pilot and the list slides away. Swiping in from the right
  edge brings the list back.
- Black and white is fine for now.
- Every dependency goes in `shell.nix`.
- Defaults the owner accepted: Kotlin app; native C++ core in the app reusing the wifi-sdr fork and devourer
  as-is; libusb via the Android-provided fd; the decoder also builds and runs on the Linux desktop against
  recorded IQ for bench testing.

## How it works

### Pieces

1. **Radio** — wifi-sdr `wsdr::Receiver` (open with an Android fd, `set()` to retune, `start(sink)` for
   64 KiB banks with `BankMeta`). Unchanged apart from the fd open path.
2. **Video decoder** (native C++, shared by app and desktop) — grown from `vtx-spike/viewer/vtxview.cpp`:
   DC removal, NCO shift (`BankMeta.shift_hz` plus the measured carrier offset), halfband 40→20 MS/s,
   discriminator, sync/luma filters, polarity and level acquisition, line PLL, vsync/field detection,
   field weaving, NTSC/PAL from the line rate. Output: 8-bit grey frames (720×480 NTSC, 720×576 PAL).
3. **Scanner** (native C++) — band sweep and pilot detection (below).
4. **App** (Kotlin) — USB permission and the USB 3 switch, lifecycle, the video surface, the pilot list,
   status. Talks to native code through a small JNI surface.
5. **Desktop harness** — a Linux binary that runs the same decoder and scanner code on `.cs16` recordings
   (`vtx-spike/rec/*.cs16` + `.json`, and `vtx-spike/sim/*.sigmf-data`) and, with an adapter plugged in,
   live. Used to fix lock behaviour on the bench.

### Scanning

- Band: the adapter tunes 5 GHz ch 16–233 (≈5080–6165 MHz). Cover at least the usual analog bands
  A, B, E, F/Fatshark, R/Raceband (≈5645–5945 MHz) and L/low-race (≈5362–5621 MHz).
- Retune + stream restart costs ~150 ms (wifi-sdr FINDINGS). At 40 MS/s each step sees ~±17 MHz usable,
  so 5645–5945 is ~9 steps, ~1.5 s per pass. The 8812BU's gapped 80 MS/s mode
  (`rate_sel = 0, allow_gapped = true`, ~77 MHz per bank) can cut the step count for the spectrum part;
  wsdr-studio already sweeps this way. Which mode the scanner uses is the builder's call.
- Detection: per step, a PSD; candidate = a hump roughly 4–10 MHz wide well above the floor (the spike
  measured ~6 MHz occupied, centroid method in `vtx-spike/decode.py:find_carrier`). For each candidate, a
  short sync check (hsync-width pulses on a 63.5/64 µs grid, as `vtxview`'s acquisition does). Only
  candidates that pass are listed, which keeps Wi-Fi and digital FPV out.
- Each pilot in the list: band/channel name (nearest standard channel, e.g. "R4"), measured frequency
  (e.g. 5769 MHz; cheap VTXs sit a few MHz off nominal and the decoder tunes to the measured carrier),
  NTSC/PAL, signal strength.
- The adapter hears one ~36 MHz slice at a time, so while watching, the rest of the list cannot stay live;
  rows show "last seen". *Builder's call, owner to confirm:* opening the list does **not** interrupt the
  video by itself; a "rescan" action in the list does, and the video comes back on the same pilot afterwards
  unless the owner taps another.

### Screen

- Landscape, full-screen video, aspect-correct (4:3 picture centred), screen kept awake.
- Small corner label: channel, frequency, locked / searching. *Builder's call:* it fades after a few
  seconds and comes back on tap.
- States the owner must be able to tell apart: no adapter; adapter at USB 2 (with a plain hint about
  port/cable); scanning, nothing found yet; locked; signal lost (keeps trying on this pilot).

### Lock robustness

The spike's live decoder lost lock and re-acquired repeatedly while the offline decoder on the same kind
of signal locked 96.6 % of lines. Root cause is not known yet. Fixing it is part of "stable video", and it
is worked on the desktop first against recordings, not at the field. Known suspects worth checking: the
"lost" rule (`lines > 32 && lines_locked * 2 < lines`) counting the vertical interval's freewheeled lines;
levels drifting as the chip AGC moves; the 60 ms acquisition window; dropped banks when the DSP thread falls
behind. The decoder should hold its line timing through short dropouts (freewheel) rather than throw away
lock, and re-acquire fast when it must.

## Working with the hardware

The builder has no hands on the hardware; the owner does (see above). Plan around hand-off points:

- Every hardware step is a scripted run the owner starts with one command (e.g. `nix-shell --run
  ./scripts/...`), producing a log, and where useful an IQ recording, that the builder reads back.
- The adapter occupies the phone's only USB-C port, so `adb` to the phone goes over Wi-Fi (wireless
  debugging). The app must also write its own log file the owner can pull afterwards.
- Ask the owner early for a recording session beyond today's bench captures (three captures of one VTX at
  ~2 m in `vtx-spike/rec`, two simulated files in `vtx-spike/sim`): a quad in flight at ordinary distance,
  a weak/fading signal, several VTXs on air, a digital VTX or Wi-Fi AP nearby. Lock tuning in step 2 runs
  against these, not only the bench captures.

## Build order (builder's call, proposed)

1. **Prove the phone streams, bus-powered, plugged directly in.** Minimal Android app (or native test binary launched by it): get the
   adapter from `UsbManager`, switch to USB 3, re-open, stream 40 MS/s for 60 s, report banks, faults and
   restarts. Measure both 40 MS/s and 20 MS/s (`rate_sel = 2`, ~±8.5 MHz usable, still wider than the
   ~6 MHz signal). *Builder's call:* 20 MS/s is an acceptable fallback for video if 40 MS/s is marginal on
   this controller, so the decoder and scanner take the input rate as a parameter instead of hard-wiring the
   40→20 halfband. Occasional stream restarts (`BankMeta.gen` changes) are tolerated if the decoder rides
   through them (keeps levels and line timing, re-syncs within a field or two); that is a decoder
   requirement. If neither rate streams usefully, stop and bring it back to the owner — everything else
   depends on it.
2. **Decoder on the desktop harness**, fix lock robustness against recordings and live on the desktop.
3. **Decoder on the phone**, measure CPU headroom at 40 MS/s, optimise until it keeps up with margin.
4. **Scanner**, desktop first, then phone.
5. **The screen** (layout B), states, polish.

## Testing

- Desktop harness against the spike's recordings: lock percentage, re-acquire count, frames out. Regression
  numbers per recording, so lock changes can be judged.
- Make new recordings for the hard cases (weak signal, quad moving, multiple VTXs on air, a Wi-Fi AP or
  digital VTX nearby) with `vtx-spike/record.sh` or an equivalent in the harness.
- On the phone: USB soak (60 s+, zero dropped banks), CPU/thermal over a 10-minute session (no dropped
  banks from throttling), then the field test the owner named.

## Left to the builder

Project layout, Kotlin UI toolkit, how frames reach the screen (e.g. `ANativeWindow` / GL texture), JNI
shape, thread layout, NEON vs auto-vectorisation, scan mode and timing, exact level/threshold tuning,
how the wifi-sdr fork is pulled in (submodule or vendored), and the desktop harness's form.

## Open risks

- X200 Pro xHCI behaviour with 40 MS/s C2H traffic (unknown; step 1).
- USB reset / re-enumeration on a wrapped Android fd (devourer bring-up does a reset).
- Whether vivo's Android build lets the app keep the USB device across the USB 3 re-enumeration smoothly.
- Phone thermals under sustained 40 MS/s DSP.
- Bus power: the adapter draws from the phone's USB-C port directly (proved in step 1).
