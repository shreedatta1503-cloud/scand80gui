# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

> **UPGRADED to QGroundControl v5.0.8 stable (2026-06-18).** `qgroundcontrol/` was re-based from upstream
> master onto the official **v5.0.8** stable tag (branch `upgrade/v5.0.8-stable`, `git describe` =
> `v5.0.8-N-g…`) and the scand80gui customizations were replayed on top. Toolchain pins are now Qt **6.8.3**,
> NDK **r26b**, Android GStreamer **1.22.12** (were Qt 6.10.3 / r27c / 1.28.1). The Android APK was rebuilt
> and is `/root/QGroundControl.apk` (~82 MB, versionName 5.0.8). See the dated entry in
> `qgroundcontrol/CLAUDE.md` for the full migration log; version-specific facts below are updated to match.

## Repository layout — read this first

This working directory (`/root`) contains **two distinct things**:

1. **`/root` itself — the `scand80gui` git repo** (remote: `github.com/shreedatta1503-cloud/scand80gui`). It tracks only a snapshot of QGroundControl's CMake build configuration:
   - `CMakeLists.txt` — byte-identical to `qgroundcontrol/CMakeLists.txt` (the QGC top-level build script).
   - `qgc-cmake/` — copies of QGC cmake modules and `build-config.json` (the source-of-truth for tool/Qt/dependency versions; see below). Note: v5.0.8 dropped `cmake/Helpers.cmake` and `cmake/Windows.cmake` (they existed only on the prior master base), so the mirrored `qgc-cmake/Helpers.cmake`/`Windows.cmake` are now obsolete snapshots; `CustomOptions.cmake` and `build-config.json` remain live mirrors.
   - These files have **no source tree to build on their own** — they reference `cmake/`, `src/`, `test/`, `resources/`, etc., which only exist inside `qgroundcontrol/`.

2. **`qgroundcontrol/` — the actual application** (a *separate* git repo with its own remote `…/qgroundcontrol.git`, **not** tracked by `scand80gui`; it is gitignored in `/root`). All real development, building, and testing happens here.

**Implication:** to build/run/test anything, `cd qgroundcontrol` first. The `scand80gui` repo is for versioning the build-config customization, not for compiling.

## Build-config source of truth

`qgc-cmake/build-config.json` mirrors `qgroundcontrol/.github/build-config.json` and pins all toolchain versions — **post-v5.0.8 upgrade: Qt `6.8.3` (min/max 6.8.3), CMake `3.22.1+`, NDK `r26b` (`26.1.10909125`), Android GStreamer `1.22.12`, JDK 17** (were Qt 6.10.x / CMake 3.25+ / NDK r27c / GStreamer 1.28.1 on the master base). Note: v5.0.8 itself has no `.github/build-config.json` — that single-source-of-truth file is a scand80gui convention; it was recreated with v5.0.8's pinned versions so `build_android.sh` keeps working. The `build_android.sh`/`.bat` pipeline reads it. Change versions there, not scattered through the CMake.

## Working in `qgroundcontrol/`

QGroundControl ships its own agent guidance — **read `qgroundcontrol/AGENTS.md` (and the docs it links) before editing**. Key entry points it names:
- `src/FactSystem/Fact.h` — parameter system foundation
- `src/Vehicle/Vehicle.h` — core vehicle model
- `src/FirmwarePlugin/FirmwarePlugin.h` — PX4/ArduPilot abstraction
- `qgroundcontrol/CODING_STYLE.md` — naming, C++20, QML style, common pitfalls
- `qgroundcontrol/.github/CONTRIBUTING.md` — architecture patterns (Fact System, Multi-Vehicle, FirmwarePlugin)
- `qgroundcontrol/test/TESTING.md` — CTest labels, base classes, coverage, sanitizers

### Common commands (run from inside `qgroundcontrol/`)

Development is driven by `just` (the `justfile`). It reads `QT_DIR`/`BUILD_TYPE` env vars and pins Python to `.venv` when present.

```bash
just configure      # qt-cmake -B build -G Ninja, with -DQGC_BUILD_TESTING=ON (Debug by default)
just build          # cmake --build build --parallel
just release        # configure + build a Release (testing off)
just run            # launch ./build/Debug/QGroundControl
just test           # ctest with CI label filters: -L "Unit|Integration" -LE "Flaky|Network"
just lint           # pre-commit run --all-files (clang-format/-tidy, ruff, pyright, qmllint, clazy, …)
just check          # lint + test
just clean [ARGS]   # tools/clean.py (pass --cache / --all / --dry-run)
just rebuild        # clean + configure + build
```

Run a **single / filtered set of tests** by overriding the label filters:
```bash
cd build && ctest --output-on-failure -L Unit -R <TestNameRegex>
LABELS=Unit EXCLUDE=Flaky just test
```

Other useful targets: `just format` / `format-fix` (clang-format), `just analyze` (static analysis), `just coverage`, `just info` (print resolved build config), `just docker` (containerized Ubuntu build).

Notes:
- `just configure` runs `git submodule update --init --recursive` first. Submodules must be initialized.
- Ubuntu's `apt install just` (1.21) is too old; the justfile needs `just >= 1.30`. Install via `python tools/setup/install_python.py dev`, `pipx install rust-just`, or `cargo install just`.
- System deps (Debian/Ubuntu): `just deps` (calls `tools/setup/install_dependencies --platform debian`, needs sudo).

### Building on this box (no `just` binary installed)

`just` is **not** installed here, so drive CMake directly. After the v5.0.8 upgrade the host Qt is **6.8.3** at `/opt/Qt/6.8.3/gcc_64` (installed by `build_android.sh`; the old `/opt/Qt/6.10.3` from the master base may still be present). The legacy `build/` dir was configured against the prior master tree (Qt 6.10.3) — for v5.0.8 use a fresh build dir. Dependencies are fetched via FetchContent/CPM and cached under `qgroundcontrol/.cache/CPM` / `build-android/cpm_modules`.

```bash
cd qgroundcontrol
export QT_DIR=/opt/Qt/6.8.3/gcc_64 PATH="$QT_DIR/bin:$PATH"
qt-cmake -S . -B build-desktop -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DPython3_EXECUTABLE="$PWD/.venv/bin/python"
cmake --build build-desktop --config Release --parallel "$(nproc)"   # 128 cores here
```

Main source builds with **warnings-as-errors**.

> **⚠️ Desktop (native Linux) build does NOT configure on this box after the v5.0.8 upgrade.** v5.0.8 +
> Qt 6.8.3 + the host system GStreamer (1.28.2) trips a `qt_add_resources` error — the bundled GStreamer
> qt6 plugin emits an absolute `.qsb` path that Qt 6.8.3 rejects (`Qt6CoreMacros.cmake:1843`). This is an
> upstream/environment quirk, **not** a fork-migration issue (a clean v5.0.8 checkout fails identically
> here), and it does **not** affect the Android APK (which statically links GStreamer 1.22.12). To get a
> desktop binary you'd need a 1.22.x GStreamer or to exclude the GStreamer qt6 plugin. The **Android APK
> build is the validated path on this box** (see below).

### Running headless (this is an Xvfb server)

No GPU/WM — Xvfb runs on `DISPLAY=:99` (1600x1000), software mesa only. QGC **refuses to run as root** (`getuid()==0`) and enforces a **single instance** (`RunGuard`/`QLockFile`). Launch as the `qgcuser` account with a **clean, isolated environment**:

```bash
cd qgroundcontrol/build/Release
runuser -u qgcuser -- env -i \
  DISPLAY=:99 HOME=/home/qgcuser TMPDIR=/tmp \
  QT_QPA_PLATFORM=xcb PATH=/usr/bin:/bin ./QGroundControl
```

- **Use `env -i` and pin `TMPDIR=/tmp`.** The shell here exports `TMPDIR=/tmp/claude-0`; QGC's single-instance lock (`$TMPDIR/qgc-<hash>.lock`, via `src/Utilities/Platform/RunGuard.cc`) lands in whatever `TMPDIR` points to. Inheriting the harness temp dir scatters the lock and invites the stale-lock trap below.
- **Stale-lock trap:** `RunGuard` uses `setStaleLockTime(0)`, so it only reclaims a lock whose owner PID is **dead**. After a crash/kill, if that PID gets reused by an unrelated live process, QGC wrongly decides "a second instance is already running," pops an `xmessage` dialog, and parks itself **single-threaded without initializing**. Recovery: kill leftover `QGroundControl`/`xmessage` procs and `rm` any `qgc-*.lock` under the temp dirs, then relaunch.
- **Verifying a launch:** a healthy instance reaches ~85 threads / ~350 MB RSS and logs `API.QGCApplication` → `QML ready`. A stuck (lock-blocked) instance sits at **1 thread**. The `GStreamer plugin not found` / `GST_PLUGIN_PATH unset` / `speechd` TTS `Critical` log lines are **non-fatal** in this env (video/audio disabled). The Qt Quick window does **not** composite to a capturable surface here, so screenshots are blank — the thread count + `QML ready` log is the real launch proof. Always shut down cleanly (SIGTERM) to release the lock.

### Building a native Android APK

A reproducible pipeline for producing a **signed, Release `arm64-v8a` APK** lives in `/root` (the `scand80gui` repo): `build_android.sh` (Linux/WSL2), `build_android.bat` (Windows 11), and `README.md`. It mirrors QGC's official CI recipe (v5.0.8: `qgroundcontrol/.github/workflows/android-linux.yml`) and reads every version from `.github/build-config.json`, so it never drifts from upstream. **Do not** try to convert the `.exe` installer — this compiles from source. **This is the validated build path on this headless box** (the native-Linux desktop build does not configure here — see the GStreamer/`qt_add_resources` caveat above).

```bash
cd /root
./build_android.sh                          # full: provision toolchain → configure → build → sign → verify
SKIP_TOOLCHAIN=1 ./build_android.sh         # reuse installed toolchain (fast; ccache warm)
OUT_APK=My.apk SKIP_TOOLCHAIN=1 ./build_android.sh   # override output filename
```

- **Output:** `/root/QGroundControl.apk` (default name; override with `OUT_APK`). **~82 MB** (v5.0.8; was ~187 MB on the master base), **versionName 5.0.8**, signed v3 scheme, `org.mavlink.qgroundcontrol`, **minSdk 29 (Android 10)**, target/compile SDK 35, arm64-v8a only. GStreamer video is enabled and **statically linked** into `libQGroundControl_arm64-v8a.so` (so there are no separate `libgst*.so` — that, plus the older GStreamer 1.22.12, accounts for the smaller size).
- **Toolchain it provisions to `/opt`** (idempotent, versions from `.github/build-config.json`): JDK 17, **Qt 6.8.3** `android_arm64_v8a` + `gcc_64` host (via `aqt`), **Android NDK r26b** (`26.1.10909125`), SDK platform-35, build-tools 35.0.0, ccache. The desktop `gcc_64` Qt doubles as `QT_HOST_PATH`. Signing keystore: `/root/android_release.keystore` (alias `QGCAndroidKeyStore`, pass `qgcandroid`; cert `CN=QGroundControl, O=QGC, C=US`).
- **Configure mirrors CI:** plain `cmake` (not `qt-cmake`) with `-DCMAKE_TOOLCHAIN_FILE=…/android_arm64_v8a/lib/cmake/Qt6/qt.toolchain.cmake`, `-DQT_HOST_PATH=…/gcc_64`, `-DQT_ANDROID_ABIS=arm64-v8a`, `-DQT_ANDROID_SIGN_APK=ON`, `-DQGC_QT_ANDROID_MIN_SDK_VERSION=29`. Then `cmake --build … --parallel` and `--target apk` (androiddeployqt → Gradle). LTO is auto-enabled for Release.
- **Python venv:** the build needs `qgroundcontrol/.venv` with **`jinja2` AND `defusedxml`** (MAVLink/codegen generators import both). Missing `defusedxml` fails the `MAVLinkInstanceFields.h` gen step at ~70% of compile.
- **Two non-obvious traps (already handled by the script):**
  - **GStreamer Android tarball** — v5.0.8 fetches it via `CPMAddPackage` (no SHA validation; URL_HASH commented out), unlike the master base's `file(DOWNLOAD)` + 60 s-timeout + `build-config.json` SHA check. `build_android.sh` pre-stages `gstreamer-1.0-android-universal-1.22.12.tar.xz` (~420 MB compressed) under `/opt/gst-stage/`, extracts it, and passes `-DFETCHCONTENT_SOURCE_DIR_GSTREAMER=/opt/gst-stage/gstreamer-android-1.22.12` so CPM bypasses the download entirely.
  - The aqt `android_arm64_v8a` archives are labelled `MacOS-…-Android-…` on the Qt mirror — that's just Qt's internal naming; they are the correct host-agnostic Android target libs and pair with the Linux host Qt via `QT_HOST_PATH`.
  - **A clean build dir is required when switching toolchains.** A stale `build-android/CMakeCache.txt` from the master base (Qt 6.10.3) pins the old Qt path and makes configure fail to find Qt6 components; run with `CLEAN=1`.
- The manifest (`qgroundcontrol/android/AndroidManifest.xml`) already ships `sensorLandscape`, USB-host/OTG intents, TCP/UDP/INTERNET, Bluetooth, and GPS — no edits needed for the standard build.

### Architecture (big picture)

QGC is a Qt6 / QML + C++20 Ground Control Station for MAVLink UAVs (PX4 & ArduPilot). The C++ backend is exposed to a QML UI. Major subsystems under `qgroundcontrol/src/`:

- **FactSystem** — the parameter abstraction (`Fact`/`FactGroup`/`FactMetaData`). Vehicle parameters and settings are surfaced to QML as "Facts"; this is the spine that most UI binds to.
- **Vehicle** — runtime model of a connected vehicle; multi-vehicle aware (always null-check the active vehicle).
- **FirmwarePlugin** / **AutoPilotPlugins** — firmware-specific behavior and setup UI, abstracting PX4 vs ArduPilot differences behind a common interface.
- **MAVLink** — protocol/link handling.
- **MissionManager** — mission planning and upload/download.
- **QmlControls** — reusable QML components; **Settings** — persistent settings.

The single executable target is defined in the top-level `CMakeLists.txt`: Qt resources, QML module (`URI QGC`), translations (`translations/qgc_*.ts`), and platform-specific includes (`Windows`/`Apple`/`Android`/`Linux`). A `custom/` directory, if present, triggers a custom build (`QGC_CUSTOM_BUILD`) and is added as a subdirectory with overrides — the likely intended use of the `scand80gui` build-config files.

## `/root` housekeeping (not in the repo)

**Tracked** in `scand80gui` alongside the build-config snapshot: `build_android.sh`, `build_android.bat`, `README.md` (the Android APK pipeline, see above), and `android_release.keystore` (the self-contained signing keystore — replace with a real release key for Play Store).

The following live in `/root` but are deliberately **gitignored** from `scand80gui` and must never be committed:
- Secrets: `.git-credentials` (GitHub token), `.gitconfig`, `.claude.json`, `.claude/`, `.bash_history`, `.ssh/`.
- `QGroundControl-installer.exe` and `QGroundControl-installer.exe.*` (~99–103 MB — at/over GitHub's 100 MB limit; use Git LFS if it must be tracked). Note: after the v5.0.8 upgrade the `.exe` is produced by the `build-windows-exe.yml` GitHub Actions workflow (MSVC/NSIS can't run on this Linux box).
- `QGroundControl.apk` (the built **~82 MB** v5.0.8 Android artifact; ship as a release asset, not in git).
- `qgc-build-resume.log` / `build_android.log` / `build_android.run.log` / `build_desktop.log` (transient build logs) and `migration-v5.0.8.diff` (the upgrade delta vs the v5.0.8 tag).
- `qgroundcontrol/` (its own repo).
