"""Compute SHA-256 + size for launcher manifest components and update the manifest.

The QGroundControl Windows bootstrap launcher refuses to install any component
whose recorded ``sha256`` does not match the downloaded artifact (fail-closed).
On a correctly deployed install every component's ``detect`` rule already passes,
so the download/repair path -- and therefore these hashes -- is only exercised as
a self-heal fallback. The manifest checked into the repo ships with placeholder
all-zero hashes; run this script after building/publishing the runtime bundle
artifacts to replace them with real values.

Usage::

    python gen_manifest_hashes.py --manifest qgc-bootstrap.json \\
        --artifacts-dir ./runtime-artifacts [--download] [--dry-run]

For each component:

* mirror-relative sources (bare file names) are resolved against
  ``--artifacts-dir``; the local file is hashed and its size recorded.
* absolute ``https://`` sources (e.g. the VC++ redist on ``aka.ms``) are hashed
  only when ``--download`` is given (fetched to memory); otherwise they are left
  unchanged with a warning, so an offline run never records a wrong hash.

Exit status is non-zero if any required component could not be resolved, so the
script is safe to use as a release gate.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
import urllib.request
from pathlib import Path, PurePosixPath
from typing import Any

_CHUNK = 1 << 20  # 1 MiB, matches the launcher's hashing chunk size.
_PLACEHOLDER = "0" * 64


def _sha256_and_size(data_iter: Any) -> tuple[str, int]:
    digest = hashlib.sha256()
    size = 0
    for chunk in data_iter:
        digest.update(chunk)
        size += len(chunk)
    return digest.hexdigest(), size


def _hash_local(path: Path) -> tuple[str, int]:
    def chunks() -> Any:
        with path.open("rb") as handle:
            while block := handle.read(_CHUNK):
                yield block

    return _sha256_and_size(chunks())


def _hash_remote(url: str) -> tuple[str, int]:
    if not url.lower().startswith("https://"):
        raise ValueError(f"refusing to download non-HTTPS source: {url}")

    def chunks() -> Any:
        # The https-only guard above is the scheme check for this open().
        with urllib.request.urlopen(url) as response:
            while block := response.read(_CHUNK):
                yield block

    return _sha256_and_size(chunks())


def _resolve_component(
    component: dict[str, Any], artifacts_dir: Path, download: bool
) -> tuple[str, int] | None:
    """Return (sha256, size) for the first resolvable source, or None."""
    for source in component.get("sources", []):
        lowered = source.lower()
        if lowered.startswith("http://"):
            print(f"  ! skipping insecure non-HTTPS source: {source}")
            continue
        if lowered.startswith("https://"):
            if not download:
                print(f"  - absolute source needs --download to hash: {source}")
                continue
            print(f"  > downloading {source}")
            return _hash_remote(source)
        # Mirror-relative: resolve the bare file name against the artifacts dir.
        artifact = artifacts_dir / PurePosixPath(source).name
        if artifact.is_file():
            print(f"  > hashing {artifact}")
            return _hash_local(artifact)
        print(f"  - artifact not found locally: {artifact}")
    return None


def update_manifest(
    manifest_path: Path, artifacts_dir: Path, *, download: bool, dry_run: bool
) -> int:
    manifest: dict[str, Any] = json.loads(manifest_path.read_text(encoding="utf-8"))
    components: list[dict[str, Any]] = manifest.get("components", [])

    changed = False
    unresolved_required: list[str] = []
    for component in components:
        name = component.get("name", "<unnamed>")
        print(f"component: {name}")
        resolved = _resolve_component(component, artifacts_dir, download)
        if resolved is None:
            note = "required" if component.get("required", True) else "optional"
            print(f"  ! unresolved ({note}); leaving existing hash")
            if component.get("required", True):
                unresolved_required.append(name)
            continue
        sha256, size = resolved
        if component.get("sha256") == sha256 and component.get("sizeBytes") == size:
            print(f"  = already current ({sha256[:12]}…, {size} bytes)")
            continue
        was_placeholder = component.get("sha256") == _PLACEHOLDER
        component["sha256"] = sha256
        component["sizeBytes"] = size
        changed = True
        origin = "placeholder" if was_placeholder else "stale"
        print(f"  + updated ({origin}) -> {sha256[:12]}…, {size} bytes")

    if unresolved_required:
        print(
            f"\nERROR: {len(unresolved_required)} required component(s) unresolved: "
            f"{', '.join(unresolved_required)}"
        )
        return 1

    if not changed:
        print("\nNothing to update; manifest already current.")
        return 0

    if dry_run:
        print("\n(dry-run) manifest not written.")
        return 0

    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"\nWrote {manifest_path}")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--manifest", type=Path, required=True, help="Path to qgc-bootstrap.json")
    parser.add_argument(
        "--artifacts-dir",
        type=Path,
        default=Path.cwd(),
        help="Directory holding the built runtime bundle artifacts (default: CWD)",
    )
    parser.add_argument(
        "--download",
        action="store_true",
        help="Fetch absolute https:// sources (e.g. vc_redist) to hash them",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Report changes without writing the manifest",
    )
    args = parser.parse_args(argv)

    if not args.manifest.is_file():
        parser.error(f"manifest not found: {args.manifest}")
    return update_manifest(
        args.manifest, args.artifacts_dir, download=args.download, dry_run=args.dry_run
    )


if __name__ == "__main__":
    sys.exit(main())
