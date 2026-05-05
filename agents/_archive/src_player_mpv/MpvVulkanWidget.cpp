#include "MpvVulkanWidget.h"

#include "core/DebugLogBuffer.h"
#include "ui/player/MpvLibplaceboRenderer.h"

#include <QMetaObject>
#include <QResizeEvent>
#include <QShowEvent>
#include <QHideEvent>

#include <cstring>

#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#include <windows.h>
#endif

#include <vulkan/vulkan.h>

extern "C" {
#include <libplacebo/log.h>
#include <libplacebo/vulkan.h>
#include <libplacebo/swapchain.h>
#include <libplacebo/gpu.h>
}

namespace {

void mvwLog(const QString& msg) {
    DebugLogBuffer::instance().info("MpvVulkanWidget", msg);
}

// libplacebo's pl_log callback adapter — routes libplacebo-internal warnings
// + errors into Tankoban's debug ring buffer for diagnostics. PL_LOG_WARN
// matches the level used by gpu_renderer.cpp for the sidecar's libplacebo.
void plLogCallback(void* /*priv*/, enum pl_log_level level, const char* msg) {
    QString s = QStringLiteral("[pl] ") + QString::fromUtf8(msg);
    if (level <= PL_LOG_WARN) {
        DebugLogBuffer::instance().info("MpvVulkanWidget", s);
    }
}

} // namespace

class MpvVulkanWidget::Impl {
public:
    pl_log         pl_log_handle = nullptr;
    pl_vulkan      vk            = nullptr;
    VkSurfaceKHR   surface       = VK_NULL_HANDLE;
    pl_swapchain   swap          = nullptr;

    int last_width  = 0;
    int last_height = 0;

    Impl() = default;
    ~Impl() = default;
};

MpvVulkanWidget::MpvVulkanWidget(QWidget* parent)
    : QWidget(parent), m_impl(std::make_unique<Impl>())
{
    // FrameCanvas pattern — separate native HWND, opt out of Qt compositing.
    // Vulkan swapchain owns pixels in our HWND directly; no QPainter / no
    // double-buffer through Qt's parent-widget compositing.
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_NoSystemBackground);
    setAutoFillBackground(false);
    // Keep WA_OpaquePaintEvent off. FrameCanvas leaves it off too; marking a
    // full-size native child opaque lets Qt skip parent backing-store paint,
    // which breaks HUD alpha over the player surface on Windows.

    // Native child HWNDs do not bubble mouse moves to VideoPlayer. Match
    // FrameCanvas so HUD/cursor lifecycle is identical on both backends.
    setMouseTracking(true);

    // 60Hz render timer. Replaced by mpv-render-callback-driven schedule
    // in Task 3+. For Task 2 just keeps clearing-to-black at vsync rate.
    m_renderTimer.setTimerType(Qt::PreciseTimer);
    m_renderTimer.setInterval(16);
    connect(&m_renderTimer, &QTimer::timeout,
            this, &MpvVulkanWidget::onRenderTick);
}

MpvVulkanWidget::~MpvVulkanWidget()
{
    m_renderTimer.stop();
    setLibplaceboRenderer(nullptr);
    teardownVulkan();
}

void MpvVulkanWidget::setMpvHandle(mpv_handle* mpv)
{
    // Task 2: store but unused. Task 3 wires this into the render path
    // (mpv frame readout via SW render API → upload to Vulkan texture →
    // libplacebo composite + present).
    m_mpv = mpv;
    if (!m_mpv) {
        setLibplaceboRenderer(nullptr);
        m_firstFrameEmitted = false;
    }
}

void MpvVulkanWidget::setLibplaceboRenderer(MpvLibplaceboRenderer* renderer)
{
    if (m_renderer == renderer) return;

    if (m_renderer) {
        m_renderer->setRenderScheduler(nullptr);
        if (m_impl && m_impl->vk) {
            m_renderer->detachGpu(m_impl->vk->gpu);
        }
    }

    m_renderer = renderer;
    m_firstFrameEmitted = false;

    if (!m_renderer) return;

    m_renderer->setRenderScheduler([this]() {
        QMetaObject::invokeMethod(this, [this]() {
            emit mpvUpdateRequested();
            onRenderTick();
        }, Qt::QueuedConnection);
    });
}

void MpvVulkanWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);

    // First show — winId() now valid (HWND realized). Init Vulkan once.
    if (!m_vulkanReady) {
        if (initVulkan()) {
            m_vulkanReady = true;
            m_renderTimer.start();
            mvwLog(QStringLiteral("Vulkan + libplacebo + swapchain ready; render timer started"));
        } else {
            mvwLog(QStringLiteral("ERROR: Vulkan init failed; widget will stay black"));
        }
    } else {
        // Re-show after hide — restart render timer.
        if (!m_renderTimer.isActive()) m_renderTimer.start();
    }
}

void MpvVulkanWidget::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    // Stop rendering when not visible — saves GPU + battery.
    m_renderTimer.stop();
}

void MpvVulkanWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    // resizeEvent is informational only — onRenderTick's per-frame
    // pl_swapchain_resize call handles the actual swapchain sync, so
    // the swapchain catches up regardless of resize-vs-init ordering.
}

// Agent 3 2026-05-02 ~13:55 — Codex's mouse-forwarding overrides (mouseMoveEvent
// + nativeEvent for WM_MOUSEMOVE) caused Tankoban to crash ~10s into startup
// in Qt6Core.dll (FATAL_USER_CALLBACK 0xc000041d). Suspected root cause:
// nativeEvent's `winId()` call recurses into Qt's window-handle creation
// during the early MainWindow construction window, before VideoPlayer is
// fully wired. Disabled until a safer mouse-bridge shape is designed.
// HUD-reveal-on-mouse-over for the mpv path is temporarily lost; HUD still
// reveals via keyboard (Space/F/L/etc) and via the `m_canvas` Qt fallback
// path which is unaffected.
//
// void MpvVulkanWidget::mouseMoveEvent(QMouseEvent* event)
// {
//     emit mouseActivityAt(event->position().y());
//     QWidget::mouseMoveEvent(event);
// }
//
// #ifdef _WIN32
// bool MpvVulkanWidget::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
// {
//     if (eventType == "windows_generic_MSG" || eventType == "windows_dispatcher_MSG") {
//         MSG* msg = static_cast<MSG*>(message);
//         if (msg && msg->hwnd == reinterpret_cast<HWND>(winId())
//             && msg->message == WM_MOUSEMOVE) {
//             const int y = static_cast<short>(HIWORD(static_cast<DWORD_PTR>(msg->lParam)));
//             emit mouseActivityAt(y);
//         }
//     }
//     return QWidget::nativeEvent(eventType, message, result);
// }
// #endif

bool MpvVulkanWidget::initVulkan()
{
#ifndef _WIN32
    mvwLog(QStringLiteral("ERROR: non-Windows path not implemented"));
    return false;
#else
    // ── 1. libplacebo log ─────────────────────────────────────────────────
    struct pl_log_params log_p{};
    log_p.log_cb    = plLogCallback;
    log_p.log_level = PL_LOG_WARN;

    m_impl->pl_log_handle = pl_log_create(PL_API_VER, &log_p);
    if (!m_impl->pl_log_handle) {
        mvwLog(QStringLiteral("pl_log_create failed"));
        return false;
    }

    // ── 2. libplacebo Vulkan instance + device + GPU ──────────────────────
    // pl_vulkan_params has TWO `extensions` fields with different scopes —
    // a foot-gun. `instance_params->extensions` is for INSTANCE extensions
    // (VK_KHR_surface, VK_KHR_win32_surface — needed so vkCreateWin32SurfaceKHR
    // resolves in step 3). `pl_vulkan_params.extensions` is for DEVICE
    // extensions (VK_KHR_swapchain — needed so pl_vulkan_create_swapchain
    // works in step 4).
    //
    // First-attempt bug 2026-05-02: passed surface extensions on the device
    // field, libplacebo tried to enable them on VkDevice, got
    // VK_ERROR_EXTENSION_NOT_PRESENT. Fix below routes each to the right
    // scope.
    static const char* instance_extensions[] = {
        "VK_KHR_surface",
        "VK_KHR_win32_surface",
    };
    static const char* device_extensions[] = {
        "VK_KHR_swapchain",
    };

    struct pl_vk_inst_params inst_p{};
    inst_p.extensions     = instance_extensions;
    inst_p.num_extensions = static_cast<int>(sizeof(instance_extensions) / sizeof(instance_extensions[0]));

    struct pl_vulkan_params vk_p = pl_vulkan_default_params;
    vk_p.allow_software   = false;
    vk_p.instance_params  = &inst_p;
    vk_p.extensions       = device_extensions;
    vk_p.num_extensions   = static_cast<int>(sizeof(device_extensions) / sizeof(device_extensions[0]));

    m_impl->vk = pl_vulkan_create(m_impl->pl_log_handle, &vk_p);
    if (!m_impl->vk) {
        mvwLog(QStringLiteral("pl_vulkan_create failed (no Vulkan device?)"));
        teardownVulkan();
        return false;
    }

    // ── 3. VkSurface from our HWND ───────────────────────────────────────
    HWND hwnd = reinterpret_cast<HWND>(winId());
    if (!hwnd) {
        mvwLog(QStringLiteral("winId() returned null HWND — widget not realized?"));
        teardownVulkan();
        return false;
    }

    auto vkCreateWin32SurfaceKHR_fn =
        reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(
            m_impl->vk->get_proc_addr(m_impl->vk->instance,
                                       "vkCreateWin32SurfaceKHR"));
    if (!vkCreateWin32SurfaceKHR_fn) {
        mvwLog(QStringLiteral("vkCreateWin32SurfaceKHR not available — extension not enabled?"));
        teardownVulkan();
        return false;
    }

    VkWin32SurfaceCreateInfoKHR sci{};
    sci.sType     = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    sci.hinstance = GetModuleHandleW(nullptr);
    sci.hwnd      = hwnd;
    if (vkCreateWin32SurfaceKHR_fn(m_impl->vk->instance, &sci, nullptr,
                                   &m_impl->surface) != VK_SUCCESS) {
        mvwLog(QStringLiteral("vkCreateWin32SurfaceKHR failed"));
        teardownVulkan();
        return false;
    }

    // ── 4. libplacebo swapchain on the surface ────────────────────────────
    struct pl_vulkan_swapchain_params sw_p{};
    sw_p.surface         = m_impl->surface;
    sw_p.present_mode    = VK_PRESENT_MODE_FIFO_KHR;  // vsync — falls back if unsupported
    sw_p.swapchain_depth = 3;

    m_impl->swap = pl_vulkan_create_swapchain(m_impl->vk, &sw_p);
    if (!m_impl->swap) {
        mvwLog(QStringLiteral("pl_vulkan_create_swapchain failed"));
        teardownVulkan();
        return false;
    }

    // ── 5. Initial size ───────────────────────────────────────────────────
    int w = width();
    int h = height();
    if (w <= 0) w = 1;
    if (h <= 0) h = 1;
    int rw = w, rh = h;
    pl_swapchain_resize(m_impl->swap, &rw, &rh);
    m_impl->last_width  = rw;
    m_impl->last_height = rh;

    mvwLog(QStringLiteral("init OK: pl_vulkan + surface + swapchain (%1x%2)")
               .arg(rw).arg(rh));
    return true;
#endif
}

void MpvVulkanWidget::teardownVulkan()
{
    // Destroy in reverse order of creation. Safe to call even if some
    // members are null (early init failure path).
    if (m_renderer && m_impl->vk) {
        m_renderer->detachGpu(m_impl->vk->gpu);
    }

    if (m_impl->swap) {
        pl_swapchain_destroy(&m_impl->swap);
        m_impl->swap = nullptr;
    }

#ifdef _WIN32
    if (m_impl->surface != VK_NULL_HANDLE && m_impl->vk) {
        auto vkDestroySurfaceKHR_fn =
            reinterpret_cast<PFN_vkDestroySurfaceKHR>(
                m_impl->vk->get_proc_addr(m_impl->vk->instance,
                                           "vkDestroySurfaceKHR"));
        if (vkDestroySurfaceKHR_fn) {
            vkDestroySurfaceKHR_fn(m_impl->vk->instance, m_impl->surface, nullptr);
        }
        m_impl->surface = VK_NULL_HANDLE;
    }
#endif

    if (m_impl->vk) {
        pl_vulkan_destroy(&m_impl->vk);
        m_impl->vk = nullptr;
    }
    if (m_impl->pl_log_handle) {
        pl_log_destroy(&m_impl->pl_log_handle);
        m_impl->pl_log_handle = nullptr;
    }

    m_vulkanReady       = false;
    m_firstFrameEmitted = false;
    m_impl->last_width  = 0;
    m_impl->last_height = 0;
}

void MpvVulkanWidget::resizeSwapchain(int width, int height)
{
    if (!m_impl->swap) return;
    int w = width;
    int h = height;
    if (!pl_swapchain_resize(m_impl->swap, &w, &h)) {
        mvwLog(QStringLiteral("pl_swapchain_resize failed for %1x%2").arg(width).arg(height));
        return;
    }
    m_impl->last_width  = w;
    m_impl->last_height = h;
}

void MpvVulkanWidget::onRenderTick()
{
    if (!m_vulkanReady || !m_impl->swap || !m_impl->vk) return;

    // Sync swapchain dimensions to widget Qt-reported size before drawing.
    // Why: at initVulkan time the native HWND may not yet be at its final
    // size (Qt's setGeometry updates the QWidget logical geometry but the
    // native MoveWindow call is queued). vkGetPhysicalDeviceSurfaceCapabilitiesKHR
    // reads HWND's actual client area, so the initial swapchain locks at
    // the pre-realization HWND size (e.g. 150x45). Calling pl_swapchain_resize
    // on every tick lets libplacebo catch up once the HWND grows. Cheap when
    // size already matches (libplacebo internally no-ops).
    {
        const int w = width();
        const int h = height();
        // Track requested size separately from libplacebo-returned size:
        // libplacebo may return a different actual size due to DPI scaling
        // or surface capability constraints (e.g. request 1280x672 logical,
        // libplacebo returns 1920x1008 physical pixels). If we stored the
        // returned size as last_*, the next tick's request would always
        // mismatch and trigger a redundant resize. Compare against last
        // REQUESTED instead.
        if (w > 0 && h > 0 && (w != m_lastRequestedW || h != m_lastRequestedH)) {
            int rw = w, rh = h;
            const bool ok = pl_swapchain_resize(m_impl->swap, &rw, &rh);
            if (ok) {
                m_lastRequestedW    = w;
                m_lastRequestedH    = h;
                m_impl->last_width  = rw;
                m_impl->last_height = rh;
                mvwLog(QStringLiteral("swapchain resize: requested=%1x%2 actual=%3x%4")
                           .arg(w).arg(h).arg(rw).arg(rh));
            }
        }
    }

    // Pull the next frame from the swapchain. May fail if surface is
    // mid-resize or in a transient state — silently skip; next tick retries.
    struct pl_swapchain_frame frame{};
    if (!pl_swapchain_start_frame(m_impl->swap, &frame)) return;

    bool renderedMpvFrame = false;
    if (m_renderer) {
        renderedMpvFrame = m_renderer->renderToSwapchain(
            m_impl->pl_log_handle,
            m_impl->vk->gpu,
            frame,
            m_impl->last_width,
            m_impl->last_height);
    }

    if (!renderedMpvFrame) {
        const float black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        pl_tex_clear(m_impl->vk->gpu, frame.fbo, black);
    }

    if (!pl_swapchain_submit_frame(m_impl->swap)) {
        if (renderedMpvFrame && m_renderer) {
            m_renderer->finishPresentedFrame(m_impl->vk->gpu);
        }
        mvwLog(QStringLiteral("pl_swapchain_submit_frame failed"));
        return;
    }
    pl_swapchain_swap_buffers(m_impl->swap);
    if (renderedMpvFrame && m_renderer) {
        m_renderer->finishPresentedFrame(m_impl->vk->gpu);
    }

    // First-frame-rendered emit (single-shot). Lets VideoPlayer dismiss any
    // "loading…" overlay even though Task 2's frame is just black.
    if (renderedMpvFrame && !m_firstFrameEmitted) {
        m_firstFrameEmitted = true;
        emit firstFrameRendered();
    }
}
