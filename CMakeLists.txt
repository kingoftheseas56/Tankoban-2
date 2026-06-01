cmake_minimum_required(VERSION 3.20)
project(Tankoban VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# TANKOBAN_BETA_DATA_NAMESPACE 2026-05-07 — when -DTANKOBAN_BETA=ON, the
# build sets the TANKOBAN_BETA_DATA_NAMESPACE preprocessor define so
# main.cpp registers QApplication under "TankobanBeta" instead of
# "Tankoban", isolating beta user data (%APPDATA%\TankobanBeta\) from
# the active dev tree's data dir. Defaults to OFF so dev builds remain
# unchanged.
option(TANKOBAN_BETA "Build a beta-flavored binary that uses an isolated user data dir" OFF)

# ── Qt6 ──────────────────────────────────────────────────────────────────────
find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Svg
    Sql
    Network
    OpenGL
    OpenGLWidgets
)

# Optional components — added as they become needed.
# One find_package per component: a single missing component would otherwise
# short-circuit the whole COMPONENTS call under QUIET and suppress later targets.
find_package(Qt6 QUIET OPTIONAL_COMPONENTS Multimedia)
find_package(Qt6 QUIET OPTIONAL_COMPONENTS WebEngineWidgets)
find_package(Qt6 QUIET OPTIONAL_COMPONENTS WebChannel)
find_package(Qt6 QUIET OPTIONAL_COMPONENTS WebSockets)
find_package(Qt6CorePrivate QUIET)
find_package(Qt6GuiPrivate QUIET)

# ── Sources ──────────────────────────────────────────────────────────────────
include(${CMAKE_SOURCE_DIR}/cmake/TankobanSources.cmake)

set(RESOURCES
    resources/resources.qrc
)

# ── Target ───────────────────────────────────────────────────────────────────
qt_add_executable(Tankoban
    ${SOURCES}
    ${HEADERS}
    ${RESOURCES}
)

target_include_directories(Tankoban PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(Tankoban PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::OpenGL
    Qt6::OpenGLWidgets
)

# QZipReader for CBZ support
if(TARGET Qt6::CorePrivate)
    target_link_libraries(Tankoban PRIVATE Qt6::CorePrivate)
    target_compile_definitions(Tankoban PRIVATE HAS_QT_ZIP=1)
endif()

if(TARGET Qt6::GuiPrivate)
    target_link_libraries(Tankoban PRIVATE Qt6::GuiPrivate)
endif()

# WebEngine for Foliate book reader
if(TARGET Qt6::WebEngineWidgets)
    target_link_libraries(Tankoban PRIVATE Qt6::WebEngineWidgets Qt6::WebChannel)
    target_compile_definitions(Tankoban PRIVATE HAS_WEBENGINE=1)
endif()

# WebSockets for Edge TTS direct WSS client (EDGE_TTS_FIX Phase 1+)
if(TARGET Qt6::WebSockets)
    target_link_libraries(Tankoban PRIVATE Qt6::WebSockets)
    target_compile_definitions(Tankoban PRIVATE HAS_WEBSOCKETS=1)
endif()

if(TARGET Qt6::Multimedia)
    target_link_libraries(Tankoban PRIVATE Qt6::Multimedia)
    target_compile_definitions(Tankoban PRIVATE HAS_MULTIMEDIA=1)
endif()

target_link_libraries(Tankoban PRIVATE Qt6::Svg)
target_link_libraries(Tankoban PRIVATE Qt6::Sql)

# TANKOBAN_BETA_DATA_NAMESPACE — see top-of-file option block.
if(TANKOBAN_BETA)
    target_compile_definitions(Tankoban PRIVATE TANKOBAN_BETA_DATA_NAMESPACE=1)
    message(STATUS "Tankoban: BETA build — user data namespaces to TankobanBeta")
endif()

# MAKE_MPV_BEAT_FFMPEG Task 1 (2026-05-02): main-app libplacebo + Vulkan build prep.
set(LIBPLACEBO_MSVC_ROOT "C:/tools/libplacebo-msvc" CACHE PATH "MSVC libplacebo install for Tankoban main app")
find_package(Vulkan REQUIRED)
find_path(LIBPLACEBO_MAIN_INCLUDE_DIR
    NAMES libplacebo/log.h libplacebo/renderer.h
    HINTS "${LIBPLACEBO_MSVC_ROOT}/include"
    REQUIRED
)
find_library(LIBPLACEBO_MAIN_LIBRARY
    NAMES placebo libplacebo
    HINTS "${LIBPLACEBO_MSVC_ROOT}/lib"
    REQUIRED
)
find_file(LIBPLACEBO_MAIN_RUNTIME
    NAMES libplacebo-362.dll libplacebo.dll placebo.dll
    HINTS "${LIBPLACEBO_MSVC_ROOT}/bin"
    REQUIRED
)
find_file(LCMS2_MAIN_RUNTIME
    NAMES lcms2-2.dll lcms2.dll
    HINTS "C:/vcpkg/installed/x64-windows/bin"
    REQUIRED
)
message(STATUS "main-app libplacebo found: ${LIBPLACEBO_MAIN_LIBRARY}")
target_include_directories(Tankoban PRIVATE "${LIBPLACEBO_MAIN_INCLUDE_DIR}")
target_compile_definitions(Tankoban PRIVATE HAS_LIBPLACEBO_MAIN=1)
target_link_libraries(Tankoban PRIVATE Vulkan::Vulkan "${LIBPLACEBO_MAIN_LIBRARY}")
if(WIN32)
    add_custom_command(TARGET Tankoban POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${LIBPLACEBO_MAIN_RUNTIME}"
            "$<TARGET_FILE_DIR:Tankoban>/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${LCMS2_MAIN_RUNTIME}"
            "$<TARGET_FILE_DIR:Tankoban>/"
        COMMENT "Copying main-app libplacebo runtime DLLs"
    )
endif()

# ── libtorrent-rasterbar + Boost + OpenSSL (vcpkg-managed) ───────────────────
# REPO_HYGIENE Phase 2 (2026-04-26): migrated from hardcoded
# C:/tools/libtorrent-2.0-msvc + C:/tools/boost-1.84.0 + C:/tools/openssl-msvc
# paths to vcpkg's find_package CONFIG style. setup.bat runs `vcpkg install`
# from the manifest at vcpkg.json, then cmake's CMAKE_TOOLCHAIN_FILE (set via
# CMakePresets.json's `default` preset) picks them up automatically.
#
# Pre-vcpkg fallback: users with custom libtorrent/Boost/OpenSSL builds outside
# vcpkg can still set -DCMAKE_PREFIX_PATH or -DLibtorrentRasterbar_DIR /
# -DBoost_DIR / -DOpenSSL_ROOT to point at their own configs. find_package's
# QUIET mode keeps the build green when any dep is missing — torrent engine
# disables and the Tankorent / Stream features unavailable but the rest of
# the app still compiles + runs.
find_package(LibtorrentRasterbar CONFIG QUIET)
find_package(Boost CONFIG QUIET COMPONENTS system filesystem)
find_package(OpenSSL QUIET)

if(LibtorrentRasterbar_FOUND AND Boost_FOUND AND OpenSSL_FOUND)
    message(STATUS "libtorrent + Boost + OpenSSL found via vcpkg/find_package")
    target_compile_definitions(Tankoban PRIVATE
        HAS_LIBTORRENT=1
        TORRENT_USE_OPENSSL
        BOOST_ALL_NO_LIB
        _WIN32_WINNT=0x0A00
    )
    target_link_libraries(Tankoban PRIVATE
        LibtorrentRasterbar::torrent-rasterbar
        Boost::system
        Boost::filesystem
        OpenSSL::SSL
        OpenSSL::Crypto
        ws2_32 mswsock crypt32 iphlpapi bcrypt
    )
else()
    # TANKORENT_DOWNLOAD_FLOW_FIX (2026-05-01): Phase 2's vcpkg migration silently
    # disabled the torrent engine when vcpkg failed to install libtorrent (libiconv
    # port build issue flagged in b1b9acf commit body). Restore the pre-Phase-2
    # hardcoded-path fallback so existing C:/tools/libtorrent-2.0-msvc installs
    # still work — Hemanth's case + anyone who has libtorrent installed outside
    # vcpkg. Either path defines HAS_LIBTORRENT; without it, addMagnet compiles
    # to a stub that returns empty hash for every magnet, surfacing as
    # "Failed to Add Magnet" on every download click.
    set(LIBTORRENT_ROOT "C:/tools/libtorrent-2.0-msvc" CACHE PATH "libtorrent MSVC install")
    set(BOOST_ROOT "C:/tools/boost-1.84.0" CACHE PATH "Boost headers")
    set(OPENSSL_MSVC_ROOT "C:/tools/openssl-msvc" CACHE PATH "OpenSSL MSVC install")

    find_path(LIBTORRENT_INCLUDE_DIR libtorrent/session.hpp HINTS "${LIBTORRENT_ROOT}/include")
    find_library(LIBTORRENT_LIBRARY torrent-rasterbar HINTS "${LIBTORRENT_ROOT}/lib")
    find_path(BOOST_INCLUDE_DIR boost/version.hpp HINTS "${BOOST_ROOT}")

    if(LIBTORRENT_INCLUDE_DIR AND LIBTORRENT_LIBRARY AND BOOST_INCLUDE_DIR)
        message(STATUS "libtorrent found via fallback: ${LIBTORRENT_LIBRARY}")
        target_include_directories(Tankoban PRIVATE
            "${LIBTORRENT_INCLUDE_DIR}"
            "${BOOST_INCLUDE_DIR}"
            "${OPENSSL_MSVC_ROOT}/include"
        )
        target_compile_definitions(Tankoban PRIVATE
            HAS_LIBTORRENT=1
            TORRENT_USE_OPENSSL
            BOOST_ALL_NO_LIB
            _WIN32_WINNT=0x0A00
        )
        target_link_libraries(Tankoban PRIVATE
            "${LIBTORRENT_LIBRARY}"
            "${OPENSSL_MSVC_ROOT}/lib/libssl.lib"
            "${OPENSSL_MSVC_ROOT}/lib/libcrypto.lib"
            ws2_32 mswsock crypt32 iphlpapi bcrypt
        )
    else()
        message(WARNING "libtorrent / Boost / OpenSSL NOT found via vcpkg OR fallback paths — torrent engine disabled. Run setup.bat to install via vcpkg, or place libtorrent at C:/tools/libtorrent-2.0-msvc/.")
    endif()
endif()

# ── libarchive (optional, for CBR/RAR support) ───────────────────────────────
set(LIBARCHIVE_ROOT "C:/tools/libarchive-msvc" CACHE PATH "libarchive install")
find_path(LIBARCHIVE_INCLUDE_DIR archive.h HINTS "${LIBARCHIVE_ROOT}/include")
find_library(LIBARCHIVE_LIBRARY archive HINTS "${LIBARCHIVE_ROOT}/lib")

if(LIBARCHIVE_INCLUDE_DIR AND LIBARCHIVE_LIBRARY)
    message(STATUS "libarchive found: ${LIBARCHIVE_LIBRARY}")
    target_include_directories(Tankoban PRIVATE "${LIBARCHIVE_INCLUDE_DIR}")
    target_compile_definitions(Tankoban PRIVATE HAS_LIBARCHIVE=1)
    target_link_libraries(Tankoban PRIVATE "${LIBARCHIVE_LIBRARY}")
else()
    message(STATUS "libarchive NOT found — CBR/RAR support disabled")
endif()

# MPV_CUTOVER 2026-05-05 — libmpv detection + linkage + DLL deployment removed.
# mpv backend retired in favor of single-backend ffmpeg sidecar architecture.
# libplacebo + Vulkan stack lives on in native_sidecar/ for the ffmpeg path.

# ── Windows-specific ─────────────────────────────────────────────────────────
if(WIN32)
    target_sources(Tankoban PRIVATE resources/tankoban.rc)
    # MAKE_MPV_SOLO Task 8.B (2026-05-02) — ole32 + propsys for the
    # Windows audio device-change watcher (IMMNotificationClient COM API
    # + PROPERTYKEY/PROPVARIANT helpers in PKEY_Device_FriendlyName lookup).
    target_link_libraries(Tankoban PRIVATE dwmapi winmm d3d11 dxgi d3dcompiler avrt ole32 propsys)
    set_target_properties(Tankoban PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
    # Incremental linking for faster link times during development
    target_link_options(Tankoban PRIVATE /INCREMENTAL)

    # THEATRE_DOWNLOAD_ONLY P2.3 (2026-05-29) — the Stremio stream-server
    # subprocess bundle (stremio-runtime.exe + server.js + ffmpeg 4.x DLLs +
    # LICENSE) and its POST_BUILD copy block were removed here. Theatre is
    # download-only now, so nothing spawns the subprocess and the ~88 MB
    # bundle no longer ships.

endif()

# ── In-Process Player POC (2026-06-01, Agent 3) ──────────────────────────────
# Phase 0 build-seam spike: prove FFmpeg's libavformat/libavcodec link into the
# main MSVC Tankoban target and run in-process. Entirely gated by
# TANKOBAN_INPROCESS_POC (default OFF) so normal builds are untouched. Phase 1+
# will add the sidecar decode/audio/sync sources + InProcessPlayer under the
# same gate. The .lib import files + runtime DLLs come from the SAME FFmpeg the
# sidecar already uses, so versions are consistent.
option(TANKOBAN_INPROCESS_POC "Build the in-process player POC path" OFF)
if(TANKOBAN_INPROCESS_POC)
    set(FFMPEG_POC_ROOT "C:/tools/ffmpeg-master-latest-win64-gpl-shared")
    target_sources(Tankoban PRIVATE src/ui/player/InProcessProbe.cpp)
    target_include_directories(Tankoban PRIVATE "${FFMPEG_POC_ROOT}/include")
    target_link_libraries(Tankoban PRIVATE
        "${FFMPEG_POC_ROOT}/lib/avformat.lib"
        "${FFMPEG_POC_ROOT}/lib/avcodec.lib"
        "${FFMPEG_POC_ROOT}/lib/avutil.lib"
        "${FFMPEG_POC_ROOT}/lib/swscale.lib"
        "${FFMPEG_POC_ROOT}/lib/swresample.lib")
    target_compile_definitions(Tankoban PRIVATE TANKOBAN_INPROCESS_POC=1)
    if(WIN32)
        foreach(dll avformat-62 avcodec-62 avutil-60 swscale-9 swresample-6)
            add_custom_command(TARGET Tankoban POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${FFMPEG_POC_ROOT}/bin/${dll}.dll" "$<TARGET_FILE_DIR:Tankoban>/"
                VERBATIM)
        endforeach()
    endif()
    message(STATUS "TANKOBAN_INPROCESS_POC ON -- in-process FFmpeg link enabled")
endif()

include(${CMAKE_SOURCE_DIR}/cmake/TankobanRuntimeAssets.cmake)

# ── Precompiled headers (cuts compile time for Qt/STL heavy includes) ────────
target_precompile_headers(Tankoban PRIVATE
    <QWidget>
    <QString>
    <QList>
    <QMap>
    <QHash>
    <QVariant>
    <QJsonObject>
    <QJsonArray>
    <QSettings>
    <QFileInfo>
    <QDir>
    <QTimer>
    <QVBoxLayout>
    <QHBoxLayout>
    <QLabel>
    <QPushButton>
    <QFrame>
    <memory>
    <algorithm>
    <functional>
)

# ── tankoctl console client (REPO_HYGIENE Phase 3) ──────────────────────────
# Companion exe to the dev-control bridge inside Tankoban.exe. Connects to the
# `TankobanDevControl` named pipe; one command per invocation. Console mode
# (WIN32_EXECUTABLE OFF) so stdout JSON can be piped / captured. Minimal Qt
# link surface (Core + Network) — no Gui, no Widgets.
add_executable(tankoctl tools/tankoctl.cpp tools/tankoctl_scenario.cpp)
set_target_properties(tankoctl PROPERTIES WIN32_EXECUTABLE OFF)
target_link_libraries(tankoctl PRIVATE Qt6::Core Qt6::Network)

include(${CMAKE_SOURCE_DIR}/cmake/TankobanTests.cmake)
