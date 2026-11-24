#!/usr/bin/env python3
# Copyright (c) 2026 UniVex Studios. All Rights Reserved.
"""Downloads the latest ABI-matching UniVex Engine .uvpackage and verifies its SHA-256 checksum.

Used by UniVex Hub CI to test end-to-end Android dlopen(uve_editor) against a real engine package
published by UniVexStudio/UniVex_Engine without any git submodule or engine rebuild.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
import urllib.error
import urllib.request
from pathlib import Path

DEFAULT_REPOS = ("UniVexStudio/UniVexReleases", "UniVexStudio/UniVexReleases-Dev")
PACKAGE_PREFIX = "univex-engine-"
PACKAGE_EXT = ".uvpackage"
CHECKSUM_SUFFIX = ".sha256"
CHUNK_BYTES = 1 << 16


def _request_json(url: str, token: str | None) -> list[dict]:
    headers = {
        "Accept": "application/vnd.github+json",
        "User-Agent": "UniVexHub-CI/1.0",
    }
    if token:
        headers["Authorization"] = f"Bearer {token}"
    req = urllib.request.Request(url, headers=headers)
    with urllib.request.urlopen(req, timeout=30) as response:
        return json.loads(response.read().decode("utf-8"))


def _download_file(url: str, dest: Path, token: str | None) -> None:
    headers = {"User-Agent": "UniVexHub-CI/1.0"}
    if token:
        headers["Authorization"] = f"Bearer {token}"
    req = urllib.request.Request(url, headers=headers)
    dest.parent.mkdir(parents=True, exist_ok=True)
    with urllib.request.urlopen(req, timeout=180) as response, dest.open("wb") as out:
        while True:
            chunk = response.read(CHUNK_BYTES)
            if not chunk:
                break
            out.write(chunk)


def _sha256_of(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(CHUNK_BYTES), b""):
            digest.update(chunk)
    return digest.hexdigest()


def find_latest_package(repos: list[str], target: str, token: str | None) -> tuple[str, str, dict, dict | None]:
    suffix = f"-{target}{PACKAGE_EXT}"
    for repo in repos:
        url = f"https://api.github.com/repos/{repo}/releases?per_page=20"
        try:
            releases = _request_json(url, token)
        except urllib.error.HTTPError as exc:
            print(f"fetch_engine_package: skipping {repo} (HTTP {exc.code})", file=sys.stderr, flush=True)
            continue
        for release in releases:
            if release.get("draft"):
                continue
            assets = release.get("assets") or []
            pkg = next(
                (a for a in assets if a.get("name", "").startswith(PACKAGE_PREFIX) and a.get("name", "").endswith(suffix)),
                None,
            )
            if pkg is None:
                continue
            checksum = next((a for a in assets if a.get("name") == pkg["name"] + CHECKSUM_SUFFIX), None)
            return repo, str(release.get("tag_name", "")), pkg, checksum
    raise RuntimeError(f"No release containing *{suffix} found in repositories: {', '.join(repos)}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--target", required=True, help="Package target, e.g. android-x86_64, linux, windows")
    parser.add_argument("--output", required=True, type=Path, help="Directory to store downloaded package")
    parser.add_argument(
        "--repos",
        default=",".join(DEFAULT_REPOS),
        help="Comma-separated owner/repo list searched in order",
    )
    args = parser.parse_args()

    repos = [entry.strip() for entry in args.repos.split(",") if entry.strip()]
    token = os.environ.get("GH_TOKEN") or os.environ.get("GITHUB_TOKEN")
    repo, tag, pkg_asset, checksum_asset = find_latest_package(repos, args.target, token)
    print(f"Downloading {pkg_asset['name']} from {repo} ({tag})...", file=sys.stderr, flush=True)

    pkg_path = args.output.resolve() / pkg_asset["name"]
    _download_file(pkg_asset["browser_download_url"], pkg_path, token)

    if checksum_asset is not None:
        sum_path = args.output.resolve() / checksum_asset["name"]
        _download_file(checksum_asset["browser_download_url"], sum_path, token)
        expected = sum_path.read_text(encoding="utf-8").strip().split()[0].lower()
        actual = _sha256_of(pkg_path).lower()
        if actual != expected:
            pkg_path.unlink(missing_ok=True)
            raise RuntimeError(f"SHA-256 mismatch for {pkg_path.name}: expected {expected}, got {actual}")
        print(f"Verified SHA-256 for {pkg_path.name}: {actual}", file=sys.stderr, flush=True)

    print(str(pkg_path))
    return 0


if __name__ == "__main__":
    sys.exit(main())
