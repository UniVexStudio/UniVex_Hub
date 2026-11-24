cmake_minimum_required(VERSION 3.24)

project(UniVexHub LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(UNIVEX_HUB_STATIC_UI "Build and link static GLFW and FreeType on Linux (for portable uve_hub_setup)" OFF)
option(UNIVEX_HUB_BUILD_TESTS "Build UniVex Hub C++ unit tests" ON)
option(UNIVEX_HUB_WERROR "Treat compiler warnings as errors on Hub targets" OFF)

if(ANDROID)
    set(UNIVEX_HUB_BUILD_TESTS OFF CACHE BOOL "" FORCE)
endif()

include(FetchContent)

# ---------------------------------------------------------------------------
# Third-party dependencies (pinned to UniVex tags)
# ---------------------------------------------------------------------------
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nlohmann_json)

if(ANDROID)
    # Android only needs GLFW headers (src/android/android_glfw.cpp implements the GLFW API).
    FetchContent_Declare(
        glfw
        URL https://github.com/glfw/glfw/archive/refs/tags/3.4.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_GetProperties(glfw)
    if(NOT glfw_POPULATED)
        FetchContent_Populate(glfw)
    endif()
    add_library(hub_glfw_headers INTERFACE)
    target_include_directories(hub_glfw_headers INTERFACE "${glfw_SOURCE_DIR}/include")

    # FreeType built statically for Android with optional third-party codecs disabled.
    FetchContent_Declare(
        freetype
        URL https://github.com/freetype/freetype/archive/refs/tags/VER-2-13-3.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(freetype)
    set(HUB_FREETYPE_TARGET freetype)
elseif(UNIX AND NOT APPLE AND UNIVEX_HUB_STATIC_UI)
    # Portable single-binary Linux setup build: static GLFW (X11) and minimal static FreeType.
    FetchContent_Declare(
        glfw
        URL https://github.com/glfw/glfw/archive/refs/tags/3.4.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_X11 ON CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(glfw)
    set(HUB_GLFW_TARGET glfw)

    FetchContent_Declare(
        freetype
        URL https://github.com/freetype/freetype/archive/refs/tags/VER-2-13-3.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(freetype)
    set(HUB_FREETYPE_TARGET freetype)
else()
    find_package(PkgConfig QUIET)
    find_package(glfw3 QUIET)
    if(TARGET glfw)
        set(HUB_GLFW_TARGET glfw)
    elseif(TARGET glfw3)
        set(HUB_GLFW_TARGET glfw3)
    elseif(PkgConfig_FOUND)
        pkg_check_modules(GLFW3 REQUIRED IMPORTED_TARGET glfw3)
        set(HUB_GLFW_TARGET PkgConfig::GLFW3)
    else()
        message(FATAL_ERROR "GLFW3 was not found. Install libglfw3-dev / mingw-w64-ucrt-x86_64-glfw or pass -DUNIVEX_HUB_STATIC_UI=ON.")
    endif()

    find_package(Freetype REQUIRED)
    set(HUB_FREETYPE_TARGET Freetype::Freetype)
endif()

if(NOT ANDROID)
    find_package(ZLIB REQUIRED)
    find_package(OpenGL REQUIRED)
    if(UNIX AND NOT APPLE)
        find_package(CURL REQUIRED)
        find_package(X11 REQUIRED)
        find_package(Threads REQUIRED)
    endif()
endif()

# ---------------------------------------------------------------------------
# Output layout: stage binaries alongside prism_assets, project_templates, and engine_feed.json
# ---------------------------------------------------------------------------
set(HUB_BIN_DIR "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${HUB_BIN_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${HUB_BIN_DIR}")

# ---------------------------------------------------------------------------
# Compiler flags helper
# ---------------------------------------------------------------------------
function(hub_configure_warnings target_name)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target_name} PRIVATE -Wall -Wextra)
        if(UNIVEX_HUB_WERROR)
            target_compile_options(${target_name} PRIVATE -Werror)
        endif()
    elseif(MSVC)
        target_compile_options(${target_name} PRIVATE /W4)
        if(UNIVEX_HUB_WERROR)
            target_compile_options(${target_name} PRIVATE /WX)
        endif()
    endif()
    if(ANDROID)
        target_compile_options(${target_name} PRIVATE
            -fvisibility=hidden
            -fvisibility-inlines-hidden
            -ffunction-sections
            -fdata-sections
        )
    endif()
endfunction()

# ---------------------------------------------------------------------------
# MinGW Windows UIAutomationCore import library for PRISM
# ---------------------------------------------------------------------------
if(WIN32 AND MINGW)
    find_program(HUB_DLLTOOL NAMES dlltool x86_64-w64-mingw32-dlltool REQUIRED)
    set(HUB_UIA_IMPLIB "${CMAKE_BINARY_DIR}/libuiautomationcore.a")
    add_custom_command(
        OUTPUT "${HUB_UIA_IMPLIB}"
        COMMAND "${HUB_DLLTOOL}" -d "${CMAKE_CURRENT_SOURCE_DIR}/src/prism/windows/uiautomationcore.def" -l "${HUB_UIA_IMPLIB}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/prism/windows/uiautomationcore.def"
        COMMENT "Generating libuiautomationcore.a import library for PRISM"
    )
    add_custom_target(hub_uia_implib DEPENDS "${HUB_UIA_IMPLIB}")
endif()

# ---------------------------------------------------------------------------
# 1. Trimmed standalone PRISM UI library
# ---------------------------------------------------------------------------
file(GLOB PRISM_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/prism/src/*.cpp")
add_library(hub_prism STATIC ${PRISM_SOURCES})
target_include_directories(hub_prism
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src/prism/include"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/prism/gl"
)
target_compile_definitions(hub_prism PUBLIC PRISM_HAS_GL=1)
target_link_libraries(hub_prism PUBLIC ${HUB_FREETYPE_TARGET})
if(WIN32)
    if(MINGW)
        add_dependencies(hub_prism hub_uia_implib)
        target_link_libraries(hub_prism PUBLIC "${HUB_UIA_IMPLIB}" ole32 oleaut32)
    else()
        target_link_libraries(hub_prism PUBLIC uiautomationcore ole32 oleaut32)
    endif()
endif()
if(ANDROID)
    target_compile_options(hub_prism PRIVATE -fvisibility=hidden -fvisibility-inlines-hidden -ffunction-sections -fdata-sections)
endif()

# ---------------------------------------------------------------------------
# 2. Core Hub logic library (common, platform, pack, online) — headless & testable
# ---------------------------------------------------------------------------
set(HUB_CORE_SOURCES
    src/common/asset_guid.cpp
    src/common/engine_version.cpp
    src/common/logging.cpp
    src/common/png_metadata.cpp
    src/platform/application_runtime.cpp
    src/platform/application_window_settings.cpp
    src/platform/editor_project_package.cpp
    src/platform/process_launcher.cpp
    src/pack/engine_install.cpp
    src/pack/project_compatibility.cpp
    src/pack/project_list.cpp
    src/pack/project_registry.cpp
    src/pack/project_template.cpp
    src/online/engine_library.cpp
    src/online/engine_release_feed.cpp
    src/online/hub_setup.cpp
    src/online/sha256.cpp
    src/online/tar_gz_archive.cpp
)
if(ANDROID)
    list(APPEND HUB_CORE_SOURCES src/online/http_client_android.cpp)
elseif(WIN32)
    list(APPEND HUB_CORE_SOURCES src/online/http_client_winhttp.cpp)
else()
    list(APPEND HUB_CORE_SOURCES src/online/http_client_curl.cpp)
endif()

add_library(hub_core STATIC ${HUB_CORE_SOURCES})
target_include_directories(hub_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/contracts/v1"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/common"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/platform"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/pack"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/online"
)
target_link_libraries(hub_core PUBLIC nlohmann_json::nlohmann_json)
if(ANDROID)
    target_link_libraries(hub_core PUBLIC z log)
elseif(WIN32)
    target_link_libraries(hub_core PUBLIC ZLIB::ZLIB winhttp shell32 ole32 uuid advapi32)
else()
    target_link_libraries(hub_core PUBLIC ZLIB::ZLIB CURL::libcurl Threads::Threads)
endif()
hub_configure_warnings(hub_core)

# ---------------------------------------------------------------------------
# 3. UI host & shared launcher widgets (host, launcher_ui, optional android host)
# ---------------------------------------------------------------------------
set(HUB_UI_SOURCES
    src/host/editor_host.cpp
    src/host/hub_window_host.cpp
    src/launcher_ui/folder_picker.cpp
    src/launcher_ui/launcher_files.cpp
    src/launcher_ui/launcher_widgets.cpp
    src/launcher_ui/launcher_window.cpp
)
if(ANDROID)
    list(APPEND HUB_UI_SOURCES
        src/android/android_activity.cpp
        src/android/android_glfw.cpp
        src/android/android_host.cpp
        src/android/android_window_manager.cpp
    )
endif()

add_library(hub_ui STATIC ${HUB_UI_SOURCES})
target_include_directories(hub_ui PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/host"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/launcher_ui"
)
if(ANDROID)
    target_include_directories(hub_ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src/android"
        "${ANDROID_NDK}/sources/android/native_app_glue"
    )
    target_link_libraries(hub_ui PUBLIC hub_core hub_prism hub_glfw_headers android log EGL GLESv3)
else()
    target_link_libraries(hub_ui PUBLIC hub_core hub_prism ${HUB_GLFW_TARGET} OpenGL::GL)
    if(WIN32)
        target_link_libraries(hub_ui PUBLIC gdi32 ole32)
    elseif(UNIX AND NOT APPLE)
        target_link_libraries(hub_ui PUBLIC ${X11_LIBRARIES} dl)
    endif()
endif()
hub_configure_warnings(hub_ui)

# ---------------------------------------------------------------------------
# 4. Application targets
# ---------------------------------------------------------------------------
if(ANDROID)
    # Android NativeActivity glue
    add_library(android_native_app_glue STATIC
        "${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c"
    )
    target_include_directories(android_native_app_glue PUBLIC
        "${ANDROID_NDK}/sources/android/native_app_glue"
    )
    target_compile_options(android_native_app_glue PRIVATE -fPIC -ffunction-sections -fdata-sections)

    # libunivex_hub.so: strictly isolated ELF shared library loaded by UniVexActivity
    add_library(univex_hub SHARED
        src/android/hub_main.cpp
        src/hub/hub_project_opener.cpp
        src/hub/hub_window.cpp
    )
    target_include_directories(univex_hub PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/hub")
    target_link_libraries(univex_hub PRIVATE hub_ui android_native_app_glue)
    target_link_options(univex_hub PRIVATE
        -static-libstdc++
        -Wl,-u,ANativeActivity_onCreate
        -Wl,--no-undefined
        -Wl,-Bsymbolic
        "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/src/android/export.map"
        -Wl,-z,max-page-size=16384
        -Wl,--gc-sections
    )
    hub_configure_warnings(univex_hub)
else()
    # Desktop UniVex Hub (uve_hub)
    set(HUB_APP_SOURCES
        src/hub/main.cpp
        src/hub/hub_project_opener.cpp
        src/hub/hub_window.cpp
    )
    if(WIN32)
        list(APPEND HUB_APP_SOURCES src/hub/hub.rc)
    endif()
    add_executable(uve_hub ${HUB_APP_SOURCES})
    target_include_directories(uve_hub PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/hub"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/brand"
    )
    target_link_libraries(uve_hub PRIVATE hub_ui)
    if(WIN32 AND MINGW)
        target_link_options(uve_hub PRIVATE -mwindows)
    elseif(UNIX AND NOT APPLE)
        target_link_options(uve_hub PRIVATE "-Wl,-rpath,\$ORIGIN/lib:\$ORIGIN" -Wl,--disable-new-dtags)
    endif()
    hub_configure_warnings(uve_hub)

    # Desktop UniVex Hub Setup (uve_hub_setup)
    set(SETUP_APP_SOURCES
        src/setup/main.cpp
        src/setup/setup_window.cpp
    )
    if(WIN32)
        list(APPEND SETUP_APP_SOURCES src/setup/setup.rc)
    endif()
    add_executable(uve_hub_setup ${SETUP_APP_SOURCES})
    target_include_directories(uve_hub_setup PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/setup"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/brand"
    )
    target_link_libraries(uve_hub_setup PRIVATE hub_ui)
    if(WIN32 AND MINGW)
        target_link_options(uve_hub_setup PRIVATE -mwindows)
    elseif(UNIX AND NOT APPLE)
        target_link_options(uve_hub_setup PRIVATE "-Wl,-rpath,\$ORIGIN/lib:\$ORIGIN" -Wl,--disable-new-dtags)
    endif()
    hub_configure_warnings(uve_hub_setup)

    # Stage runtime assets beside uve_hub / uve_hub_setup
    add_custom_command(TARGET uve_hub POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/prism_assets"
            "${HUB_BIN_DIR}/prism_assets"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/templates"
            "${HUB_BIN_DIR}/project_templates"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/engine_feed.json"
            "${HUB_BIN_DIR}/engine_feed.json"
        COMMENT "Staging Hub runtime assets into ${HUB_BIN_DIR}"
    )

    # -----------------------------------------------------------------------
    # 5. C++ Unit & Contract Tests (GoogleTest)
    # -----------------------------------------------------------------------
    if(UNIVEX_HUB_BUILD_TESTS)
        enable_testing()
        FetchContent_Declare(
            googletest
            URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)

        add_executable(uve_hub_tests
            tests/Support/test_scratch.cpp
            tests/android_abi_contract_tests.cpp
            tests/application_runtime_tests.cpp
            tests/editor_project_package_tests.cpp
            tests/hub_setup_tests.cpp
            tests/online_tests.cpp
            tests/project_compatibility_tests.cpp
            tests/project_list_tests.cpp
            tests/project_registry_tests.cpp
            tests/project_template_tests.cpp
        )
        target_include_directories(uve_hub_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests")
        target_compile_definitions(uve_hub_tests PRIVATE UVE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
        target_link_libraries(uve_hub_tests PRIVATE hub_core GTest::gtest_main)
        hub_configure_warnings(uve_hub_tests)

        include(GoogleTest)
        gtest_discover_tests(uve_hub_tests)
    endif()
endif()
