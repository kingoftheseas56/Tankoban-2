# Tankoban runtime-asset deployment (POST_BUILD copies of shipped resource files).
# Extracted from CMakeLists.txt (REPO_STRUCTURE_CLEANUP P3 incr.3, 2026-05-29). include()d
# AFTER qt_add_executable(Tankoban) so the add_custom_command(TARGET Tankoban) steps bind to
# the existing target in the same scope. Static resource copies only (manga_uploader_trust.json,
# qwebp.dll) — the libplacebo runtime-DLL copy stays inline with its find_file setup. Add a new
# shipped-resource copy HERE, not in CMakeLists.txt.

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

# ── FANDOM_CATALOG qwebp plugin: WebP image-format decoder (2026-05-22) ──
# Fandom's Static Wikia CDN serves cover images as WebP regardless of the .jpg
# URL extension. Without Qt's qwebp.dll plugin, QImageReader silently fails to
# decode the response and the cover renders as a blank tile. The plugin lives
# in Qt's optional qtimageformats addon (not in the default qt6sdk install), so
# vendor it into resources/imageformats/ and deploy from there — keeps
# clones-and-builds hermetic with zero extra setup per dev machine.
add_custom_command(TARGET Tankoban POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:Tankoban>/imageformats"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/resources/imageformats/qwebp.dll"
        "$<TARGET_FILE_DIR:Tankoban>/imageformats/qwebp.dll"
    COMMENT "Deploying qwebp.dll Qt image-format plugin for Fandom CDN WebP covers"
)
