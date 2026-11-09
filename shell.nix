# kepyur build environment: everything the desktop harness and the Android app need.
#   nix-shell            # then: scripts/build-desktop.sh, scripts/build-android.sh
let
  pkgs = import <nixpkgs> {
    config.allowUnfree = true;
    config.android_sdk.accept_license = true;
  };
  ndkVersion = "28.2.13676358";
  android = pkgs.androidenv.composeAndroidPackages {
    platformVersions = [ "35" ];
    buildToolsVersions = [ "35.0.1" ];
    includeNDK = true;
    ndkVersions = [ ndkVersion ];
    cmakeVersions = [ "3.31.6" ];
    includeEmulator = false;
    includeSystemImages = false;
  };
  sdk = android.androidsdk;
  # wifi-sdr assembles the 8812BU's on-chip streamer (MIPS16e2) at build time
  mips = pkgs.pkgsCross.mipsel-linux-gnu.buildPackages.binutils;
in
pkgs.mkShell {
  packages = [
    sdk
    pkgs.jdk17
    pkgs.gradle
    pkgs.cmake
    pkgs.ninja
    pkgs.pkg-config
    pkgs.libusb1
    mips
    pkgs.ffmpeg
    (pkgs.python3.withPackages (ps: [ ps.numpy ps.scipy ps.matplotlib ps.pillow ]))
  ];
  ANDROID_HOME = "${sdk}/libexec/android-sdk";
  ANDROID_SDK_ROOT = "${sdk}/libexec/android-sdk";
  ANDROID_NDK_ROOT = "${sdk}/libexec/android-sdk/ndk/${ndkVersion}";
  KEPYUR_NDK_VERSION = ndkVersion;
  KEPYUR_MIPS_PREFIX = "${mips}/bin/mipsel-unknown-linux-gnu-";
  JAVA_HOME = "${pkgs.jdk17}";
  # aapt2 from Maven is a dynamically linked binary that NixOS cannot run; use the SDK's
  GRADLE_OPTS = "-Dorg.gradle.project.android.aapt2FromMavenOverride=${sdk}/libexec/android-sdk/build-tools/35.0.1/aapt2";
}
