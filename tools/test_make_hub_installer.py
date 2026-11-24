#!/usr/bin/env python3
# Copyright (c) 2026 UniVex Studios. All Rights Reserved.
"""Unit tests for tools/make_hub_installer.py."""

from __future__ import annotations

import hashlib
import io
import os
import struct
import tarfile
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import make_hub_installer


def make_fake_bin(root: Path, target: str) -> Path:
    suffix = make_hub_installer.EXECUTABLE_SUFFIX[target]
    bin_dir = root / "bin"
    bin_dir.mkdir(parents=True)
    (bin_dir / f"{make_hub_installer.HUB_STEM}{suffix}").write_bytes(b"fake-hub-binary")
    (bin_dir / f"{make_hub_installer.SETUP_STEM}{suffix}").write_bytes(b"FAKE_SETUP_EXE")
    for directory in make_hub_installer.HUB_DIRECTORIES:
        d = bin_dir / directory
        d.mkdir()
        (d / "sample.txt").write_text("ok\n", encoding="utf-8")
    (bin_dir / make_hub_installer.FEED_FILE).write_text('{"format":"uvenginefeed","schemaVersion":1}\n', encoding="utf-8")
    return bin_dir


class MakeHubInstallerTest(unittest.TestCase):
    def test_linux_self_extracting_installer_layout(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            bin_dir = make_fake_bin(root, "linux")
            out_dir = root / "dist"
            installer = make_hub_installer.build_installer(
                bin_dir, "linux", "hub-v0.1.1", out_dir, check_version=False
            )
            self.assertEqual(installer.name, "UniVexHubSetup-0.1.1-linux")
            self.assertTrue(installer.is_file())
            self.assertTrue((out_dir / f"{installer.name}.sha256").is_file())

            raw = installer.read_bytes()
            magic_len = len(make_hub_installer.PAYLOAD_MAGIC)
            self.assertEqual(raw[-magic_len:], make_hub_installer.PAYLOAD_MAGIC)
            (payload_size,) = struct.unpack(
                make_hub_installer.SIZE_FORMAT, raw[-magic_len - 8 : -magic_len]
            )
            expected_digest = raw[-magic_len - 8 - 32 : -magic_len - 8]
            payload = raw[len(b"FAKE_SETUP_EXE") : len(b"FAKE_SETUP_EXE") + payload_size]
            self.assertEqual(hashlib.sha256(payload).digest(), expected_digest)

            with tarfile.open(fileobj=io.BytesIO(payload), mode="r:gz") as tar:
                names = set(tar.getnames())
            self.assertIn("uve_hub", names)
            self.assertIn("engine_feed.json", names)
            self.assertIn("icons/univex-logo-256.png", names)

    def test_windows_wizard_invokes_iscc(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            bin_dir = make_fake_bin(root, "windows")
            out_dir = root / "dist"
            iscc = root / "ISCC.exe"
            iscc.write_text("#!/bin/sh\n", encoding="utf-8")
            iscc.chmod(0o755)

            def fake_run(cmd, check=False):
                out_name = next(a.split("=", 1)[1] for a in cmd if a.startswith("/DOutputName="))
                out_path = next(a.split("=", 1)[1] for a in cmd if a.startswith("/DOutputDir="))
                (Path(out_path) / f"{out_name}.exe").write_bytes(b"MZ-fake-wizard")
                return mock.Mock(returncode=0)

            with mock.patch.object(make_hub_installer, "copy_windows_dlls"), mock.patch.object(
                make_hub_installer.subprocess, "run", side_effect=fake_run
            ) as run_mock:
                installer = make_hub_installer.build_installer(
                    bin_dir, "windows", "hub-v0.1.1", out_dir, check_version=False, iscc=iscc
                )
            self.assertEqual(installer.name, "UniVexHubSetup-0.1.1-windows.exe")
            self.assertTrue((out_dir / f"{installer.name}.sha256").is_file())
            self.assertEqual(run_mock.call_count, 1)

    def test_deb_package_version_and_name(self) -> None:
        self.assertEqual(make_hub_installer.deb_package_version("0.1.1+build.0"), "0.1.1")
        self.assertEqual(make_hub_installer.deb_package_version("0.1.1+build.4"), "0.1.1.0.4")
        self.assertEqual(make_hub_installer.deb_installer_name("0.1.1+build.0"), "UniVexHub-0.1.1-linux-amd64.deb")

    def test_parse_hub_tag_rejects_non_hub_tag(self) -> None:
        with self.assertRaises(make_hub_installer.PackagingError):
            make_hub_installer.parse_hub_tag("v0.1.1")
        self.assertEqual(make_hub_installer.parse_hub_tag("hub-v0.1.1-beta"), "0.1.1+build.0")


if __name__ == "__main__":
    unittest.main()
