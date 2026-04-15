/*
 * D3D11-to-OpenGL Bridge DLL — Holy Grail Step 4
 *
 * Takes an NT handle to a shared D3D11 texture (from the FFmpeg sidecar)
 * and makes it available as an OpenGL texture for Qt's QOpenGLWidget.
 *
 * Pipeline: NT handle → OpenSharedResource1 → wglDXRegisterObjectNV → GL texture
 *
 * Exported functions (C ABI for Python ctypes):
 *   bridge_init()            — create D3D11 device + wglDX interop
 *   bridge_import_texture()  — open NT handle as GL texture
 *   bridge_lock()            — lock for GL rendering
 *   bridge_unlock()          — unlock after rendering
 *   bridge_destroy()         — cleanup
 */

#ifdef _WIN32

#include <windows.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <GL/gl.h>
#include <cstdio>
#include <cstdint>

using Microsoft::WRL::ComPtr;

// WGL_NV_DX_interop function types (loaded dynamically)
typedef BOOL  (WINAPI *PFN_wglDXSetResourceShareHandleNV)(void*, HANDLE);
typedef HANDLE(WINAPI *PFN_wglDXOpenDeviceNV)(void*);
typedef BOOL  (WINAPI *PFN_wglDXCloseDeviceNV)(HANDLE);
typedef HANDLE(WINAPI *PFN_wglDXRegisterObjectNV)(HANDLE, void*, GLuint, GLenum, GLenum);
typedef BOOL  (WINAPI *PFN_wglDXUnregisterObjectNV)(HANDLE, HANDLE);
typedef BOOL  (WINAPI *PFN_wglDXObjectAccessNV)(HANDLE, GLenum);
typedef BOOL  (WINAPI *PFN_wglDXLockObjectsNV)(HANDLE, GLint, HANDLE*);
typedef BOOL  (WINAPI *PFN_wglDXUnlockObjectsNV)(HANDLE, GLint, HANDLE*);

#ifndef WGL_ACCESS_READ_ONLY_NV
#define WGL_ACCESS_READ_ONLY_NV  0x0000
#endif
#ifndef WGL_ACCESS_READ_WRITE_NV
#define WGL_ACCESS_READ_WRITE_NV 0x0001
#endif
#ifndef GL_TEXTURE_2D
#define GL_TEXTURE_2D 0x0DE1
#endif

// GL shader / program types (loaded dynamically — not in gl.h)
typedef char    GLchar;
typedef ptrdiff_t GLsizeiptr;
typedef GLuint (APIENTRY *PFN_glCreateShader)(GLenum);
typedef void   (APIENTRY *PFN_glShaderSource)(GLuint, GLsizei, const GLchar**, const GLint*);
typedef void   (APIENTRY *PFN_glCompileShader)(GLuint);
typedef void   (APIENTRY *PFN_glGetShaderiv)(GLuint, GLenum, GLint*);
typedef void   (APIENTRY *PFN_glGetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef GLuint (APIENTRY *PFN_glCreateProgram)();
typedef void   (APIENTRY *PFN_glAttachShader)(GLuint, GLuint);
typedef void   (APIENTRY *PFN_glLinkProgram)(GLuint);
typedef void   (APIENTRY *PFN_glUseProgram)(GLuint);
typedef GLint  (APIENTRY *PFN_glGetUniformLocation)(GLuint, const GLchar*);
typedef void   (APIENTRY *PFN_glUniform1i)(GLint, GLint);
typedef void   (APIENTRY *PFN_glGenBuffers)(GLsizei, GLuint*);
typedef void   (APIENTRY *PFN_glBindBuffer)(GLenum, GLuint);
typedef void   (APIENTRY *PFN_glBufferData)(GLenum, GLsizeiptr, const void*, GLenum);
typedef void   (APIENTRY *PFN_glEnableVertexAttribArray)(GLuint);
typedef void   (APIENTRY *PFN_glVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
typedef void   (APIENTRY *PFN_glGenVertexArrays)(GLsizei, GLuint*);
typedef void   (APIENTRY *PFN_glBindVertexArray)(GLuint);
typedef void   (APIENTRY *PFN_glDeleteShader)(GLuint);
typedef void   (APIENTRY *PFN_glDeleteProgram)(GLuint);
typedef void   (APIENTRY *PFN_glDeleteBuffers)(GLsizei, const GLuint*);
typedef void   (APIENTRY *PFN_glDeleteVertexArrays)(GLsizei, const GLuint*);
typedef void   (APIENTRY *PFN_glActiveTexture)(GLenum);

#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER  0x8B30
#define GL_VERTEX_SHADER    0x8B31
#define GL_COMPILE_STATUS   0x8B81
#define GL_ARRAY_BUFFER     0x8889
#define GL_STATIC_DRAW      0x88E4
#define GL_TEXTURE0         0x84C0
#endif

// Shader GL function pointers (loaded once)
static PFN_glCreateShader           p_glCreateShader = nullptr;
static PFN_glShaderSource           p_glShaderSource = nullptr;
static PFN_glCompileShader          p_glCompileShader = nullptr;
static PFN_glGetShaderiv            p_glGetShaderiv = nullptr;
static PFN_glGetShaderInfoLog       p_glGetShaderInfoLog = nullptr;
static PFN_glCreateProgram          p_glCreateProgram = nullptr;
static PFN_glAttachShader           p_glAttachShader = nullptr;
static PFN_glLinkProgram            p_glLinkProgram = nullptr;
static PFN_glUseProgram             p_glUseProgram = nullptr;
static PFN_glGetUniformLocation     p_glGetUniformLocation = nullptr;
static PFN_glUniform1i              p_glUniform1i = nullptr;
static PFN_glGenBuffers             p_glGenBuffers = nullptr;
static PFN_glBindBuffer             p_glBindBuffer = nullptr;
static PFN_glBufferData             p_glBufferData = nullptr;
static PFN_glEnableVertexAttribArray p_glEnableVertexAttribArray = nullptr;
static PFN_glVertexAttribPointer    p_glVertexAttribPointer = nullptr;
static PFN_glGenVertexArrays        p_glGenVertexArrays = nullptr;
static PFN_glBindVertexArray        p_glBindVertexArray = nullptr;
static PFN_glDeleteShader           p_glDeleteShader = nullptr;
static PFN_glDeleteProgram          p_glDeleteProgram = nullptr;
static PFN_glDeleteBuffers          p_glDeleteBuffers = nullptr;
static PFN_glDeleteVertexArrays     p_glDeleteVertexArrays = nullptr;
static PFN_glActiveTexture          p_glActiveTexture = nullptr;

// State
static ComPtr<ID3D11Device1>        g_device;
static ComPtr<ID3D11DeviceContext>   g_context;
static ComPtr<ID3D11Texture2D>      g_imported_tex;    // local texture (registered with GL)
static ComPtr<ID3D11Texture2D>      g_shared_tex;      // opened from NT handle (cross-process)
static HANDLE                       g_dx_interop_device = nullptr;
static HANDLE                       g_dx_interop_object = nullptr;
static GLuint                       g_gl_texture = 0;
static bool                         g_initialized = false;
static bool                         g_texture_imported = false;

// Shader pipeline state
static GLuint  g_shader_program = 0;
static GLuint  g_vao = 0;
static GLuint  g_vbo = 0;
static bool    g_shader_ready = false;

// CPU readback staging texture (for when wglDX interop can't share with Qt)
static ComPtr<ID3D11Texture2D>  g_staging_tex;
static int                      g_staging_w = 0;
static int                      g_staging_h = 0;
static bool                     g_staging_mapped = false;

// wglDX function pointers
static PFN_wglDXOpenDeviceNV        p_wglDXOpenDeviceNV = nullptr;
static PFN_wglDXCloseDeviceNV       p_wglDXCloseDeviceNV = nullptr;
static PFN_wglDXRegisterObjectNV    p_wglDXRegisterObjectNV = nullptr;
static PFN_wglDXUnregisterObjectNV  p_wglDXUnregisterObjectNV = nullptr;
static PFN_wglDXLockObjectsNV       p_wglDXLockObjectsNV = nullptr;
static PFN_wglDXUnlockObjectsNV     p_wglDXUnlockObjectsNV = nullptr;

static FILE* bridge_log() {
    static FILE* f = nullptr;
    if (!f) f = fopen("d3d11_gl_bridge.log", "w");
    return f;
}
#define BLOG(...) do { FILE* _f = bridge_log(); if (_f) { std::fprintf(_f, __VA_ARGS__); std::fflush(_f); } } while(0)

static bool load_wgl_functions() {
    // wglGetProcAddress requires an active GL context
    auto wglGetProc = (void*(*)(const char*))GetProcAddress(
        GetModuleHandleA("opengl32.dll"), "wglGetProcAddress");
    if (!wglGetProc) {
        BLOG("Failed to get wglGetProcAddress\n");
        return false;
    }

    p_wglDXOpenDeviceNV       = (PFN_wglDXOpenDeviceNV)wglGetProc("wglDXOpenDeviceNV");
    p_wglDXCloseDeviceNV      = (PFN_wglDXCloseDeviceNV)wglGetProc("wglDXCloseDeviceNV");
    p_wglDXRegisterObjectNV   = (PFN_wglDXRegisterObjectNV)wglGetProc("wglDXRegisterObjectNV");
    p_wglDXUnregisterObjectNV = (PFN_wglDXUnregisterObjectNV)wglGetProc("wglDXUnregisterObjectNV");
    p_wglDXLockObjectsNV      = (PFN_wglDXLockObjectsNV)wglGetProc("wglDXLockObjectsNV");
    p_wglDXUnlockObjectsNV    = (PFN_wglDXUnlockObjectsNV)wglGetProc("wglDXUnlockObjectsNV");

    if (!p_wglDXOpenDeviceNV || !p_wglDXRegisterObjectNV ||
        !p_wglDXLockObjectsNV || !p_wglDXUnlockObjectsNV) {
        BLOG("WGL_NV_DX_interop not available (driver doesn't support it)\n");
        return false;
    }

    BLOG("WGL_NV_DX_interop loaded successfully\n");
    return true;
}

// ═══════════════════════════════════════════════════════════════════
// Exported C API
// ═══════════════════════════════════════════════════════════════════

extern "C" {

__declspec(dllexport)
int bridge_init() {
    if (g_initialized) return 1;

    BLOG("bridge_init: starting\n");

    // Load WGL_NV_DX_interop (requires active GL context from Qt)
    if (!load_wgl_functions()) {
        return 0;
    }

    // Create D3D11 device (separate from sidecar's device — needed for
    // OpenSharedResource1 which requires the consumer to have its own device)
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    ComPtr<ID3D11Device> base_device;
    ComPtr<ID3D11DeviceContext> base_ctx;

    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        levels, 2, D3D11_SDK_VERSION,
        &base_device, nullptr, &base_ctx);

    if (FAILED(hr)) {
        BLOG("bridge_init: D3D11CreateDevice failed hr=0x%lx\n", hr);
        return 0;
    }

    hr = base_device.As(&g_device);
    if (FAILED(hr)) {
        BLOG("bridge_init: QueryInterface ID3D11Device1 failed\n");
        return 0;
    }
    base_device->GetImmediateContext(&g_context);

    // Register D3D11 device with WGL_NV_DX_interop
    g_dx_interop_device = p_wglDXOpenDeviceNV(g_device.Get());
    if (!g_dx_interop_device) {
        BLOG("bridge_init: wglDXOpenDeviceNV failed (error=%lu)\n", GetLastError());
        g_device.Reset();
        g_context.Reset();
        return 0;
    }

    g_initialized = true;
    BLOG("bridge_init: success (device=%p interop=%p)\n",
         g_device.Get(), g_dx_interop_device);
    return 1;
}

// The sidecar PID — needed to duplicate its NT handle into our process
static DWORD g_sidecar_pid = 0;

__declspec(dllexport)
void bridge_set_sidecar_pid(unsigned int pid) {
    g_sidecar_pid = static_cast<DWORD>(pid);
    BLOG("bridge_set_sidecar_pid: %u\n", g_sidecar_pid);
}

__declspec(dllexport)
unsigned int bridge_import_texture(uint64_t nt_handle, int width, int height) {
    if (!g_initialized || !g_device) return 0;

    BLOG("bridge_import: handle=0x%llx %dx%d sidecar_pid=%u\n",
         (unsigned long long)nt_handle, width, height, g_sidecar_pid);

    // Cleanup previous import
    if (g_dx_interop_object) {
        p_wglDXUnregisterObjectNV(g_dx_interop_device, g_dx_interop_object);
        g_dx_interop_object = nullptr;
    }
    if (g_gl_texture) {
        glDeleteTextures(1, &g_gl_texture);
        g_gl_texture = 0;
    }
    g_imported_tex.Reset();

    // The NT handle is from the sidecar process — we need to duplicate it
    // into our process before OpenSharedResource1 can use it.
    HANDLE remote_handle = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(nt_handle));
    HANDLE local_handle = nullptr;

    if (g_sidecar_pid > 0) {
        HANDLE sidecar_proc = OpenProcess(PROCESS_DUP_HANDLE, FALSE, g_sidecar_pid);
        if (sidecar_proc) {
            BOOL dup_ok = DuplicateHandle(
                sidecar_proc, remote_handle,
                GetCurrentProcess(), &local_handle,
                0, FALSE, DUPLICATE_SAME_ACCESS);
            CloseHandle(sidecar_proc);
            if (!dup_ok) {
                BLOG("bridge_import: DuplicateHandle failed (error=%lu)\n", GetLastError());
                return 0;
            }
            BLOG("bridge_import: duplicated handle 0x%llx -> 0x%llx\n",
                 (unsigned long long)nt_handle,
                 (unsigned long long)(uintptr_t)local_handle);
        } else {
            BLOG("bridge_import: OpenProcess(%u) failed (error=%lu)\n",
                 g_sidecar_pid, GetLastError());
            return 0;
        }
    } else {
        local_handle = remote_handle;  // same process (shouldn't happen but fallback)
    }

    // Open the duplicated NT handle as a D3D11 texture on our device
    HANDLE handle = local_handle;
    ComPtr<ID3D11Texture2D> shared_tex;
    HRESULT hr = g_device->OpenSharedResource1(handle, IID_PPV_ARGS(&shared_tex));
    if (FAILED(hr)) {
        BLOG("bridge_import: OpenSharedResource1 failed hr=0x%lx\n", hr);
        return 0;
    }
    BLOG("bridge_import: OpenSharedResource1 succeeded\n");

    // Create a LOCAL texture on our device for wglDX interop.
    // wglDXRegisterObjectNV requires the texture to be owned by the
    // device registered with wglDXOpenDeviceNV (our bridge device).
    D3D11_TEXTURE2D_DESC local_desc = {};
    local_desc.Width            = width;
    local_desc.Height           = height;
    local_desc.MipLevels        = 1;
    local_desc.ArraySize        = 1;
    local_desc.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
    local_desc.SampleDesc.Count = 1;
    local_desc.Usage            = D3D11_USAGE_DEFAULT;
    local_desc.BindFlags        = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    local_desc.MiscFlags        = 0;  // plain local texture, no sharing needed

    ComPtr<ID3D11Texture2D> local_tex;
    hr = g_device->CreateTexture2D(&local_desc, nullptr, &local_tex);
    if (FAILED(hr)) {
        BLOG("bridge_import: CreateTexture2D (local) failed hr=0x%lx\n", hr);
        return 0;
    }

    // Copy shared → local (first frame)
    g_context->CopyResource(local_tex.Get(), shared_tex.Get());
    g_context->Flush();

    g_imported_tex = local_tex;
    g_shared_tex = shared_tex;  // keep reference for per-frame copies

    // Create GL texture
    glGenTextures(1, &g_gl_texture);
    if (!g_gl_texture) {
        BLOG("bridge_import: glGenTextures failed\n");
        g_imported_tex.Reset();
        g_shared_tex.Reset();
        return 0;
    }

    // Register LOCAL texture with OpenGL via wglDXInterop
    g_dx_interop_object = p_wglDXRegisterObjectNV(
        g_dx_interop_device,
        g_imported_tex.Get(),  // local texture owned by our device
        g_gl_texture,
        GL_TEXTURE_2D,
        WGL_ACCESS_READ_ONLY_NV);

    if (!g_dx_interop_object) {
        BLOG("bridge_import: wglDXRegisterObjectNV failed (error=%lu)\n", GetLastError());
        glDeleteTextures(1, &g_gl_texture);
        g_gl_texture = 0;
        g_imported_tex.Reset();
        return 0;
    }

    g_texture_imported = true;
    BLOG("bridge_import: success gl_tex=%u interop_obj=%p\n",
         g_gl_texture, g_dx_interop_object);
    return g_gl_texture;
}

__declspec(dllexport)
int bridge_lock() {
    if (!g_texture_imported || !g_dx_interop_object) return 0;

    // Copy latest frame from shared texture (sidecar) → local texture (GL)
    if (g_shared_tex && g_imported_tex && g_context) {
        g_context->CopyResource(g_imported_tex.Get(), g_shared_tex.Get());
        g_context->Flush();
    }

    BOOL ok = p_wglDXLockObjectsNV(g_dx_interop_device, 1, &g_dx_interop_object);
    return ok ? 1 : 0;
}

__declspec(dllexport)
int bridge_unlock() {
    if (!g_texture_imported || !g_dx_interop_object) return 0;
    BOOL ok = p_wglDXUnlockObjectsNV(g_dx_interop_device, 1, &g_dx_interop_object);
    return ok ? 1 : 0;
}

__declspec(dllexport)
int bridge_get_texture_id() {
    return static_cast<int>(g_gl_texture);
}

// ═══════════════════════════════════════════════════════════════════
// Shader-based rendering
// ═══════════════════════════════════════════════════════════════════

static bool load_shader_functions() {
    auto wglGetProc = (void*(*)(const char*))GetProcAddress(
        GetModuleHandleA("opengl32.dll"), "wglGetProcAddress");
    if (!wglGetProc) return false;

    #define LOAD_GL(name) p_##name = (PFN_##name)wglGetProc(#name)
    LOAD_GL(glCreateShader);
    LOAD_GL(glShaderSource);
    LOAD_GL(glCompileShader);
    LOAD_GL(glGetShaderiv);
    LOAD_GL(glGetShaderInfoLog);
    LOAD_GL(glCreateProgram);
    LOAD_GL(glAttachShader);
    LOAD_GL(glLinkProgram);
    LOAD_GL(glUseProgram);
    LOAD_GL(glGetUniformLocation);
    LOAD_GL(glUniform1i);
    LOAD_GL(glGenBuffers);
    LOAD_GL(glBindBuffer);
    LOAD_GL(glBufferData);
    LOAD_GL(glEnableVertexAttribArray);
    LOAD_GL(glVertexAttribPointer);
    LOAD_GL(glGenVertexArrays);
    LOAD_GL(glBindVertexArray);
    LOAD_GL(glDeleteShader);
    LOAD_GL(glDeleteProgram);
    LOAD_GL(glDeleteBuffers);
    LOAD_GL(glDeleteVertexArrays);
    LOAD_GL(glActiveTexture);
    #undef LOAD_GL

    bool ok = p_glCreateShader && p_glShaderSource && p_glCompileShader
           && p_glCreateProgram && p_glAttachShader && p_glLinkProgram
           && p_glUseProgram && p_glGenBuffers && p_glBindBuffer
           && p_glBufferData && p_glEnableVertexAttribArray
           && p_glVertexAttribPointer && p_glGenVertexArrays
           && p_glBindVertexArray && p_glActiveTexture;
    if (!ok) BLOG("load_shader_functions: some functions missing\n");
    return ok;
}

static GLuint compile_shader(GLenum type, const char* src) {
    GLuint s = p_glCreateShader(type);
    p_glShaderSource(s, 1, &src, nullptr);
    p_glCompileShader(s);
    GLint ok = 0;
    p_glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        p_glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        BLOG("shader compile error: %s\n", log);
    }
    return s;
}

__declspec(dllexport)
int bridge_setup_shader() {
    if (g_shader_ready) return 1;

    if (!load_shader_functions()) {
        BLOG("bridge_setup_shader: failed to load GL functions\n");
        return 0;
    }

    // Fullscreen textured quad — vertex shader
    const char* vs_src =
        "#version 330 core\n"
        "layout(location=0) in vec2 aPos;\n"
        "layout(location=1) in vec2 aUV;\n"
        "out vec2 vUV;\n"
        "void main() {\n"
        "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
        "    vUV = aUV;\n"
        "}\n";

    // Fragment shader — sample BGRA texture, swap R/B for correct color
    const char* fs_src =
        "#version 330 core\n"
        "in vec2 vUV;\n"
        "out vec4 fragColor;\n"
        "uniform sampler2D tex;\n"
        "void main() {\n"
        "    fragColor = texture(tex, vUV);\n"
        "}\n";

    GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);

    g_shader_program = p_glCreateProgram();
    p_glAttachShader(g_shader_program, vs);
    p_glAttachShader(g_shader_program, fs);
    p_glLinkProgram(g_shader_program);
    p_glDeleteShader(vs);
    p_glDeleteShader(fs);

    // Fullscreen quad: 2 triangles, positions + UVs interleaved
    // UV Y is flipped (1→0) to correct for D3D11 top-down vs GL bottom-up
    float verts[] = {
        // pos        uv
        -1.f, -1.f,  0.f, 1.f,
         1.f, -1.f,  1.f, 1.f,
        -1.f,  1.f,  0.f, 0.f,
         1.f,  1.f,  1.f, 0.f,
    };

    p_glGenVertexArrays(1, &g_vao);
    p_glGenBuffers(1, &g_vbo);

    p_glBindVertexArray(g_vao);
    p_glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    p_glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

    // aPos = location 0
    p_glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    p_glEnableVertexAttribArray(0);
    // aUV = location 1
    p_glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    p_glEnableVertexAttribArray(1);

    p_glBindVertexArray(0);

    // Set texture uniform
    p_glUseProgram(g_shader_program);
    GLint loc = p_glGetUniformLocation(g_shader_program, "tex");
    p_glUniform1i(loc, 0);  // texture unit 0
    p_glUseProgram(0);

    g_shader_ready = true;
    BLOG("bridge_setup_shader: success program=%u vao=%u vbo=%u\n",
         g_shader_program, g_vao, g_vbo);
    return 1;
}

__declspec(dllexport)
int bridge_render(int viewport_w, int viewport_h) {
    if (!g_texture_imported || !g_shader_ready || !g_dx_interop_object) {
        static int skip_count = 0;
        if (skip_count++ < 3) BLOG("bridge_render: skipped (imported=%d shader=%d interop=%p)\n",
            g_texture_imported, g_shader_ready, g_dx_interop_object);
        return 0;
    }

    // Copy latest frame: shared texture (sidecar) → local texture (GL-registered)
    if (g_shared_tex && g_imported_tex && g_context) {
        g_context->CopyResource(g_imported_tex.Get(), g_shared_tex.Get());
        g_context->Flush();
    }

    // Lock for GL access
    BOOL locked = p_wglDXLockObjectsNV(g_dx_interop_device, 1, &g_dx_interop_object);
    if (!locked) {
        static int lock_fail = 0;
        if (lock_fail++ < 3) BLOG("bridge_render: lock failed\n");
        return 0;
    }

    // Render
    glViewport(0, 0, viewport_w, viewport_h);
    GLenum err = glGetError();

    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);

    p_glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_gl_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    p_glUseProgram(g_shader_program);
    p_glBindVertexArray(g_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    err = glGetError();
    static int diag_count = 0;
    if (diag_count < 5) {
        BLOG("bridge_render: drew quad vp=%dx%d tex=%u prog=%u vao=%u err=0x%x\n",
             viewport_w, viewport_h, g_gl_texture, g_shader_program, g_vao, err);
        diag_count++;
    }

    p_glBindVertexArray(0);
    p_glUseProgram(0);

    // Unlock
    p_wglDXUnlockObjectsNV(g_dx_interop_device, 1, &g_dx_interop_object);

    return 1;
}

// ═══════════════════════════════════════════════════════════════════
// CPU readback path — maps D3D11 texture to CPU for Qt glTexImage2D
// ═══════════════════════════════════════════════════════════════════

__declspec(dllexport)
const void* bridge_lock_readback(int* out_width, int* out_height, int* out_stride) {
    if (!g_initialized || !g_shared_tex || !g_context) return nullptr;

    // Get shared texture dimensions
    D3D11_TEXTURE2D_DESC desc;
    g_shared_tex->GetDesc(&desc);
    int w = static_cast<int>(desc.Width);
    int h = static_cast<int>(desc.Height);

    // Create or resize staging texture
    if (!g_staging_tex || g_staging_w != w || g_staging_h != h) {
        g_staging_tex.Reset();
        D3D11_TEXTURE2D_DESC sd = {};
        sd.Width            = desc.Width;
        sd.Height           = desc.Height;
        sd.MipLevels        = 1;
        sd.ArraySize        = 1;
        sd.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
        sd.SampleDesc.Count = 1;
        sd.Usage            = D3D11_USAGE_STAGING;
        sd.CPUAccessFlags   = D3D11_CPU_ACCESS_READ;
        sd.BindFlags        = 0;

        HRESULT hr = g_device->CreateTexture2D(&sd, nullptr, &g_staging_tex);
        if (FAILED(hr)) {
            BLOG("bridge_lock_readback: staging texture failed hr=0x%lx\n", hr);
            return nullptr;
        }
        g_staging_w = w;
        g_staging_h = h;
        BLOG("bridge_lock_readback: staging texture created %dx%d\n", w, h);
    }

    // Copy shared → staging
    g_context->CopyResource(g_staging_tex.Get(), g_shared_tex.Get());

    // Map for CPU read
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = g_context->Map(g_staging_tex.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        BLOG("bridge_lock_readback: Map failed hr=0x%lx\n", hr);
        return nullptr;
    }

    g_staging_mapped = true;
    if (out_width)  *out_width  = w;
    if (out_height) *out_height = h;
    if (out_stride) *out_stride = static_cast<int>(mapped.RowPitch);

    return mapped.pData;
}

__declspec(dllexport)
void bridge_unlock_readback() {
    if (g_staging_mapped && g_staging_tex && g_context) {
        g_context->Unmap(g_staging_tex.Get(), 0);
        g_staging_mapped = false;
    }
}

__declspec(dllexport)
void bridge_destroy() {
    BLOG("bridge_destroy\n");

    // Shader cleanup
    if (g_shader_ready) {
        if (g_vao && p_glDeleteVertexArrays) p_glDeleteVertexArrays(1, &g_vao);
        if (g_vbo && p_glDeleteBuffers) p_glDeleteBuffers(1, &g_vbo);
        if (g_shader_program && p_glDeleteProgram) p_glDeleteProgram(g_shader_program);
        g_vao = 0; g_vbo = 0; g_shader_program = 0;
        g_shader_ready = false;
    }

    if (g_dx_interop_object && p_wglDXUnregisterObjectNV) {
        p_wglDXUnregisterObjectNV(g_dx_interop_device, g_dx_interop_object);
        g_dx_interop_object = nullptr;
    }
    if (g_gl_texture) {
        glDeleteTextures(1, &g_gl_texture);
        g_gl_texture = 0;
    }
    g_imported_tex.Reset();
    g_shared_tex.Reset();
    g_texture_imported = false;
    if (g_staging_mapped && g_staging_tex && g_context) {
        g_context->Unmap(g_staging_tex.Get(), 0);
        g_staging_mapped = false;
    }
    g_staging_tex.Reset();
    g_staging_w = 0; g_staging_h = 0;

    if (g_dx_interop_device && p_wglDXCloseDeviceNV) {
        p_wglDXCloseDeviceNV(g_dx_interop_device);
        g_dx_interop_device = nullptr;
    }
    g_context.Reset();
    g_device.Reset();
    g_initialized = false;
}

} // extern "C"

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_DETACH) {
        bridge_destroy();
    }
    return TRUE;
}

#endif // _WIN32
