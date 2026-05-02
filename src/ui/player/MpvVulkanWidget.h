#pragma once

// MpvVulkanWidget — Task 2 of MAKE_MPV_BEAT_FFMPEG (2026-05-02).
//
// Native HWND-backed Qt widget that owns a libplacebo+Vulkan render context.
// Replaces MpvVideoWidget's QOpenGLWidget for the mpv backend's video surface.
// The new pipeline is: mpv decodes → frames feed libplacebo → libplacebo
// composites/scales/HDR-tone-maps → Vulkan swapchain presents.
//
// Task 2 scope: empty Vulkan window only — initializes Vulkan + libplacebo,
// runs a 60Hz render timer that clears the framebuffer to black and
// presents. No mpv frame integration yet (Task 3 wires that). The public
// interface mirrors MpvVideoWidget so VideoPlayer's plumbing-swap site
// stays minimal — `setMpvHandle()` is accepted (and stored) but does
// nothing visible until Task 3.
//
// Architecture follows FrameCanvas's WA_PaintOnScreen + WA_NativeWindow
// pattern. Qt gives us a separate native HWND that doesn't bubble paint
// events; we drive rendering via QTimer + libplacebo's swapchain. Same
// shape used by SMPlayer, mpv.net, IINA for embedded mpv windows.

#include <QWidget>
#include <QTimer>
#include <QMouseEvent>

#include <memory>

struct mpv_handle;
class MpvLibplaceboRenderer;

class MpvVulkanWidget : public QWidget {
    Q_OBJECT

public:
    explicit MpvVulkanWidget(QWidget* parent = nullptr);
    ~MpvVulkanWidget() override;

    // Public interface mirrors MpvVideoWidget so VideoPlayer's widget-
    // swap site at construction stays a single-line change. Task 2 stores
    // the handle but does nothing with it; Task 3 wires mpv frame readout
    // into the libplacebo render path.
    void setMpvHandle(mpv_handle* mpv);
    void setLibplaceboRenderer(MpvLibplaceboRenderer* renderer);

    // True iff Vulkan + libplacebo + swapchain initialized successfully on
    // showEvent. Lets VideoPlayer / fallback paths detect init failure
    // (e.g., Vulkan device not available) early. False before show; never
    // re-set after a successful init.
    bool vulkanReady() const { return m_vulkanReady; }

signals:
    // Mirrors MpvVideoWidget::mpvUpdateRequested — fired from libmpv's
    // update callback (any thread) and queued to the GUI thread for repaint
    // scheduling. Wired in Task 3; emits never in Task 2.
    void mpvUpdateRequested();

    // Fires once after the first successful render-loop tick (clear + swap)
    // post showEvent. VideoPlayer can use this to dismiss any "loading…"
    // overlay. Same contract as MpvVideoWidget::firstFrameRendered, except
    // in Task 2 the "frame" is the empty black panel.
    void firstFrameRendered();

    // Mirrors FrameCanvas::mouseActivityAt. The Vulkan surface is also a
    // native child HWND, so mouse movement does not bubble to VideoPlayer.
    void mouseActivityAt(int y);

protected:
    // Opt out of Qt's compositing — we own pixels in this widget's HWND
    // via Vulkan swapchain, not via Qt's QPainter. Same pattern as
    // FrameCanvas / D3D11.
    QPaintEngine* paintEngine() const override { return nullptr; }

    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    // Agent 3 2026-05-02 — mouseMoveEvent + nativeEvent overrides disabled
    // (see MpvVulkanWidget.cpp). Caused FATAL_USER_CALLBACK in Qt6Core.dll
    // ~10s into startup before any window appeared.
    // void mouseMoveEvent(QMouseEvent* event) override;
    // #ifdef _WIN32
    // bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
    // #endif

private slots:
    // 60Hz render timer tick: pl_swapchain_start_frame → pl_tex_clear(black)
    // → pl_swapchain_submit_frame → pl_swapchain_swap_buffers. Replaced
    // with mpv-frame-driven render in Task 3+; Task 2 just clears.
    void onRenderTick();

private:
    // Lifecycle helpers — defined in .cpp where libplacebo headers live.
    bool initVulkan();
    void teardownVulkan();
    void resizeSwapchain(int width, int height);

    // Pimpl holds the libplacebo + Vulkan state so this header doesn't
    // pull in libplacebo headers (which use C99 idioms not friendly to
    // every translation unit that includes this header).
    class Impl;
    std::unique_ptr<Impl> m_impl;

    QTimer  m_renderTimer;
    bool    m_vulkanReady       = false;
    bool    m_firstFrameEmitted = false;
    int     m_lastRequestedW    = 0;
    int     m_lastRequestedH    = 0;
    mpv_handle* m_mpv           = nullptr;
    MpvLibplaceboRenderer* m_renderer = nullptr;  // Non-owning; MpvBackend owns it.
};
