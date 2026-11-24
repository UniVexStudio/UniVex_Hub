# UniVex Hub

Standalone repository for **UniVex Hub** — the desktop launcher (`uve_hub`), Linux/Windows installers (`uve_hub_setup`, `.deb`, Windows Inno Setup wizard), and Android Hub APK host (`com.univex.hub`).

UniVex Hub owns a trimmed C++23 PRISM UI snapshot, `.uvproject` / template management, engine feed downloads, and the Android `UniVexActivity` host with zero git submodule dependency on `UniVexStudio/UniVex_Engine`. Shared interfaces between the Hub and Engine are documented and tested under [`contracts/v1/`](contracts/v1/README.md).

## Building Locally (Linux)

```bash
# Configure and build Hub + unit tests
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/release -j"$(nproc)"
ctest --test-dir build/release --output-on-failure

# Build portable single-file Linux setup executable (static GLFW + FreeType)
cmake -S . -B build/setup-static -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DUNIVEX_HUB_STATIC_UI=ON -DUNIVEX_HUB_BUILD_TESTS=OFF
cmake --build build/setup-static --target uve_hub_setup -j"$(nproc)"
cp build/setup-static/bin/uve_hub_setup build/release/bin/uve_hub_setup

# Package Linux installer and .deb
hub_ver="$(./build/release/bin/uve_hub --version)"
python3 tools/make_hub_installer.py --bin build/release/bin --target linux \
  --tag "hub-v${hub_ver}" --output dist --deb
```

## Android Hub APK

```bash
python3 tools/build_apk.py --sdk "$ANDROID_HOME" --build-dir build/android --abi arm64-v8a
```

Pass `--keystore /path/to/release.keystore` with `UVE_ANDROID_KEYSTORE_PASSWORD` and `UVE_ANDROID_KEY_ALIAS` set to produce a release-signed APK. Never commit keystore files or base64 exports into git.

## Releases & GitHub Secrets

Hub releases (`hub-v*` tags and `main` prereleases) publish to this repository by default using `GITHUB_TOKEN`. Configure these repository secrets under **Settings -> Secrets and variables -> Actions**:

- `ANDROID_KEYSTORE`: base64-encoded release `.keystore` / `.jks` file
- `ANDROID_KEYSTORE_PASSWORD`: keystore password
- `ANDROID_KEY_ALIAS`: signing key alias

Optional repository variables:

- `HUB_RELEASES_REPOSITORY`: override release target repository (defaults to this repository; requires `RELEASES_TOKEN` secret with `Contents: write` if pointing to another repository)
- `ENGINE_RELEASES_REPOSITORIES`: comma-separated repositories scanned by Android CI smoke tests for the latest `android-x86_64.uvpackage` (default: `UniVexStudio/UniVexReleases,UniVexStudio/UniVexReleases-Dev`)

## License

UniVex Hub is licensed under the [UniVex Engine & Hub EULA v1.0](LICENSE). See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for third-party attributions.
