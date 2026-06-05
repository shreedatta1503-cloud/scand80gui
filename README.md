# QGroundControl — Native Android APK Build Pipeline

Reproducible pipeline that compiles a **native, signed, Release `arm64-v8a` APK**
from **QGroundControl source** (Qt 6 / C++20 / QML). It mirrors QGC's own CI
recipe (`.github/workflows/android.yml`) so it never drifts from what the
project actually supports.

> This builds the real application from source. It does **not** wrap or convert
> the Windows `.exe` installer — that is impossible and is explicitly avoided.

---

## What you get

| Property            | Value                                                            |
|---------------------|------------------------------------------------------------------|
| ABI                 | `arm64-v8a` (default) — `armeabi-v7a`, `x86_64` selectable        |
| Min Android         | **API 29 = Android 10** (override of QGC's default API 28)        |
| Target / compile    | API 35 (Android 15)                                              |
| Orientation         | `sensorLandscape` (tablet-optimized, landscape) — already in manifest |
| Build type          | `Release` — optimized, debug symbols stripped (`-s`)             |
| Signing             | v1+v2+v3 APK signature via keystore (auto-generated if none)      |
| Package id          | `org.mavlink.qgroundcontrol`                                     |

---

## Toolchain (provisioned automatically, versions pinned by `build-config.json`)

| Component           | Version          | Source                          |
|---------------------|------------------|---------------------------------|
| Qt                  | `6.10.3`         | `aqtinstall` (`android_arm64_v8a` + host `gcc_64`) |
| Android NDK         | `r27c` (`27.2.12479018`) | `sdkmanager`            |
| Android SDK platform| `android-35`     | `sdkmanager`                    |
| Build-tools         | `35.0.0`         | `sdkmanager` (`zipalign`, `apksigner`) |
| cmdline-tools       | `13114758`       | Google zip                      |
| JDK                 | `17`             | apt / Temurin (`keytool`, Gradle) |
| CMake / Ninja       | `>= 3.25` / any  | apt                             |
| ccache + mold       | latest           | apt (compile cache + fast linker) |

All version numbers are read at runtime from
`qgroundcontrol/.github/build-config.json` — the single source of truth. Change
them there, not in the scripts.

---

## Quick start

### Ubuntu 22.04+ / Debian / WSL2

```bash
./build_android.sh
```

That's the whole thing. It will (idempotently):
1. clone QGC `--recursive` if `qgroundcontrol/` is absent (else reuse + sync submodules),
2. install JDK 17, ccache, ninja, mold,
3. install Android cmdline-tools, platform 35, build-tools 35.0.0, NDK r27c,
4. install Qt 6.10.3 (Android target + Linux host) via `aqtinstall`,
5. generate a signing keystore if none exists,
6. configure with CMake + the Qt Android toolchain file,
7. build all native targets, then run the `apk` target (androiddeployqt → Gradle),
8. copy the signed APK next to the script and verify its signature.

**Output:** `./QGroundControl.apk` (override the name with `OUT_APK=...`)

### Windows 11 (native)

Install **Git for Windows**, **Python 3.10+**, and **JDK 17** (set `JAVA_HOME`)
first, then:

```bat
build_android.bat
```

Uses the MSVC host Qt (`win64_msvc2022_64`) for host tools; the target build is
NDK/Clang either way. Run from a *Developer Command Prompt for VS 2022*.

### WSL2

Use the **Linux** script (`build_android.sh`) inside the WSL2 distro — it is the
recommended path on Windows because the toolchain and Gradle behave exactly like
CI. Keep the repo on the Linux filesystem (`~/...`, not `/mnt/c/...`) for sane
I/O. Pull the APK out afterwards with
`cp QGroundControl-*.apk /mnt/c/Users/<you>/Desktop/`.

---

## Environment variables (all optional — sensible defaults)

| Variable | Default | Meaning |
|----------|---------|---------|
| `QGC_SRC` | `./qgroundcontrol` | QGC source tree (cloned if missing) |
| `QGC_REPO` | `https://github.com/mavlink/qgroundcontrol.git` | clone URL |
| `BUILD_DIR` | `$QGC_SRC/build-android` | out-of-source build dir |
| `BUILD_TYPE` | `Release` | `Release` / `Debug` / `RelWithDebInfo` |
| `QGC_ABIS` | `arm64-v8a` | `;`-separated ABI list, e.g. `arm64-v8a;armeabi-v7a` |
| `ANDROID_MIN_SDK` | `29` | minimum Android API (29 = Android 10) |
| `TOOLS_ROOT` | `/opt` (Linux), `C:\qgc-android` (Win) | where SDK/NDK/Qt install |
| `ANDROID_SDK_ROOT` | `$TOOLS_ROOT/android-sdk` | SDK location |
| `QT_BASE` | `$TOOLS_ROOT/Qt` | aqt output dir |
| `JOBS` | `nproc` | parallel compile jobs (uses **all cores**) |
| `OUT_APK` | `QGroundControl.apk` | filename of the APK copied beside the script |
| `SKIP_TOOLCHAIN` | `0` | `1` = reuse installed tools, just (re)build |
| `CLEAN` | `0` | `1` = wipe `BUILD_DIR` before configuring |
| `QT_ANDROID_KEYSTORE_PATH` | `./android_release.keystore` | signing keystore (generated if absent) |
| `QT_ANDROID_KEYSTORE_ALIAS` / `_STORE_PASS` / `_KEY_PASS` | `QGCAndroidKeyStore` / `qgcandroid` / = store pass | signing credentials |

**Production signing:** point the keystore vars at your real release keystore:

```bash
export QT_ANDROID_KEYSTORE_PATH=/secure/path/qgc-release.keystore
export QT_ANDROID_KEYSTORE_ALIAS=QGCAndroidKeyStore
export QT_ANDROID_KEYSTORE_STORE_PASS='********'
export QT_ANDROID_KEYSTORE_KEY_PASS='********'
./build_android.sh
```

---

## Performance / optimization (requirements #3 & #4)

- **All CPU cores:** `JOBS=$(nproc)` → `cmake --build … --parallel "$JOBS"` and
  Gradle runs in parallel. On this reference box that is **128 cores**.
- **Compiler cache:** `ccache` is wired via
  `-DCMAKE_C[XX]_COMPILER_LAUNCHER=ccache` (20 GB cap). Second builds are minutes.
- **Fast linker:** `mold` is installed.
- **Release config:** optimized `-O` flags + `NDEBUG`.
- **Strip symbols:** Qt's Android Release deployment strips the packaged `.so`
  files (`androiddeployqt` runs `llvm-strip`); the APK ships stripped libraries.
- **LTO:** enable per-build with `-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON`
  (off by default — it multiplies link time across every ABI). Add it to the
  configure step if you want it.
- **APK size:** single-ABI (`arm64-v8a`) keeps the APK lean; add other ABIs only
  when needed. For Play Store, build an **AAB** instead:
  `cmake --build "$BUILD_DIR" --target aab`.

---

## Android target details (requirement #5)

Already correct in `qgroundcontrol/android/AndroidManifest.xml` — no edits
required, but here is what makes it tablet/landscape ready:

```xml
<activity
    android:screenOrientation="sensorLandscape"      <!-- landscape, tablet-friendly -->
    android:configChanges="orientation|uiMode|screenLayout|screenSize|smallestScreenSize|
                           layoutDirection|locale|fontScale|keyboard|keyboardHidden|
                           navigation|mcc|mnc|density"   <!-- no relaunch on rotate/resize -->
    ... />
```

`minSdk` is injected by the build (`-DQGC_QT_ANDROID_MIN_SDK_VERSION=29`) → the
generated manifest declares `android:minSdkVersion="29"` (Android 10+).

### Manifest entries for the validation features (requirement #6)

All **already declared** in the shipped manifest — listed so you can verify:

| Feature              | Manifest entry |
|----------------------|----------------|
| USB OTG (serial)     | `<uses-feature android:name="android.hardware.usb.host">` + `USB_DEVICE_ATTACHED`/`DETACHED` intent filters + `device_filter` meta-data |
| TCP/UDP / video      | `INTERNET`, `ACCESS_NETWORK_STATE`, `ACCESS_WIFI_STATE` permissions |
| Telemetry over BT    | `<uses-feature android:name="android.hardware.bluetooth">`, `BLUETOOTH_*` perms |
| GPS / location       | `ACCESS_FINE_LOCATION`, `hardware.location.gps` |
| Mission file I/O     | scoped storage + `FileProvider` (`android.support.FILE_PROVIDER_PATHS`) |

To tighten/loosen, edit `qgroundcontrol/android/AndroidManifest.xml` (the
`QT_ANDROID_PACKAGE_SOURCE_DIR`) and rebuild with `SKIP_TOOLCHAIN=1`.

### Gradle configuration

QGC uses Qt's `androiddeployqt`, which generates the Gradle project under
`$BUILD_DIR/android-build/`. Knobs are passed through CMake target properties
(`cmake/platform/Android.cmake`), not by hand-editing Gradle:

- `QT_ANDROID_MIN_SDK_VERSION`, `QT_ANDROID_TARGET_SDK_VERSION`,
  `QT_ANDROID_COMPILE_SDK_VERSION`
- `QT_ANDROID_VERSION_CODE` / `QT_ANDROID_VERSION_NAME` (Play-Store-monotonic
  code `BBMIPPDDD`)
- `QT_ANDROID_PACKAGE_NAME = org.mavlink.qgroundcontrol`

The build disables the Gradle daemon (`GRADLE_OPTS=-Dorg.gradle.daemon=false`)
for CI parity. To customize raw Gradle, edit the templates in
`qgroundcontrol/android/` (e.g. `build.gradle`) before building.

---

## Validation (requirement #6)

After install (`adb install -r QGroundControl-*.apk`), validate on a device/emulator:

| Capability        | How to check |
|-------------------|--------------|
| App launch        | `adb logcat -s QGroundControl` → reaches `QML ready` |
| **MAVLink**       | Connect via UDP (`:14550`) from a SITL/PX4 instance; vehicle appears |
| **TCP/UDP**       | App Settings → Comm Links → add UDP/TCP link; confirm heartbeat |
| **Telemetry**     | Instrument panel shows live attitude/altitude/battery |
| **Mission plan**  | Plan view → create waypoints → upload/download to vehicle |
| **Video stream**  | Settings → Video → RTSP/UDP H.264; live frame renders |
| **USB OTG**       | Plug a serial radio/Pixhawk via OTG → Android USB-permission dialog → link auto-adds |

The QGC repo's own emulator smoke test
(`.github/actions/android-emulator-test`) boots the same package
(`org.mavlink.qgroundcontrol`) this pipeline builds.

---

## Build error triage (requirement #7)

The script runs `set -Eeuo pipefail` with an `ERR` trap that prints the failing
line. Common cases:

| Symptom | Fix |
|---------|-----|
| `NDK … is too old` | Let the script install the pinned `ndk;27.2.12479018`; don't point at a system NDK. |
| `Could not find qt.toolchain.cmake` | Target Qt not installed — re-run without `SKIP_TOOLCHAIN`; check `$QT_BASE/6.10.3/android_arm64_v8a`. |
| `jinja2` / Python import error | venv missing — script creates `$QGC_SRC/.venv`; ensure `python3-venv` installed. |
| `keytool: command not found` | JDK 17 not on PATH — `apt install openjdk-17-jdk-headless`. |
| Gradle hangs after `BUILD SUCCESSFUL` | daemon — already disabled via `GRADLE_OPTS`. |
| `aqt: command not found` | `pip install --user aqtinstall` (script does this). |
| Submodule errors | `git -C qgroundcontrol submodule update --init --recursive`. |
| Re-run after a fix without re-downloading | `SKIP_TOOLCHAIN=1 ./build_android.sh` (add `CLEAN=1` to wipe the build dir). |

Logs: full output in `build_android.log` (and `build_android.run.log` if run via `nohup`).

---

## APK output path (requirement #8)

```
./QGroundControl.apk                                     # copied beside the script (rename via OUT_APK)
$BUILD_DIR/android-build/build/outputs/apk/release/*.apk # Gradle's original
```

Install: `adb install -r ./QGroundControl.apk`

---

## Files in this pipeline

| File | Purpose |
|------|---------|
| `build_android.sh` | Linux / WSL2 driver (provision → configure → build → sign → verify) |
| `build_android.bat` | Windows 11 native driver (same recipe, MSVC host Qt) |
| `README.md` | this document |
| `android_release.keystore` | generated signing keystore (replace with your own for production) |

---

## Notes

- First build downloads ~3–4 GB (Qt Android, NDK, SDK) and compiles QGC + all
  CPM dependencies for the target ABI — expect a long first run; later runs are
  ccache-fast.
- Everything is pinned to `build-config.json`; bump versions there and re-run to
  track upstream QGC.
