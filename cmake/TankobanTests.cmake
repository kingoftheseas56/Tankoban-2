# Tankoban unit-test target (opt-in; TANKOBAN_BUILD_TESTS=ON).
# Extracted from CMakeLists.txt (REPO_STRUCTURE_CLEANUP P3 incr.2, 2026-05-29) to keep
# the root CMakeLists lean. include()d at the end of CMakeLists (after the Tankoban +
# tankoctl targets) so it runs in the same scope and resolves src/ + tests/ paths and
# ${CMAKE_SOURCE_DIR} identically to before. Add/remove a test source HERE, not in CMakeLists.txt.

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
        tests/test_main.cpp
        tests/core/stream/test_quality_scorer.cpp
        tests/core/stream/test_auto_source_picker.cpp
        tests/core/stream/test_episode_display_state.cpp
        tests/core/stream/test_downloads_command_model.cpp
        tests/core/stream/test_anime_catalog_resolver.cpp
        tests/core/stream/test_stream_download_index_dedup.cpp
        tests/core/stream/test_stream_pack_parser.cpp
        tests/core/stream/test_stream_download_index_state.cpp
        tests/core/stream/test_unified_progress_store.cpp
        tests/core/stream/test_pack_classifier.cpp
        tests/core/stream/test_title_metadata_estimator.cpp
        tests/core/torrent/test_torrent_repository_schema.cpp
        tests/core/torrent/test_torrent_repository_downloads.cpp
        tests/core/torrent/test_torrent_repository_crud.cpp
        tests/core/torrent/test_torrent_repository_groups.cpp
        tests/core/torrent/test_legacy_importer_torrents.cpp
        tests/core/torrent/test_legacy_importer_resume.cpp
        tests/core/torrent/test_legacy_importer_groups.cpp
        tests/core/torrent/test_legacy_importer_downloads.cpp
        tests/core/torrent/test_legacy_importer_orchestrator.cpp
        tests/core/torrent/test_torrent_repository_durability.cpp
        src/core/torrent/TorrentRepository.cpp
        src/core/torrent/LegacyImporter.cpp
        # Per-show transfer lane queue (TANKORENT_QUALITY_AND_QUEUE Phase 1)
        tests/core/queue/test_transfer_queue.cpp
        src/core/queue/TransferQueue.cpp
        # Book tests
        tests/core/book/test_book_catalogue_result.cpp
        tests/core/book/test_catalogue_record.cpp
        tests/core/book/test_books_catalogue_library_store.cpp
        src/core/book/CatalogueRecord.cpp
        src/core/book/BooksCatalogueLibraryStore.cpp
        tests/core/book/test_fictiondb_client_parser.cpp
        src/core/book/FictionDbClient.cpp
        tests/core/book/test_book_series_index.cpp
        src/core/book/BookSeriesIndex.cpp
        tests/core/book/test_catalogue_rerank.cpp
        src/core/book/BookCatalogueAggregator.cpp
        tests/core/book/test_book_downloader_magnet.cpp
        # Tankorent search service tests
        tests/core/test_tankorent_search_service.cpp
        tests/core/MockTorrentIndexer.h
        src/core/TankorentSearchService.cpp
        src/core/TorrentIndexer.cpp
        src/core/indexers/NyaaIndexer.cpp
        src/core/indexers/PirateBayIndexer.cpp
        src/core/indexers/X1337xIndexer.cpp
        src/core/indexers/YtsIndexer.cpp
        src/core/indexers/EztvIndexer.cpp
        src/core/indexers/ExtTorrentsIndexer.cpp
        src/core/indexers/TorrentsCsvIndexer.cpp
        src/core/indexers/CloudflareCookieHarvester.cpp
        tests/core/manga/AniListVolumeMapperTest.cpp
        tests/core/manga/anilist/test_anilist_parser.cpp
        tests/core/manga/anilist/test_anilist_cache.cpp
        tests/core/manga/mangafire/test_manga_weebcentral_resolver.cpp
        tests/core/manga/test_weebcentral_chapter_quality.cpp
        src/core/manga/MangaScraper.h
        src/core/manga/WeebCentralScraper.cpp
        src/core/manga/mangafire/MangaFireCatalogClient.cpp
        tests/core/manga/test_western_catalog_loader.cpp
        tests/core/manga/MangaUpdatesStatusParserTest.cpp
        tests/core/manga/MangaUpdatesDisambiguatorTest.cpp
        tests/core/manga/mangaupdates/test_japanese_title_picker.cpp
        src/core/manga/mangaupdates/JapaneseTitlePicker.cpp
        tests/core/manga/test_trusted_uploaders.cpp
        src/core/manga/TrustedUploaders.cpp
        tests/core/manga/test_volume_quality_classifier.cpp
        src/core/manga/VolumeQualityClassifier.cpp
        tests/core/manga/wikidata/test_wikidata_cache.cpp
        tests/core/manga/wikipedia/test_wikipedia_resolver.cpp
        src/core/manga/MangaCatalogTypes.cpp
        src/core/manga/LocalMangaCatalogLoader.cpp
        src/core/manga/WesternCatalogLoader.cpp
        src/core/manga/LocalMangaCatalogIndex.cpp
        src/core/manga/mangafire/MangaWeebCentralResolver.cpp
        src/core/manga/wikidata/WikidataCache.cpp
        src/core/manga/wikipedia/WikipediaParser.cpp
        tests/core/PerModeNavControllerTest.cpp
        src/core/DebugLogBuffer.cpp
        src/core/JsonlEventLog.cpp
        src/core/JsonStore.cpp
        src/core/stream/QualityScorer.cpp
        src/core/stream/AutoSourcePicker.cpp
        src/core/stream/EpisodeDisplayState.cpp
        src/core/stream/DownloadsCommandModel.cpp
        src/core/stream/AnimeCatalogResolver.cpp
        src/core/stream/AnimeIdMapCache.cpp
        src/core/stream/StreamDownloadIndex.cpp
        src/core/stream/UnifiedProgressStore.cpp
        src/core/stream/PackClassifier.cpp
        src/core/stream/StreamPackParser.cpp
        src/core/stream/TitleMetadataEstimator.cpp
        src/core/manga/anilist/AniListParser.cpp
        src/core/manga/anilist/AniListVolumeMapper.cpp
        src/core/manga/anilist/AniListCache.cpp
        src/core/manga/mangaupdates/MangaUpdatesStatusParser.cpp
        src/core/manga/mangaupdates/MangaUpdatesDisambiguator.cpp
        src/ui/PerModeNavController.cpp
        # VolumeTile state tests (COMICS_CATALOG_SERIES_VIEW Phase 2 Task 8+9)
        tests/ui/test_volume_tile_state.cpp
        tests/ui/readers/test_comic_reader_pairing.cpp
        src/ui/pages/comics/VolumeTile.cpp
        src/core/manga/MangaDownloadIndex.cpp
        tests/core/manga/test_manga_download_index.cpp
        tests/core/manga/WeebCentralPairedParseTest.cpp
        src/core/manga/WesternSeriesParse.cpp
        tests/core/manga/WesternSeriesParseTest.cpp
        src/core/manga/GetComicsParse.cpp
        tests/core/manga/GetComicsParseTest.cpp
        src/core/manga/ReadComicsPageParse.cpp
        tests/core/manga/ReadComicsPageParseTest.cpp
        src/core/manga/WesternCatalogLoader.cpp
        tests/core/manga/WesternCatalogLoaderTest.cpp
        tests/core/manga/WesternIssueKeyTest.cpp
        tests/core/manga/WesternMangaIsolationTest.cpp
        src/core/manga/WesternLibrary.cpp
        tests/core/manga/WesternLibraryTest.cpp
    )
    target_include_directories(tankoban_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/src
    )
    target_compile_definitions(tankoban_tests PRIVATE
        TANKOBAN_TEST_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
    )
    find_package(Qt6 REQUIRED COMPONENTS Test Network Widgets)
    target_link_libraries(tankoban_tests PRIVATE
        GTest::gtest
        Qt6::Core
        Qt6::Network
        Qt6::Sql
        Qt6::Test
        Qt6::Widgets
    )
    include(GoogleTest)
    # TANKOBAN_TESTS_QT_PATH_FIX 2026-05-17 — gtest_discover_tests runs the
    # test binary at build time (POST_BUILD) to enumerate cases. tankoban_tests
    # links Qt6Test.lib (test utilities) but Qt6Test.dll is NOT part of the
    # standard windeployqt deployment for Tankoban.exe (the main app doesn't
    # link Qt6Test), so it's absent from out/ when discovery fires — loader
    # fails with 0xc0000135 STATUS_DLL_NOT_FOUND, which halts ninja.
    # Fix: copy Qt6Test.dll next to the test binary as a POST_BUILD step
    # BEFORE gtest_discover_tests' own POST_BUILD discovery step runs.
    # POST_BUILD commands on the same target execute in declaration order.
    add_custom_command(TARGET tankoban_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>"
            "$<TARGET_FILE_DIR:tankoban_tests>"
        COMMENT "Deploying Qt6Test.dll alongside tankoban_tests.exe for discovery + ctest runtime"
    )
    # TORRENT_PERSISTENCE_COLLAPSE P0.4 (2026-05-20) — Qt6Sql.dll is required
    # by TorrentRepository (now linked into tankoban_tests) but is not part of
    # windeployqt's main-app deployment, same situation as Qt6Test.dll above.
    # Copy it next to tankoban_tests.exe so loader can resolve at discovery.
    add_custom_command(TARGET tankoban_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Sql>"
            "$<TARGET_FILE_DIR:tankoban_tests>"
        COMMENT "Deploying Qt6Sql.dll alongside tankoban_tests.exe for discovery + ctest runtime"
    )
    # QSQLITE driver is loaded as a plugin from <exe_dir>/sqldrivers/qsqlite.dll
    # at QSqlDatabase::addDatabase("QSQLITE") time. Without it, the call
    # silently fails and subsequent operations on the handle SEH-segfault
    # (0xc0000005). Copy the plugin alongside tankoban_tests.exe.
    add_custom_command(TARGET tankoban_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:tankoban_tests>/sqldrivers"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${Qt6_DIR}/../../../plugins/sqldrivers/qsqlite.dll"
            "$<TARGET_FILE_DIR:tankoban_tests>/sqldrivers/qsqlite.dll"
        COMMENT "Deploying qsqlite.dll Qt SQL driver plugin alongside tankoban_tests.exe"
    )
    gtest_discover_tests(tankoban_tests)
endif()
