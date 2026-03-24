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
)

# Optional components — added as they become needed
find_package(Qt6 QUIET COMPONENTS WebEngineWidgets WebChannel Multimedia)
find_package(Qt6CorePrivate QUIET)

# ── Sources ──────────────────────────────────────────────────────────────────
set(SOURCES
    src/main.cpp
    src/core/JsonStore.cpp
    src/core/CoreBridge.cpp
    src/core/LibraryScanner.cpp
    src/core/BooksScanner.cpp
    src/core/VideosScanner.cpp
    src/core/ArchiveReader.cpp
    src/ui/MainWindow.cpp
    src/ui/GlassBackground.cpp
    src/ui/RootFoldersOverlay.cpp
    src/ui/pages/ComicsPage.cpp
    src/ui/pages/BooksPage.cpp
    src/ui/pages/VideosPage.cpp
    src/ui/pages/SeriesView.cpp
    src/ui/pages/TileCard.cpp
    src/ui/pages/TileStrip.cpp
    src/ui/readers/ComicReader.cpp
)

set(HEADERS
    src/core/JsonStore.h
    src/core/CoreBridge.h
    src/core/LibraryScanner.h
    src/core/BooksScanner.h
    src/core/VideosScanner.h
    src/core/ArchiveReader.h
    src/ui/MainWindow.h
    src/ui/GlassBackground.h
    src/ui/RootFoldersOverlay.h
    src/ui/pages/ComicsPage.h
    src/ui/pages/BooksPage.h
    src/ui/pages/VideosPage.h
    src/ui/pages/SeriesView.h
    src/ui/pages/TileCard.h
    src/ui/pages/TileStrip.h
    src/ui/readers/ComicReader.h
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

# ── Windows-specific ─────────────────────────────────────────────────────────
if(WIN32)
    target_link_libraries(Tankoban PRIVATE dwmapi)
    set_target_properties(Tankoban PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
endif()
