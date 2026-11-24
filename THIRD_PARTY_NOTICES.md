# Third-Party Notices

UniVex Hub includes, links against, or bundles the third-party open-source libraries, fonts, and runtime components listed below. Each component remains under its own license.

## Libraries fetched by CMake

| Component | Version / Tag | License | Upstream |
| --- | --- | --- | --- |
| nlohmann/json | `v3.11.3` | MIT | <https://github.com/nlohmann/json> |
| GLFW | `3.4` | zlib/libpng | <https://github.com/glfw/glfw> |
| FreeType | `VER-2-13-3` | FreeType License (FTL) | <https://github.com/freetype/freetype> |
| GoogleTest (tests only) | `v1.15.2` | BSD-3-Clause | <https://github.com/google/googletest> |

Required attribution for FreeType (FTL):

- **FreeType (FTL):** Portions of this software are copyright © The FreeType Project (<https://www.freetype.org>). All rights reserved.

## Bundled fonts

| Component | Repository Path | License | License File |
| --- | --- | --- | --- |
| Inter (Regular, Medium, SemiBold) | `assets/prism_assets/fonts/Inter-*.otf` | SIL Open Font License 1.1 | [assets/prism_assets/fonts/LICENSE-Inter.txt](assets/prism_assets/fonts/LICENSE-Inter.txt) |
| DejaVu Sans Mono | `assets/prism_assets/fonts/DejaVuSansMono.ttf` | Bitstream Vera / DejaVu License | [assets/prism_assets/fonts/LICENSE-DejaVu.txt](assets/prism_assets/fonts/LICENSE-DejaVu.txt) |

## System and toolchain runtime libraries

- **zlib** (<https://github.com/madler/zlib>): zlib license. Linked from the host system on Linux, the Android NDK on Android, and MSYS2 on Windows.
- **libcurl** (<https://curl.se/docs/copyright.html>): curl license (MIT/X derivative). Used on Linux for Hub HTTPS requests.
- **MinGW-w64 / GCC UCRT64 runtime DLLs** (`libstdc++-6.dll`, `libgcc_s_seh-1.dll`, `libwinpthread-1.dll`): GNU GPLv3 with the GCC Runtime Library Exception (<https://gcc.gnu.org/onlinedocs/libstdc++/manual/license.html>) and MIT/BSD for `winpthreads`. Bundled in Windows installers.
- **Android NDK Native App Glue** (`android_native_app_glue`): Apache-2.0 (<https://developer.android.com/ndk>). Linked into Android builds.
