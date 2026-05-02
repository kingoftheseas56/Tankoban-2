// MAKE_MPV_BEAT_FFMPEG Task 1 build probe (Agent 7, 2026-05-02).
// Agent 3 replaces this with the real libplacebo/Vulkan renderer in Task 2.
// This file intentionally references one symbol from each import library so
// the main app link proves both libraries are available to the MSVC build.

#include <cstdint>

#include <libplacebo/log.h>
#include <vulkan/vulkan.h>

extern "C" __declspec(dllexport) std::uintptr_t tankoban_mpv_libplacebo_build_probe()
{
    return reinterpret_cast<std::uintptr_t>(&pl_log_create) ^
           reinterpret_cast<std::uintptr_t>(&vkGetInstanceProcAddr);
}
