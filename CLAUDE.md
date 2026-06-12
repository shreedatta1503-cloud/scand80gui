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
- `scand80-dev` — long-running development branch; the Windows installers up to `1d825a136` (2026-06-05) were built from here.
- `feature/payload-confirm-applock-eventlog` — `scand80-dev` + one commit (`7594aec0b`, 2026-06-08) adding payload-drop confirmation, an app password-lock, and async event logging. **Only ever built into an APK, never an installer.**
- `feature/remove-applock-eventlog` — **current state of the shipped artifacts.** Branched off the above; removed the app-lock + event-log features (kept payload-drop confirmation + nav lights). Both `/root/QGroundControl.apk` and `/root/QGroundControl-installer.exe` are built from its HEAD (`dbb32ca19`, 2026-06-12). See the dated entry under "Windows builds" below.
- `payload-drop` — legacy branch with an earlier, different payload implementation (a 6-servo radial widget driven by `DO_SET_SERVO`). Superseded by the widget on `scand80-dev`; kept for reference.

Git identity in this clone: `shreedatta1503-cloud`. Push/pull is pre-authorized (a classic PAT with `repo`+`workflow` scopes is stored in `~/.git-credentials`; `workflow` scope is required to push `.github/workflows/**`).

## Payload Drop widget (the main feature on `scand80-dev`)

A Fly View widget for a two-stage payload release on an ArduPilot + Cube Orange+ setup. Hidden by
default; revealed by RC Channel 9 activity. Channel mapping uses ArduPilot's convention **AUX OUT n = SERVOn**.

Flow: **RC Ch9 high (`2000µs`) shows** the widget / **low (`1000µs`) hides** it (non-destructive — see
below) → **Remove Pin** (saffron button) → immediately fires `DO_SET_SERVO(9, 2000)` (AUX OUT 9 pin
actuator) → monitor `SERVO_OUTPUT_RAW[10]` (AUX OUT 10); when it crosses 1500µs the **Remove Pin button
turns neon green (`#39FF14`)** with its 1px red border and **DROP** is enabled → **DROP** (circular
button, red→sky-blue) → `DO_SET_SERVO(11, 2000)` (AUX OUT 11 release servo) → **completes silently** (no
dialog; the skyblue DROP state persists, no auto-reset).

> **AUX 10 acknowledgement is a button colour change, not a dialog** (commit `503f5cf66`). The old
> **"Pin Removed"** `MessageDialog` was removed; acknowledgement feedback is now purely the neon-green
> recolour of the Remove Pin button (held until `resetState()`). No modal/toast/popup is raised on ack.
> The payload-release commands, DROP flow and "Payload Dropped" reset are unchanged.

> **Remove Pin has NO confirmation dialog** (commit `7cb990b88`). The old `confirmRemovePinDialog`
> Yes/No `MessageDialog` ("Are you sure?") was removed; the button's `onClicked` now calls
> `sendPayloadPinRelease()` (AUX OUT 9) directly, so the pin fires immediately on a single click.
> `sendPayloadPinRelease()` enqueues a non-blocking MAVLink command and returns at once — the GUI
> thread is never blocked. The post-DROP **"Payload Dropped"** notice was the only remaining dialog —
> now also removed; see the next note.

> **RC9 2000/1000 visibility, silent drop, threaded C++ backend** (commit `e1f68a441`). The widget was
> refactored from a self-contained QML widget (state + RC monitoring inline on the GUI thread) into a
> **thin presentation layer over a new C++ backend**, `PayloadDropController` (+ `PayloadDropWorker`).
> Three behaviour changes plus a threading change:
> - **Visibility is now strictly RC Channel 9, two-position:** high (`2000µs`, i.e. `≥ 1500µs`) shows the
>   widget, low (`1000µs`) hides it. Hidden via `visible:false` (removed from view *and* layout, not just
>   disabled). Replaces the old "reveal on *any* Ch9 change" logic.
> - **Hiding is non-destructive / state-preserving:** the authoritative workflow state (rc9 reading,
>   pin-release-requested, pin-removed, drop-completed) lives in `PayloadDropWorker`, *decoupled from the
>   QML widget's visibility*. Toggling `visible` never resets it, so RC9 1000→2000 resumes exactly where
>   it left off; repeated hide/show cycles never lose progress.
> - **DROP completes silently:** the post-DROP `payloadDroppedDialog` `MessageDialog` (and the
>   `QtQuick.Dialogs` import) were removed. Release command (AUX OUT 11), logging and telemetry are
>   preserved; there is now **no dialog/toast/modal anywhere** in the widget.
> - **Threading model:** `PayloadDropController` (GUI thread) owns a dedicated worker `QThread` running
>   `PayloadDropWorker`. `Vehicle::rc9TriggerChanged` / `servoOutputsChanged` are delivered to the worker
>   via **queued** cross-thread connections (RC9 + servo evaluation runs *off* the UI thread); the worker
>   pushes results back to the controller via **queued** signals, and only the controller's GUI-thread
>   slots mutate the QML-visible `Q_PROPERTY`s. State is **lock-free by thread confinement** (the worker
>   is the sole owner/mutator of its state — no mutexes, hence no deadlocks/priority inversion);
>   determinism comes from the worker's single event loop serialising all events. Drop-completion
>   telemetry is formatted off-thread via `QtConcurrent::run` (global `QThreadPool`). Per-event work is a
>   couple of int comparisons — intentionally *not* sharded across a pool. (This supersedes the earlier
>   "no new threads were added" stance, which was the right call for the prior pure-QML widget; the
>   worker thread here exists to satisfy an explicit off-UI-thread requirement and adds only a sub-ms
>   queued hop.) **No new MAVLink handlers**; reuses the existing Vehicle signals + send invokables.

**Files (Payload Drop core `c482907f0`; AUX10-ack + Nav Lights `503f5cf66`; Remove-Pin no-confirm `7cb990b88`; RC9-visibility + silent-drop + threaded backend `e1f68a441`):**
| File | Role |
|------|------|
| `src/FlyView/PayloadDropWidget.qml` | **Thin view** (`e1f68a441`): binds `visible`/colours/enabled to the controller, buttons call `controller.requestPinRelease()`/`requestDrop()`. No `Connections`/state/dialogs of its own. Instantiates one `PayloadDropController` bound to `activeVehicle` |
| `src/QmlControls/PayloadDropController.{h,cc}` | **New backend** (`e1f68a441`): `PayloadDropController` (GUI-thread `QML_ELEMENT` facade + cached `Q_PROPERTY`s) and `PayloadDropWorker` (worker-`QThread` object owning the authoritative RC9/pin/drop state + evaluation). Registered in `QGroundControl.Controls` via QmlControls `target_sources` (mirrors `ServoOutputMonitorController`) |
| `src/QmlControls/CMakeLists.txt` | Registers `PayloadDropController.cc/.h` in the QmlControls `target_sources` |
| `src/FlyView/NavigationLightsWidget.qml` | Always-visible Nav Lights status/toggle widget (AUX OUT 13) — see section below. **Untouched by `e1f68a441`** |
| `src/FlyView/FlyViewTopRightColumnLayout.qml` | Mounts NavigationLightsWidget (always) directly above the PayloadDrop `Loader` (vehicle-gated) |
| `src/FlyView/CMakeLists.txt` | Registers `PayloadDropWidget.qml` and `NavigationLightsWidget.qml` in `QML_FILES` |
| `src/Vehicle/Vehicle.h` / `Vehicle.cc` | `Q_INVOKABLE sendPayloadPinRelease()` (AUX9), `sendPayloadDrop()` (AUX11), `sendNavigationLights(int pwmUs)` (AUX13) — each a `MAV_CMD_DO_SET_SERVO`, mirroring `sendGripperAction`. (Nav Lights takes a literal PWM since `cb031df7f`. Since `e31045686` it pins `SERVO13_FUNCTION=0`/`REVERSED=0`; since `96f424173` `_ensureNavigationLightsChannelLatches(int)` returns `bool` and `sendNavigationLights()` commands immediately only when already-latchable, else defers the `DO_SET_SERVO` (new helper `_commandNavigationLightsServo(int,int)`) until the reboot-required `SERVO13_FUNCTION=0` write is confirmed via `Fact::vehicleUpdated` — see Nav Lights section.) |

Key reuse: RC channel and servo-output feedback use the **existing** `Vehicle::rc9TriggerChanged`
and `Vehicle::servoOutputsChanged` signals (Ch9 = index 8, SERVO10 = index 9, SERVO13 = index 12).
**No new MAVLink handlers were added.**

**⚠️ Design caveat:** AUX OUT 10 feedback is read from `SERVO_OUTPUT_RAW`, which reports the *commanded*
output PWM, not a true external limit-switch input. The backend's `_pinFeedbackServoIndex` /
`_pinFeedbackThresholdUs` / `_rc9ShowThresholdUs` constants (in `PayloadDropWorker`, since `e1f68a441`;
the widget keeps `rcTriggerChannel`/`rcTriggerThresholdUs` for the on-screen readout) make the
source/threshold easy to change if a real switch is later wired to an RC input instead.

## Navigation Lights widget (commit `503f5cf66`, branch `scand80-dev`)

A second Fly View widget (`src/FlyView/NavigationLightsWidget.qml`) mounted **directly above** the
Payload Drop widget in `FlyViewTopRightColumnLayout.qml`. Unlike Payload Drop it is **always visible**
(instantiated directly, *not* via a vehicle-gated `Loader`). It is both a **status indicator** and a
**toggle** for the navigation-lights output on **AUX OUT 13 (SERVO13)**.

> **⚠️ Active-low polarity (commit `cb031df7f`, branch `scand80-dev`).** AUX 13 is **active-low**: the
> channel's idle/disarm/failsafe rail is the **HIGH** rail (`SERVO13_TRIM ≈ 2200µs`, == `SERVO13_MAX`) =
> light **OFF**, and the lamp is energised by pulling the channel **LOW**. The original code assumed the
> opposite (active-high) and commanded OFF on the *low* rail — which (a) inverted the lamp (GUI-ON drove
> high → light off) and (b) made OFF **blink**, because commanded-OFF (`1000µs`) sat on the *opposite*
> rail from `TRIM` (`2200µs`), so every reassertion of the channel default toggled the output. The fix
> flips the feature to active-low **and** commands OFF on the *same (high) rail as `TRIM`**, so a
> disarm/failsafe/competing-function reassertion of the default can no longer make it toggle. This
> requires the vehicle side to agree: `SERVO13_FUNCTION` = Disabled/GPIO (so `DO_SET_SERVO` holds and
> nothing else drives the channel), `SERVO13_TRIM=2200`, `MIN=850`, `MAX=2200`, `REVERSED=0`.

> **⚠️ ON-state blink fix — enforce the latch in code (commit `e31045686`, branch `scand80-dev`).**
> `cb031df7f` (above) stopped the *OFF*-state blink but **GUI-ON still blinked continuously**. Root
> cause: `MAV_CMD_DO_SET_SERVO` is a **one-shot, non-latching** override that only holds on a channel
> the autopilot is *not otherwise driving*. If `SERVO13_FUNCTION ≠ 0`, ArduPilot keeps reasserting the
> channel's idle/`TRIM` (HIGH) rail every output cycle while the ON command pulls the opposite (LOW)
> rail — the two **contend → the lamp blinks**. (OFF never blinked only because commanded-OFF `2000µs`
> sits on the *same* HIGH/idle rail the autopilot reasserts, so there was nothing to contend with.) The
> `SERVO13_FUNCTION = Disabled` requirement was *documented but never enforced* — that unenforced
> assumption was the untouched blink source. **The blink is not generated by any QGC code**: the widget
> sends exactly one command per click (its only `Timer` is `repeat:false` and never re-sends),
> `sendMavCommand` retransmits at most `_mavCommandMaxRetryCount=3` then stops, and there is no
> `DO_REPEAT_SERVO`/strobe/beacon path involved — the oscillation is purely vehicle-side contention.
>
> **Fix:** `Vehicle::sendNavigationLights()` now calls a new private helper
> `_ensureNavigationLightsChannelLatches(13)` **before** issuing the command. It idempotently pins the
> channel to a latchable config so the one-shot override actually holds: writes `SERVO13_FUNCTION=0`
> (Disabled) and `SERVO13_REVERSED=0` **only when they differ**, `qCWarning`-logs every override of a
> non-default value (audit trail), and *verify-only*-warns (never overwrites) if `SERVO13_TRIM` sits on
> the ON/LOW rail (`< 1500`, which would make a disarm energise the lamp). It is a **no-op until
> `parametersReady()`** (warns that ON may blink until the channel is Disabled). Channel number is a
> single C++ constant `kNavLightsChannel=13`; only AUX13 is touched — AUX9/AUX11 payload senders and any
> beacon/strobe functions are untouched. Once `SERVO13_FUNCTION=0` is written it persists on the
> vehicle.
>
> ⚠️ **This `e31045686` write was necessary but not sufficient** — its "only the first GUI-ON can
> briefly flicker" assumption was wrong on two counts, corrected by `96f424173` below.

> **⚠️ ON-state blink fix, part 2 — defer the command until the latch is confirmed (commit
> `96f424173`, branch `scand80-dev`).** `e31045686` wrote `SERVO13_FUNCTION=0` but **GUI-ON still
> blinked continuously** on any vehicle whose SERVO13 was assigned a function. Two defects in that
> write, both verified against ArduPilot's own parameter metadata
> (`.cache/CPM/ardupilotparams/.../Copter-4.8/apm.pdef.json`):
> - **`SERVO13_FUNCTION` is `RebootRequired:True`.** Its doc string: *"Setting this to Disabled(0) will
>   setup this output for control by … MAVLink servo set commands; any other value will enable the
>   corresponding function."* So writing `0` at runtime does **not** free the channel **until the next
>   FC reboot** — the autopilot keeps re-driving SERVO13 and contends with the one-shot `DO_SET_SERVO`
>   for the **whole session**, not the "brief flicker" `e31045686` claimed.
> - **The write is asynchronous.** `Fact::setRawValue(0)` only *queues* a `PARAM_SET` (emits
>   `containerRawValueChanged` → `ParameterManager::_mavlinkParamSet`); the old code then fired
>   `DO_SET_SERVO` **synchronously in the same call**, so the command reached the vehicle before the
>   write was even on the wire.
>
> **Fix:** `_ensureNavigationLightsChannelLatches()` now **returns `bool`** — whether the channel is
> latchable *right now*. `sendNavigationLights()` commands **immediately** (via the new
> `_commandNavigationLightsServo()`) when `SERVO13_FUNCTION` is **already 0** (the steady,
> correctly-provisioned case → no blink). When it has to write `0` over an assigned function it
> **persists the value, `qCWarning`-logs that it is the blink source and is reboot-required, and
> DEFERS** the `DO_SET_SERVO` until the vehicle confirms the write (`Fact::vehicleUpdated`,
> `Qt::SingleShotConnection`) — never firing into a channel the autopilot still owns. `REVERSED`/`TRIM`
> handling is unchanged; only AUX13 is touched.
>
> **Operational note no code can override:** because `SERVO13_FUNCTION` is reboot-required, a vehicle
> currently using SERVO13 for another function needs a **one-time provisioning** — set
> `SERVO13_FUNCTION=0` then **reboot** the FC. After that the one-shot command latches and GUI-ON is
> rock-steady on every toggle. The fix removes the race and the false "it's steady" promise; it cannot
> make a reboot-required parameter take effect without the reboot.

- **States (driven only by observed PWM; active-low):** OFF (`PWM 2000`, i.e. `> onThresholdUs 1500`,
  the high/idle rail) = translucent grey @ 0.45 opacity; ON (`PWM 1000`, i.e. `≤ onThresholdUs 1500`,
  the low rail) = dark green `#006400` with a thin saffron `#FF9933` border. Unknown (`-1`, no SERVO13
  yet) reads as OFF (the `_navPwm >= 0` guard keeps `-1` from satisfying the `≤` test).
- **Monitoring:** a `Connections { onServoOutputsChanged }` handler reads `servoValues[12]` (SERVO13).
  Reuses the **existing** `Vehicle::servoOutputsChanged` signal — event-driven, no polling, no new
  MAVLink handler.
- **Toggle:** click sends `Vehicle::sendNavigationLights(int pwmUs)` → `MAV_CMD_DO_SET_SERVO(13, 1000|2000)`
  (ON=1000 low / OFF=2000 high), with `showError=false` so failures are logged (never popped up). The
  widget passes the literal target PWM (its `onPwmUs`/`offPwmUs` are the **single source of truth**; the
  C++ side just range-clamps to `[800,2200]` — no more duplicated `kOnPwmUs`/`kOffPwmUs` constants). The
  button repaints **only** when the commanded PWM is observed back, never optimistically on click.
  Before commanding, `sendNavigationLights()` calls `_ensureNavigationLightsChannelLatches(13)` to pin
  `SERVO13_FUNCTION=0`/`REVERSED=0` so the one-shot `DO_SET_SERVO` *latches* steady. Since `96f424173`
  that helper returns whether the channel is latchable *now*: if so it commands at once, otherwise it
  defers the command (via `Fact::vehicleUpdated`) until the reboot-required `SERVO13_FUNCTION=0` write
  is confirmed — the two-part ON-state blink fix above.
- **Click protection:** a `_commandPending` flag disables the button while a toggle is in flight; it is
  cleared on PWM confirmation **or** by a 3 s fallback `Timer`. No dialogs/toasts anywhere in the widget.
- **Threading:** all Nav Lights state lives on the GUI thread and updates via Qt's queued signal/slot
  from the existing comms threads — **this widget adds no new threads, mutexes, or thread pools** (its
  event-driven design needs none). Note: Payload Drop *did* gain a worker thread in `e1f68a441`, but
  Payload Drop (AUX 10) and Nav Lights (AUX 13) own separate state in separate objects, so there is
  still **no shared cross-thread state to guard** between the two widgets.

Same `SERVO_OUTPUT_RAW`-is-*commanded*-PWM caveat as Payload Drop applies; `navServo`/`onThresholdUs`/
`onPwmUs`/`offPwmUs`/`commandTimeoutMs` are exposed properties for easy retargeting.

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

### ✅ Done — app-lock + event-log REMOVED; APK and installer rebuilt & swapped (2026-06-12)

The application password-lock and the asynchronous event-logging subsystem (both added 2026-06-08 in
commit `7594aec0b` on `feature/payload-confirm-applock-eventlog`, alongside the payload-drop
confirmation dialog) were **removed in full** on branch `feature/remove-applock-eventlog`. The
payload-drop confirmation dialog, the Payload Drop widget/worker-thread model, and the Navigation
Lights widget are **kept**.

**Why this entry matters:** the app-lock/event-log code only ever shipped in an **APK** (the
2026-06-04 / 2026-06-12 builds). No installer ever contained it — the prior `.exe` was built from
`scand80-dev @ 1d825a136`, which predates `7594aec0b`. So "remove it from the .exe" was already true;
this session rebuilt the installer from the cleaned source so the `.exe` and `.apk` carry an **identical
feature set** (payload-drop confirmation + nav lights, no app-lock/event-log).

**What was removed (commit `dbb32ca19`):** 10 files deleted — `src/Security/{AppLockManager.{cc,h},
CMakeLists.txt}`, `src/EventLog/{EventLogger.{cc,h},CMakeLists.txt}`, `src/Settings/{SecuritySettings.
{cc,h},Security.SettingsGroup.json}`, `src/AppSettings/SecuritySettingsPage.qml`; de-integrated from
`src/CMakeLists.txt`, `Settings`/`AppSettings` CMakeLists, `SettingsPages.json`, `QGCApplication.cc`,
`QGroundControlQmlGlobal.{cc,h}`, `SettingsManager.{cc,h}`, `MainWindow.qml` (login overlay + unlock
gate), `PayloadDropController.cc`, `PayloadDropWidget.qml`, `NavigationLightsWidget.qml`. Net −958 lines.

**Android `-Werror` fix carried on the same branch (commit `5321c29ac`):** the Android NDK r27c **Clang**
rejects `-Wunused-lambda-capture` on the deferred `Fact::vehicleUpdated` lambda in
`Vehicle::sendNavigationLights()` — `channel` is a `const int` constant usable inside the nested lambda
without capture. Desktop **GCC** has no such warning (hence the local `-Werror` build always passed),
so this only surfaced when building for Android. Dropping the capture is behaviour-preserving on both
compilers; **required for any Android build of this branch.**

**Artifacts (both from `dbb32ca19`):**
- **APK** — `/root/QGroundControl.apk`, signed Release arm64-v8a, minSdk 29, sha `b28a8856…`. Verified:
  `AppLockManager`/`EventLogger`/`SecuritySettings` = 0 symbols in `libQGroundControl_arm64-v8a.so`;
  `PayloadDropController`/`sendPayloadDrop`/`sendNavigationLights` present; `apksigner` v3 → *Verifies*.
- **Installer** — `/root/QGroundControl-installer.exe`, sha `4342e131…`, NSIS `7z t` → *Everything is Ok*.
  Built via `build-windows-exe.yml` run [`27416577329`](https://github.com/shreedatta1503-cloud/scand80gui/actions/runs/27416577329)
  (success, ~38 min); artifact `QGroundControl-Windows-Release` (id `7592598060`) fetched with the 32-way
  parallel ranged download; picked `QGroundControl-installer-AMD64.exe`. Bundled `QGroundControlApp.exe`
  symbol check: app-lock/event-log = 0, `PayloadDropController`/`sendPayloadDrop` present (so the `.exe`
  now also has the payload-drop confirmation, matching the APK). Previous installer backed up to
  **`/root/QGroundControl-installer.exe.prev-20260612-de822460`**.

**Runtime verification (the meaningful "does it run" test):** the `.apk` (Android arm64) and `.exe`
(Windows PE) **cannot be executed on this headless x86-64 Linux box** (no device/emulator/qemu, no
Wine). Instead the **same source** was built as the native Linux Release binary and launched headless as
`qgcuser` on `:99` → **healthy: ~86 threads, ~342 MB RSS, reached `QML ready`, zero QML/runtime errors**
(specifically no dangling references to the removed `appLockManager`/`eventLogger`/`securitySettings`
QML properties — the exact failure mode a bad removal would cause). The usual GStreamer/speechd/pipewire
log lines are non-fatal in this env. **Not yet PR'd/merged into `scand80-dev`.**

### ✅ Done — installer rebuilt & swapped for Nav Lights ON-blink part-2 fix (2026-06-05, later)

The **part-2** Nav Lights ON-blink fix (commit `96f424173` — defer `DO_SET_SERVO` until the
reboot-required `SERVO13_FUNCTION=0` write is confirmed; command immediately only when already
latchable; see the Nav Lights section) plus its doc correction (`1d825a136`) are pushed to
`scand80-dev`, the Windows build **succeeded**, and the installer carrying it is now the local `.exe`.

**What was done (2026-06-05, later):**
- Local Linux Release build (`-Werror`) passed clean; `sendNavigationLights(int)`,
  `_ensureNavigationLightsChannelLatches(int)` (now `bool`) and the new `_commandNavigationLightsServo(int,int)`
  symbols are all in `build/Release/QGroundControl`.
- Dispatched `build-windows-exe.yml` on `scand80-dev`. Run
  [`27011616483`](https://github.com/shreedatta1503-cloud/scand80gui/actions/runs/27011616483)
  finished **success** (~38 min), built from `head_sha 1d825a136`; MSVC compile + NSIS clean.
- Downloaded artifact `QGroundControl-Windows-Release` (id `7435832683`, 242,813,439 bytes) via the
  32-connection parallel ranged download; assembled size matched and `unzip -t` passed. Picked the NSIS
  installer **`QGroundControl-installer-AMD64.exe`** (103,779,939 bytes, sha256 `de822460…54dbb7`);
  `7z t` → *Everything is Ok*.
- **Provenance is the authoritative proof** the fix shipped: CI compiled the exact commit `1d825a136`
  (which sits on `96f424173`), and the installer genuinely differs from the prior one (`714e26e4…` →
  `de822460…`, +3,140 bytes). The plaintext-symbol grep is *not* authoritative for the new helper:
  `_commandNavigationLightsServo` is a plain `private:` method (not a moc slot) and tiny, so MSVC
  inlined it into its two call sites and dropped the out-of-line symbol (0 grep hits in the Windows
  app binary, but **1 hit in the local GCC binary** which keeps it). `sendNavigationLights` (×4) and
  `_ensureNavigationLightsChannelLatches` (×1) are present in the Windows binary.
- Backed up the previous (`e31045686`) installer to
  **`/root/QGroundControl-installer.exe.prev-20260605-1d825a13-was-e3104568`** (sha `714e26e4…`) and
  swapped the new one into **`/root/QGroundControl-installer.exe`** (sha `de822460…`, `7z t` Ok).

> Same caveat as always: the Windows GUI **cannot be executed here** (headless Linux, no Wine/QEMU/PE
> binfmt), so validation is build-success + NSIS structure + provenance. End-to-end behaviour
> (toggle → steady ON, no blink) needs a real Windows machine against the ArduPilot vehicle/SITL — and,
> for a vehicle whose SERVO13 was assigned another function, the one-time `SERVO13_FUNCTION=0` + reboot.

### ✅ Done — installer rebuilt & swapped for Nav Lights ON-blink latch fix (2026-06-05)

The Nav Lights ON-state blink fix (commit `e31045686` — `_ensureNavigationLightsChannelLatches`, see
the Nav Lights section) is pushed to `scand80-dev`, the Windows build **succeeded**, and the installer
carrying it is now the local `.exe`.

**What was done (2026-06-05):**
- Local Linux Release build (`-Werror`) passed clean; both `sendNavigationLights(int)` and the new
  `_ensureNavigationLightsChannelLatches(int)` symbols are in `build/Release/QGroundControl`.
- Dispatched `build-windows-exe.yml` on `scand80-dev`. Run
  [`27003061115`](https://github.com/shreedatta1503-cloud/scand80gui/actions/runs/27003061115)
  finished **success**, built from `head_sha e31045686`; MSVC compile + NSIS clean.
- Downloaded artifact `QGroundControl-Windows-Release` (id `7432275377`, 242,803,376 bytes); size
  matched and `unzip -t` passed. Picked the NSIS installer **`QGroundControl-installer-AMD64.exe`**
  (103,776,799 bytes, sha256 `714e26e4…13254e`); `7z t` → *Everything is Ok*. Confirmed the fix shipped
  by finding `_ensureNavigationLightsChannelLatches` (+ `sendNavigationLights`) embedded in the bundled
  `QGroundControlApp.exe` (moc/C++ symbols survive `qmlcachegen`, unlike QML strings).
- Backed up the previous `.exe` to **`/root/QGroundControl-installer.exe.prev-20260605-e3104568`**
  (sha `e7a3f7d9…`) and swapped the new one into **`/root/QGroundControl-installer.exe`**.

> Same caveat as always: the Windows GUI **cannot be executed here** (headless Linux, no Wine/QEMU/PE
> binfmt), so validation is build-success + NSIS structure + embedded symbols. End-to-end behaviour
> (toggle → steady ON, no blink) needs a real Windows machine against the ArduPilot vehicle/SITL.

### ✅ Done — installer rebuilt & swapped for Remove-Pin no-confirm (2026-06-03)

Removing the Remove Pin **"Are you sure?"** confirmation dialog (commit `7cb990b88`) is pushed to
`scand80-dev`, the Windows build **succeeded** (all steps green incl. NSIS), and the installer carrying
it is now the local `.exe`.

**What was done (2026-06-03, latest):**
- Dispatched `build-windows-exe.yml` on `scand80-dev`. Run
  [`26865447580`](https://github.com/shreedatta1503-cloud/scand80gui/actions/runs/26865447580)
  finished **success** (~36 min), built from `head_sha 7cb990b88`; MSVC compile + `qmlcachegen` clean.
- Downloaded artifact `QGroundControl-Windows-Release` (id `7376860664`, 242,780,234 bytes) via the
  **32-connection parallel ranged download**; assembled size matched, `unzip -t` passed.
- Picked the NSIS installer **`QGroundControl-installer-AMD64.exe`** (103,764,760 bytes, sha256
  `dbea73c6…6c6458`). `7z t` → *Everything is Ok* (2109 files); its `bin/QGroundControlApp.exe`
  (sha `80fce735…`) is byte-identical to the CI artifact.
- Backed up the previous (Nav Lights) build to **`/root/QGroundControl-installer.exe.prev-20260603`**
  (sha `b325f502…234c535`) and swapped the new one into **`/root/QGroundControl-installer.exe`**.

> **Verification caveat:** Qt's `rcc`/`qmlcachegen` **compresses the QML string table**, so a plaintext
> binary grep is blind to *all* QML strings (`Remove Pin`, `confirmRemovePinDialog`, etc. show 0 in both
> old and new binaries; `Are you sure` count is identical in both — those are unrelated C++ dialogs).
> The authoritative proof the change shipped is **provenance**: CI compiled the exact commit `7cb990b88`,
> and the binaries genuinely differ (+8,704 bytes). As always, the Windows GUI **cannot be executed
> here** (no Wine/QEMU/PE binfmt) — click-testing requires a real Windows machine.

### ✅ Done — installer rebuilt & swapped for Nav Lights + AUX10-ack (2026-06-02)

The Navigation Lights widget + AUX10-acknowledgement-by-colour change (commit `503f5cf66`) is pushed
to `scand80-dev`, the Windows build **succeeded** (all steps green incl. NSIS), and the installer
carrying it is now the local `.exe`.

**What was done (2026-06-02, latest):**
- Dispatched `build-windows-exe.yml` on `scand80-dev` (Release, video off, bootstrap on). Run
  [`26814736327`](https://github.com/shreedatta1503-cloud/scand80gui/actions/runs/26814736327)
  finished **success** (~40 min); MSVC compile passed clean, matching the local `-Werror` build.
- Downloaded artifact `QGroundControl-Windows-Release` (id `7356346953`, 242,771,949 bytes) via a
  **32-connection parallel chunked download** (HTTP Range against the SAS-signed Azure blob URL;
  `Accept-Ranges: bytes`, no auth header needed). Assembled size matched exactly; `unzip -t` passed.
- Zip held 4 files; picked the NSIS installer **`QGroundControl-installer-AMD64.exe`**
  (103,752,618 bytes). `7z l` confirmed it bundles `bin/QGroundControlApp.exe` (built this run) + the
  bootstrap launcher + Qt6 DLLs; the moc symbol `sendNavigationLights` is embedded in the app binary,
  proving the change shipped.
- Backed up the previous build to **`/root/QGroundControl-installer.exe.prev-20260602`** and swapped
  the fresh one into **`/root/QGroundControl-installer.exe`** (sha256 `b325f502…234c535`, verified
  equal to the extracted source). The earlier SBUS-only build (sha `92e7ba05…`) is the `.prev` backup;
  the original 169 MB file remains as `.stale-20260527`.

> Could **not** run/launch the `.exe` here: this box is headless Linux with **no Wine/QEMU/PE binfmt**,
> so a Windows GUI installer cannot execute. Validation was build-success + NSIS structure + embedded
> symbols. To actually run it, use a Windows machine.

**If the `.exe` ever needs regenerating** (artifact expires ~2026-06-16, 14-day retention): re-dispatch
`build-windows-exe.yml` on `scand80-dev` (via the Actions tab or `POST .../workflows/build-windows-exe.yml/dispatches`
with `{"ref":"scand80-dev"}`), then resolve a fresh SAS URL and re-run the 32-way parallel range download:
```bash
TOKEN=$(sed -n 's#https://\([^:]*\):\([^@]*\)@github.com#\2#p' ~/.git-credentials | head -1)
# 1) find the latest run + its artifact id:
curl -s -H "Authorization: Bearer $TOKEN" \
  "https://api.github.com/repos/shreedatta1503-cloud/scand80gui/actions/workflows/build-windows-exe.yml/runs?branch=scand80-dev&per_page=1"
# 2) resolve the (no-auth, range-capable) signed blob URL:
curl -s -o /dev/null -w '%{redirect_url}' -H "Authorization: Bearer $TOKEN" \
  "https://api.github.com/repos/shreedatta1503-cloud/scand80gui/actions/artifacts/<ARTIFACT_ID>/zip"
```

## Related repo

`shreedatta1503-cloud/scand80gui-buildconfig` is a separate, small repo holding an extracted copy of
QGC's CMake build configuration. It is **not** this fork and is not needed to build QGC.
