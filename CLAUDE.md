@AGENTS.md

---

# CLAUDE.md — scand80gui fork

This file provides guidance to Claude Code (claude.ai/code) when working in this repository.
The `@AGENTS.md` include above carries the upstream QGroundControl agent guidance (coding style,
architecture patterns, test framework); everything below is specific to **this fork**.

## What this fork is

This repo is **`shreedatta1503-cloud/scand80gui`** — a true GitHub fork of `mavlink/qgroundcontrol`.

- `origin`   → `https://github.com/shreedatta1503-cloud/scand80gui.git`
- `upstream` → `https://github.com/mavlink/qgroundcontrol.git`

**Branches:**
- `master` — kept pristine-equal to upstream `mavlink/qgroundcontrol` master; used only for syncing. Do not commit features here.
- `scand80-dev` — **active development branch.** All fork work lives here.
- `payload-drop` — legacy branch with an earlier, different payload implementation (a 6-servo radial widget driven by `DO_SET_SERVO`). Superseded by the widget on `scand80-dev`; kept for reference.

Git identity in this clone: `shreedatta1503-cloud`. Push/pull is pre-authorized (a classic PAT with `repo`+`workflow` scopes is stored in `~/.git-credentials`; `workflow` scope is required to push `.github/workflows/**`).

## Payload Drop widget (the main feature on `scand80-dev`)

A Fly View widget for a two-stage payload release on an ArduPilot + Cube Orange+ setup. Hidden by
default; revealed by RC Channel 9 activity. Channel mapping uses ArduPilot's convention **AUX OUT n = SERVOn**.

Flow: `RC Ch9 > 1500µs` reveals widget → **Remove Pin** (saffron button) → confirm dialog →
`DO_SET_SERVO(9, 2000)` (AUX OUT 9 pin actuator) → monitor `SERVO_OUTPUT_RAW[10]` (AUX OUT 10);
when it crosses 1500µs show **"Pin Removed"** and enable **DROP** → **DROP** (circular button,
red→sky-blue) → `DO_SET_SERVO(11, 2000)` (AUX OUT 11 release servo) → **"Payload Dropped"** →
state resets and widget hides.

**Files (commit `c482907f0`):**
| File | Role |
|------|------|
| `src/FlyView/PayloadDropWidget.qml` | The widget: RC-Ch9 visibility, Remove Pin + DROP buttons, AUX10 feedback monitor, confirm/info dialogs, auto-hide/reset |
| `src/FlyView/FlyViewTopRightColumnLayout.qml` | Mounts the widget via a `Loader` when a vehicle is connected |
| `src/FlyView/CMakeLists.txt` | Registers `PayloadDropWidget.qml` in `QML_FILES` |
| `src/Vehicle/Vehicle.h` / `Vehicle.cc` | `Q_INVOKABLE sendPayloadPinRelease()` (AUX9) and `sendPayloadDrop()` (AUX11), each a `MAV_CMD_DO_SET_SERVO`, mirroring `sendGripperAction` |

Key reuse: RC channel and servo-output feedback use the **existing** `Vehicle::rcChannelsRawChanged`
and `Vehicle::servoOutputsChanged` signals (Ch9 = index 8, SERVO10 = index 9). **No new MAVLink
handlers were added.**

**⚠️ Design caveat:** AUX OUT 10 feedback is read from `SERVO_OUTPUT_RAW`, which reports the *commanded*
output PWM, not a true external limit-switch input. The widget's `pinFeedbackServo`/`pinFeedbackThresholdUs`
(and `rcTriggerChannel`/`rcTriggerThresholdUs`) properties make the source/threshold easy to change if a
real switch is later wired to an RC input instead.

## Message-panel noise filter (suppress ArduPilot SBUS debug spam)

ArduPilot emits a `MAV_SEVERITY_DEBUG` STATUSTEXT — `RCInput: decoding SBUS(n)` — every time it
(re)locks onto the SBUS RC protocol. It only confirms which RC protocol was decoded, carries no
actionable info, yet repeats constantly and floods the in-app **Messages** panel (shown there with
the `Debug:` severity label). It is unrelated to whether RC Ch9 / the Payload Drop widget works.

**Fix (commit `737e84ac6`, branch `scand80-dev`):** drop it in `Vehicle::_textMessageReceived`
(`src/Vehicle/Vehicle.cc`), alongside QGC's existing filters for PX4 event/preflight noise, so it
never reaches the panel or the speech engine:

```cpp
if ((severity == MAV_SEVERITY::MAV_SEVERITY_DEBUG) && text.startsWith(QStringLiteral("RCInput: decoding SBUS"))) {
    qCDebug(VehicleLog) << "Dropping noisy ArduPilot SBUS decode message:" << text;
    return;
}
```

Deliberately narrow — guarded on **DEBUG severity + exact text prefix** — so no genuinely useful
RC/status message is ever hidden. Related explanatory note lives in `Vehicle::_handleRCChannels()`.

## Building & running (Linux dev box — headless)

The dev environment is a **headless Linux server** (no GPU).

- **Build:** already-configured CMake build at `build/` (Qt 6.10.3 at `/opt/Qt/6.10.3/gcc_64`, Release,
  ccache + mold). Incremental: `cmake --build build --config Release --parallel`. Binary at
  `build/Release/QGroundControl`. Main source builds with **warnings-as-errors**.
- **Running:** Xvfb is on `DISPLAY=:99`, but there is no GPU/GLX/WM, so the Qt Quick UI does **not**
  render to a capturable surface — screenshots come out blank. Visual UI verification is not possible
  here without real GL + a display. QGC also **refuses to run as root** and enforces a **single instance**;
  launch as the `qgcuser` account:
  `runuser -u qgcuser -- env DISPLAY=:99 HOME=/home/qgcuser ./build/Release/QGroundControl`
- To exercise the widget end-to-end you need **ArduPilot SITL** (for a vehicle / RC stream on Ch9) plus
  a viewable display. The widget stays hidden without RC Ch9 input.

## Windows builds (for the .exe with these changes)

Three options, in order of convenience:

1. **GitHub Actions (downloadable installer):** the workflow `.github/workflows/build-windows-exe.yml`
   (commit `7ff43db64`) is a fork-friendly manual build — no detect-changes gating, attestation, or AWS.
   Run it from the Actions tab (Run workflow → branch `scand80-dev`); download the
   `QGroundControl-Windows-Release` artifact. GStreamer/video is off by default for reliability.
2. **Local Qt Creator build:** open `CMakeLists.txt` with the **Desktop Qt 6.10.3 MSVC2022 64bit** kit
   (requires Visual Studio 2022 with the "Desktop development with C++" workload — the MSVC compiler).
3. **Command line:** from the "x64 Native Tools Command Prompt for VS 2022":
   `C:\Qt\6.10.3\msvc2022_64\bin\qt-cmake.bat -B build -G Ninja -DCMAKE_BUILD_TYPE=Release` then
   `cmake --build build`.

Toolchain pins (from `build-config.json`): Qt **6.10.3** (min 6.10.0), CMake **3.25+**, MSVC 2022 on Windows.

### ⏸️ In progress — resume the installer download (paused 2026-06-01)

The SBUS-noise fix (commit `737e84ac6`) is pushed to `scand80-dev` and the Windows build **succeeded**.
The installer artifact is built and sitting on GitHub; only the local download + `.exe` swap remains.
The download was started, then terminated by request at ~24 MB / 242.8 MB.

**To finish (do this 2026-06-02):**
- **Run:** `26757993797` (conclusion: success) —
  https://github.com/shreedatta1503-cloud/scand80gui/actions/runs/26757993797
- **Artifact:** `QGroundControl-Windows-Release`, id `7333556224`, 242.8 MB.
  ⚠️ **14-day retention — expires ~2026-06-15.** After that, re-dispatch `build-windows-exe.yml`
  on `scand80-dev` to regenerate it.
- **Download (token lives in `~/.git-credentials`):**
  ```bash
  TOKEN=$(sed -n 's#https://\([^:]*\):\([^@]*\)@github.com#\2#p' ~/.git-credentials | head -1)
  curl -sL -H "Authorization: Bearer $TOKEN" \
    "https://api.github.com/repos/shreedatta1503-cloud/scand80gui/actions/artifacts/7333556224/zip" \
    -o /tmp/qgc-artifact.zip
  ```
  (Throughput was ~3 MB/min last time, so allow ~80 min — or download from the run URL by hand.)
- **Then:** `unzip -l /tmp/qgc-artifact.zip` to find the NSIS installer `.exe` (name matches
  `installer|QGroundControl-`; raw `QGroundControl.exe`/`QGroundControlApp.exe` are fallbacks), then
  replace **`/root/QGroundControl-installer.exe`** (currently still the stale 2026-05-27, 169 MB file).
- **Not yet committed:** this `CLAUDE.md` documentation update (the code fix itself is already pushed).

## Related repo

`shreedatta1503-cloud/scand80gui-buildconfig` is a separate, small repo holding an extracted copy of
QGC's CMake build configuration. It is **not** this fork and is not needed to build QGC.
