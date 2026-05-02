// MpvProbe — Phase 1 smoke for MPV_RENDER_API_INTEGRATION_TODO.
// Compiled only when CMake locates libmpv (HAS_LIBMPV=1). When libmpv is
// absent, this TU is not added to the Tankoban target and main.cpp's gate
// is preprocessed away.

#include "MpvProbe.h"

#include <mpv/client.h>

#include <cstdio>

namespace tankoban {

int runMpvProbe()
{
    std::fprintf(stderr,
                 "[mpv-probe] mpv_client_api_version=0x%lx\n",
                 static_cast<unsigned long>(mpv_client_api_version()));

    mpv_handle* h = mpv_create();
    if (!h) {
        std::fprintf(stderr, "[mpv-probe] mpv_create FAILED\n");
        return 2;
    }

    int rc = mpv_initialize(h);
    if (rc < 0) {
        std::fprintf(stderr,
                     "[mpv-probe] mpv_initialize FAILED: %s\n",
                     mpv_error_string(rc));
        mpv_terminate_destroy(h);
        return 3;
    }

    // Exercise the cross-CRT alloc/free boundary. Strings returned by
    // mpv_get_property_string are libmpv-allocated; freeing with MSVC free()
    // would crash a MinGW-built libmpv. Always use mpv_free.
    char* ver = mpv_get_property_string(h, "mpv-version");
    std::fprintf(stderr, "[mpv-probe] mpv-version=%s\n", ver ? ver : "(null)");
    if (ver) mpv_free(ver);

    char* ff = mpv_get_property_string(h, "ffmpeg-version");
    std::fprintf(stderr, "[mpv-probe] ffmpeg=%s\n", ff ? ff : "(null)");
    if (ff) mpv_free(ff);

    mpv_terminate_destroy(h);
    std::fprintf(stderr, "[mpv-probe] OK\n");
    return 0;
}

} // namespace tankoban
