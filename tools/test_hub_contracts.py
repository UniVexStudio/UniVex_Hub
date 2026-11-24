#!/usr/bin/env python3
# Copyright (c) 2026 UniVex Studios. All Rights Reserved.
"""Verifies all Hub <-> Engine v1 contracts across C++ headers, Python tools, JSON assets, and Android manifests."""

from __future__ import annotations

import json
import re
import unittest
from pathlib import Path

import build_apk
import make_hub_installer

REPO_ROOT = Path(__file__).resolve().parents[1]

HEADERS = (
    "src/online/engine_release_feed.h",
    "src/online/engine_library.h",
    "src/online/hub_setup.h",
    "src/pack/engine_install.h",
    "src/pack/project_template.h",
    "src/launcher_ui/launcher_window.h",
    "src/platform/editor_project_package.h",
)

CONSTANT_PATTERN = re.compile(
    r"inline\s+constexpr\s+[\w:<>]+\s+(k[A-Z]\w*)\s*=\s*(\"(?:[^\"\\]|\\.)*\"|\d+U?|k[A-Z]\w*)\s*;"
)


def read_cpp_constants() -> dict[str, object]:
    raw: dict[str, str] = {}
    for header in HEADERS:
        text = (REPO_ROOT / header).read_text(encoding="utf-8")
        for name, value in CONSTANT_PATTERN.findall(text):
            raw[name] = value

    def resolve(name: str, seen: frozenset[str] = frozenset()) -> object:
        if name in seen:
            raise ValueError(f"{name} refers to itself")
        value = raw[name]
        if value.startswith('"'):
            return json.loads(value)
        if value.startswith("k"):
            return resolve(value, seen | {name})
        return int(value.rstrip("U"))

    return {name: resolve(name) for name in raw}


class HubContractsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.cpp = read_cpp_constants()

    def assert_cpp(self, name: str, expected: object) -> None:
        self.assertIn(name, self.cpp, f"{name} missing from Hub headers")
        self.assertEqual(self.cpp[name], expected, f"{name} drifted between C++ and contract/tools")

    def test_contract_1_engine_feed_and_package_names(self) -> None:
        self.assert_cpp("kEngineFeedFileName", make_hub_installer.FEED_FILE)
        self.assert_cpp("kEnginePackagePrefix", "univex-engine-")
        self.assert_cpp("kEnginePackageExtension", ".uvpackage")
        self.assert_cpp("kEngineChecksumSuffix", ".sha256")
        self.assert_cpp("kEngineManifestFileName", "engine.json")
        self.assert_cpp("kEngineManifestFormat", "uvengine")
        self.assert_cpp("kCurrentEngineManifestSchemaVersion", 1)

        feed_path = REPO_ROOT / "assets" / "engine_feed.json"
        feed = json.loads(feed_path.read_text(encoding="utf-8"))
        self.assertEqual(feed["format"], self.cpp["kEngineFeedFormat"])
        self.assertEqual(feed["schemaVersion"], self.cpp["kCurrentEngineFeedSchemaVersion"])
        self.assertTrue(feed["owner"] and feed["repository"])

    def test_contract_2_project_templates_and_assets(self) -> None:
        self.assert_cpp("kPrismAssetsDirectoryName", make_hub_installer.PRISM_ASSETS_DIRECTORY)
        self.assert_cpp("kProjectTemplatesDirectoryName", make_hub_installer.PROJECT_TEMPLATES_DIRECTORY)
        self.assertTrue((REPO_ROOT / "assets" / "prism_assets").is_dir())
        self.assertTrue((REPO_ROOT / "assets" / "templates").is_dir())
        for tmpl in (REPO_ROOT / "assets" / "templates").glob("*/template.json"):
            data = json.loads(tmpl.read_text(encoding="utf-8"))
            self.assertEqual(data["format"], "uvtemplate")
            self.assertEqual(data["schemaVersion"], 1)
        settings_cpp = (REPO_ROOT / "src" / "platform" / "application_window_settings.cpp").read_text(encoding="utf-8")
        smoke_py = (REPO_ROOT / "tools" / "smoke_hub.py").read_text(encoding="utf-8")
        for profile_id in ("compatibility", "mobile", "forward_plus"):
            self.assertIn(f'"{profile_id}"', settings_cpp)
        self.assertIn('"rendererProfile": "mobile"', smoke_py)

    def test_contract_3_installer_and_desktop_names(self) -> None:
        self.assert_cpp("kEditorExecutableStem", "uve_editor")
        self.assert_cpp("kHubExecutableStem", make_hub_installer.HUB_STEM)
        self.assert_cpp("kSetupExecutableStem", make_hub_installer.SETUP_STEM)
        self.assert_cpp("kSetupPayloadMagic", make_hub_installer.PAYLOAD_MAGIC.decode("ascii"))
        self.assert_cpp("kHubIconsDirectoryName", make_hub_installer.ICONS_DIRECTORY)
        self.assert_cpp("kHubWindowsIconFileName", make_hub_installer.WINDOWS_ICON_FILE)
        self.assert_cpp("kHubDisplayName", make_hub_installer.HUB_DISPLAY_NAME)
        self.assert_cpp("kHubDescription", make_hub_installer.HUB_DESCRIPTION)
        self.assert_cpp("kHubInstallFolderName", make_hub_installer.HUB_INSTALL_FOLDER)
        self.assert_cpp("kPublisher", make_hub_installer.PUBLISHER)
        self.assert_cpp("kEditorProjectFileExtension", make_hub_installer.PROJECT_EXTENSION)
        self.assert_cpp("kProjectProgramId", make_hub_installer.PROJECT_PROGRAM_ID)
        self.assert_cpp("kProjectTypeDisplayName", make_hub_installer.PROJECT_TYPE_NAME)

        brand_pngs = make_hub_installer.BRAND_DIR / make_hub_installer.BRAND_PNG_DIRECTORY
        self.assertTrue((brand_pngs / str(self.cpp["kHubIconFileName"])).is_file())

    def test_contract_4_android_abi_and_manifest(self) -> None:
        abi_header = (REPO_ROOT / "contracts" / "v2" / "univex_android_editor_abi.h").read_text(encoding="utf-8")
        self.assertIn('#define UVE_ANDROID_EDITOR_ABI_VERSION 2U', abi_header)
        self.assertIn('#define UVE_ANDROID_EDITOR_ENTRY_SYMBOL "UveEditorRun"', abi_header)
        self.assertIn('#define UVE_ANDROID_EDITOR_ABI_VERSION_SYMBOL "UveEditorAbiVersion"', abi_header)
        self.assert_cpp("kEditorAndroidEntryPoint", "UveEditorRun")

        manifest = build_apk.MANIFEST_PATH.read_text(encoding="utf-8")
        self.assertIn('package="com.univex.hub"', manifest)
        self.assertIn('android:screenOrientation="sensorLandscape"', manifest)
        for ref in re.findall(r'android:(?:round)?[iI]con="@mipmap/([\w]+)"', manifest):
            found = list(build_apk.RESOURCES_DIR.glob(f"mipmap-*/{ref}.*"))
            self.assertTrue(found, f"Missing mipmap resource {ref}")

        version = build_apk.read_hub_version()
        c1, n1 = build_apk.android_version_metadata(version, build_number=10)
        c2, n2 = build_apk.android_version_metadata(version, build_number=11)
        self.assertEqual(n1, n2)
        self.assertGreater(c2, c1)


if __name__ == "__main__":
    unittest.main()
