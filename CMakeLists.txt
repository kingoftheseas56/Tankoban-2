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
set(SOURCES
    src/main.cpp
    src/core/DebugLogBuffer.cpp
    src/devtools/DevControlServer.cpp
    src/core/JsonStore.cpp
    src/core/CoreBridge.cpp
    src/core/LibraryScanner.cpp
    src/core/BooksScanner.cpp
    src/core/library/VideoCategory.cpp
    src/core/library/VideoCategoryStore.cpp
    src/core/library/VideoClassifier.cpp
    src/core/VideosScanner.cpp
    src/core/ScannerUtils.cpp
    src/core/PosterFetcher.cpp
    src/core/PosterCache.cpp
    src/core/ArchiveReader.cpp
    src/ui/MainWindow.cpp
    src/ui/NavHistory.cpp
    src/ui/Theme.cpp
    src/ui/GlassBackground.cpp
    src/ui/RootFoldersOverlay.cpp
    src/ui/ContextMenuHelper.cpp
    src/ui/widgets/FadingStackedWidget.cpp
    src/ui/widgets/LibraryListView.cpp
    src/ui/widgets/SidebarDrawer.cpp
    src/ui/widgets/Toast.cpp
    src/ui/widgets/ThemePicker.cpp
    src/ui/pages/ComicsPage.cpp
    src/ui/pages/BooksPage.cpp
    src/ui/pages/VideosPage.cpp
    src/ui/pages/OrganisePage.cpp
    src/ui/pages/CategoryEditorPage.cpp
    src/ui/pages/PosterPickerPopover.cpp
    src/ui/pages/SeriesView.cpp
    src/ui/pages/BookSeriesView.cpp
    src/ui/pages/TileCard.cpp
    src/ui/pages/TileStrip.cpp
    src/ui/pages/ShowView.cpp
    src/ui/readers/ComicReader.cpp
    src/ui/readers/BookReader.cpp
    src/ui/readers/BookBridge.cpp
    src/core/tts/EdgeTtsClient.cpp
    src/core/tts/EdgeTtsWorker.cpp
    src/ui/readers/ScrollStripCanvas.cpp
    src/ui/readers/SmoothScrollArea.cpp
    src/ui/readers/DecodeTask.cpp
    src/ui/readers/PageCache.cpp
    src/ui/readers/ThumbnailGenerator.cpp
    src/core/EpubParser.cpp
    src/ui/pages/TankorentPage.cpp
    src/ui/pages/IndexerStatusPanel.cpp
    src/ui/pages/tankorent/TorrentPropertiesWidget.cpp
    src/ui/pages/tankorent/TorrentFilesTab.cpp
    src/ui/pages/tankorent/TorrentTrackersTab.cpp
    src/ui/pages/tankorent/TorrentPeersTab.cpp
    src/ui/pages/tankorent/TorrentGeneralTab.cpp
    src/core/TorrentIndexer.cpp
    src/core/indexers/TorrentsCsvIndexer.cpp
    src/core/indexers/NyaaIndexer.cpp
    src/core/indexers/PirateBayIndexer.cpp
    src/core/indexers/YtsIndexer.cpp
    src/core/indexers/EztvIndexer.cpp
    src/core/indexers/X1337xIndexer.cpp
    src/core/indexers/ExtTorrentsIndexer.cpp
    src/core/indexers/CloudflareCookieHarvester.cpp
    src/ui/player/SidecarProcess.cpp
    # MAKE_MPV_SOLO Task 8.B (2026-05-02) — Windows audio device-change
    # watcher (IMMNotificationClient COM listener + Qt-thread marshaling).
    # Closes the deferred per-device-delay-recall deliverable from Task 8.
    src/ui/player/AudioDeviceWatcher.cpp
    src/ui/player/ShmFrameReader.cpp
    src/ui/player/OverlayShmReader.cpp
    src/ui/player/FrameCanvas.cpp
    src/ui/player/VideoPlayer.cpp
    src/ui/player/VolumeHud.cpp
    src/ui/player/SeekSlider.cpp
    src/ui/player/KeyBindings.cpp
    src/ui/player/VsyncTimingLogger.cpp
    src/ui/player/CenterFlash.cpp
    src/ui/player/LoadingOverlay.cpp
    src/ui/player/ToastHud.cpp
    src/ui/player/PlaylistDrawer.cpp
    src/ui/player/SubtitleOverlay.cpp
    # VIDEO_HUD_MINIMALIST 2026-04-25 — TrackPopover / FilterPopover /
    # EqualizerPopover / SubtitleMenu replaced by SubtitlePopover +
    # AudioPopover + SettingsPopover (merged subtitle list absorbs the
    # SubtitleMenu external/addon-subs + load-from-file functionality).
    src/ui/player/SubtitlePopover.cpp
    src/ui/player/AudioPopover.cpp
    src/ui/player/SettingsPopover.cpp
    # MAKE_MPV_SOLO Task 9 (2026-05-01) — brightness-only filter popover.
    # Hemanth-narrowed scope revert of VIDEO_HUD_MINIMALIST Phase 1's
    # filter-stack removal; contrast/saturation stay removed.
    src/ui/player/BrightnessPopover.cpp
    src/ui/player/VideoContextMenu.cpp
    src/ui/player/OpenUrlDialog.cpp
    src/ui/player/KeybindingEditor.cpp
    src/ui/player/StatsBadge.cpp
    src/core/torrent/TorrentEngine.cpp
    src/core/torrent/TorrentClient.cpp
    src/ui/dialogs/AddTorrentDialog.cpp
    src/ui/dialogs/AddFromUrlDialog.cpp
    src/ui/dialogs/StreamRescueProgressDialog.cpp
    src/ui/dialogs/SpeedLimitDialog.cpp
    src/ui/dialogs/SeedingRulesDialog.cpp
    src/ui/dialogs/QueueLimitsDialog.cpp
    src/ui/dialogs/HistoryDialog.cpp
    src/core/manga/WeebCentralScraper.cpp
    src/core/manga/ReadComicsScraper.cpp
    src/core/manga/MangaSeriesDetail.cpp
    src/core/manga/MangaSourceRegistry.cpp
    src/core/manga/ComicsLibraryRecord.cpp
    src/core/manga/ComicsTankoyomiLibrary.cpp
    src/core/manga/MangaPosterCache.cpp
    src/core/manga/MangaDownloader.cpp
    src/core/manga/MangaDownloadIndex.cpp
    src/core/manga/PremiumCatalog.cpp
    src/core/manga/TorrentRequestLedger.cpp
    src/core/manga/TorrentVolumeProvider.cpp
    src/core/manga/MangaTransferCoordinator.cpp
    src/core/manga/PremiumArchiveValidator.cpp
    src/core/manga/PremiumCoverExtractor.cpp
    src/core/manga/NyaaRuntimeSource.cpp
    src/core/manga/WeebCentralVolumePacker.cpp
    src/core/manga/ComicsPrePivotMigrator.cpp
    src/core/manga/anilist/AniListClient.cpp
    src/core/manga/anilist/AniListCache.cpp
    src/core/manga/anilist/AniListVolumeMapper.cpp
    src/core/book/AnnaArchiveScraper.cpp
    src/core/book/AaSlowDownloadWaitHandler.cpp
    src/core/book/LibGenScraper.cpp
    src/core/book/AbbScraper.cpp
    src/core/book/BookDownloader.cpp
    src/ui/dialogs/MangaTransferDialog.cpp
    src/ui/dialogs/AddAddonDialog.cpp
    src/ui/pages/comics/SidecarMeta.cpp
    src/ui/pages/comics/ComicsTankoyomiSearchWidget.cpp
    src/ui/pages/comics/ComicsSeriesView.cpp
    src/ui/pages/comics/ComicsSourcesPanel.cpp
    src/ui/pages/tankoyomi/ChapterDownloadIndicator.cpp
    src/ui/pages/tankoyomi/ChapterRangeDialog.cpp
    src/ui/pages/TankoLibraryPage.cpp
    src/ui/pages/tankolibrary/BookResultsGrid.cpp
    src/ui/pages/tankolibrary/TransfersView.cpp
    src/core/stream/MetaAggregator.cpp
    # STREAM_SERVER_PIVOT Phase 3 (2026-04-25) — legacy libtorrent stream
    # engine deleted (StreamEngine, StreamHttpServer, StreamPieceWaiter,
    # StreamPrioritizer, StreamSeekClassifier, StreamSession). Stream mode
    # runs exclusively against the Stremio stream-server subprocess.
    src/core/stream/stremio/StreamServerProcess.cpp
    src/core/stream/stremio/StreamServerClient.cpp
    src/core/stream/stremio/StreamServerEngine.cpp
    src/core/stream/StreamLibrary.cpp
    src/core/stream/addon/AddonTransport.cpp
    src/core/stream/addon/AddonRegistry.cpp
    src/core/stream/CatalogAggregator.cpp
    src/core/stream/StreamAggregator.cpp
    src/core/stream/BulkSourceCollector.cpp
    src/core/stream/BulkPackVerifier.cpp
    src/core/stream/SubtitlesAggregator.cpp
    src/core/stream/StreamDownloadIndex.cpp
    src/core/stream/QualityScorer.cpp
    src/core/stream/UnifiedProgressStore.cpp
    src/core/stream/StreamRescueScanner.cpp
    src/core/stream/CalendarEngine.cpp
    src/core/stream/StreamBulkPlan.cpp
    src/core/stream/PackClassifier.cpp
    src/core/stream/UnifiedPackSearchEngine.cpp
    src/core/stream/TitleMetadataEstimator.cpp
    src/ui/pages/StreamPage.cpp
    src/ui/pages/stream/StreamLibraryLayout.cpp
    src/ui/pages/stream/StreamSearchWidget.cpp
    src/ui/pages/stream/StreamDetailView.cpp
    src/ui/pages/stream/TorrentPackPicker.cpp
    src/ui/pages/stream/TheatreDownloadPanel.cpp
    src/ui/pages/stream/PackListItem.cpp
    src/ui/pages/stream/EpisodeTile.cpp
    src/ui/pages/stream/StreamSourceChoice.cpp
    src/ui/pages/stream/StreamSourceCard.cpp
    src/ui/pages/stream/StreamSourceList.cpp
    src/ui/pages/stream/StreamPlayerController.cpp
    src/ui/pages/stream/StreamContinueStrip.cpp
    src/ui/pages/stream/AddonManagerScreen.cpp
    src/ui/pages/stream/AddonDetailPanel.cpp
    src/ui/pages/stream/StreamHomeBoard.cpp
    src/ui/pages/stream/CatalogBrowseScreen.cpp
    src/ui/pages/stream/CalendarScreen.cpp
)

set(HEADERS
    src/core/DebugLogBuffer.h
    src/devtools/DevControlServer.h
    src/core/JsonStore.h
    src/core/CoreBridge.h
    src/core/LibraryScanner.h
    src/core/BooksScanner.h
    src/core/library/VideoCategory.h
    src/core/library/VideoCategoryStore.h
    src/core/library/VideoClassifier.h
    src/core/VideosScanner.h
    src/core/ScannerUtils.h
    src/core/PosterFetcher.h
    src/core/PosterCache.h
    src/core/ArchiveReader.h
    src/ui/INavStateProvider.h
    src/ui/MainWindow.h
    src/ui/LayerEntry.h
    src/ui/NavHistory.h
    src/ui/GlassBackground.h
    src/ui/RootFoldersOverlay.h
    src/ui/ContextMenuHelper.h
    src/ui/widgets/FadingStackedWidget.h
    src/ui/widgets/LibraryListView.h
    src/ui/widgets/SidebarDrawer.h
    src/ui/widgets/Toast.h
    src/ui/pages/ComicsPage.h
    src/ui/pages/BooksPage.h
    src/ui/pages/VideosPage.h
    src/ui/pages/OrganisePage.h
    src/ui/pages/CategoryEditorPage.h
    src/ui/pages/PosterPickerPopover.h
    src/ui/pages/SeriesView.h
    src/ui/pages/BookSeriesView.h
    src/ui/pages/TileCard.h
    src/ui/pages/TileStrip.h
    src/ui/pages/ShowView.h
    src/ui/readers/ComicReader.h
    src/ui/readers/comic_progress_key.h
    src/ui/readers/BookReader.h
    src/ui/readers/BookBridge.h
    src/core/tts/EdgeTtsClient.h
    src/core/tts/EdgeTtsWorker.h
    src/ui/readers/SmoothScrollArea.h
    src/ui/readers/DecodeTask.h
    src/ui/readers/PageCache.h
    src/ui/readers/ThumbnailGenerator.h
    src/core/EpubParser.h
    src/ui/pages/TankorentPage.h
    src/ui/pages/IndexerStatusPanel.h
    src/ui/pages/tankorent/TorrentPropertiesWidget.h
    src/ui/pages/tankorent/TorrentFilesTab.h
    src/ui/pages/tankorent/TorrentTrackersTab.h
    src/ui/pages/tankorent/TorrentPeersTab.h
    src/ui/pages/tankorent/TorrentGeneralTab.h
    src/core/TorrentResult.h
    src/core/TorrentIndexer.h
    src/core/indexers/TorrentsCsvIndexer.h
    src/core/indexers/NyaaIndexer.h
    src/core/indexers/PirateBayIndexer.h
    src/core/indexers/YtsIndexer.h
    src/core/indexers/EztvIndexer.h
    src/core/indexers/X1337xIndexer.h
    src/core/indexers/ExtTorrentsIndexer.h
    src/core/indexers/CloudflareCookieHarvester.h
    src/ui/player/IPlayerBackend.h
    src/ui/player/SidecarProcess.h
    src/ui/player/AudioDeviceWatcher.h
    src/ui/player/ShmFrameReader.h
    src/ui/player/FrameCanvas.h
    src/ui/player/VideoPlayer.h
    src/ui/player/VolumeHud.h
    src/ui/player/SeekSlider.h
    src/ui/player/CenterFlash.h
    src/ui/player/PlaylistDrawer.h
    src/ui/player/SubtitleOverlay.h
    src/ui/player/SubtitlePopover.h
    src/ui/player/AudioPopover.h
    src/ui/player/SettingsPopover.h
    src/ui/player/BrightnessPopover.h
    src/ui/player/VideoContextMenu.h
    src/ui/player/KeybindingEditor.h
    src/ui/player/StatsBadge.h
    src/core/torrent/TorrentEngine.h
    src/core/torrent/TorrentClient.h
    src/ui/dialogs/AddTorrentDialog.h
    src/ui/dialogs/AddFromUrlDialog.h
    src/ui/dialogs/StreamRescueProgressDialog.h
    src/ui/dialogs/SpeedLimitDialog.h
    src/ui/dialogs/SeedingRulesDialog.h
    src/ui/dialogs/QueueLimitsDialog.h
    src/ui/dialogs/HistoryDialog.h
    src/core/manga/MangaResult.h
    src/core/manga/MangaScraper.h
    src/core/manga/MangaSeriesDetail.h
    src/core/manga/MangaSourceRegistry.h
    src/core/manga/ComicsLibraryRecord.h
    src/core/manga/ComicsTankoyomiLibrary.h
    src/core/manga/MangaPosterCache.h
    src/core/manga/WeebCentralScraper.h
    src/core/manga/ReadComicsScraper.h
    src/core/manga/MangaDownloader.h
    src/core/manga/MangaDownloadIndex.h
    src/core/manga/PremiumCatalog.h
    src/core/manga/PremiumCatalogSchema.h
    src/core/manga/TorrentRequestLedger.h
    src/core/manga/TorrentVolumeProvider.h
    src/core/manga/MangaTransferCoordinator.h
    src/core/manga/PremiumArchiveValidator.h
    src/core/manga/PremiumCoverExtractor.h
    src/core/manga/NyaaRuntimeSource.h
    src/core/manga/WeebCentralVolumePacker.h
    src/core/manga/ComicsPrePivotMigrator.h
    src/core/manga/CanonicalChapterKey.h
    src/core/manga/anilist/AniListTypes.h
    src/core/manga/anilist/AniListClient.h
    src/core/manga/anilist/AniListCache.h
    src/core/manga/anilist/AniListVolumeMapper.h
    src/core/book/BookResult.h
    src/core/book/BookScraper.h
    src/core/book/AnnaArchiveScraper.h
    src/core/book/AaSlowDownloadWaitHandler.h
    src/core/book/LibGenScraper.h
    src/core/book/AbbScraper.h
    src/core/book/BookDownloader.h
    src/ui/dialogs/MangaTransferDialog.h
    src/ui/dialogs/AddAddonDialog.h
    src/ui/pages/comics/SidecarMeta.h
    src/ui/pages/comics/ComicsTankoyomiSearchWidget.h
    src/ui/pages/comics/ComicsSeriesView.h
    src/ui/pages/comics/ComicsSourcesPanel.h
    src/ui/pages/tankoyomi/ChapterDownloadIndicator.h
    src/ui/pages/tankoyomi/ChapterRangeDialog.h
    src/ui/pages/TankoLibraryPage.h
    src/ui/pages/tankolibrary/BookResultsGrid.h
    src/ui/pages/tankolibrary/TransfersView.h
    src/core/stream/MetaAggregator.h
    # STREAM_SERVER_PIVOT Phase 3 (2026-04-25) — legacy engine headers
    # deleted along with their .cpp counterparts. StreamTypes.h holds the
    # struct + enum definitions that used to live in StreamEngine.h.
    src/core/stream/StreamTypes.h
    src/core/stream/stremio/StreamServerProcess.h
    src/core/stream/stremio/StreamServerClient.h
    src/core/stream/stremio/StreamServerEngine.h
    src/core/stream/StreamLibrary.h
    src/core/stream/StreamProgress.h
    src/core/stream/addon/Manifest.h
    src/core/stream/addon/Descriptor.h
    src/core/stream/addon/ResourcePath.h
    src/core/stream/addon/StreamSource.h
    src/core/stream/addon/SubtitleInfo.h
    src/core/stream/addon/StreamInfo.h
    src/core/stream/addon/MetaItem.h
    src/core/stream/addon/AddonTransport.h
    src/core/stream/addon/AddonRegistry.h
    src/core/stream/CatalogAggregator.h
    src/core/stream/StreamAggregator.h
    src/core/stream/BulkSourceCollector.h
    src/core/stream/BulkPackVerifier.h
    src/core/stream/SubtitlesAggregator.h
    src/core/stream/StreamDownloadIndex.h
    src/core/stream/QualityScorer.h
    src/core/stream/UnifiedProgressStore.h
    src/core/stream/StreamRescueScanner.h
    src/core/stream/CalendarEngine.h
    src/core/stream/StreamBulkPlan.h
    src/core/stream/PackClassifier.h
    src/core/stream/UnifiedPackSearchEngine.h
    src/core/stream/TitleMetadataEstimator.h
    src/ui/pages/StreamPage.h
    src/ui/pages/stream/StreamLibraryLayout.h
    src/ui/pages/stream/StreamSearchWidget.h
    src/ui/pages/stream/StreamDetailView.h
    src/ui/pages/stream/TorrentPackPicker.h
    src/ui/pages/stream/TheatreDownloadPanel.h
    src/ui/pages/stream/PackListItem.h
    src/ui/pages/stream/EpisodeTile.h
    src/ui/pages/stream/StreamSourceChoice.h
    src/ui/pages/stream/StreamSourceCard.h
    src/ui/pages/stream/StreamSourceList.h
    src/ui/pages/stream/StreamPlayerController.h
    src/ui/pages/stream/StreamContinueStrip.h
    src/ui/pages/stream/AddonManagerScreen.h
    src/ui/pages/stream/AddonDetailPanel.h
    src/ui/pages/stream/StreamHomeBoard.h
    src/ui/pages/stream/CatalogBrowseScreen.h
    src/ui/pages/stream/CalendarScreen.h
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

    # STREAM_SERVER_PIVOT Phase 3 (2026-04-25) — copy Stremio stream-server
    # subprocess bundle next to Tankoban.exe at post-build time. The stream-
    # server is Stremio's open-source Node.js SEA (MIT licensed); Tankoban
    # ships it byte-identical to upstream + an attribution file. Attempted
    # 2026-04-25 to rebrand the binary via rcedit (VERSIONINFO + icon) but
    # that corrupts the embedded Node.js SEA blob → segfault on launch; the
    # Stremio branding shows in Windows Firewall dialog + Task Manager until
    # we invest in rebuilding stream-server from upstream source with our own
    # SEA construction (deferred, non-trivial). LICENSE.stream-server.txt
    # ships alongside as MIT attribution.
    # Required files:
    #   stremio-runtime.exe (~22 MB)       Node.js SEA — BYTE-IDENTICAL TO UPSTREAM
    #   server.js           (~6 MB)        webpacked stream-server JS
    #   ffmpeg.exe / ffprobe.exe           stream-server probes content via these
    #   av*-58.dll + misc                  ffmpeg 4.x shared libs (DO NOT reuse
    #                                      Tankoban's own ffmpeg_sidecar/ 6.x DLLs —
    #                                      major version mismatch)
    #   vcruntime140*.dll                  MSVC runtime
    #   LICENSE.stream-server.txt          MIT attribution for the upstream code
    add_custom_command(TARGET Tankoban POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:Tankoban>/stream_server"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/resources/stream_server/stremio-runtime.exe"
            "${CMAKE_SOURCE_DIR}/resources/stream_server/server.js"
            "${CMAKE_SOURCE_DIR}/resources/stream_server/ffmpeg.exe"
            "${CMAKE_SOURCE_DIR}/resources/stream_server/ffprobe.exe"
            "${CMAKE_SOURCE_DIR}/resources/stream_server/avcodec-58.dll"
            # avdevice-58.dll dropped 2026-04-24 (STREAM_SERVER_PIVOT Phase 2A).
            # ffmpeg avdevice is for device-class inputs (-f dshow / v4l2 / decklink
            # etc.) which stream-server never uses for playback. Saves ~1.5 MiB off
            # the bundle. If stream-server fails to spawn with a DLL-load error on
            # Windows, restore this line + the file under resources/stream_server/.
            "${CMAKE_SOURCE_DIR}/resources/stream_server/avfilter-7.dll"
            "${CMAKE_SOURCE_DIR}/resources/stream_server/avformat-58.dll"
            "${CMAKE_SOURCE_DIR}/resources/stream_server/avutil-56.dll"
            "${CMAKE_SOURCE_DIR}/resources/stream_server/postproc-55.dll"
            "${CMAKE_SOURCE_DIR}/resources/stream_server/swresample-3.dll"
            "${CMAKE_SOURCE_DIR}/resources/stream_server/swscale-5.dll"
            "${CMAKE_SOURCE_DIR}/resources/stream_server/vcruntime140.dll"
            "${CMAKE_SOURCE_DIR}/resources/stream_server/vcruntime140_1.dll"
            "${CMAKE_SOURCE_DIR}/resources/stream_server/LICENSE.stream-server.txt"
            "$<TARGET_FILE_DIR:Tankoban>/stream_server/"
        COMMENT "Copying Stremio stream-server bundle (MIT-licensed upstream) to build output"
        VERBATIM
    )

endif()

# ── TANKOYOMI_VOLUME_PIVOT Phase 4: deploy uploader-trust JSON for NyaaRuntimeSource ──
# NyaaRuntimeSource loads this file at construction to populate tier1/tier2/blocked
# uploader sets used for ranking nyaa.si RSS search results. Shipped read-only
# alongside the exe under resources/manga_uploader_trust.json.
add_custom_command(TARGET Tankoban POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:Tankoban>/resources"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/resources/manga_uploader_trust.json"
        "$<TARGET_FILE_DIR:Tankoban>/resources/manga_uploader_trust.json"
    COMMENT "Copying manga_uploader_trust.json (NyaaRuntimeSource Phase 4) to build output"
    VERBATIM
)

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
add_executable(tankoctl tools/tankoctl.cpp)
set_target_properties(tankoctl PROPERTIES WIN32_EXECUTABLE OFF)
target_link_libraries(tankoctl PRIVATE Qt6::Core Qt6::Network)

# ── Unit tests (opt-in; TANKOBAN_BUILD_TESTS=ON) ─────────────────────────────
# Pure-logic unit tests built with GoogleTest via FetchContent.
# Run with: cmake -DTANKOBAN_BUILD_TESTS=ON; cmake --build out --target tankoban_tests
#           cd out && ctest --output-on-failure -R tankoban_tests
option(TANKOBAN_BUILD_TESTS "Build tankoban_tests unit test binary" OFF)

if(TANKOBAN_BUILD_TESTS)
    enable_testing()
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    )
    # Prevent GoogleTest from overriding our compiler/linker settings on Windows
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    add_executable(tankoban_tests
        tests/core/stream/test_quality_scorer.cpp
        tests/core/stream/test_stream_download_index_dedup.cpp
        tests/core/stream/test_unified_progress_store.cpp
        tests/core/stream/test_pack_classifier.cpp
        tests/core/stream/test_title_metadata_estimator.cpp
        tests/core/manga/AniListVolumeMapperTest.cpp
        src/core/DebugLogBuffer.cpp
        src/core/JsonStore.cpp
        src/core/stream/QualityScorer.cpp
        src/core/stream/StreamDownloadIndex.cpp
        src/core/stream/UnifiedProgressStore.cpp
        src/core/stream/PackClassifier.cpp
        src/core/stream/TitleMetadataEstimator.cpp
        src/core/manga/anilist/AniListVolumeMapper.cpp
    )
    target_include_directories(tankoban_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/src
    )
    target_link_libraries(tankoban_tests PRIVATE
        GTest::gtest_main
        Qt6::Core
    )
    include(GoogleTest)
    gtest_discover_tests(tankoban_tests)
endif()
