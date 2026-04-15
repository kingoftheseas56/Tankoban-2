add_executable(sidecar_tests
    test_protocol.cpp
    test_ring_buffer.cpp
    test_encoding_detect.cpp
    test_subtitle_renderer.cpp
    test_hwaccel.cpp
    test_filter_graph.cpp
    ../src/protocol.cpp
    ../src/ring_buffer.cpp
    ../src/encoding_detect.cpp
    ../src/subtitle_renderer.cpp
    ../src/filter_graph.cpp
)

target_include_directories(sidecar_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
    "${GTEST_INCLUDE_DIR}"
    "${UCHARDET_INCLUDE_DIR}"
    "${AVCODEC_INCLUDE_DIR}"
    "${AVFORMAT_INCLUDE_DIR}"
    "${AVUTIL_INCLUDE_DIR}"
    "${SWSCALE_INCLUDE_DIR}"
    "${AVFILTER_INCLUDE_DIR}"
    "${LIBASS_INCLUDE_DIR}"
    "${FREETYPE_INCLUDE_DIR}"
    "${FRIBIDI_INCLUDE_DIR}"
    "${HARFBUZZ_INCLUDE_DIR}"
)

target_link_libraries(sidecar_tests PRIVATE
    nlohmann_json
    "${GTEST_LIBRARY}"
    "${GTEST_MAIN_LIBRARY}"
    "${UCHARDET_LIBRARY}"
    "${LIBASS_LIBRARY}"
    "${FREETYPE_LIBRARY}"
    "${FRIBIDI_LIBRARY}"
    "${HARFBUZZ_LIBRARY}"
    "${AVCODEC_LIBRARY}"
    "${AVFORMAT_LIBRARY}"
    "${AVUTIL_LIBRARY}"
    "${SWSCALE_LIBRARY}"
    "${AVFILTER_LIBRARY}"
)

if(WIN32)
    # Mirror the ffmpeg_sidecar target's Windows link list. subtitle_renderer.cpp
    # calls MMCSS (AvSetMmThreadCharacteristicsW / AvRevertMmThreadCharacteristics)
    # which lives in avrt; the test target links that same object file so it
    # needs the same import libraries as the main exe.
    target_link_libraries(sidecar_tests PRIVATE
        ws2_32 dwrite gdi32 shlwapi dbghelp d3d11 dxgi winmm avrt)
endif()

add_test(NAME sidecar_tests COMMAND sidecar_tests)
