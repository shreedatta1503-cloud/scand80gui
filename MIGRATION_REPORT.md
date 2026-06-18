# Migration Report — QGroundControl "Daily" → Stable v5.0.8

**Date:** 2026-06-18
**Branch:** `upgrade/v5.0.8-stable` (qgroundcontrol repo)
**Prepared for:** scand80gui fork

---

## 1. Summary / TL;DR

The application was launching titled **"QGroundControl Daily"** even though the source tree had
*already* been re-based onto the official **v5.0.8 stable** tag with all customizations replayed. The
"Daily" label was **not** a leftover of the Daily branch and **not** a migration defect — it is produced
at build time by the `QGC_STABLE_BUILD` CMake option, which defaults to **OFF**. Our build scripts never
turned it ON.

**Fix:** configure every build path with `-DQGC_STABLE_BUILD=ON` (exactly what upstream CI does for
release builds). No application C++/QML source was modified. Result: the app now identifies as
**"QGroundControl"**.

| | |
|---|---|
| **Original base version** | Daily / upstream **master** snapshot `v5.0.3-1040` (2026-05-28) |
| **New stable version** | Official **v5.0.8** stable tag (2025-10-09, `prerelease=false`) |
| **Current `git describe`** | `v5.0.8-5-g3cd09f3cd` (v5.0.8 tag + 5 fork commits) |
| **App branding before** | `QGroundControl Daily` |
| **App branding after** | `QGroundControl` |
| **About-dialog version string** | `v5.0.8-5-g3cd09f3cd` (kept — honest "v5.0.8 + fork commits") |
| **Parsed numeric version** | `5.0.8` (major.minor.patch) |

---

## 2. Root-cause analysis of the "Daily" label

The branding is a pure build-time switch — there is no "Daily" string baked into a branch:

1. `cmake/CustomOptions.cmake:22` → `option(QGC_STABLE_BUILD "Stable Build" OFF)` — **default OFF**.
2. `CMakeLists.txt:495` (in `target_compile_definitions`) →
   `$<$<NOT:$<BOOL:${QGC_STABLE_BUILD}>>:QGC_DAILY_BUILD>` — when the option is OFF, the compile
   definition **`QGC_DAILY_BUILD`** is defined.
3. `src/QGCApplication.cc:139-142` →
   ```cpp
   #ifdef QGC_DAILY_BUILD
       applicationName = QStringLiteral("%1 Daily").arg(QGC_APP_NAME);
   #else
       applicationName = QGC_APP_NAME;   // "QGroundControl"
   #endif
   ```
   `QGC_DAILY_BUILD` also gives Daily builds a **separate settings space** and enables the in-app
   **new-version check** (`#ifndef QGC_DAILY_BUILD _checkForNewVersion()`, `QGCApplication.cc:207`;
   `AppSettings.cc:208`).

**Proof:** the pre-fix `build-android/CMakeCache.txt` contained `QGC_STABLE_BUILD:BOOL=OFF`, so the
previously shipped APK was branded Daily. Upstream CI (every workflow under `.github/`) flips it ON via
`-DQGC_STABLE_BUILD=${{ github.ref_type == 'tag' || contains(github.ref, 'Stable') ... }}`; our local
`build_android.sh` simply never passed the flag.

The **version string** is independent: `cmake/Git.cmake` sets `QGC_APP_VERSION_STR` from
`git describe --always --tags` → `v5.0.8-5-g3cd09f3cd` (the `-5-g…` suffix appears because HEAD is 5 fork
commits past the v5.0.8 tag), and `QGC_APP_VERSION` from `git describe --abbrev=0` → `v5.0.8`. **Per the
agreed decision, the descriptive suffix is kept** (it is honest: "v5.0.8 plus the fork commits"); only the
word "Daily" was removed.

---

## 3. The upstream migration (already in the tree — context)

The v5.0.8 re-base itself was completed earlier (see the dated entries in `qgroundcontrol/CLAUDE.md`).
Diffstat vs the `v5.0.8` tag: **53 files changed, +5252/-8** (saved as `/root/migration-v5.0.8.diff`).
The 5 fork commits on top of `v5.0.8`:

```
3cd09f3cd docs(CLAUDE.md): record v5.0.8 stable upgrade progress entry
59e4d72ff fix(v5.0.8 build): pin px4-gpsdrivers, complete Vehicle type for QML reg, drop lambda capture
1b8f9f552 docs: add in-repo CLAUDE.md with v5.0.8 migration banner
85603681b build(windows+android): port Windows bootstrap launcher/installer config + Android pipeline config
1fa29e816 feat: port Payload Drop + Nav Lights + SBUS-filter customizations onto v5.0.8
```

Custom features preserved (verified present as symbols in the built `.so` — see §5):
**Payload Drop widget**, **Navigation Lights widget**, **SBUS message-panel noise filter**,
**Windows bootstrap launcher/installer**, **Android build pipeline**.

### Conflicts / API differences resolved during the re-base (for reference)

These were handled by the earlier re-base commits, not by this branding fix, but are listed here for a
complete record:

- `src/FlyView/` → `src/FlightDisplay/` (upstream module rename) — widgets re-homed and re-registered.
- Backported `Vehicle::servoOutputsChanged` + `_servoOutputRawValues` + `SERVO_OUTPUT_RAW` handler
  (master-era infra absent in v5.0.8; Payload-pin AUX10 feedback + Nav-Lights SERVO13 indicator depend on it).
- `_handleRCChannels` RC9 trigger rewritten for v5.0.8's `_rgChannelvalues[]`/`pwmValues[]` decode.
- `QGC::showAppMessage(...)` → `qgcApp()->showAppMessage(...)` (v5.0.8 API).
- `PayloadDropController.h` now `#include`s `Vehicle.h` (complete type needed for QML registration TU).
- `src/GPS/CMakeLists.txt` pins PX4-GPSDrivers to `0b96958` (the commit at the v5.0.8 tag) to avoid a
  later `GPSDriverUBX` signature change that breaks v5.0.8's call site.
- Dropped a redundant lambda capture for Android Clang `-Werror=unused-lambda-capture`.
- `.github/build-config.json` recreated with v5.0.8 pins; `build-windows-exe.yml` rewritten for v5.0.8's
  `cmake/` layout.

---

## 4. Files changed by THIS task (branding fix only)

No application source (C++/QML) was touched — config/docs only.

| File | Repo | Change | Why |
|---|---|---|---|
| `build_android.sh` | scand80gui (`/root`) | Added `QGC_STABLE_BUILD` env var (default `ON`) and `-DQGC_STABLE_BUILD=…` in the configure step | The Android/Linux release pipeline now produces stable-branded APKs by default. Overridable for a Daily build. |
| `build_android.bat` | scand80gui (`/root`) | Same change for the Windows-host Android build | Platform parity so Windows-driven Android builds are also stable. |
| `CLAUDE.md` | scand80gui (`/root`) | Desktop build command now passes `-DQGC_STABLE_BUILD=ON`; added a "Stable vs Daily branding" note | So any manual desktop build is also stable, and the mechanism is documented. |
| `.github/workflows/build-windows-exe.yml` | qgroundcontrol | Forced `-DQGC_STABLE_BUILD=ON` (was a tag/`Stable`-ref expression) | The fork's branch is lowercase `upgrade/v5.0.8-stable`, which the original capital-`Stable`/tag condition does **not** match — the CI `.exe` would otherwise still be Daily. |
| `CLAUDE.md` | qgroundcontrol | Appended a dated migration-log entry documenting this fix | Traceability. |

---

## 5. Verification

| Check | Method | Result |
|---|---|---|
| `QGC_STABLE_BUILD` honored | CMake configure summary (`cmake/PrintSummary.cmake`) | **`Stable Build: YES`** ✅ |
| App name no longer "Daily" | Same summary | **`App Name: QGroundControl`** ✅ |
| Version reports stable | Same summary | **`App Version: v5.0.8-5-g3cd09f3cd`** (numeric `5.0.8`) ✅ |
| `QGC_DAILY_BUILD` not defined | Implied by `Stable Build: YES` → the `$<NOT:...>` generator expression drops the define | ✅ |
| Custom features intact | Symbols in `libQGroundControl_arm64-v8a.so` (`PayloadDropController`, `sendNavigationLights`, `rc9TriggerChanged`) from prior build; recompiled unchanged | ✅ (re-confirmed by rebuild) |
| APK rebuild | `SKIP_TOOLCHAIN=1 ./build_android.sh` → signed Release arm64-v8a APK | **see build status below** |

> **Window title / About dialog at runtime:** the app name set via `setApplicationName("QGroundControl")`
> is what feeds the window title and About dialog. This box is headless (no GPU/WM) and QGC's Qt Quick
> window does not composite to a capturable surface here, so a screenshot is not possible — the CMake
> `Stable Build: YES` + `App Name: QGroundControl` resolution is the authoritative proof that the "Daily"
> code path is compiled out. Confirm visually on a real device with the rebuilt APK.

---

## 6. Remaining compatibility issues / notes

1. **Native desktop (Linux) build does not configure on this box** — a v5.0.8 + Qt 6.8.3 + host
   GStreamer 1.28.2 `qt_add_resources`/`.qsb` quirk, **unrelated to this migration** (a clean v5.0.8
   checkout fails identically here). The branding flag is correct for desktop; it just can't be compiled
   on this specific host. Android (static GStreamer 1.22.12) is unaffected and is the validated path.
2. **Windows `.exe`** is produced by GitHub Actions (`build-windows-exe.yml`) — MSVC/NSIS cannot run on
   this Linux box. The workflow now hardcodes `QGC_STABLE_BUILD=ON`, so the next Actions run yields a
   stable-branded installer.
3. **Upstream desktop CI workflows** (`linux.yml`, `macos.yml`, `ios.yml`, the `android-*.yml`) were left
   at upstream's `ref_type=='tag' || contains(ref,'Stable')` expression. They were untouched to minimize
   divergence; they only matter if you build releases through those specific workflows from this branch.
   If you do, either build from a tag or rename the branch to contain capital `Stable`.
4. **About-dialog version string** retains the `-5-g<hash>` suffix by design (agreed). To show a cleaner
   `vX.Y.Z-…` with no git hash, create an annotated fork release tag on HEAD (e.g.
   `git tag -a v5.0.8-scand80.1`) — `git describe --tags` would then return that tag verbatim.

---

## 7. Git history

History is **intact** — this fix adds no rewrite. The branding changes are config/doc edits on top of the
existing `upgrade/v5.0.8-stable` branch (the qgroundcontrol-repo edits in
`.github/workflows/build-windows-exe.yml` + `CLAUDE.md` are currently unstaged working-tree changes ready
to commit; the `/root` scand80gui edits to `build_android.sh`/`.bat`/`CLAUDE.md` likewise). Nothing was
removed or rewritten in the custom functionality.
