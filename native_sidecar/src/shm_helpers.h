#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "platform.h"

#if PLATFORM_WINDOWS
#include <windows.h>
#endif

struct ShmRegion {
    void*       ptr    = nullptr;
    size_t      size   = 0;
    std::string name;
#if PLATFORM_WINDOWS
    HANDLE      handle = nullptr;
#else
    int         fd     = -1;
#endif
};

// Create a named shared-memory region of `size` bytes.
// Name format: "tankoban_frame_<pid>_<counter>"
ShmRegion create_shm(const std::string& name, size_t size);

// Unmap and close/unlink the shared-memory region.
void cleanup_shm(ShmRegion& region);

// Generate a unique SHM name using PID and an atomic counter.
std::string generate_shm_name();
