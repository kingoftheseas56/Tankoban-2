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
)

# Optional components — added as they become needed
find_package(Qt6 QUIET COMPONENTS WebEngineWidgets WebChannel Multimedia Svg)
find_package(Qt6CorePrivate QUIET)

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
    src/ui/pages/ComicsPage.cpp
    src/ui/pages/BooksPage.cpp
    src/ui/pages/VideosPage.cpp
    src/ui/pages/SeriesView.cpp
    src/ui/pages/BookSeriesView.cpp
    src/ui/pages/TileCard.cpp
    src/ui/pages/TileStrip.cpp
    src/ui/pages/ShowView.cpp
    src/ui/readers/ComicReader.cpp
    src/ui/readers/SmoothScrollArea.cpp
    src/ui/readers/DecodeTask.cpp
    src/ui/readers/PageCache.cpp
    src/ui/readers/BookReader.cpp
    src/ui/readers/BookBridge.cpp
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
    src/ui/player/FfmpegDecoder.cpp
    src/ui/player/AudioDecoder.cpp
    src/ui/player/FrameCanvas.cpp
    src/ui/player/VideoPlayer.cpp
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
    src/ui/pages/ComicsPage.h
    src/ui/pages/BooksPage.h
    src/ui/pages/VideosPage.h
    src/ui/pages/SeriesView.h
    src/ui/pages/BookSeriesView.h
    src/ui/pages/TileCard.h
    src/ui/pages/TileStrip.h
    src/ui/pages/ShowView.h
    src/ui/readers/ComicReader.h
    src/ui/readers/SmoothScrollArea.h
    src/ui/readers/DecodeTask.h
    src/ui/readers/PageCache.h
    src/ui/readers/BookReader.h
    src/ui/readers/BookBridge.h
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
    src/ui/player/SyncClock.h
    src/ui/player/FfmpegDecoder.h
    src/ui/player/AudioDecoder.h
    src/ui/player/FrameCanvas.h
    src/ui/player/VideoPlayer.h
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

# ── FFmpeg ──────────────────────────────────────────────────────────────────
set(FFMPEG_DIR "C:/tools/ffmpeg-master-latest-win64-gpl-shared")

target_include_directories(Tankoban PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${FFMPEG_DIR}/include
)

target_link_libraries(Tankoban PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    ${FFMPEG_DIR}/lib/avformat.lib
    ${FFMPEG_DIR}/lib/avcodec.lib
    ${FFMPEG_DIR}/lib/avutil.lib
    ${FFMPEG_DIR}/lib/swscale.lib
    ${FFMPEG_DIR}/lib/swresample.lib
)

# QZipReader for CBZ support
if(TARGET Qt6::CorePrivate)
    target_link_libraries(Tankoban PRIVATE Qt6::CorePrivate)
    target_compile_definitions(Tankoban PRIVATE HAS_QT_ZIP=1)
endif()

if(TARGET Qt6::WebEngineWidgets)
    target_link_libraries(Tankoban PRIVATE Qt6::WebEngineWidgets Qt6::WebChannel)
    target_compile_definitions(Tankoban PRIVATE HAS_WEBENGINE=1)
endif()

if(TARGET Qt6::Multimedia)
    target_link_libraries(Tankoban PRIVATE Qt6::Multimedia)
    target_compile_definitions(Tankoban PRIVATE HAS_MULTIMEDIA=1)
endif()

target_link_libraries(Tankoban PRIVATE Qt6::Svg)

# ── Windows-specific ─────────────────────────────────────────────────────────
if(WIN32)
    target_link_libraries(Tankoban PRIVATE dwmapi winmm)
    set_target_properties(Tankoban PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
endif()
