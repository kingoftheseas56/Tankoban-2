// TANKOBAN_HDR_PROBE: define to activate one-shot HDR capability probes (Task 6.1).
// Probe output captured 2026-05-02; define removed. Re-add #define to re-probe.
// #define TANKOBAN_HDR_PROBE 1

#include "MpvLibplaceboRenderer.h"

#include "core/DebugLogBuffer.h"

#include <mpv/client.h>
#include <mpv/render.h>
#include <mpv/render_gl.h>

#include <QByteArray>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QSurfaceFormat>
#include <QString>

#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#include <windows.h>
#endif

#include <vulkan/vulkan.h>

extern "C" {
#include <libplacebo/colorspace.h>
#include <libplacebo/gpu.h>
#include <libplacebo/log.h>
#include <libplacebo/renderer.h>
#include <libplacebo/shaders/colorspace.h>
#include <libplacebo/swapchain.h>
#include <libplacebo/vulkan.h>
}

#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

#ifndef GL_HANDLE_TYPE_OPAQUE_WIN32_EXT
#define GL_HANDLE_TYPE_OPAQUE_WIN32_EXT 0x9587
#endif
#ifndef GL_LAYOUT_COLOR_ATTACHMENT_EXT
#define GL_LAYOUT_COLOR_ATTACHMENT_EXT 0x958E
#endif
#ifndef GL_LAYOUT_SHADER_READ_ONLY_EXT
#define GL_LAYOUT_SHADER_READ_ONLY_EXT 0x9591
#endif
#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif

namespace {

constexpr int kInteropSlots = 3;

void mprLog(const QString& msg)
{
    DebugLogBuffer::instance().info("MpvLibplaceboRenderer", msg);
}

bool duplicateHandleForImport(HANDLE source, HANDLE* duplicate)
{
    if (!source || source == INVALID_HANDLE_VALUE || !duplicate) return false;
    HANDLE dup = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), source,
                         GetCurrentProcess(), &dup,
                         0, FALSE, DUPLICATE_SAME_ACCESS)) {
        return false;
    }
    *duplicate = dup;
    return true;
}

using GlCreateMemoryObjectsEXT =
    void(APIENTRY*)(GLsizei n, GLuint* memoryObjects);
using GlDeleteMemoryObjectsEXT =
    void(APIENTRY*)(GLsizei n, const GLuint* memoryObjects);
using GlImportMemoryWin32HandleEXT =
    void(APIENTRY*)(GLuint memory, GLuint64 size, GLenum handleType, void* handle);
using GlTexStorageMem2DEXT =
    void(APIENTRY*)(GLenum target, GLsizei levels, GLenum internalFormat,
                    GLsizei width, GLsizei height, GLuint memory, GLuint64 offset);
using GlGenSemaphoresEXT =
    void(APIENTRY*)(GLsizei n, GLuint* semaphores);
using GlDeleteSemaphoresEXT =
    void(APIENTRY*)(GLsizei n, const GLuint* semaphores);
using GlImportSemaphoreWin32HandleEXT =
    void(APIENTRY*)(GLuint semaphore, GLenum handleType, void* handle);
using GlWaitSemaphoreEXT =
    void(APIENTRY*)(GLuint semaphore, GLuint numBufferBarriers, const GLuint* buffers,
                    GLuint numTextureBarriers, const GLuint* textures,
                    const GLenum* srcLayouts);
using GlSignalSemaphoreEXT =
    void(APIENTRY*)(GLuint semaphore, GLuint numBufferBarriers, const GLuint* buffers,
                    GLuint numTextureBarriers, const GLuint* textures,
                    const GLenum* dstLayouts);

struct GlInteropFns {
    GlCreateMemoryObjectsEXT createMemoryObjects = nullptr;
    GlDeleteMemoryObjectsEXT deleteMemoryObjects = nullptr;
    GlImportMemoryWin32HandleEXT importMemoryWin32Handle = nullptr;
    GlTexStorageMem2DEXT texStorageMem2D = nullptr;
    GlGenSemaphoresEXT genSemaphores = nullptr;
    GlDeleteSemaphoresEXT deleteSemaphores = nullptr;
    GlImportSemaphoreWin32HandleEXT importSemaphoreWin32Handle = nullptr;
    GlWaitSemaphoreEXT waitSemaphore = nullptr;
    GlSignalSemaphoreEXT signalSemaphore = nullptr;

    bool load(QOpenGLContext& ctx, QString* missing)
    {
        auto get = [&ctx](const char* name) -> void* {
            return reinterpret_cast<void*>(ctx.getProcAddress(QByteArray(name)));
        };

        createMemoryObjects = reinterpret_cast<GlCreateMemoryObjectsEXT>(
            get("glCreateMemoryObjectsEXT"));
        deleteMemoryObjects = reinterpret_cast<GlDeleteMemoryObjectsEXT>(
            get("glDeleteMemoryObjectsEXT"));
        importMemoryWin32Handle = reinterpret_cast<GlImportMemoryWin32HandleEXT>(
            get("glImportMemoryWin32HandleEXT"));
        texStorageMem2D = reinterpret_cast<GlTexStorageMem2DEXT>(
            get("glTexStorageMem2DEXT"));
        genSemaphores = reinterpret_cast<GlGenSemaphoresEXT>(
            get("glGenSemaphoresEXT"));
        deleteSemaphores = reinterpret_cast<GlDeleteSemaphoresEXT>(
            get("glDeleteSemaphoresEXT"));
        importSemaphoreWin32Handle = reinterpret_cast<GlImportSemaphoreWin32HandleEXT>(
            get("glImportSemaphoreWin32HandleEXT"));
        waitSemaphore = reinterpret_cast<GlWaitSemaphoreEXT>(
            get("glWaitSemaphoreEXT"));
        signalSemaphore = reinterpret_cast<GlSignalSemaphoreEXT>(
            get("glSignalSemaphoreEXT"));

        struct Required {
            const char* name;
            bool ok;
        } required[] = {
            { "glCreateMemoryObjectsEXT", createMemoryObjects != nullptr },
            { "glDeleteMemoryObjectsEXT", deleteMemoryObjects != nullptr },
            { "glImportMemoryWin32HandleEXT", importMemoryWin32Handle != nullptr },
            { "glTexStorageMem2DEXT", texStorageMem2D != nullptr },
            { "glGenSemaphoresEXT", genSemaphores != nullptr },
            { "glDeleteSemaphoresEXT", deleteSemaphores != nullptr },
            { "glImportSemaphoreWin32HandleEXT", importSemaphoreWin32Handle != nullptr },
            { "glWaitSemaphoreEXT", waitSemaphore != nullptr },
            { "glSignalSemaphoreEXT", signalSemaphore != nullptr },
        };

        for (const Required& fn : required) {
            if (!fn.ok) {
                if (missing) *missing = QString::fromLatin1(fn.name);
                return false;
            }
        }
        return true;
    }
};

enum class SlotState {
    Empty,
    AvailableToGl,
    Rendering,
    ReadyForVulkan,
    Presenting,
    Displayed,
};

struct InteropSlot {
    pl_tex tex = nullptr;

    VkSemaphore vkToGlVk = VK_NULL_HANDLE;
    VkSemaphore glToVkVk = VK_NULL_HANDLE;
    HANDLE vkToGlHandle = nullptr;
    HANDLE glToVkHandle = nullptr;

    GLuint glTexture = 0;
    GLuint glFramebuffer = 0;
    GLuint glMemory = 0;
    GLuint glVkToGlSemaphore = 0;
    GLuint glToVkSemaphore = 0;
    bool glImported = false;

    int width = 0;
    int height = 0;
    std::size_t sharedSize = 0;
    std::size_t sharedOffset = 0;

    SlotState state = SlotState::Empty;
};

// MAKE_MPV_BEAT_FFMPEG Task 6 step 2 — libplacebo color management defaults,
// matched verbatim against native_sidecar/src/gpu_renderer.cpp:62-63 +
// 112-113. Constructed once per process. pl_color_map_default_params
// + pl_peak_detect_default_params provide libplacebo's curated defaults
// (BT.709/sRGB target, smooth dynamic peak detection); we don't tune
// them. The renderer points pl_render_params at these per-call so all
// playback shares one config. Foundation for HDR; effective once Task 3
// (16F texture) + Task 4 (mpv tone-map disable) + Task 5 (metadata
// bridge) land — until then the path stays SDR because mpv pre-tone-maps.
const pl_color_map_params kColorMapParams = pl_color_map_default_params;
const pl_peak_detect_params kPeakDetectParams = pl_peak_detect_default_params;

} // namespace

struct MpvLibplaceboRenderer::State {
    mpv_handle* mpv = nullptr;
    mpv_render_context* renderContext = nullptr;

    pl_gpu gpu = nullptr;
    pl_renderer renderer = nullptr;
    int width = 0;
    int height = 0;
    pl_fmt format = nullptr;

    std::vector<InteropSlot> interopSlots;

    std::thread renderThread;
    mutable std::mutex mutex;
    std::condition_variable cv;
    std::condition_variable startupCv;
    std::condition_variable importCv;
    std::condition_variable destroyCv;

    bool stopping = false;
    bool startupComplete = false;
    bool startupOk = false;
    QString startupError;

    bool updatePending = false;
    bool importPending = false;
    bool importComplete = false;
    bool importOk = false;
    QString importError;

    bool destroyGlPending = false;
    bool destroyGlComplete = false;

    bool interopBlocked = false;
    QString blockReason;

    std::atomic_bool contextReady{false};
    std::atomic_bool hasRenderedFrame{false};
    std::atomic_int glRenderCount{0};
    std::atomic_int presentCount{0};
    std::atomic_bool noFrameLogged{false};

    RenderScheduler scheduler;

    static void* getProcAddress(void* ctx, const char* name)
    {
        auto* gl = static_cast<QOpenGLContext*>(ctx);
        if (!gl || !name) return nullptr;
        return reinterpret_cast<void*>(gl->getProcAddress(QByteArray(name)));
    }

    static void renderUpdateCallback(void* ctx)
    {
        auto* self = static_cast<State*>(ctx);
        if (!self) return;
        {
            std::lock_guard<std::mutex> lock(self->mutex);
            self->updatePending = true;
        }
        self->cv.notify_one();
    }

    void callScheduler()
    {
        RenderScheduler copy;
        {
            std::lock_guard<std::mutex> lock(mutex);
            copy = scheduler;
        }
        if (copy) copy();
    }

    bool anyAvailableSlotLocked() const
    {
        return std::any_of(interopSlots.begin(), interopSlots.end(), [](const InteropSlot& slot) {
            return slot.glImported && slot.state == SlotState::AvailableToGl;
        });
    }

    void renderLoop()
    {
        QSurfaceFormat fmt;
        fmt.setRenderableType(QSurfaceFormat::OpenGL);
        fmt.setProfile(QSurfaceFormat::CoreProfile);
        fmt.setVersion(4, 5);
        fmt.setDepthBufferSize(0);
        fmt.setStencilBufferSize(0);

        QOpenGLContext context;
        context.setFormat(fmt);
        if (!context.create()) {
            finishStartup(false, QStringLiteral("QOpenGLContext::create failed"));
            return;
        }

        QOffscreenSurface surface;
        surface.setFormat(context.format());
        surface.create();
        if (!surface.isValid() || !context.makeCurrent(&surface)) {
            finishStartup(false, QStringLiteral("offscreen OpenGL surface/context activation failed"));
            return;
        }

        QOpenGLExtraFunctions* gl = context.extraFunctions();
        if (!gl) {
            finishStartup(false, QStringLiteral("QOpenGLExtraFunctions init failed"));
            return;
        }
        gl->initializeOpenGLFunctions();

#ifdef TANKOBAN_HDR_PROBE
        {
            // Step 1.2 — one-shot GL extension probe for HDR interop capability.
            QByteArray exts(reinterpret_cast<const char*>(gl->glGetString(GL_EXTENSIONS)));
            mprLog(QStringLiteral("[hdr-probe] GL_EXT_memory_object: %1").arg(exts.contains("GL_EXT_memory_object")));
            mprLog(QStringLiteral("[hdr-probe] GL_EXT_memory_object_win32: %1").arg(exts.contains("GL_EXT_memory_object_win32")));
            mprLog(QStringLiteral("[hdr-probe] GL_EXT_semaphore_win32: %1").arg(exts.contains("GL_EXT_semaphore_win32")));
        }
#endif

        GlInteropFns interopFns;
        QString missing;
        if (!interopFns.load(context, &missing)) {
            finishStartup(false,
                          QStringLiteral("required GL interop entry point missing: %1")
                              .arg(missing));
            return;
        }

        mpv_opengl_init_params glInit{};
        glInit.get_proc_address = &State::getProcAddress;
        glInit.get_proc_address_ctx = &context;

        char apiType[] = MPV_RENDER_API_TYPE_OPENGL;
        mpv_render_param params[] = {
            { MPV_RENDER_PARAM_API_TYPE, apiType },
            { MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glInit },
            { MPV_RENDER_PARAM_INVALID, nullptr },
        };

        const int rc = mpv_render_context_create(&renderContext, mpv, params);
        if (rc < 0 || !renderContext) {
            finishStartup(false,
                          QStringLiteral("OpenGL render context create failed: %1")
                              .arg(QString::fromUtf8(mpv_error_string(rc))));
            return;
        }

        mpv_render_context_set_update_callback(
            renderContext, &State::renderUpdateCallback, this);
        contextReady.store(true, std::memory_order_release);
        finishStartup(true, QString());
        mprLog(QStringLiteral("OpenGL render context ready on dedicated render thread"));

        while (true) {
            int slotIndex = -1;

            {
                std::unique_lock<std::mutex> lock(mutex);
                cv.wait(lock, [this]() {
                    return stopping || importPending || destroyGlPending
                        || (updatePending && anyAvailableSlotLocked());
                });

                if (stopping) break;

                if (destroyGlPending) {
                    destroyImportedGlLocked(gl, interopFns);
                    destroyGlPending = false;
                    destroyGlComplete = true;
                    destroyCv.notify_all();
                    continue;
                }

                if (importPending) {
                    importOk = importSlotsLocked(gl, interopFns);
                    importPending = false;
                    importComplete = true;
                    importCv.notify_all();
                    continue;
                }

                for (int i = 0; i < static_cast<int>(interopSlots.size()); ++i) {
                    if (interopSlots[i].glImported && interopSlots[i].state == SlotState::AvailableToGl) {
                        slotIndex = i;
                        interopSlots[i].state = SlotState::Rendering;
                        updatePending = false;
                        break;
                    }
                }
            }

            if (slotIndex < 0) continue;

            const bool rendered = renderOneSlot(slotIndex, gl, interopFns);
            RenderScheduler readyScheduler;
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (slotIndex >= 0 && slotIndex < static_cast<int>(interopSlots.size())) {
                    interopSlots[slotIndex].state = rendered
                        ? SlotState::ReadyForVulkan
                        : SlotState::AvailableToGl;
                }
                if (rendered) {
                    hasRenderedFrame.store(true, std::memory_order_release);
                    readyScheduler = scheduler;
                }
            }
            cv.notify_one();
            if (readyScheduler) readyScheduler();
        }

        mpv_render_context_set_update_callback(renderContext, nullptr, nullptr);
        mpv_render_context_free(renderContext);
        renderContext = nullptr;
        contextReady.store(false, std::memory_order_release);

        {
            std::lock_guard<std::mutex> lock(mutex);
            destroyImportedGlLocked(gl, interopFns);
        }

        context.doneCurrent();
    }

    void finishStartup(bool ok, const QString& error)
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            startupOk = ok;
            startupError = error;
            startupComplete = true;
        }
        startupCv.notify_all();
    }

    bool importSlotsLocked(QOpenGLExtraFunctions* gl, GlInteropFns& fns)
    {
        importError.clear();
        for (InteropSlot& slot : interopSlots) {
            if (slot.glImported) continue;

            HANDLE textureHandle = nullptr;
            HANDLE vkToGlHandle = nullptr;
            HANDLE glToVkHandle = nullptr;
            if (!duplicateHandleForImport(slot.tex->shared_mem.handle.handle, &textureHandle)
                || !duplicateHandleForImport(slot.vkToGlHandle, &vkToGlHandle)
                || !duplicateHandleForImport(slot.glToVkHandle, &glToVkHandle)) {
                if (textureHandle) CloseHandle(textureHandle);
                if (vkToGlHandle) CloseHandle(vkToGlHandle);
                if (glToVkHandle) CloseHandle(glToVkHandle);
                importError = QStringLiteral("DuplicateHandle failed for GL import");
                return false;
            }

            fns.createMemoryObjects(1, &slot.glMemory);
            fns.importMemoryWin32Handle(slot.glMemory,
                                        static_cast<GLuint64>(slot.sharedSize),
                                        GL_HANDLE_TYPE_OPAQUE_WIN32_EXT,
                                        textureHandle);

            gl->glGenTextures(1, &slot.glTexture);
            gl->glBindTexture(GL_TEXTURE_2D, slot.glTexture);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            fns.texStorageMem2D(GL_TEXTURE_2D, 1, GL_RGBA8,
                                slot.width, slot.height,
                                slot.glMemory,
                                static_cast<GLuint64>(slot.sharedOffset));

            gl->glGenFramebuffers(1, &slot.glFramebuffer);
            gl->glBindFramebuffer(GL_FRAMEBUFFER, slot.glFramebuffer);
            gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                       GL_TEXTURE_2D, slot.glTexture, 0);
            if (gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                importError = QStringLiteral("external-memory FBO is incomplete");
                return false;
            }

            fns.genSemaphores(1, &slot.glVkToGlSemaphore);
            fns.importSemaphoreWin32Handle(slot.glVkToGlSemaphore,
                                           GL_HANDLE_TYPE_OPAQUE_WIN32_EXT,
                                           vkToGlHandle);
            fns.genSemaphores(1, &slot.glToVkSemaphore);
            fns.importSemaphoreWin32Handle(slot.glToVkSemaphore,
                                           GL_HANDLE_TYPE_OPAQUE_WIN32_EXT,
                                           glToVkHandle);

            const GLenum err = gl->glGetError();
            if (err != GL_NO_ERROR) {
                importError = QStringLiteral("GL external-memory import failed, glGetError=0x%1")
                                  .arg(static_cast<unsigned>(err), 0, 16);
                return false;
            }

            slot.glImported = true;
        }

        gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
        gl->glBindTexture(GL_TEXTURE_2D, 0);
        return true;
    }

    void destroyImportedGlLocked(QOpenGLExtraFunctions* gl, GlInteropFns& fns)
    {
        for (InteropSlot& slot : interopSlots) {
            if (slot.glFramebuffer) {
                gl->glDeleteFramebuffers(1, &slot.glFramebuffer);
                slot.glFramebuffer = 0;
            }
            if (slot.glTexture) {
                gl->glDeleteTextures(1, &slot.glTexture);
                slot.glTexture = 0;
            }
            if (slot.glVkToGlSemaphore) {
                fns.deleteSemaphores(1, &slot.glVkToGlSemaphore);
                slot.glVkToGlSemaphore = 0;
            }
            if (slot.glToVkSemaphore) {
                fns.deleteSemaphores(1, &slot.glToVkSemaphore);
                slot.glToVkSemaphore = 0;
            }
            if (slot.glMemory) {
                fns.deleteMemoryObjects(1, &slot.glMemory);
                slot.glMemory = 0;
            }
            slot.glImported = false;
            if (slot.state != SlotState::Empty) {
                slot.state = SlotState::AvailableToGl;
            }
        }
    }

    bool renderOneSlot(int slotIndex, QOpenGLExtraFunctions* gl, GlInteropFns& fns)
    {
        InteropSlot slotCopy;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (slotIndex < 0 || slotIndex >= static_cast<int>(interopSlots.size())) {
                return false;
            }
            slotCopy = interopSlots[slotIndex];
        }

        const uint64_t flags = mpv_render_context_update(renderContext);
        if (!(flags & MPV_RENDER_UPDATE_FRAME) && !hasRenderedFrame.load(std::memory_order_acquire)) {
            bool expected = false;
            if (noFrameLogged.compare_exchange_strong(expected, true)) {
                mprLog(QStringLiteral("mpv update had no frame before first GL render"));
            }
            return false;
        }

        const GLenum colorLayout = GL_LAYOUT_COLOR_ATTACHMENT_EXT;
        fns.waitSemaphore(slotCopy.glVkToGlSemaphore, 0, nullptr,
                          1, &slotCopy.glTexture, &colorLayout);

        gl->glBindFramebuffer(GL_FRAMEBUFFER, slotCopy.glFramebuffer);
        gl->glViewport(0, 0, slotCopy.width, slotCopy.height);
        const GLfloat clear[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        gl->glClearBufferfv(GL_COLOR, 0, clear);

        mpv_opengl_fbo fbo{};
        fbo.fbo = static_cast<int>(slotCopy.glFramebuffer);
        fbo.w = slotCopy.width;
        fbo.h = slotCopy.height;
        fbo.internal_format = GL_RGBA8;
        int flipY = 0;
        mpv_render_param params[] = {
            { MPV_RENDER_PARAM_OPENGL_FBO, &fbo },
            { MPV_RENDER_PARAM_FLIP_Y, &flipY },
            { MPV_RENDER_PARAM_INVALID, nullptr },
        };

        const int rc = mpv_render_context_render(renderContext, params);
        if (rc < 0) {
            mprLog(QStringLiteral("OpenGL mpv render failed: %1")
                       .arg(QString::fromUtf8(mpv_error_string(rc))));
            return false;
        }

        const GLenum shaderReadLayout = GL_LAYOUT_SHADER_READ_ONLY_EXT;
        fns.signalSemaphore(slotCopy.glToVkSemaphore, 0, nullptr,
                            1, &slotCopy.glTexture, &shaderReadLayout);
        gl->glFlush();
        const GLenum err = gl->glGetError();
        if (err != GL_NO_ERROR) {
            mprLog(QStringLiteral("GL render finished with glGetError=0x%1")
                       .arg(static_cast<unsigned>(err), 0, 16));
            return false;
        }

        const int count = glRenderCount.fetch_add(1, std::memory_order_relaxed) + 1;
        if (count <= 3) {
            mprLog(QStringLiteral("GL mpv render succeeded into interop slot %1 (%2x%3)")
                       .arg(slotIndex).arg(slotCopy.width).arg(slotCopy.height));
        }
        return true;
    }
};

MpvLibplaceboRenderer::MpvLibplaceboRenderer()
    : m_state(std::make_unique<State>())
{
}

MpvLibplaceboRenderer::~MpvLibplaceboRenderer()
{
    detachMpv();
}

bool MpvLibplaceboRenderer::attachMpv(mpv_handle* mpv)
{
    detachMpv();
    if (!mpv) return false;

    m_state = std::make_unique<State>();
    m_state->mpv = mpv;
    m_state->renderThread = std::thread([state = m_state.get()]() {
        state->renderLoop();
    });

    std::unique_lock<std::mutex> lock(m_state->mutex);
    m_state->startupCv.wait(lock, [this]() {
        return m_state->startupComplete;
    });

    if (!m_state->startupOk) {
        const QString error = m_state->startupError;
        lock.unlock();
        mprLog(error);
        detachMpv();
        return false;
    }

    return true;
}

void MpvLibplaceboRenderer::detachMpv()
{
    if (!m_state) return;

    {
        std::lock_guard<std::mutex> lock(m_state->mutex);
        m_state->scheduler = nullptr;
    }

    detachGpu();

    {
        std::lock_guard<std::mutex> lock(m_state->mutex);
        m_state->stopping = true;
        m_state->updatePending = false;
    }
    m_state->cv.notify_all();

    if (m_state->renderThread.joinable()) {
        m_state->renderThread.join();
    }

    m_state->mpv = nullptr;
    m_state->contextReady.store(false, std::memory_order_release);
    m_state->hasRenderedFrame.store(false, std::memory_order_release);
}

void MpvLibplaceboRenderer::setRenderScheduler(RenderScheduler scheduler)
{
    if (!m_state) return;

    bool shouldSchedule = false;
    {
        std::lock_guard<std::mutex> lock(m_state->mutex);
        m_state->scheduler = std::move(scheduler);
        shouldSchedule = std::any_of(m_state->interopSlots.begin(), m_state->interopSlots.end(),
                                     [](const InteropSlot& slot) {
                                         return slot.state == SlotState::ReadyForVulkan;
                                     });
    }

    if (shouldSchedule) {
        m_state->callScheduler();
    }
}

void MpvLibplaceboRenderer::detachGpu(pl_gpu gpu)
{
    if (!m_state) return;
    if (gpu && m_state->gpu && gpu != m_state->gpu) return;

    {
        std::unique_lock<std::mutex> lock(m_state->mutex);
        if (m_state->renderThread.joinable() && !m_state->interopSlots.empty()) {
            m_state->destroyGlPending = true;
            m_state->destroyGlComplete = false;
            m_state->cv.notify_all();
            m_state->destroyCv.wait(lock, [this]() {
                return m_state->destroyGlComplete || m_state->stopping;
            });
        }
    }

    if (m_state->gpu) {
        for (InteropSlot& slot : m_state->interopSlots) {
            if (slot.tex) {
                pl_tex_destroy(m_state->gpu, &slot.tex);
            }
            if (slot.vkToGlVk != VK_NULL_HANDLE) {
                pl_vulkan_sem_destroy(m_state->gpu, &slot.vkToGlVk);
            }
            if (slot.glToVkVk != VK_NULL_HANDLE) {
                pl_vulkan_sem_destroy(m_state->gpu, &slot.glToVkVk);
            }
            if (slot.vkToGlHandle) {
                CloseHandle(slot.vkToGlHandle);
                slot.vkToGlHandle = nullptr;
            }
            if (slot.glToVkHandle) {
                CloseHandle(slot.glToVkHandle);
                slot.glToVkHandle = nullptr;
            }
        }
    }

    if (m_state->renderer) {
        pl_renderer_destroy(&m_state->renderer);
    }

    m_state->interopSlots.clear();
    m_state->gpu = nullptr;
    m_state->format = nullptr;
    m_state->width = 0;
    m_state->height = 0;
    m_state->interopBlocked = false;
    m_state->blockReason.clear();
}

void MpvLibplaceboRenderer::resetFrameState()
{
    if (!m_state) return;

    {
        std::lock_guard<std::mutex> lock(m_state->mutex);
        m_state->updatePending = false;
    }
    m_state->hasRenderedFrame.store(false, std::memory_order_release);
    detachGpu();
}

bool MpvLibplaceboRenderer::renderToSwapchain(pl_log log,
                                              pl_gpu gpu,
                                              const pl_swapchain_frame& frame,
                                              int width,
                                              int height)
{
    if (!m_state || !m_state->contextReady.load(std::memory_order_acquire)
        || !gpu || !frame.fbo || width <= 0 || height <= 0) {
        return false;
    }

    if (!m_state->gpu || m_state->gpu != gpu
        || m_state->width != width || m_state->height != height) {
        detachGpu(m_state->gpu);
        m_state->gpu = gpu;

        const pl_handle_caps required = PL_HANDLE_WIN32;
        if ((gpu->export_caps.tex & required) == 0) {
            m_state->interopBlocked = true;
            m_state->blockReason = QStringLiteral("libplacebo Vulkan GPU cannot export WIN32 textures");
        } else if ((gpu->export_caps.sync & required) == 0) {
            m_state->interopBlocked = true;
            m_state->blockReason = QStringLiteral("libplacebo Vulkan GPU cannot export WIN32 semaphores");
        }

        if (m_state->interopBlocked) {
            mprLog(QStringLiteral("OpenGL/Vulkan interop blocked: %1")
                       .arg(m_state->blockReason));
            return false;
        }

#ifdef TANKOBAN_HDR_PROBE
        {
            // Step 1.1 — one-shot Vulkan/libplacebo probe for RGBA16F exportable format.
            static bool probed = false;
            if (!probed) {
                probed = true;
                const pl_fmt fmt16f = pl_find_fmt(gpu, PL_FMT_FLOAT, 4, 16, 16,
                    static_cast<pl_fmt_caps>(PL_FMT_CAP_SAMPLEABLE | PL_FMT_CAP_RENDERABLE | PL_FMT_CAP_HOST_READABLE));
                mprLog(QStringLiteral("[hdr-probe] PL_FMT_FLOAT 16/16/4: %1").arg(fmt16f ? fmt16f->name : "NOT FOUND"));
                if (fmt16f) {
                    pl_tex_params tp{};
                    tp.w = 1920;
                    tp.h = 1080;
                    tp.format = fmt16f;
                    tp.sampleable = true;
                    tp.renderable = true;
                    tp.export_handle = PL_HANDLE_WIN32;
                    pl_tex probe = pl_tex_create(gpu, &tp);
                    mprLog(QStringLiteral("[hdr-probe] WIN32-exportable RGBA16F tex: %1")
                        .arg(probe && probe->shared_mem.handle.handle ? "YES" : "NO"));
                    if (probe) pl_tex_destroy(gpu, &probe);
                }
            }
        }
#endif

        m_state->format = pl_find_fmt(
            gpu, PL_FMT_UNORM, 4, 8, 8,
            static_cast<pl_fmt_caps>(PL_FMT_CAP_SAMPLEABLE | PL_FMT_CAP_RENDERABLE));
        if (!m_state->format) {
            m_state->interopBlocked = true;
            m_state->blockReason = QStringLiteral("no RGBA8 sampleable+renderable Vulkan format");
            mprLog(QStringLiteral("OpenGL/Vulkan interop blocked: %1")
                       .arg(m_state->blockReason));
            return false;
        }

        m_state->interopSlots.resize(kInteropSlots);
        for (InteropSlot& slot : m_state->interopSlots) {
            slot.width = width;
            slot.height = height;
            slot.state = SlotState::Empty;

            pl_tex_params tp{};
            tp.w = width;
            tp.h = height;
            tp.format = m_state->format;
            tp.sampleable = true;
            tp.renderable = true;
            tp.export_handle = PL_HANDLE_WIN32;
            slot.tex = pl_tex_create(gpu, &tp);
            if (!slot.tex || !slot.tex->shared_mem.handle.handle
                || slot.tex->shared_mem.size == 0) {
                mprLog(QStringLiteral("pl_tex_create/export WIN32 failed for interop slot"));
                detachGpu(gpu);
                return false;
            }
            slot.sharedSize = slot.tex->shared_mem.size;
            slot.sharedOffset = slot.tex->shared_mem.offset;

            union pl_handle vkToGlHandle{};
            pl_vulkan_sem_params vkToGlParams{};
            vkToGlParams.type = VK_SEMAPHORE_TYPE_BINARY;
            vkToGlParams.export_handle = PL_HANDLE_WIN32;
            vkToGlParams.out_handle = &vkToGlHandle;
            slot.vkToGlVk = pl_vulkan_sem_create(gpu, &vkToGlParams);
            slot.vkToGlHandle = static_cast<HANDLE>(vkToGlHandle.handle);

            union pl_handle glToVkHandle{};
            pl_vulkan_sem_params glToVkParams{};
            glToVkParams.type = VK_SEMAPHORE_TYPE_BINARY;
            glToVkParams.export_handle = PL_HANDLE_WIN32;
            glToVkParams.out_handle = &glToVkHandle;
            slot.glToVkVk = pl_vulkan_sem_create(gpu, &glToVkParams);
            slot.glToVkHandle = static_cast<HANDLE>(glToVkHandle.handle);

            if (slot.vkToGlVk == VK_NULL_HANDLE || slot.glToVkVk == VK_NULL_HANDLE
                || !slot.vkToGlHandle || !slot.glToVkHandle) {
                mprLog(QStringLiteral("WIN32 semaphore export failed for interop slot"));
                detachGpu(gpu);
                return false;
            }

            pl_vulkan_hold_params holdParams{};
            holdParams.tex = slot.tex;
            holdParams.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            holdParams.qf = VK_QUEUE_FAMILY_EXTERNAL;
            holdParams.semaphore = { slot.vkToGlVk, 0 };
            if (!pl_vulkan_hold_ex(gpu, &holdParams)) {
                mprLog(QStringLiteral("pl_vulkan_hold_ex failed for initial GL ownership"));
                detachGpu(gpu);
                return false;
            }
            slot.state = SlotState::AvailableToGl;
        }

        {
            std::unique_lock<std::mutex> lock(m_state->mutex);
            m_state->importPending = true;
            m_state->importComplete = false;
            m_state->importOk = false;
            m_state->importError.clear();
            m_state->cv.notify_all();
            m_state->importCv.wait(lock, [this]() {
                return m_state->importComplete || m_state->stopping;
            });
            if (!m_state->importOk) {
                const QString error = m_state->importError;
                lock.unlock();
                mprLog(QStringLiteral("OpenGL/Vulkan interop import failed: %1").arg(error));
                detachGpu(gpu);
                return false;
            }
        }

        m_state->width = width;
        m_state->height = height;
        mprLog(QStringLiteral("OpenGL/Vulkan shared texture ring ready: %1 interopSlots %2x%3")
                   .arg(kInteropSlots).arg(width).arg(height));
    }

    if (!m_state->renderer) {
        m_state->renderer = pl_renderer_create(log, gpu);
        if (!m_state->renderer) {
            mprLog(QStringLiteral("pl_renderer_create failed"));
            return false;
        }
    }

    int slotIndex = -1;
    bool consumingNewFrame = false;
    {
        std::lock_guard<std::mutex> lock(m_state->mutex);
        for (int i = 0; i < static_cast<int>(m_state->interopSlots.size()); ++i) {
            if (m_state->interopSlots[i].state == SlotState::ReadyForVulkan) {
                slotIndex = i;
                m_state->interopSlots[i].state = SlotState::Presenting;
                consumingNewFrame = true;
                break;
            }
        }
        if (slotIndex < 0) {
            for (int i = 0; i < static_cast<int>(m_state->interopSlots.size()); ++i) {
                if (m_state->interopSlots[i].state == SlotState::Displayed) {
                    slotIndex = i;
                    break;
                }
            }
        }
    }

    if (slotIndex < 0) {
        m_state->cv.notify_one();
        return false;
    }

    InteropSlot* slot = &m_state->interopSlots[slotIndex];
    if (consumingNewFrame) {
        pl_vulkan_release_params releaseParams{};
        releaseParams.tex = slot->tex;
        releaseParams.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        releaseParams.qf = VK_QUEUE_FAMILY_EXTERNAL;
        releaseParams.semaphore = { slot->glToVkVk, 0 };
        pl_vulkan_release_ex(gpu, &releaseParams);
    }

    pl_frame src{};
    src.num_planes = 1;
    src.planes[0].texture = slot->tex;
    src.planes[0].flipped = false;
    src.planes[0].components = 3;
    src.planes[0].component_mapping[0] = PL_CHANNEL_R;
    src.planes[0].component_mapping[1] = PL_CHANNEL_G;
    src.planes[0].component_mapping[2] = PL_CHANNEL_B;
    src.planes[0].component_mapping[3] = PL_CHANNEL_NONE;
    src.repr.sys = PL_COLOR_SYSTEM_RGB;
    src.repr.levels = PL_COLOR_LEVELS_FULL;
    src.repr.alpha = PL_ALPHA_NONE;
    src.repr.bits.sample_depth = 8;
    src.repr.bits.color_depth = 8;
    src.color = pl_color_space_srgb;
    src.crop.x0 = 0.0f;
    src.crop.y0 = 0.0f;
    src.crop.x1 = static_cast<float>(width);
    src.crop.y1 = static_cast<float>(height);

    pl_frame target{};
    pl_frame_from_swapchain(&target, &frame);

    // MAKE_MPV_BEAT_FFMPEG Task 5 + Task 6 step 2 (2026-05-02) — match the
    // ffmpeg sidecar's libplacebo scaler + color management config at
    // native_sidecar/src/gpu_renderer.cpp:58-63 + 108-114. Task 5 added
    // scalers (ewa_lanczossharp upscale + hermite downscale); Task 6 step
    // 2 adds color_map_params + peak_detect_params (foundation — effective
    // once Tasks 3-5 deliver HDR-capable RGBA16F texture + mpv tone-map
    // disable + metadata bridge).
    //
    // pl_render_default_params is the base (NOT pl_render_fast_params).
    // Task 3.5 originally shipped pl_render_fast_params (CHEAP preset =
    // bilinear scalers, the deliberate Tier-0 floor from MAKE_MPV_SOLO
    // Task 12.B). After Task 4 measured the new pipeline at 0.000 drops/sec
    // on Community SDR, we had the GPU budget to flip to default + the
    // sidecar's high-quality scaler config — which Hemanth's eyeball-
    // verified GREEN on Sopranos S06E04 ("they both look the same...
    // pretty much as good as can be") at Task 5 close. The default base
    // also keeps the apples-to-apples comparison with the sidecar
    // reference: same baseline + same scalers + same color params.
    pl_render_params params = pl_render_default_params;
    params.upscaler           = &pl_filter_ewa_lanczossharp;
    params.downscaler         = &pl_filter_hermite;
    params.color_map_params   = &kColorMapParams;
    params.peak_detect_params = &kPeakDetectParams;

    const bool rendered = pl_render_image(
        m_state->renderer, &src, &target, &params);
    if (!rendered) {
        mprLog(QStringLiteral("pl_render_image failed for OpenGL interop frame"));
    } else {
        const int count = m_state->presentCount.fetch_add(1, std::memory_order_relaxed) + 1;
        if (count <= 3) {
            mprLog(QStringLiteral("Vulkan composite consumed OpenGL interop slot %1")
                       .arg(slotIndex));
        }
    }

    if (!rendered && consumingNewFrame) {
        pl_vulkan_hold_params holdParams{};
        holdParams.tex = slot->tex;
        holdParams.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        holdParams.qf = VK_QUEUE_FAMILY_EXTERNAL;
        holdParams.semaphore = { slot->vkToGlVk, 0 };
        if (!pl_vulkan_hold_ex(gpu, &holdParams)) {
            mprLog(QStringLiteral("pl_vulkan_hold_ex failed while returning failed slot to GL"));
        }

        {
            std::lock_guard<std::mutex> lock(m_state->mutex);
            slot->state = SlotState::AvailableToGl;
        }
        m_state->cv.notify_one();
    }
    return rendered;
}

void MpvLibplaceboRenderer::finishPresentedFrame(pl_gpu gpu)
{
    if (!m_state || !gpu || gpu != m_state->gpu) return;

    bool returnedAny = false;
    {
        std::lock_guard<std::mutex> lock(m_state->mutex);
        for (InteropSlot& slot : m_state->interopSlots) {
            if (slot.state != SlotState::Presenting) continue;

            for (InteropSlot& oldSlot : m_state->interopSlots) {
                if (&oldSlot == &slot || oldSlot.state != SlotState::Displayed) {
                    continue;
                }

                pl_vulkan_hold_params oldHoldParams{};
                oldHoldParams.tex = oldSlot.tex;
                oldHoldParams.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                oldHoldParams.qf = VK_QUEUE_FAMILY_EXTERNAL;
                oldHoldParams.semaphore = { oldSlot.vkToGlVk, 0 };
                if (!pl_vulkan_hold_ex(gpu, &oldHoldParams)) {
                    mprLog(QStringLiteral("pl_vulkan_hold_ex failed while retiring displayed slot"));
                }
                oldSlot.state = SlotState::AvailableToGl;
                returnedAny = true;
            }

            slot.state = SlotState::Displayed;
        }
    }

    if (returnedAny) {
        m_state->cv.notify_one();
    }
}

bool MpvLibplaceboRenderer::hasRenderedFrame() const
{
    return m_state && m_state->hasRenderedFrame.load(std::memory_order_acquire);
}

bool MpvLibplaceboRenderer::swContextReady() const
{
    return m_state && m_state->contextReady.load(std::memory_order_acquire);
}

