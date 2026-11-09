#!/usr/bin/env bash
# Desktop tools: kp_replay (recordings) and kp_live (adapter plugged into this PC). Run inside nix-shell.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cmake -S "$ROOT/native" -B "$ROOT/build/desktop" -G Ninja -DKEPYUR_RADIO=ON >/dev/null
ninja -C "$ROOT/build/desktop" kp_replay kp_live
