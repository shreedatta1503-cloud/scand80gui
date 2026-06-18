# QGroundControl Windows Bootstrap Launcher

A tiny, dependency-free Win32 executable that becomes **`QGroundControl.exe`** on
Windows when the build is configured with `-DQGC_WINDOWS_BOOTSTRAP=ON`. The real
Qt application is renamed to **`QGroundControlApp.exe`**.

On a clean machine the Windows loader resolves an exe's Qt/MSVC DLLs *before*
`main()` runs, so a self-check inside the Qt app cannot recover a missing DLL —
the process never starts. This launcher solves that: it is statically linked
(`/MT`), uses only the Win32 API (no Qt, no MSVC redistributable), so it always
starts, then **verifies and repairs** the runtime before launching the app.

## What it does

1. Always shows a small splash/progress window.
2. Holds a single-instance mutex (`Global\QGroundControlLauncher`).
3. Loads the dependency manifest `qgc-bootstrap.json` from the install directory.
   If `manifestUrl` is set and the machine is online, it tries to fetch a newer
   manifest from that URL and adopts it only if `manifestVersion` is greater.
4. Runs each component's **detect** rule to find what is missing/outdated.
5. For each missing component: downloads from the first reachable HTTPS source
   (failover across `sources`/`mirrors`), verifies **SHA-256** (and Authenticode
   for redists), then installs (`copy` / `extract` / `run-silent`).
6. Launches `QGroundControlApp.exe`, **forwarding all CLI arguments**
   (so the Start-menu `-desktop` / `-swrast` shortcuts keep working).

All steps are logged to `%LOCALAPPDATA%\QGroundControl\launcher.log`.

## Offline / graceful failure

- Nothing missing → launch immediately, **no network access**.
- Missing but offline, and only *optional* components are missing → launch anyway
  (logged as a warning).
- A *required* component is missing and the machine is offline, or a download
  fails after all sources → an actionable error dialog (what's missing, the
  configured download source, and the log path); the launcher exits non-zero.

On any failed repair the partial changes are rolled back (overwritten files are
restored from backups taken in the staging directory).

## Manifest schema (`qgc-bootstrap.json`)

```jsonc
{
  "schemaVersion": 1,                       // must be 1
  "manifestVersion": "2026.06.01",          // YYYY.MM.DD; used for remote>local compare
  "appExecutable": "QGroundControlApp.exe", // the Qt app the launcher spawns
  "manifestUrl": "",                        // optional: HTTPS URL of a newer manifest
  "mirrors": [                              // ordered base URLs for relative sources
    "https://github.com/<org>/<repo>/releases/download/runtime-2026.06.01/"
  ],
  "components": [
    {
      "name": "vc-redist-x64",
      "type": "vcredist",                   // file | vcredist | driver | qt-bundle (informational)
      "required": true,                     // required => fatal if it can't be repaired
      "detect": { "rule": "file-exists", "path": "vcruntime140.dll" },
      "sources": [ "https://aka.ms/vs/17/release/vc_redist.x64.exe" ],
      "sha256": "<64 hex chars>",           // MANDATORY for every download
      "sizeBytes": 0,                       // optional; drives the progress bar
      "install": { "action": "run-silent", "args": "/install /quiet /norestart", "elevate": true },
      "verifyAuthenticode": true,           // verify signature before run-silent
      "publisher": "Microsoft Corporation"  // expected signer subject (substring)
    }
  ]
}
```

### `detect.rule`
- `file-exists` — `path` (relative to the install dir) exists.
- `dll-version` — `path` exists and its file version ≥ `minVersion`.
- `registry-key` — `hive` (`HKLM`/`HKCU`) + `key` + `value` equals `expect`
  (and, if `minVersion` is set, a sibling `Version` value is ≥ `minVersion`).

### `install.action`
- `copy` — copy the downloaded file to `dest` (relative to the install dir).
- `extract` — extract the downloaded archive into `dest` (`.` = install root).
- `run-silent` — run the downloaded installer with `args`; set `elevate: true`
  to request admin via UAC (used for the system VC++ redistributable).

> **Note on the bundled-vs-download VC++ runtime.** The build deploys the VC
> runtime DLLs *app-local* (windeployqt `--compiler-runtime`), so on a normal
> install `vcruntime140.dll` is present and the `vc-redist-x64` component is
> already satisfied — **nothing is downloaded and no UAC prompt appears.** The
> `run-silent` vc_redist source is only the fallback for a machine where those
> DLLs were somehow stripped.

## Updating dependencies without rebuilding the app

The manifest is **data**, not code. To ship new Qt versions, redists, or fixes:

1. Build the new runtime bundle ZIP(s) and compute their SHA-256 — the helper
   `tools/gen_manifest_hashes.py` fills `sha256`/`sizeBytes` from a directory of
   built artifacts (`--download` also hashes absolute https sources like the VC++
   redist; it exits non-zero if a required component is unresolved, so it works
   as a release gate).
2. Upload them as assets to a GitHub Release (or any HTTPS host).
3. Update `qgc-bootstrap.json` (`manifestVersion`, `mirrors`, `sources`,
   `sha256`, `sizeBytes`) and publish it as a release asset too.
4. Point installed clients at it by setting `manifestUrl` in the shipped local
   manifest (or in a future installer build). Clients adopt the newer manifest
   automatically on next launch.

No launcher or QGroundControl rebuild is required.

## ⚠️ SHA-256 placeholders

The checked-in `qgc-bootstrap.json` ships with **placeholder all-zero SHA-256
values** and `sizeBytes: 0`. These are intentionally invalid: on a correctly
deployed install every component's `detect` rule passes, so **no download or
verification ever runs** and the placeholders are harmless. Before relying on the
*download/repair* path you must publish real bundles and replace the placeholder
hashes with their real values — a component with a placeholder hash will fail
verification (and, if `required`, fail the launch) rather than install unverified
content. This is the intended fail-closed behavior.

## Security model

- **SHA-256 is mandatory** on every artifact; a mismatch discards the file and
  tries the next source — content is never installed unverified.
- **HTTPS only** — non-`https://` sources are rejected before any request.
- **Authenticode** (`WinVerifyTrust` + publisher check) is verified before any
  `run-silent` installer is executed.
- **No silent elevation** — the launcher manifest is `asInvoker`; UAC is only
  raised for components with `"elevate": true`.
- **Path-traversal safe** — archives are extracted into an isolated staging dir
  with bsdtar (which refuses `..`/absolute members), and every placed file is
  containment-checked against the install dir before writing.
- The local manifest lives in `Program Files` (admin-write-only); a remote
  manifest is only fetched when the local file authorizes it via `manifestUrl`,
  over HTTPS, and its components still require SHA-256 + Authenticode.

## Build

```bat
qt-cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DQGC_WINDOWS_BOOTSTRAP=ON
cmake --build build --config Release
cmake --install build --config Release
cpack -G NSIS -C Release
```

The launcher target (`qgc_launcher`) is MSVC-only and statically links the CRT;
the Qt app keeps the dynamic CRT (`/MD`). They coexist because
`MSVC_RUNTIME_LIBRARY` is set per target.
