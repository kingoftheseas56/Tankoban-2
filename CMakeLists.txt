cmake_minimum_required(VERSION 3.20)
project(Tankoban VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ── Qt6 ──────────────────────────────────────────────────────────────────────
find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Svg
    Network
    OpenGL
    OpenGLWidgets
)

# Optional components — added as they become needed
find_package(Qt6 QUIET COMPONENTS Multimedia Svg WebEngineWidgets WebChannel)
find_package(Qt6CorePrivate QUIET)
find_package(Qt6GuiPrivate QUIET)
find_package(Qt6GuiPrivate QUIET)

# ── Sources ──────────────────────────────────────────────────────────────────
set(SOURCES
    src/main.cpp
    src/core/JsonStore.cpp
    src/core/CoreBridge.cpp
    src/core/LibraryScanner.cpp
    src/core/BooksScanner.cpp
    src/core/VideosScanner.cpp
    src/core/ScannerUtils.cpp
    src/core/ArchiveReader.cpp
    src/ui/MainWindow.cpp
    src/ui/GlassBackground.cpp
    src/ui/RootFoldersOverlay.cpp
    src/ui/ContextMenuHelper.cpp
    src/ui/widgets/FadingStackedWidget.cpp
    src/ui/widgets/LibraryListView.cpp
    src/ui/pages/ComicsPage.cpp
    src/ui/pages/BooksPage.cpp
    src/ui/pages/VideosPage.cpp
    src/ui/pages/SeriesView.cpp
    src/ui/pages/BookSeriesView.cpp
    src/ui/pages/TileCard.cpp
    src/ui/pages/TileStrip.cpp
    src/ui/pages/ShowView.cpp
    src/ui/readers/ComicReader.cpp
    src/ui/readers/BookReader.cpp
    src/ui/readers/BookBridge.cpp
    src/ui/readers/ScrollStripCanvas.cpp
    src/ui/readers/SmoothScrollArea.cpp
    src/ui/readers/DecodeTask.cpp
    src/ui/readers/PageCache.cpp
    src/core/EpubParser.cpp
    src/ui/pages/SourcesPage.cpp
    src/ui/pages/TankorentPage.cpp
    src/ui/pages/TankoyomiPage.cpp
    src/core/indexers/TorrentsCsvIndexer.cpp
    src/core/indexers/NyaaIndexer.cpp
    src/core/indexers/PirateBayIndexer.cpp
    src/core/indexers/YtsIndexer.cpp
    src/core/indexers/EztvIndexer.cpp
    src/core/indexers/X1337xIndexer.cpp
    src/core/indexers/ExtTorrentsIndexer.cpp
    src/ui/player/SidecarProcess.cpp
    src/ui/player/ShmFrameReader.cpp
    src/ui/player/FrameCanvas.cpp
    src/ui/player/VideoPlayer.cpp
    src/ui/player/VolumeHud.cpp
    src/ui/player/SeekSlider.cpp
    src/ui/player/KeyBindings.cpp
    src/ui/player/EqualizerPopover.cpp
    src/ui/player/CenterFlash.cpp
    src/ui/player/ToastHud.cpp
    src/ui/player/ShortcutsOverlay.cpp
    src/ui/player/PlaylistDrawer.cpp
    src/ui/player/SubtitleOverlay.cpp
    src/ui/player/TrackPopover.cpp
    src/ui/player/FilterPopover.cpp
    src/ui/player/VideoContextMenu.cpp
    src/core/torrent/TorrentEngine.cpp
    src/core/torrent/TorrentClient.cpp
    src/ui/dialogs/AddTorrentDialog.cpp
    src/ui/dialogs/SpeedLimitDialog.cpp
    src/ui/dialogs/SeedingRulesDialog.cpp
    src/ui/dialogs/QueueLimitsDialog.cpp
    src/ui/dialogs/TorrentFilesDialog.cpp
    src/ui/dialogs/HistoryDialog.cpp
    src/core/manga/WeebCentralScraper.cpp
    src/core/manga/ReadComicsScraper.cpp
    src/core/manga/MangaDownloader.cpp
    src/ui/dialogs/AddMangaDialog.cpp
    src/core/tts/KokoroTtsEngine.cpp
    src/core/stream/CinemetaClient.cpp
    src/core/stream/TorrentioClient.cpp
    src/core/stream/StreamHttpServer.cpp
    src/core/stream/StreamEngine.cpp
    src/core/stream/StreamLibrary.cpp
    src/ui/pages/StreamPage.cpp
    src/ui/pages/stream/StreamLibraryLayout.cpp
    src/ui/pages/stream/StreamSearchWidget.cpp
    src/ui/pages/stream/StreamDetailView.cpp
    src/ui/pages/stream/TorrentPickerDialog.cpp
    src/ui/pages/stream/StreamPlayerController.cpp
    src/ui/pages/stream/StreamContinueStrip.cpp
)

set(HEADERS
    src/core/JsonStore.h
    src/core/CoreBridge.h
    src/core/LibraryScanner.h
    src/core/BooksScanner.h
    src/core/VideosScanner.h
    src/core/ScannerUtils.h
    src/core/ArchiveReader.h
    src/ui/MainWindow.h
    src/ui/GlassBackground.h
    src/ui/RootFoldersOverlay.h
    src/ui/ContextMenuHelper.h
    src/ui/widgets/FadingStackedWidget.h
    src/ui/widgets/LibraryListView.h
    src/ui/pages/ComicsPage.h
    src/ui/pages/BooksPage.h
    src/ui/pages/VideosPage.h
    src/ui/pages/SeriesView.h
    src/ui/pages/BookSeriesView.h
    src/ui/pages/TileCard.h
    src/ui/pages/TileStrip.h
    src/ui/pages/ShowView.h
    src/ui/readers/ComicReader.h
    src/ui/readers/BookReader.h
    src/ui/readers/BookBridge.h
    src/ui/readers/SmoothScrollArea.h
    src/ui/readers/DecodeTask.h
    src/ui/readers/PageCache.h
    src/core/EpubParser.h
    src/ui/pages/SourcesPage.h
    src/ui/pages/TankorentPage.h
    src/ui/pages/TankoyomiPage.h
    src/core/TorrentResult.h
    src/core/TorrentIndexer.h
    src/core/indexers/TorrentsCsvIndexer.h
    src/core/indexers/NyaaIndexer.h
    src/core/indexers/PirateBayIndexer.h
    src/core/indexers/YtsIndexer.h
    src/core/indexers/EztvIndexer.h
    src/core/indexers/X1337xIndexer.h
    src/core/indexers/ExtTorrentsIndexer.h
    src/ui/player/SidecarProcess.h
    src/ui/player/ShmFrameReader.h
    src/ui/player/FrameCanvas.h
    src/ui/player/VideoPlayer.h
    src/ui/player/VolumeHud.h
    src/ui/player/SeekSlider.h
    src/ui/player/CenterFlash.h
    src/ui/player/ShortcutsOverlay.h
    src/ui/player/PlaylistDrawer.h
    src/ui/player/SubtitleOverlay.h
    src/ui/player/TrackPopover.h
    src/ui/player/FilterPopover.h
    src/ui/player/VideoContextMenu.h
    src/core/torrent/TorrentEngine.h
    src/core/torrent/TorrentClient.h
    src/ui/dialogs/AddTorrentDialog.h
    src/ui/dialogs/SpeedLimitDialog.h
    src/ui/dialogs/SeedingRulesDialog.h
    src/ui/dialogs/QueueLimitsDialog.h
    src/ui/dialogs/TorrentFilesDialog.h
    src/ui/dialogs/HistoryDialog.h
    src/core/manga/MangaResult.h
    src/core/manga/MangaScraper.h
    src/core/manga/WeebCentralScraper.h
    src/core/manga/ReadComicsScraper.h
    src/core/manga/MangaDownloader.h
    src/ui/dialogs/AddMangaDialog.h
    src/core/tts/KokoroTtsEngine.h
    src/core/stream/CinemetaClient.h
    src/core/stream/TorrentioClient.h
    src/core/stream/StreamHttpServer.h
    src/core/stream/StreamEngine.h
    src/core/stream/StreamLibrary.h
    src/core/stream/StreamProgress.h
    src/ui/pages/StreamPage.h
    src/ui/pages/stream/StreamLibraryLayout.h
    src/ui/pages/stream/StreamSearchWidget.h
    src/ui/pages/stream/StreamDetailView.h
    src/ui/pages/stream/TorrentPickerDialog.h
    src/ui/pages/stream/StreamPlayerController.h
    src/ui/pages/stream/StreamContinueStrip.h
)

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

if(TARGET Qt6::Multimedia)
    target_link_libraries(Tankoban PRIVATE Qt6::Multimedia)
    target_compile_definitions(Tankoban PRIVATE HAS_MULTIMEDIA=1)
endif()

target_link_libraries(Tankoban PRIVATE Qt6::Svg)

# ── libtorrent-rasterbar (optional) ──────────────────────────────────────────
set(LIBTORRENT_ROOT "C:/tools/libtorrent-2.0-msvc" CACHE PATH "libtorrent MSVC install")
set(BOOST_ROOT "C:/tools/boost-1.84.0" CACHE PATH "Boost headers")
set(OPENSSL_MSVC_ROOT "C:/tools/openssl-msvc" CACHE PATH "OpenSSL MSVC install")

find_path(LIBTORRENT_INCLUDE_DIR libtorrent/session.hpp HINTS "${LIBTORRENT_ROOT}/include")
find_library(LIBTORRENT_LIBRARY torrent-rasterbar HINTS "${LIBTORRENT_ROOT}/lib")
find_path(BOOST_INCLUDE_DIR boost/version.hpp HINTS "${BOOST_ROOT}")

if(LIBTORRENT_INCLUDE_DIR AND LIBTORRENT_LIBRARY AND BOOST_INCLUDE_DIR)
    message(STATUS "libtorrent found: ${LIBTORRENT_LIBRARY}")
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
    message(STATUS "libtorrent NOT found — torrent engine disabled")
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

# ── sherpa-onnx TTS (optional, for Kokoro TTS) ──────────────────────────────
set(SHERPA_ONNX_DIR "${CMAKE_SOURCE_DIR}/third_party/sherpa-onnx/sherpa-onnx-v1.12.21-win-x64-shared"
    CACHE PATH "sherpa-onnx prebuilt shared library")

find_path(SHERPA_ONNX_INCLUDE_DIR sherpa-onnx/c-api/c-api.h
    HINTS "${SHERPA_ONNX_DIR}/include")
find_library(SHERPA_ONNX_LIBRARY sherpa-onnx-c-api
    HINTS "${SHERPA_ONNX_DIR}/lib")

if(SHERPA_ONNX_INCLUDE_DIR AND SHERPA_ONNX_LIBRARY)
    message(STATUS "sherpa-onnx found: ${SHERPA_ONNX_LIBRARY}")
    target_include_directories(Tankoban PRIVATE "${SHERPA_ONNX_INCLUDE_DIR}")
    target_compile_definitions(Tankoban PRIVATE HAS_SHERPA_ONNX=1)
    target_link_libraries(Tankoban PRIVATE "${SHERPA_ONNX_LIBRARY}")
else()
    message(STATUS "sherpa-onnx NOT found — Kokoro TTS disabled")
endif()

# ── Windows-specific ─────────────────────────────────────────────────────────
if(WIN32)
    target_link_libraries(Tankoban PRIVATE dwmapi winmm)
    set_target_properties(Tankoban PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
    # Incremental linking for faster link times during development
    target_link_options(Tankoban PRIVATE /INCREMENTAL)
endif()

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
