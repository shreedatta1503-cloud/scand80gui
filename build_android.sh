#!/usr/bin/env bash
# =============================================================================
# build_android.sh — Reproducible production Android APK build for QGroundControl
# -----------------------------------------------------------------------------
# Builds a NATIVE, SIGNED, Release arm64-v8a APK from QGroundControl source.
# Mirrors the project's official CI recipe (.github/workflows/android.yml).
#
# Works on: Ubuntu 22.04+, Debian, and WSL2. (Windows 11 native: use build_android.bat)
#
# Toolchain it provisions (idempotent — skips anything already present):
#   - JDK 17                 (apt: openjdk-17-jdk  → provides keytool)
#   - Android cmdline-tools  (Google zip, version from build-config.json)
#   - Android platform / build-tools / platform-tools  (sdkmanager)
#   - Android NDK r27c       (sdkmanager → provides toolchain + strip)
#   - Qt 6.10.x for Android  (aqtinstall: android_arm64_v8a + QGC modules)
#   - ccache, ninja, mold    (apt) for fast incremental, cached builds
#
# All versions are read from $QGC_SRC/.github/build-config.json (single source
# of truth) so this script never drifts from what QGC actually supports.
#
# Usage:
#   ./build_android.sh                 # full build, default arm64-v8a Release
#   QGC_ABIS="arm64-v8a;armeabi-v7a" ./build_android.sh
#   SKIP_TOOLCHAIN=1 ./build_android.sh        # reuse already-installed tools
#   CLEAN=1 ./build_android.sh                 # wipe build dir first
#
# Output: prints the absolute path of the produced .apk on success.
# =============================================================================
set -Eeuo pipefail

# -----------------------------------------------------------------------------
# 0. Paths & tunables (override any of these via environment)
# -----------------------------------------------------------------------------
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
: "${QGC_SRC:=${HERE}/qgroundcontrol}"          # QGC source tree (cloned if absent)
: "${QGC_REPO:=https://github.com/mavlink/qgroundcontrol.git}"
: "${BUILD_DIR:=${QGC_SRC}/build-android}"       # out-of-source build dir
: "${BUILD_TYPE:=Release}"                        # Release  → stripped, optimized
: "${QGC_ABIS:=arm64-v8a}"                        # target ABI(s); ';'-separated
: "${ANDROID_MIN_SDK:=29}"                        # 29 = Android 10  (req: Android 10+)
: "${TOOLS_ROOT:=/opt}"                           # where SDK/NDK/Qt live
: "${ANDROID_SDK_ROOT:=${TOOLS_ROOT}/android-sdk}"
: "${QT_BASE:=${TOOLS_ROOT}/Qt}"                  # aqt --outputdir
: "${JOBS:=$(nproc)}"                             # CPU-count-driven parallelism
: "${SKIP_TOOLCHAIN:=0}"
: "${CLEAN:=0}"

# Keystore for signing (a self-contained release keystore is generated if none).
: "${QT_ANDROID_KEYSTORE_PATH:=${HERE}/android_release.keystore}"
: "${QT_ANDROID_KEYSTORE_ALIAS:=QGCAndroidKeyStore}"
: "${QT_ANDROID_KEYSTORE_STORE_PASS:=qgcandroid}"
: "${QT_ANDROID_KEYSTORE_KEY_PASS:=${QT_ANDROID_KEYSTORE_STORE_PASS}}"

LOG="${HERE}/build_android.log"
say() { printf '\033[1;36m[build]\033[0m %s\n' "$*" | tee -a "$LOG" ; }
die() { printf '\033[1;31m[fail]\033[0m %s\n' "$*" | tee -a "$LOG" >&2 ; exit 1 ; }
trap 'die "line $LINENO failed (see $LOG). Re-run with SKIP_TOOLCHAIN=1 to resume after a toolchain step."' ERR
: > "$LOG"

# -----------------------------------------------------------------------------
# 1. Source tree + version pins
# -----------------------------------------------------------------------------
if [[ ! -d "$QGC_SRC/.git" ]]; then
  say "Cloning QGroundControl (recursive) into $QGC_SRC ..."
  git clone --recursive --depth 1 "$QGC_REPO" "$QGC_SRC"
else
  say "Using existing QGC source: $QGC_SRC ($(git -C "$QGC_SRC" rev-parse --short HEAD))"
  git -C "$QGC_SRC" submodule update --init --recursive
fi

CFG="$QGC_SRC/.github/build-config.json"
[[ -f "$CFG" ]] || die "build-config.json not found at $CFG"
read_cfg() { python3 -c "import json,sys;print(json.load(open('$CFG'))['$1'])"; }
QT_VERSION="$(read_cfg qt_version)"
QT_MODULES="$(read_cfg qt_modules)"
NDK_FULL="$(read_cfg ndk_full_version)"
ANDROID_PLATFORM="$(read_cfg android_platform)"
ANDROID_BUILD_TOOLS="$(read_cfg android_build_tools)"
CMDLINE_TOOLS="$(read_cfg android_cmdline_tools)"
JAVA_VERSION="$(read_cfg java_version)"
say "Pins → Qt $QT_VERSION | NDK $NDK_FULL | SDK platform $ANDROID_PLATFORM | build-tools $ANDROID_BUILD_TOOLS | JDK $JAVA_VERSION | minSdk $ANDROID_MIN_SDK"

HOST_QT_ROOT="${QT_BASE}/${QT_VERSION}/gcc_64"
TARGET_QT_ROOT="${QT_BASE}/${QT_VERSION}/android_arm64_v8a"
ANDROID_NDK_ROOT="${ANDROID_SDK_ROOT}/ndk/${NDK_FULL}"

# -----------------------------------------------------------------------------
# 2. Toolchain provisioning (idempotent)
# -----------------------------------------------------------------------------
if [[ "$SKIP_TOOLCHAIN" != "1" ]]; then

  say "Installing host packages (JDK $JAVA_VERSION, ccache, ninja, mold, build essentials) ..."
  export DEBIAN_FRONTEND=noninteractive
  if command -v sudo >/dev/null 2>&1 && [[ $EUID -ne 0 ]]; then SUDO=sudo; else SUDO=""; fi
  $SUDO apt-get update -qq
  $SUDO apt-get install -y --no-install-recommends \
      "openjdk-${JAVA_VERSION}-jdk-headless" ca-certificates curl unzip \
      ninja-build cmake ccache mold python3 python3-venv python3-pip git >/dev/null
  export JAVA_HOME="$(dirname "$(dirname "$(readlink -f "$(command -v javac)")")")"
  say "JAVA_HOME=$JAVA_HOME"

  # --- Python venv for QGC code generators (jinja2) ---
  if [[ ! -x "$QGC_SRC/.venv/bin/python" ]]; then
    python3 -m venv "$QGC_SRC/.venv"
  fi
  "$QGC_SRC/.venv/bin/python" -m pip install -q --upgrade pip jinja2 defusedxml

  # --- Android cmdline-tools ---
  if [[ ! -x "${ANDROID_SDK_ROOT}/cmdline-tools/latest/bin/sdkmanager" ]]; then
    say "Installing Android cmdline-tools ${CMDLINE_TOOLS} ..."
    $SUDO mkdir -p "${ANDROID_SDK_ROOT}/cmdline-tools"
    $SUDO chown -R "$(id -u):$(id -g)" "${ANDROID_SDK_ROOT}" || true
    tmp="$(mktemp -d)"
    curl -fSL -o "$tmp/cmdtools.zip" \
      "https://dl.google.com/android/repository/commandlinetools-linux-${CMDLINE_TOOLS}_latest.zip"
    unzip -q "$tmp/cmdtools.zip" -d "$tmp"
    rm -rf "${ANDROID_SDK_ROOT}/cmdline-tools/latest"
    mv "$tmp/cmdline-tools" "${ANDROID_SDK_ROOT}/cmdline-tools/latest"
    rm -rf "$tmp"
  fi
  export ANDROID_SDK_ROOT ANDROID_HOME="$ANDROID_SDK_ROOT"
  SDKMANAGER="${ANDROID_SDK_ROOT}/cmdline-tools/latest/bin/sdkmanager"
  export PATH="${ANDROID_SDK_ROOT}/cmdline-tools/latest/bin:${ANDROID_SDK_ROOT}/platform-tools:${PATH}"

  say "Accepting SDK licenses + installing platform-tools, platform $ANDROID_PLATFORM, build-tools $ANDROID_BUILD_TOOLS, NDK $NDK_FULL ..."
  yes | "$SDKMANAGER" --licenses >/dev/null 2>&1 || true
  "$SDKMANAGER" \
      "platform-tools" \
      "platforms;android-${ANDROID_PLATFORM}" \
      "build-tools;${ANDROID_BUILD_TOOLS}" \
      "ndk;${NDK_FULL}" >/dev/null

  # --- Qt for Android (target) + host tools must both exist ---
  command -v aqt >/dev/null 2>&1 || python3 -m pip install --user -q aqtinstall
  export PATH="$HOME/.local/bin:$PATH"
  if [[ ! -d "$HOST_QT_ROOT" ]]; then
    say "Installing host Qt $QT_VERSION (gcc_64) for moc/qmlcachegen ..."
    aqt install-qt linux desktop "$QT_VERSION" linux_gcc_64 \
        --outputdir "$QT_BASE" --modules $QT_MODULES
  fi
  if [[ ! -d "$TARGET_QT_ROOT" ]]; then
    say "Installing target Qt $QT_VERSION (android_arm64_v8a) + modules ..."
    aqt install-qt linux android "$QT_VERSION" android_arm64_v8a \
        --outputdir "$QT_BASE" --modules $QT_MODULES
  fi
fi   # end toolchain

# Re-derive env (so SKIP_TOOLCHAIN runs still get correct exports)
export JAVA_HOME="${JAVA_HOME:-$(dirname "$(dirname "$(readlink -f "$(command -v javac)")")")}"
export ANDROID_SDK_ROOT ANDROID_HOME="$ANDROID_SDK_ROOT" ANDROID_NDK_ROOT
export PATH="${ANDROID_SDK_ROOT}/cmdline-tools/latest/bin:${ANDROID_SDK_ROOT}/platform-tools:${ANDROID_SDK_ROOT}/build-tools/${ANDROID_BUILD_TOOLS}:${HOME}/.local/bin:${PATH}"
export PATH="${HOST_QT_ROOT}/bin:${PATH}"

# ccache: cache compiles, cap at 20G
export CCACHE_DIR="${CCACHE_DIR:-${HOME}/.cache/ccache}"
ccache -M 20G >/dev/null 2>&1 || true

[[ -d "$TARGET_QT_ROOT" ]] || die "Target Qt missing at $TARGET_QT_ROOT (run without SKIP_TOOLCHAIN)"
[[ -d "$ANDROID_NDK_ROOT" ]] || die "NDK missing at $ANDROID_NDK_ROOT"
command -v keytool >/dev/null || die "keytool missing (JDK not installed)"

# -----------------------------------------------------------------------------
# 3. Signing keystore (generate a self-contained release keystore if absent)
# -----------------------------------------------------------------------------
if [[ ! -f "$QT_ANDROID_KEYSTORE_PATH" ]]; then
  say "Generating release keystore at $QT_ANDROID_KEYSTORE_PATH ..."
  keytool -genkeypair -v \
    -keystore "$QT_ANDROID_KEYSTORE_PATH" \
    -alias "$QT_ANDROID_KEYSTORE_ALIAS" \
    -keyalg RSA -keysize 2048 -validity 10000 \
    -storepass "$QT_ANDROID_KEYSTORE_STORE_PASS" \
    -keypass "$QT_ANDROID_KEYSTORE_KEY_PASS" \
    -dname "CN=QGroundControl, O=QGC, C=US"
fi
export QT_ANDROID_KEYSTORE_PATH QT_ANDROID_KEYSTORE_ALIAS \
       QT_ANDROID_KEYSTORE_STORE_PASS QT_ANDROID_KEYSTORE_KEY_PASS

# -----------------------------------------------------------------------------
# 4. Configure  (mirrors .github/actions/cmake-configure, use-qt-cmake=false)
# -----------------------------------------------------------------------------
[[ "$CLEAN" == "1" ]] && { say "CLEAN=1 → removing $BUILD_DIR"; rm -rf "$BUILD_DIR"; }
mkdir -p "$BUILD_DIR"

VENV_PY="$QGC_SRC/.venv/bin/python"
say "Configuring (CMake $(cmake --version | head -1 | awk '{print $3}'), $JOBS jobs) ..."
cmake -S "$QGC_SRC" -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DCMAKE_TOOLCHAIN_FILE="${TARGET_QT_ROOT}/lib/cmake/Qt6/qt.toolchain.cmake" \
  -DCMAKE_PREFIX_PATH="${TARGET_QT_ROOT}" \
  -DQT_HOST_PATH="${HOST_QT_ROOT}" \
  -DQT_ANDROID_ABIS="${QGC_ABIS}" \
  -DANDROID_SDK_ROOT="${ANDROID_SDK_ROOT}" \
  -DANDROID_NDK="${ANDROID_NDK_ROOT}" \
  -DANDROID_NDK_ROOT="${ANDROID_NDK_ROOT}" \
  -DQT_ANDROID_SIGN_APK=ON \
  -DQGC_QT_ANDROID_MIN_SDK_VERSION="${ANDROID_MIN_SDK}" \
  -DPython3_EXECUTABLE="${VENV_PY}" \
  -DCMAKE_C_COMPILER_LAUNCHER=ccache \
  -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
  -DCMAKE_WARN_DEPRECATED=FALSE \
  2>&1 | tee -a "$LOG"

# -----------------------------------------------------------------------------
# 5. Build → APK
# -----------------------------------------------------------------------------
# Release linker strips symbols (-s); LTO/optimization come from Qt's Release
# flags. Gradle daemon disabled (CI parity / avoids hung daemons).
export GRADLE_OPTS="-Dorg.gradle.daemon=false"

say "Building all native targets ($JOBS parallel jobs) ..."
cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" --parallel "$JOBS" 2>&1 | tee -a "$LOG"

say "Packaging signed APK (apk target → androiddeployqt + gradle) ..."
cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" --target apk --parallel "$JOBS" 2>&1 | tee -a "$LOG"

# -----------------------------------------------------------------------------
# 6. Locate, verify, report
# -----------------------------------------------------------------------------
APK="$(find "$BUILD_DIR" -path '*android-build*' -name '*.apk' \
        \( -name '*release*' -o -name '*signed*' \) 2>/dev/null | head -1)"
[[ -n "$APK" ]] || APK="$(find "$BUILD_DIR" -name '*.apk' 2>/dev/null | head -1)"
[[ -n "$APK" ]] || die "No APK produced — inspect $LOG"

: "${OUT_APK:=QGroundControl.apk}"
OUT="${HERE}/${OUT_APK}"
cp "$APK" "$OUT"

# Verify signature if apksigner is available
APKSIGNER="${ANDROID_SDK_ROOT}/build-tools/${ANDROID_BUILD_TOOLS}/apksigner"
if [[ -x "$APKSIGNER" ]]; then
  say "Verifying APK signature ..."
  "$APKSIGNER" verify --verbose "$OUT" 2>&1 | tee -a "$LOG" | grep -E 'Verified|scheme' || true
fi

say "=============================================================="
say " BUILD SUCCESS"
say "   APK : $OUT"
say "   size: $(du -h "$OUT" | cut -f1)"
say "   abis: $QGC_ABIS   minSdk: $ANDROID_MIN_SDK   type: $BUILD_TYPE"
say "   install: adb install -r \"$OUT\""
say "=============================================================="
