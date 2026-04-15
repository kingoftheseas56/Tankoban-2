#include "shm_helpers.h"

#include <atomic>
#include <cstdio>
#include <cstring>

#if PLATFORM_WINDOWS
// Already included via header
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

static std::atomic<int> g_shm_counter{0};

std::string generate_shm_name() {
    int count = g_shm_counter.fetch_add(1);
#if PLATFORM_WINDOWS
    DWORD pid = GetCurrentProcessId();
#else
    int pid = static_cast<int>(getpid());
#endif
    char buf[128];
    std::snprintf(buf, sizeof(buf), "tankoban_frame_%u_%d",
                  static_cast<unsigned>(pid), count);
    return std::string(buf);
}

ShmRegion create_shm(const std::string& name, size_t size) {
    ShmRegion r;
    r.name = name;
    r.size = size;

#if PLATFORM_WINDOWS
    r.handle = CreateFileMappingA(
        INVALID_HANDLE_VALUE,  // backed by paging file
        nullptr,               // default security
        PAGE_READWRITE,
        0,                     // high-order size (0 for < 4 GB)
        static_cast<DWORD>(size),
        name.c_str()
    );
    if (!r.handle) {
        std::fprintf(stderr, "CreateFileMappingA failed for '%s': %lu\n",
                     name.c_str(), GetLastError());
        return r;
    }

    r.ptr = MapViewOfFile(r.handle, FILE_MAP_ALL_ACCESS, 0, 0, size);
    if (!r.ptr) {
        std::fprintf(stderr, "MapViewOfFile failed for '%s': %lu\n",
                     name.c_str(), GetLastError());
        CloseHandle(r.handle);
        r.handle = nullptr;
        return r;
    }

    // Zero the region
    std::memset(r.ptr, 0, size);

#else
    r.fd = shm_open(name.c_str(), O_CREAT | O_RDWR, 0600);
    if (r.fd < 0) {
        std::perror("shm_open");
        return r;
    }
    if (ftruncate(r.fd, static_cast<off_t>(size)) != 0) {
        std::perror("ftruncate");
        close(r.fd);
        shm_unlink(name.c_str());
        r.fd = -1;
        return r;
    }
    r.ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, r.fd, 0);
    if (r.ptr == MAP_FAILED) {
        std::perror("mmap");
        close(r.fd);
        shm_unlink(name.c_str());
        r.ptr = nullptr;
        r.fd = -1;
        return r;
    }
    std::memset(r.ptr, 0, size);
#endif

    return r;
}

void cleanup_shm(ShmRegion& region) {
#if PLATFORM_WINDOWS
    if (region.ptr) {
        UnmapViewOfFile(region.ptr);
        region.ptr = nullptr;
    }
    if (region.handle) {
        CloseHandle(region.handle);
        region.handle = nullptr;
    }
#else
    if (region.ptr && region.ptr != MAP_FAILED) {
        munmap(region.ptr, region.size);
        region.ptr = nullptr;
    }
    if (region.fd >= 0) {
        close(region.fd);
        region.fd = -1;
    }
    if (!region.name.empty()) {
        shm_unlink(region.name.c_str());
    }
#endif
    region.size = 0;
}
