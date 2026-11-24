#!/usr/bin/env python3
# Copyright (c) 2026 UniVex Studios. All Rights Reserved.
"""Builds UniVex Hub's Android native library via CMake/NDK and packages a signed APK (no Gradle).

    python3 tools/build_apk.py --sdk "$ANDROID_HOME" --build-dir build/android --abi arm64-v8a
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]

ANDROID_NDK_VERSION = "29.0.14206865"
ANDROID_NDK_HOST_TAG = "linux-x86_64"
ANDROID_MIN_API_LEVEL = 28
ANDROID_PLATFORM = "android-35"
ANDROID_BUILD_TOOLS = "35.0.0"
ANDROID_PAGE_SIZE_BYTES = 16384
ANDROID_ABIS = ("arm64-v8a", "x86_64")
ANDROID_DEFAULT_ABI = "arm64-v8a"

ZIP_ALIGNMENT = "4"
ZIP_PAGE_ALIGNMENT_KB = str(ANDROID_PAGE_SIZE_BYTES // 1024)

DEBUG_KEYSTORE = "debug.keystore"
DEBUG_KEY_ALIAS = "androiddebugkey"
DEBUG_KEY_PASSWORD = "android"
DEBUG_KEY_SUBJECT = "CN=Android Debug,O=UniVex,C=US"
DEBUG_KEY_VALIDITY_DAYS = "10000"
DEBUG_KEY_BITS = "2048"

LIBRARY_NAME = "univex_hub"
APK_PREFIX = "UniVexHub"
MANIFEST_PATH = REPO_ROOT / "src" / "android" / "AndroidManifest.xml"
JAVA_SOURCES_DIR = REPO_ROOT / "src" / "android" / "java"
RESOURCES_DIR = REPO_ROOT / "src" / "android" / "res"
HUB_VERSION_HEADER = REPO_ROOT / "src" / "online" / "hub_setup.h"

JAVA_RELEASE = "11"
DEX_FILE = "classes.dex"
COMPILED_RESOURCES = "resources.zip"
ASSET_INDEX = "asset-index.txt"

APK_ASSETS = (
    (REPO_ROOT / "assets" / "prism_assets", "prism_assets"),
    (REPO_ROOT / "assets" / "templates", "project_templates"),
    (REPO_ROOT / "assets" / "engine_feed.json", "engine_feed.json"),
)


def run(*command) -> None:
    subprocess.run([str(part) for part in command], check=True)


def parse_version_tuple(text: str) -> tuple[int, int, int, int]:
    match = re.match(r"^v?(\d+)\.(\d+)\.(\d+)(?:\+build\.(\d+)|\.(\d+))?$", text.strip())
    if not match:
        raise RuntimeError(f"Invalid version '{text}'; expected MAJOR.MINOR.PATCH[+build.BUILD]")
    major, minor, patch, build_a, build_b = match.groups()
    return int(major), int(minor), int(patch), int(build_a or build_b or 0)


def read_hub_version() -> tuple[int, int, int, int]:
    source = HUB_VERSION_HEADER.read_text(encoding="utf-8")
    match = re.search(r"kHubVersionUVE\s*\{\s*(\d+)U?\s*,\s*(\d+)U?\s*,\s*(\d+)U?\s*,\s*(\d+)U?\s*\}", source)
    if not match:
        raise RuntimeError(f"Could not read kHubVersionUVE from {HUB_VERSION_HEADER}")
    return tuple(int(part) for part in match.groups())  # type: ignore[return-value]


def android_version_metadata(version: tuple[int, int, int, int], build_number: int = 0) -> tuple[int, str]:
    major, minor, patch, build = version
    effective_build = max(0, build) + max(0, build_number)
    version_code = max(1, major * 100_000_000 + minor * 1_000_000 + patch * 10_000 + min(effective_build, 9_999))
    version_name = f"{major}.{minor}.{patch}" + (f"+build.{build}" if build else "")
    return version_code, version_name


def verify_elf_symbol_isolation(ndk_bin: Path, library: Path) -> None:
    out = subprocess.check_output([str(ndk_bin / "llvm-nm"), "-D", "--defined-only", str(library)], text=True)
    exported = [line.split()[-1] for line in out.splitlines() if line.strip()]
    if "ANativeActivity_onCreate" not in exported:
        raise RuntimeError(f"{library.name} does not export ANativeActivity_onCreate")
    allowed = {"ANativeActivity_onCreate", "_edata", "_end", "__bss_start"}
    unexpected = [sym for sym in exported if sym not in allowed]
    if unexpected:
        raise RuntimeError(f"{library.name} leaks unexpected dynamic symbols: {' '.join(unexpected)}")
    print(f"Verified ELF symbol isolation for {library.name}: {', '.join(exported)}", flush=True)


def build_native_library(sdk: Path, abi: str, cmake_build_dir: Path, jobs: int) -> Path:
    ndk = sdk / "ndk" / ANDROID_NDK_VERSION
    toolchain = ndk / "build" / "cmake" / "android.toolchain.cmake"
    if not toolchain.is_file():
        raise RuntimeError(f"Missing NDK CMake toolchain: {toolchain}")

    cmake_args = [
        "cmake",
        "-S", str(REPO_ROOT),
        "-B", str(cmake_build_dir),
        "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=Release",
        f"-DCMAKE_TOOLCHAIN_FILE={toolchain}",
        f"-DANDROID_ABI={abi}",
        f"-DANDROID_PLATFORM=android-{ANDROID_MIN_API_LEVEL}",
        "-DANDROID_STL=c++_static",
        "-DUNIVEX_HUB_BUILD_TESTS=OFF",
    ]
    launcher = os.environ.get("CMAKE_CXX_COMPILER_LAUNCHER") or os.environ.get("HUB_COMPILER_LAUNCHER")
    if launcher:
        cmake_args += [
            f"-DCMAKE_C_COMPILER_LAUNCHER={launcher}",
            f"-DCMAKE_CXX_COMPILER_LAUNCHER={launcher}",
        ]
    run(*cmake_args)
    run("cmake", "--build", str(cmake_build_dir), "--target", LIBRARY_NAME, f"-j{jobs}")
    library = cmake_build_dir / "bin" / f"lib{LIBRARY_NAME}.so"
    if not library.is_file():
        raise RuntimeError(f"Expected native library at {library}")
    return library


def compile_java(build: Path, tools: Path, platform_jar: Path) -> Path:
    classes = build / "classes"
    dex_dir = build / "dex"
    for folder in (classes, dex_dir):
        if folder.exists():
            shutil.rmtree(folder)
        folder.mkdir()
    sources = sorted(JAVA_SOURCES_DIR.rglob("*.java"))
    if not sources:
        raise RuntimeError(f"No Java sources found under {JAVA_SOURCES_DIR}")
    run("javac", "--release", JAVA_RELEASE, "-Xlint:all", "-Werror", "-classpath", platform_jar, "-d", classes, *sources)
    run(
        tools / "d8", "--release", "--min-api", str(ANDROID_MIN_API_LEVEL), "--lib", platform_jar, "--output", dex_dir,
        *sorted(classes.rglob("*.class")),
    )
    return dex_dir / DEX_FILE


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--sdk", type=Path, default=os.environ.get("ANDROID_HOME"), help="Android SDK (default: $ANDROID_HOME)")
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--abi", choices=ANDROID_ABIS, default=ANDROID_DEFAULT_ABI)
    parser.add_argument("--native-lib", type=Path, help="Use a prebuilt libunivex_hub.so instead of compiling it")
    parser.add_argument("--cmake-build-dir", type=Path, help="Optional CMake build directory for native compilation")
    parser.add_argument("--version", help="Override APK version (MAJOR.MINOR.PATCH[+build.BUILD])")
    parser.add_argument("--build-number", type=int, default=0, help="Monotonic CI run number added to versionCode")
    parser.add_argument("--debuggable", action="store_true", help="Mark APK debuggable for emulator inspection only")
    parser.add_argument("--keystore", type=Path, help="Release keystore (reads UVE_ANDROID_KEYSTORE_PASSWORD / UVE_ANDROID_KEY_ALIAS)")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    args = parser.parse_args()

    if args.sdk is None:
        parser.error("--sdk is required when ANDROID_HOME is not set")
    if args.jobs < 1:
        parser.error("--jobs must be positive")

    version_tuple = parse_version_tuple(args.version) if args.version else read_hub_version()
    version_code, version_name = android_version_metadata(version_tuple, args.build_number)
    print(f"Android Hub APK version: versionName={version_name} versionCode={version_code}", flush=True)

    sdk = args.sdk.resolve()
    tools = sdk / "build-tools" / ANDROID_BUILD_TOOLS
    platform_jar = sdk / "platforms" / ANDROID_PLATFORM / "android.jar"
    ndk_bin = sdk / "ndk" / ANDROID_NDK_VERSION / "toolchains" / "llvm" / "prebuilt" / ANDROID_NDK_HOST_TAG / "bin"
    for required in (tools / "aapt2", tools / "d8", platform_jar, ndk_bin / "llvm-strip", ndk_bin / "llvm-nm"):
        if not required.is_file():
            parser.error(f"Missing Android tool: {required}")

    build = args.build_dir.resolve()
    build.mkdir(parents=True, exist_ok=True)
    if args.native_lib:
        library = args.native_lib.resolve()
        if not library.is_file():
            parser.error(f"Missing --native-lib: {library}")
    else:
        cmake_dir = (args.cmake_build_dir or (REPO_ROOT / "build" / f"android-{args.abi}")).resolve()
        library = build_native_library(sdk, args.abi, cmake_dir, args.jobs)

    stripped = build / f"lib{LIBRARY_NAME}.so"
    shutil.copy2(library, stripped)
    run(ndk_bin / "llvm-strip", "--strip-unneeded", stripped)
    verify_elf_symbol_isolation(ndk_bin, stripped)

    assets = build / "assets"
    if assets.exists():
        shutil.rmtree(assets)
    assets.mkdir()
    for source, name in APK_ASSETS:
        if source.is_dir():
            shutil.copytree(source, assets / name)
        else:
            shutil.copy2(source, assets / name)
    names = sorted(f.relative_to(assets).as_posix() for f in assets.rglob("*") if f.is_file())
    (assets / ASSET_INDEX).write_text("\n".join(names) + "\n", encoding="utf-8")

    dex = compile_java(build, tools, platform_jar)
    resources = build / COMPILED_RESOURCES
    run(tools / "aapt2", "compile", "--dir", RESOURCES_DIR, "-o", resources)

    unsigned = build / "unsigned.apk"
    aapt2_link = [
        tools / "aapt2", "link", "-I", platform_jar, "--manifest", MANIFEST_PATH,
        "-A", assets, "-R", resources, "--auto-add-overlay",
        "--version-code", str(version_code), "--version-name", version_name,
    ]
    if args.debuggable:
        aapt2_link.append("--debug-mode")
    aapt2_link += ["-o", unsigned]
    run(*aapt2_link)

    with zipfile.ZipFile(unsigned, "a", compression=zipfile.ZIP_STORED) as package:
        package.write(dex, DEX_FILE)
        package.write(stripped, f"lib/{args.abi}/lib{LIBRARY_NAME}.so")
        existing = set(package.namelist())
        for asset in sorted(assets.rglob("*")):
            if asset.is_file():
                archive_name = "assets/" + asset.relative_to(assets).as_posix()
                if archive_name not in existing:
                    package.write(asset, archive_name)
                    existing.add(archive_name)

    aligned = build / "aligned.apk"
    run(tools / "zipalign", "-f", "-P", ZIP_PAGE_ALIGNMENT_KB, ZIP_ALIGNMENT, unsigned, aligned)

    if args.keystore:
        keystore = args.keystore.resolve()
        password = os.environ.get("UVE_ANDROID_KEYSTORE_PASSWORD")
        alias = os.environ.get("UVE_ANDROID_KEY_ALIAS")
        if not password or not alias:
            parser.error("--keystore needs UVE_ANDROID_KEYSTORE_PASSWORD and UVE_ANDROID_KEY_ALIAS")
        signing = ["--ks", keystore, "--ks-key-alias", alias, "--ks-pass", "env:UVE_ANDROID_KEYSTORE_PASSWORD"]
    else:
        keystore = build / DEBUG_KEYSTORE
        if not keystore.exists():
            run(
                "keytool", "-genkeypair", "-keystore", keystore,
                "-storepass", DEBUG_KEY_PASSWORD, "-keypass", DEBUG_KEY_PASSWORD,
                "-alias", DEBUG_KEY_ALIAS, "-dname", DEBUG_KEY_SUBJECT,
                "-keyalg", "RSA", "-keysize", DEBUG_KEY_BITS, "-validity", DEBUG_KEY_VALIDITY_DAYS,
            )
        signing = ["--ks", keystore, "--ks-pass", f"pass:{DEBUG_KEY_PASSWORD}", "--key-pass", f"pass:{DEBUG_KEY_PASSWORD}"]

    output = build / f"{APK_PREFIX}-{args.abi}.apk"
    run(tools / "apksigner", "sign", *signing, "--out", output, aligned)
    run(tools / "apksigner", "verify", output)
    run(tools / "zipalign", "-c", "-P", ZIP_PAGE_ALIGNMENT_KB, ZIP_ALIGNMENT, output)
    print(f"APK: {output}", flush=True)


if __name__ == "__main__":
    main()
