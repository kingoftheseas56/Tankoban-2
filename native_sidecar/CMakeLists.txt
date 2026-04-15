cmake_minimum_required(VERSION 3.20)
project(ffmpeg_sidecar LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Dependency hint paths (pre-built libraries under C:/tools)
# ---------------------------------------------------------------------------
set(FFMPEG_ROOT "C:/tools/ffmpeg-master-latest-win64-gpl-shared" CACHE PATH "FFmpeg install prefix")
set(PORTAUDIO_ROOT "C:/tools/portaudio" CACHE PATH "PortAudio install prefix")
set(GTEST_ROOT "C:/tools/googletest" CACHE PATH "GoogleTest install prefix")
set(UCHARDET_ROOT "C:/tools/uchardet" CACHE PATH "uchardet install prefix")
set(FREETYPE_ROOT "C:/tools/freetype" CACHE PATH "FreeType install prefix")
set(FRIBIDI_ROOT "C:/tools/fribidi" CACHE PATH "fribidi install prefix")
set(HARFBUZZ_ROOT "C:/tools/harfbuzz" CACHE PATH "HarfBuzz install prefix")
set(LIBASS_ROOT "C:/tools/libass" CACHE PATH "libass install prefix")
set(LIBPLACEBO_ROOT "C:/tools/libplacebo" CACHE PATH "libplacebo install prefix")
set(VULKAN_ROOT "C:/tools/vulkan" CACHE PATH "Vulkan install prefix")
set(GLSLANG_ROOT "C:/tools/glslang" CACHE PATH "glslang install prefix")
set(LCMS2_ROOT "C:/tools/lcms2" CACHE PATH "lcms2 install prefix")

# ---------------------------------------------------------------------------
# FFmpeg (headers + import libs — linked in 8N.2)
# ---------------------------------------------------------------------------
find_path(AVCODEC_INCLUDE_DIR libavcodec/avcodec.h HINTS "${FFMPEG_ROOT}/include")
find_path(AVFORMAT_INCLUDE_DIR libavformat/avformat.h HINTS "${FFMPEG_ROOT}/include")
find_path(AVUTIL_INCLUDE_DIR libavutil/avutil.h HINTS "${FFMPEG_ROOT}/include")
find_path(SWSCALE_INCLUDE_DIR libswscale/swscale.h HINTS "${FFMPEG_ROOT}/include")
find_path(SWRESAMPLE_INCLUDE_DIR libswresample/swresample.h HINTS "${FFMPEG_ROOT}/include")

find_library(AVCODEC_LIBRARY avcodec HINTS "${FFMPEG_ROOT}/lib")
find_library(AVFORMAT_LIBRARY avformat HINTS "${FFMPEG_ROOT}/lib")
find_library(AVUTIL_LIBRARY avutil HINTS "${FFMPEG_ROOT}/lib")
find_library(SWSCALE_LIBRARY swscale HINTS "${FFMPEG_ROOT}/lib")
find_library(SWRESAMPLE_LIBRARY swresample HINTS "${FFMPEG_ROOT}/lib")

find_path(AVFILTER_INCLUDE_DIR libavfilter/avfilter.h HINTS "${FFMPEG_ROOT}/include")
find_library(AVFILTER_LIBRARY avfilter HINTS "${FFMPEG_ROOT}/lib")

# ---------------------------------------------------------------------------
# PortAudio (linked in 8N.3 for audio output)
# ---------------------------------------------------------------------------
find_path(PORTAUDIO_INCLUDE_DIR portaudio.h HINTS "${PORTAUDIO_ROOT}/include")
find_library(PORTAUDIO_LIBRARY portaudio HINTS "${PORTAUDIO_ROOT}/lib")

# ---------------------------------------------------------------------------
# uchardet (encoding detection for subtitle files)
# ---------------------------------------------------------------------------
find_path(UCHARDET_INCLUDE_DIR uchardet/uchardet.h HINTS "${UCHARDET_ROOT}/include")
find_library(UCHARDET_LIBRARY uchardet HINTS "${UCHARDET_ROOT}/lib")

# ---------------------------------------------------------------------------
# libass subtitle rendering (+ freetype, fribidi, harfbuzz dependencies)
# ---------------------------------------------------------------------------
find_path(FREETYPE_INCLUDE_DIR ft2build.h HINTS "${FREETYPE_ROOT}/include/freetype2")
find_library(FREETYPE_LIBRARY freetype HINTS "${FREETYPE_ROOT}/lib")

find_path(FRIBIDI_INCLUDE_DIR fribidi/fribidi.h HINTS "${FRIBIDI_ROOT}/include")
find_library(FRIBIDI_LIBRARY fribidi HINTS "${FRIBIDI_ROOT}/lib")

find_path(HARFBUZZ_INCLUDE_DIR harfbuzz/hb.h HINTS "${HARFBUZZ_ROOT}/include")
find_library(HARFBUZZ_LIBRARY harfbuzz HINTS "${HARFBUZZ_ROOT}/lib")

find_path(LIBASS_INCLUDE_DIR ass/ass.h HINTS "${LIBASS_ROOT}/include")
find_library(LIBASS_LIBRARY ass HINTS "${LIBASS_ROOT}/lib")

# ---------------------------------------------------------------------------
# libplacebo GPU rendering (+ Vulkan + glslang)
# ---------------------------------------------------------------------------
find_path(LIBPLACEBO_INCLUDE_DIR libplacebo/renderer.h HINTS "${LIBPLACEBO_ROOT}/include")
find_library(LIBPLACEBO_LIBRARY placebo HINTS "${LIBPLACEBO_ROOT}/lib")

find_path(VULKAN_INCLUDE_DIR vulkan/vulkan.h HINTS "${VULKAN_ROOT}/include")
find_library(VULKAN_LIBRARY vulkan-1 HINTS "${VULKAN_ROOT}/lib")

find_path(GLSLANG_INCLUDE_DIR glslang/Include/glslang_c_interface.h HINTS "${GLSLANG_ROOT}/include")
find_library(GLSLANG_LIBRARY glslang HINTS "${GLSLANG_ROOT}/lib")
find_library(SPIRV_LIBRARY SPIRV HINTS "${GLSLANG_ROOT}/lib")
find_library(MACHINEINDEPENDENT_LIBRARY MachineIndependent HINTS "${GLSLANG_ROOT}/lib")
find_library(GENERICCODEGEN_LIBRARY GenericCodeGen HINTS "${GLSLANG_ROOT}/lib")
find_library(OSDEPENDENT_LIBRARY OSDependent HINTS "${GLSLANG_ROOT}/lib")
find_library(GLSLANG_DEFAULT_RESOURCE_LIBRARY glslang-default-resource-limits HINTS "${GLSLANG_ROOT}/lib")

if(LIBPLACEBO_LIBRARY AND VULKAN_LIBRARY)
    set(HAS_LIBPLACEBO TRUE)
    message(STATUS "libplacebo + Vulkan found — GPU renderer enabled")
    # lcms2 only needed with libplacebo (ICC color management)
    find_path(LCMS2_INCLUDE_DIR lcms2.h HINTS "${LCMS2_ROOT}/include")
    find_library(LCMS2_LIBRARY lcms2 HINTS "${LCMS2_ROOT}/lib")
else()
    set(HAS_LIBPLACEBO FALSE)
    message(STATUS "libplacebo/Vulkan not found — GPU renderer disabled (stubs)")
endif()

# ---------------------------------------------------------------------------
# nlohmann/json (vendored single header)
# ---------------------------------------------------------------------------
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/vendor")

# ---------------------------------------------------------------------------
# Main executable
# ---------------------------------------------------------------------------
add_executable(ffmpeg_sidecar
    src/main.cpp
    src/protocol.cpp
    src/state_machine.cpp
    src/heartbeat.cpp
    src/shm_helpers.cpp
    src/ring_buffer.cpp
    src/demuxer.cpp
    src/video_decoder.cpp
    src/audio_decoder.cpp
    src/av_sync_clock.cpp
    src/encoding_detect.cpp
    src/subtitle_renderer.cpp
    src/filter_graph.cpp
    src/d3d11_presenter.cpp
)

if(HAS_LIBPLACEBO)
    target_sources(ffmpeg_sidecar PRIVATE
        src/gpu_renderer.cpp
        src/gpu_renderer_avutil.c
    )
    target_compile_definitions(ffmpeg_sidecar PRIVATE HAS_LIBPLACEBO PL_STATIC)
endif()

target_include_directories(ffmpeg_sidecar PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${AVCODEC_INCLUDE_DIR}"
    "${AVFORMAT_INCLUDE_DIR}"
    "${AVUTIL_INCLUDE_DIR}"
    "${SWSCALE_INCLUDE_DIR}"
    "${SWRESAMPLE_INCLUDE_DIR}"
    "${AVFILTER_INCLUDE_DIR}"
    "${PORTAUDIO_INCLUDE_DIR}"
    "${UCHARDET_INCLUDE_DIR}"
    "${FREETYPE_INCLUDE_DIR}"
    "${FRIBIDI_INCLUDE_DIR}"
    "${HARFBUZZ_INCLUDE_DIR}"
    "${LIBASS_INCLUDE_DIR}"
)

if(HAS_LIBPLACEBO)
    target_include_directories(ffmpeg_sidecar PRIVATE
        "${LIBPLACEBO_INCLUDE_DIR}"
        "${VULKAN_INCLUDE_DIR}"
        "${GLSLANG_INCLUDE_DIR}"
    )
endif()

target_link_libraries(ffmpeg_sidecar PRIVATE
    nlohmann_json
    "${AVCODEC_LIBRARY}"
    "${AVFORMAT_LIBRARY}"
    "${AVUTIL_LIBRARY}"
    "${SWSCALE_LIBRARY}"
    "${SWRESAMPLE_LIBRARY}"
    "${AVFILTER_LIBRARY}"
    "${PORTAUDIO_LIBRARY}"
    "${UCHARDET_LIBRARY}"
    "${LIBASS_LIBRARY}"
    "${FREETYPE_LIBRARY}"
    "${FRIBIDI_LIBRARY}"
    "${HARFBUZZ_LIBRARY}"
)

if(HAS_LIBPLACEBO)
    target_link_libraries(ffmpeg_sidecar PRIVATE
        "${LIBPLACEBO_LIBRARY}"
        "${VULKAN_LIBRARY}"
        "${GLSLANG_LIBRARY}"
        "${SPIRV_LIBRARY}"
        "${MACHINEINDEPENDENT_LIBRARY}"
        "${GENERICCODEGEN_LIBRARY}"
        "${OSDEPENDENT_LIBRARY}"
        "${GLSLANG_DEFAULT_RESOURCE_LIBRARY}"
        "${LCMS2_LIBRARY}"
    )
endif()

if(WIN32)
    target_link_libraries(ffmpeg_sidecar PRIVATE ws2_32 dwrite gdi32 shlwapi dbghelp d3d11 dxgi winmm avrt)
endif()

# Ensure console app (stdin/stdout needed for JSON protocol)
set_target_properties(ffmpeg_sidecar PROPERTIES
    WIN32_EXECUTABLE OFF
)

# ---------------------------------------------------------------------------
# D3D11-to-OpenGL bridge DLL (Holy Grail step 4)
# ---------------------------------------------------------------------------
if(WIN32)
    add_library(d3d11_gl_bridge SHARED src/d3d11_gl_bridge.cpp)
    target_link_libraries(d3d11_gl_bridge PRIVATE d3d11 dxgi opengl32)
    set_target_properties(d3d11_gl_bridge PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
endif()

# ---------------------------------------------------------------------------
# C++ unit tests (optional — only if GoogleTest found)
# ---------------------------------------------------------------------------
find_path(GTEST_INCLUDE_DIR gtest/gtest.h HINTS "${GTEST_ROOT}/include")
find_library(GTEST_LIBRARY gtest HINTS "${GTEST_ROOT}/lib")
find_library(GTEST_MAIN_LIBRARY gtest_main HINTS "${GTEST_ROOT}/lib")

if(GTEST_INCLUDE_DIR AND GTEST_LIBRARY AND GTEST_MAIN_LIBRARY)
    enable_testing()
    add_subdirectory(tests)
else()
    message(STATUS "GoogleTest not found — skipping C++ unit tests")
endif()
