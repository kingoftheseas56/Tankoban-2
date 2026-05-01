#pragma once

// MpvVideoWidget — Phase 5 (redux) of MPV_RENDER_API_INTEGRATION.
//
// QOpenGLWidget that owns the libmpv render context and renders directly
// into its own framebuffer. The widget integrates into the Qt Widgets tree
// the same way any QWidget does — Qt composites its pixels into the parent
// surface; no native child HWND, no window-in-window seam.
//
// Architecture follows Stremio's stremio-shell mpv.cpp pattern but adapted
// to QOpenGLWidget (Qt Widgets) instead of QQuickFramebufferObject (Qt
// Quick). The earlier Phase 5 attempt at D3D11/OpenGL zero-copy interop
// (D3D11GLBridge + MpvRenderContext) was structurally blocked on Intel
// iGPU drivers — Intel's WGL_NV_DX_interop2 doesn't accept D3D11 NTHANDLE
// textures. This direct-paint approach bypasses cross-API sharing entirely
// and works on any GL-capable driver (Intel/NVIDIA/AMD/ANGLE).
//
// Threading: libmpv's update callback fires from internal threads. We
// marshal back to the GUI thread via a queued signal that calls update()
// on the widget; Qt's normal repaint machinery then schedules paintGL()
// on the GUI thread (where the widget's QOpenGLContext is current).

#include <QOpenGLWidget>

#include <cstdint>

struct mpv_handle;
struct mpv_render_context;

class MpvVideoWidget : public QOpenGLWidget {
    Q_OBJECT

public:
    explicit MpvVideoWidget(QWidget* parent = nullptr);
    ~MpvVideoWidget() override;

    // Attach to an initialized libmpv handle. Creates the render context
    // on the widget's QOpenGLContext (after initializeGL has fired). Pass
    // null to detach + tear down render context (called when MpvBackend
    // tears down its mpv_handle).
    void setMpvHandle(mpv_handle* mpv);

signals:
    // Internal — fired from libmpv's update callback (any thread); queued
    // to onUpdateRequested() on the GUI thread, which calls update() to
    // schedule a paintGL.
    void mpvUpdateRequested();

    // Fires once after the first successful paintGL. VideoPlayer can use
    // this to dismiss any "loading…" overlay.
    void firstFrameRendered();

protected:
    void initializeGL() override;
    void paintGL() override;

private slots:
    void onUpdateRequested();

private:
    static void  wakeupCallback(void* ctx);
    static void* getProcAddress(void* ctx, const char* name);

    void createRenderContextIfReady();
    void destroyRenderContext();
    void logRenderStats();

    mpv_handle*         m_mpv         = nullptr;
    mpv_render_context* m_renderCtx   = nullptr;
    bool   m_glInitialized            = false;
    bool   m_firstFrameEmitted        = false;

    int     m_frameCount    = 0;
    qint64  m_lastLogTimeMs = 0;
};
