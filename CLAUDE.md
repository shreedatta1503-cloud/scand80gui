# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository layout — read this first

This working directory (`/root`) contains **two distinct things**:

1. **`/root` itself — the `scand80gui` git repo** (remote: `github.com/shreedatta1503-cloud/scand80gui`). It tracks only a snapshot of QGroundControl's CMake build configuration:
   - `CMakeLists.txt` — byte-identical to `qgroundcontrol/CMakeLists.txt` (the QGC top-level build script).
   - `qgc-cmake/` — copies of QGC cmake modules (`Helpers.cmake`, `CustomOptions.cmake`, `Windows.cmake`) and `build-config.json` (the source-of-truth for tool/Qt/dependency versions; see below).
   - These files have **no source tree to build on their own** — they reference `cmake/`, `src/`, `test/`, `resources/`, etc., which only exist inside `qgroundcontrol/`.

2. **`qgroundcontrol/` — the actual application** (a *separate* git repo with its own remote `…/qgroundcontrol.git`, **not** tracked by `scand80gui`; it is gitignored in `/root`). All real development, building, and testing happens here.

**Implication:** to build/run/test anything, `cd qgroundcontrol` first. The `scand80gui` repo is for versioning the build-config customization, not for compiling.

## Build-config source of truth

`qgc-cmake/build-config.json` (mirror of `qgroundcontrol/.../build-config.json`) pins all toolchain versions — Qt `6.10.x`, CMake `3.25+`, NDK `r27c`, GStreamer, etc. The top-level `CMakeLists.txt` reads these; `cmake_minimum_required()` in the CMake file must be kept in sync with `cmake_minimum_version` in the JSON. Change versions here, not scattered through the CMake.

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

`just` is **not** installed here, so drive CMake directly. The build is already configured (`build/`, Release, Qt 6.10.3 at `/opt/Qt/6.10.3/gcc_64`, ccache + mold). Dependencies are fetched via FetchContent/CPM and cached in `qgroundcontrol/.cache/CPM` (~940 MB, all 19 deps present) — `build/_deps/*` therefore has only `-build`/`-subbuild` dirs, no `-src` (sources live in the CPM cache). Incremental rebuild across all cores:

```bash
cd qgroundcontrol
export QT_DIR=/opt/Qt/6.10.3/gcc_64 PATH="$QT_DIR/bin:$PATH"
cmake --build build --config Release --parallel "$(nproc)"   # 128 cores here
```

Binary: `build/Release/QGroundControl` (~56 MB). Main source builds with **warnings-as-errors**.

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

The following live in `/root` but are deliberately **gitignored** from `scand80gui` and must never be committed:
- Secrets: `.git-credentials` (GitHub token), `.gitconfig`, `.claude.json`, `.claude/`, `.bash_history`.
- `QGroundControl-installer.exe` (~169 MB — exceeds GitHub's 100 MB limit; use Git LFS if it must be tracked).
- `qgroundcontrol/` (its own repo).
