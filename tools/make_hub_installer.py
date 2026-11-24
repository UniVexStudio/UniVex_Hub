#!/usr/bin/env python3
# Copyright (c) 2026 UniVex Studios. All Rights Reserved.
"""Packages UniVex Hub into its platform installer:

  * Linux: self-extracting `UniVexHubSetup-<version>-linux` executable + optional `.deb` package.
  * Windows: Inno Setup wizard `UniVexHubSetup-<version>-windows.exe` (via ISCC.exe).

Writes a `<installer>.sha256` checksum file beside every produced artifact.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path

HUB_TAG_PREFIX = "hub-"
INSTALLER_PREFIX = "UniVexHubSetup-"
HUB_STEM = "uve_hub"
SETUP_STEM = "uve_hub_setup"
PRISM_ASSETS_DIRECTORY = "prism_assets"
PROJECT_TEMPLATES_DIRECTORY = "project_templates"
HUB_DIRECTORIES = (PRISM_ASSETS_DIRECTORY, PROJECT_TEMPLATES_DIRECTORY)
FEED_FILE = "engine_feed.json"
ICONS_DIRECTORY = "icons"
WINDOWS_ICON_FILE = "univex-logo.ico"
LINUX_LIB_DIRECTORY = "lib"
CHECKSUM_SUFFIX = ".sha256"
BETA_TAG_SUFFIX = "-beta"

PAYLOAD_MAGIC = b"UVXSETUP"
SIZE_FORMAT = "<Q"

HUB_DISPLAY_NAME = "UniVex Hub"
HUB_DESCRIPTION = "Create, open and manage UniVex projects and engine versions"
HUB_INSTALL_FOLDER = "UniVex Hub"
PUBLISHER = "UniVex Studios"
PROJECT_EXTENSION = ".uvproject"
PROJECT_PROGRAM_ID = "UniVex.Project"
PROJECT_TYPE_NAME = "UniVex Project"
WINDOWS_APP_ID = "{{7B3F4C8E-2D9A-4E6B-9A15-8C6E2F5A1D3B}"

ISCC_ENVIRONMENT = "UVE_ISCC"
ISCC_NAMES = ("ISCC.exe", "iscc")

REPO_ROOT = Path(__file__).resolve().parents[1]
BRAND_DIR = REPO_ROOT / "assets" / "brand"
BRAND_PNG_DIRECTORY = "png"
WINDOWS_INSTALLER_SCRIPT = REPO_ROOT / "assets" / "windows" / "UniVexHub.iss"

EXECUTABLE_SUFFIX = {
    "linux": "",
    "windows": ".exe",
}
TAG_PATTERN = re.compile(r"^v?(\d+)\.(\d+)\.(\d+)(?:\+build\.(\d+)|\.(\d+))?$")
FILE_MODE = 0o644
EXECUTABLE_MODE = 0o755
DIRECTORY_MODE = 0o755
CHUNK_BYTES = 1 << 16

LINUX_SYSTEM_LIB_PREFIXES = (
    "linux-vdso.so",
    "ld-linux",
    "libc.so",
    "libm.so",
    "libdl.so",
    "libpthread.so",
    "librt.so",
    "libresolv.so",
    "libutil.so",
    "libanl.so",
    "libnsl.so",
    "libnss_",
    "libstdc++.so",
    "libgcc_s.so",
    "libGL.so",
    "libGLX.so",
    "libGLdispatch.so",
    "libOpenGL.so",
    "libEGL.so",
    "libGLESv",
    "libvulkan.so",
    "libdrm.so",
    "libgbm.so",
    "libX11.so",
    "libX11-xcb.so",
    "libXext.so",
    "libXau.so",
    "libXdmcp.so",
    "libXrandr.so",
    "libXrender.so",
    "libXfixes.so",
    "libXcursor.so",
    "libXinerama.so",
    "libXi.so",
    "libXxf86vm.so",
    "libxcb.so",
    "libxcb-",
    "libwayland-",
    "libxkbcommon.so",
    "libdbus-1.so",
    "libasound.so",
    "libpulse",
    "libcurl.so",
    "libssl.so",
    "libcrypto.so",
    "libz.so",
)

DEB_PACKAGE_NAME = "univex-hub"
DEB_INSTALL_ROOT = "opt/univex-hub"
DEB_ARCHITECTURE = "amd64"
DEB_MAINTAINER = "UniVex Studios <support@univexstudios.com>"
DEB_MIME_TYPE = "application/x-univex-project"
DEB_ICON_NAME = "univex-hub"
DEB_MIME_ICON_NAME = "application-x-univex-project"
DEB_ICON_SIZES = (16, 24, 32, 48, 64, 128, 256, 512)


class PackagingError(RuntimeError):
    """Raised when an installer package cannot be built from the given inputs."""


def parse_tag(tag: str) -> tuple[str, str]:
    channel = "stable"
    if tag.endswith(BETA_TAG_SUFFIX):
        channel = "beta"
        tag = tag[: -len(BETA_TAG_SUFFIX)]
    match = TAG_PATTERN.match(tag)
    if not match:
        raise PackagingError(f"'{tag}' is not a valid version tag such as v1.2.3 or v1.2.3+build.4")
    major, minor, patch, build_a, build_b = match.groups()
    build = build_a or build_b or "0"
    return f"{int(major)}.{int(minor)}.{int(patch)}+build.{int(build)}", channel


def display_version(version: str) -> str:
    core, _, build = version.partition("+build.")
    return core if build in ("", "0") else version


def parse_hub_tag(tag: str) -> str:
    if not tag.startswith(HUB_TAG_PREFIX):
        raise PackagingError(f"'{tag}' is not a Hub release tag such as hub-v1.0.0")
    version, _channel = parse_tag(tag[len(HUB_TAG_PREFIX) :])
    return version


def installer_name(version: str, target: str) -> str:
    return f"{INSTALLER_PREFIX}{display_version(version)}-{target}{EXECUTABLE_SUFFIX[target]}"


def numeric_version(version: str) -> str:
    core, _, build = version.partition("+build.")
    return f"{core}.{build or 0}"


def built_version(program: Path) -> str:
    result = subprocess.run([str(program), "--version"], capture_output=True, text=True, check=False)
    if result.returncode != 0:
        raise PackagingError(f"{program} --version failed: {result.stderr.strip()}")
    return result.stdout.strip()


def sha256_of(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(CHUNK_BYTES), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_checksum(artifact: Path) -> None:
    checksum = artifact.with_name(artifact.name + CHECKSUM_SUFFIX)
    checksum.write_text(f"{sha256_of(artifact)}  {artifact.name}\n", encoding="utf-8")


def is_elf_file(path: Path) -> bool:
    try:
        with path.open("rb") as handle:
            return handle.read(4) == b"\x7fELF"
    except OSError:
        return False


def is_linux_system_lib(soname: str) -> bool:
    return any(soname.startswith(prefix) for prefix in LINUX_SYSTEM_LIB_PREFIXES)


def _readelf_needed(path: Path) -> list[str]:
    output = subprocess.check_output(["readelf", "-d", str(path)], text=True)
    return re.findall(r"\(NEEDED\)\s+Shared library:\s+\[([^\]]+)\]", output)


def _ldd_resolved(path: Path) -> dict[str, Path | None]:
    output = subprocess.check_output(["ldd", str(path)], text=True)
    resolved: dict[str, Path | None] = {}
    for line in output.splitlines():
        match = re.match(r"^\s*(\S+)\s+=>\s+(.+?)(?:\s+\(0x[0-9a-fA-F]+\))?$", line)
        if not match:
            continue
        soname, target = match.group(1), match.group(2).strip()
        resolved[soname] = None if target == "not found" else Path(target)
    return resolved


def copy_linux_shared_libs(stage: Path) -> set[str]:
    elf_binaries = [p for p in sorted(stage.iterdir()) if p.is_file() and is_elf_file(p)]
    if not elf_binaries:
        return set()
    readelf = shutil.which("readelf")
    ldd = shutil.which("ldd")
    if readelf is None or ldd is None:
        raise PackagingError("readelf and ldd are needed to bundle Linux shared libraries")

    lib_dir = stage / LINUX_LIB_DIRECTORY
    bundled: dict[str, Path] = {}
    pending: list[Path] = list(elf_binaries)
    while pending:
        binary = pending.pop()
        needed = _readelf_needed(binary)
        resolved = _ldd_resolved(binary)
        for soname in needed:
            if is_linux_system_lib(soname) or soname in bundled:
                continue
            if soname not in resolved or resolved[soname] is None:
                raise PackagingError(f"{binary.name} requires {soname}, which could not be resolved via ldd")
            source = resolved[soname].resolve()
            if not source.is_file():
                raise PackagingError(f"{binary.name} resolved {soname} to missing file {source}")
            lib_dir.mkdir(exist_ok=True)
            dest = lib_dir / soname
            shutil.copy2(source, dest, follow_symlinks=True)
            bundled[soname] = dest
            pending.append(dest)

    patchelf = shutil.which("patchelf")
    if bundled and patchelf is not None:
        for dest in bundled.values():
            subprocess.run([patchelf, "--force-rpath", "--set-rpath", "$ORIGIN", str(dest)], check=True)
        for binary in elf_binaries:
            subprocess.run([patchelf, "--force-rpath", "--set-rpath", "$ORIGIN/lib:$ORIGIN", str(binary)], check=True)
    return set(bundled.keys())


def copy_windows_dlls(stage: Path) -> None:
    compiler = shutil.which("g++")
    if compiler is None:
        raise PackagingError("g++ is needed to locate the DLLs a Windows package carries")
    library_dir = Path(compiler).parent
    pending = list(stage.glob("*.exe"))
    copied: set[str] = set()
    while pending:
        binary = pending.pop()
        imports = subprocess.check_output(["objdump", "-p", str(binary)], text=True)
        for name in re.findall(r"DLL Name:\s*(\S+)", imports):
            source = library_dir / name
            if name.lower() in copied or not source.is_file():
                continue
            copied.add(name.lower())
            target = stage / name
            shutil.copy2(source, target)
            pending.append(target)


def stage_files(bin_dir: Path, target: str, stems: tuple[str, ...], directories: tuple[str, ...], stage: Path) -> None:
    suffix = EXECUTABLE_SUFFIX[target]
    for stem in stems:
        source = bin_dir / f"{stem}{suffix}"
        if not source.is_file():
            raise PackagingError(f"missing executable {source}")
        shutil.copy2(source, stage / source.name)
    for directory in directories:
        source = bin_dir / directory
        if not source.is_dir():
            raise PackagingError(f"missing directory {source}")
        shutil.copytree(source, stage / directory, symlinks=False)
    if target == "windows":
        copy_windows_dlls(stage)
    elif target == "linux":
        copy_linux_shared_libs(stage)


def write_archive(stage: Path, archive: Path, executables: set[str]) -> None:
    def normalise(info: tarfile.TarInfo) -> tarfile.TarInfo:
        info.uid = info.gid = 0
        info.uname = info.gname = ""
        if info.isdir():
            info.mode = DIRECTORY_MODE
        else:
            info.mode = EXECUTABLE_MODE if Path(info.name).name in executables else FILE_MODE
        return info

    with tarfile.open(archive, "w:gz", format=tarfile.USTAR_FORMAT) as tar:
        for path in sorted(stage.rglob("*")):
            if path.is_symlink() or not (path.is_file() or path.is_dir()):
                raise PackagingError(f"{path} is neither a regular file nor a directory")
            tar.add(path, arcname=path.relative_to(stage).as_posix(), recursive=False, filter=normalise)


def stage_payload(bin_dir: Path, target: str, stage: Path) -> set[str]:
    stage_files(bin_dir, target, (HUB_STEM,), HUB_DIRECTORIES, stage)
    feed = bin_dir / FEED_FILE
    if not feed.is_file():
        raise PackagingError(f"missing {feed}")
    shutil.copy2(feed, stage / FEED_FILE)
    icons = stage / ICONS_DIRECTORY
    icons.mkdir()
    for source in sorted((BRAND_DIR / BRAND_PNG_DIRECTORY).glob("*.png")) + [BRAND_DIR / WINDOWS_ICON_FILE]:
        if not source.is_file():
            raise PackagingError(f"missing {source}")
        shutil.copy2(source, icons / source.name)
    executables = {f"{HUB_STEM}{EXECUTABLE_SUFFIX[target]}"}
    executables.update(path.name for path in stage.glob("*.dll"))
    linux_lib_dir = stage / LINUX_LIB_DIRECTORY
    if linux_lib_dir.is_dir():
        executables.update(path.name for path in linux_lib_dir.iterdir() if path.is_file())
    return executables


def find_iscc(explicit: Path | None) -> Path:
    candidates = [explicit, Path(os.environ[ISCC_ENVIRONMENT]) if os.environ.get(ISCC_ENVIRONMENT) else None]
    candidates += [Path(found) for found in (shutil.which(name) for name in ISCC_NAMES) if found]
    for candidate in candidates:
        if candidate is not None and candidate.is_file():
            return candidate
    raise PackagingError(f"Inno Setup's ISCC was not found; pass --iscc or set {ISCC_ENVIRONMENT}")


def build_windows_wizard(bin_dir: Path, version: str, output: Path, iscc: Path) -> Path:
    installer = output / installer_name(version, "windows")
    with tempfile.TemporaryDirectory() as scratch:
        stage = Path(scratch) / "hub"
        stage.mkdir()
        stage_payload(bin_dir, "windows", stage)
        hub = f"{HUB_STEM}{EXECUTABLE_SUFFIX['windows']}"
        defines = {
            "AppId": WINDOWS_APP_ID,
            "AppName": HUB_DISPLAY_NAME,
            "AppVersion": display_version(version),
            "NumericVersion": numeric_version(version),
            "Publisher": PUBLISHER,
            "Description": HUB_DESCRIPTION,
            "InstallFolder": HUB_INSTALL_FOLDER,
            "HubExe": hub,
            "IconFile": str(BRAND_DIR / WINDOWS_ICON_FILE),
            "WindowsIcon": f"{ICONS_DIRECTORY}\\{WINDOWS_ICON_FILE}",
            "ProjectExtension": PROJECT_EXTENSION,
            "ProgramId": PROJECT_PROGRAM_ID,
            "ProjectTypeName": PROJECT_TYPE_NAME,
            "StageDir": str(stage),
            "OutputDir": str(output.resolve()),
            "OutputName": installer.stem,
        }
        subprocess.run(
            [str(iscc), "/Qp", *(f"/D{name}={value}" for name, value in defines.items()), str(WINDOWS_INSTALLER_SCRIPT)],
            check=True,
        )
    if not installer.is_file():
        raise PackagingError(f"ISCC did not write {installer}")
    return installer


def build_installer(
    bin_dir: Path,
    target: str,
    tag: str,
    output: Path,
    check_version: bool = True,
    iscc: Path | None = None,
) -> Path:
    version = parse_hub_tag(tag)
    suffix = EXECUTABLE_SUFFIX[target]
    if check_version:
        reported = built_version(bin_dir / f"{HUB_STEM}{suffix}")
        if reported != version:
            raise PackagingError(f"tag {tag} names Hub {version}, but the build is Hub {reported}")
    output.mkdir(parents=True, exist_ok=True)
    if target == "windows":
        installer = build_windows_wizard(bin_dir, version, output, find_iscc(iscc))
        write_checksum(installer)
        return installer
    setup = bin_dir / f"{SETUP_STEM}{suffix}"
    if not setup.is_file():
        raise PackagingError(f"missing executable {setup}")
    installer = output / installer_name(version, target)
    with tempfile.TemporaryDirectory() as scratch:
        stage = Path(scratch) / "hub"
        stage.mkdir()
        executables = stage_payload(bin_dir, target, stage)
        payload = Path(scratch) / "payload.tar.gz"
        write_archive(stage, payload, executables)
        data = payload.read_bytes()
    with installer.open("wb") as handle:
        handle.write(setup.read_bytes())
        handle.write(data)
        handle.write(hashlib.sha256(data).digest())
        handle.write(struct.pack(SIZE_FORMAT, len(data)))
        handle.write(PAYLOAD_MAGIC)
    installer.chmod(EXECUTABLE_MODE)
    write_checksum(installer)
    return installer


def deb_installer_name(version: str, architecture: str = DEB_ARCHITECTURE) -> str:
    return f"UniVexHub-{display_version(version)}-linux-{architecture}.deb"


def deb_package_version(version: str) -> str:
    core, _, build = version.partition("+build.")
    return core if build in ("", "0") else f"{core}.0.{build}"


def build_linux_deb(bin_dir: Path, version: str, output: Path, architecture: str = DEB_ARCHITECTURE) -> Path:
    dpkg_deb = shutil.which("dpkg-deb")
    if dpkg_deb is None:
        raise PackagingError("dpkg-deb is needed to build the Linux .deb installer")
    output.mkdir(parents=True, exist_ok=True)
    deb_path = output / deb_installer_name(version, architecture)
    display_ver = display_version(version)
    deb_ver = deb_package_version(version)
    with tempfile.TemporaryDirectory() as scratch:
        pkg_root = Path(scratch) / "pkg"
        opt_dir = pkg_root / DEB_INSTALL_ROOT
        opt_dir.mkdir(parents=True)
        executables = stage_payload(bin_dir, "linux", opt_dir)
        for path in opt_dir.rglob("*"):
            if path.is_dir():
                path.chmod(DIRECTORY_MODE)
            elif path.is_file():
                path.chmod(EXECUTABLE_MODE if path.name in executables else FILE_MODE)

        usr_bin = pkg_root / "usr" / "bin"
        usr_bin.mkdir(parents=True)
        (usr_bin / HUB_STEM).symlink_to(f"/{DEB_INSTALL_ROOT}/{HUB_STEM}")

        apps_dir = pkg_root / "usr" / "share" / "applications"
        apps_dir.mkdir(parents=True)
        desktop_entry = (
            "[Desktop Entry]\n"
            "Type=Application\n"
            f"Name={HUB_DISPLAY_NAME}\n"
            f"Comment={HUB_DESCRIPTION}\n"
            "Version=1.5\n"
            f"X-UniVex-Version={display_ver}\n"
            f"Exec=/{DEB_INSTALL_ROOT}/{HUB_STEM} %f\n"
            f"Path=/{DEB_INSTALL_ROOT}\n"
            f"Icon={DEB_ICON_NAME}\n"
            f"StartupWMClass={HUB_DISPLAY_NAME}\n"
            f"MimeType={DEB_MIME_TYPE};\n"
            "Terminal=false\n"
            "Categories=Development;IDE;\n"
        )
        desktop_file = apps_dir / "univex-hub.desktop"
        desktop_file.write_text(desktop_entry, encoding="utf-8")
        desktop_file.chmod(FILE_MODE)

        mime_dir = pkg_root / "usr" / "share" / "mime" / "packages"
        mime_dir.mkdir(parents=True)
        mime_xml = (
            '<?xml version="1.0" encoding="UTF-8"?>\n'
            '<mime-info xmlns="http://www.freedesktop.org/standards/shared-mime-info">\n'
            f'  <mime-type type="{DEB_MIME_TYPE}">\n'
            f"    <comment>{PROJECT_TYPE_NAME}</comment>\n"
            f'    <icon name="{DEB_MIME_ICON_NAME}"/>\n'
            f'    <glob pattern="*{PROJECT_EXTENSION}"/>\n'
            "  </mime-type>\n"
            "</mime-info>\n"
        )
        mime_file = mime_dir / "univex-project.xml"
        mime_file.write_text(mime_xml, encoding="utf-8")
        mime_file.chmod(FILE_MODE)

        for size in DEB_ICON_SIZES:
            src_png = BRAND_DIR / BRAND_PNG_DIRECTORY / f"univex-logo-{size}.png"
            if not src_png.is_file():
                continue
            for ctx, icon_stem in (("apps", DEB_ICON_NAME), ("mimetypes", DEB_MIME_ICON_NAME)):
                dst_dir = pkg_root / "usr" / "share" / "icons" / "hicolor" / f"{size}x{size}" / ctx
                dst_dir.mkdir(parents=True, exist_ok=True)
                dst_png = dst_dir / f"{icon_stem}.png"
                shutil.copy2(src_png, dst_png)
                dst_png.chmod(FILE_MODE)

        installed_kb = max(1, sum(p.stat().st_size for p in pkg_root.rglob("*") if p.is_file()) // 1024)
        debian_dir = pkg_root / "DEBIAN"
        debian_dir.mkdir()
        debian_dir.chmod(DIRECTORY_MODE)
        control = (
            f"Package: {DEB_PACKAGE_NAME}\n"
            f"Version: {deb_ver}\n"
            "Section: devel\n"
            "Priority: optional\n"
            f"Architecture: {architecture}\n"
            f"Installed-Size: {installed_kb}\n"
            "Depends: libc6, libgl1, libx11-6, libcurl4 | libcurl4t64\n"
            f"Maintainer: {DEB_MAINTAINER}\n"
            f"Description: {HUB_DISPLAY_NAME}\n"
            f" {HUB_DESCRIPTION}.\n"
        )
        control_file = debian_dir / "control"
        control_file.write_text(control, encoding="utf-8")
        control_file.chmod(FILE_MODE)

        refresh_script = (
            "#!/bin/sh\n"
            "set -e\n"
            "command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database -q /usr/share/applications || true\n"
            "command -v update-mime-database >/dev/null 2>&1 && update-mime-database /usr/share/mime || true\n"
            "command -v gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache -q -t -f /usr/share/icons/hicolor || true\n"
            "exit 0\n"
        )
        for script_name in ("postinst", "postrm"):
            script_path = debian_dir / script_name
            script_path.write_text(refresh_script, encoding="utf-8")
            script_path.chmod(EXECUTABLE_MODE)

        subprocess.run([dpkg_deb, "--root-owner-group", "--build", str(pkg_root), str(deb_path)], check=True)
    write_checksum(deb_path)
    return deb_path


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--bin", required=True, type=Path, help="Build output directory containing uve_hub")
    parser.add_argument("--target", required=True, choices=sorted(EXECUTABLE_SUFFIX))
    parser.add_argument("--tag", required=True, help="Hub release tag, e.g. hub-v0.1.1")
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--skip-version-check", action="store_true")
    parser.add_argument("--deb", action="store_true", help="Also build a Linux .deb package")
    parser.add_argument("--iscc", type=Path, help=f"Inno Setup ISCC path (default: ${ISCC_ENVIRONMENT} or PATH)")
    args = parser.parse_args(argv)
    try:
        installer = build_installer(
            args.bin, args.target, args.tag, args.output, not args.skip_version_check, args.iscc
        )
        print(installer)
        if args.deb:
            if args.target != "linux":
                raise PackagingError("--deb is only supported with --target linux")
            deb = build_linux_deb(args.bin, parse_hub_tag(args.tag), args.output)
            print(deb)
    except (PackagingError, subprocess.CalledProcessError) as error:
        print(f"make_hub_installer: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
