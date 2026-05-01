// MpvVideoWidget.cpp — Phase 5 redux. Direct GL paint, no D3D11 sharing.

#include "MpvVideoWidget.h"

#include "core/DebugLogBuffer.h"

#include <mpv/client.h>
#include <mpv/render.h>
#include <mpv/render_gl.h>

#include <QByteArray>
#include <QDateTime>
#include <QOpenGLContext>
#include <QString>
#include <QSurfaceFormat>

namespace {

void wLog(const QString& msg)
{
    DebugLogBuffer::instance().info("MpvVideoWidget", msg);
}

} // namespace

MpvVideoWidget::MpvVideoWidget(QWidget* parent)
    : QOpenGLWidget(parent)
{
    // 3.2 core matches what libmpv's gpu vo expects internally. Set on the
    // widget's surface format so Qt creates the right context. Note: this
    // must be set BEFORE the widget is shown (before Qt creates its GL
    // context).
    QSurfaceFormat fmt;
    fmt.setVersion(3, 2);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setRenderableType(QSurfaceFormat::OpenGL);
    fmt.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    setFormat(fmt);

    // MAKE_MPV_SOLO Task 7 (2026-05-01) — HUD reveal-on-hover parity with
    // FrameCanvas (FrameCanvas.cpp:38). Without setMouseTracking, Qt only
    // generates mouseMoveEvent when a button is pressed; hover-only
    // movement skips the event, so the bottom-HUD never reveals when the
    // mouse drifts over the player canvas on the mpv path. Qt's default
    // QWidget::mouseMoveEvent calls event->ignore() so the propagation up
    // to VideoPlayer's mouseMoveEvent (which drives the HUD reveal) works
    // automatically once tracking is on.
    setMouseTracking(true);

    // The libmpv update callback fires from internal threads; queue back to
    // GUI thread before calling update().
    connect(this, &MpvVideoWidget::mpvUpdateRequested,
            this, &MpvVideoWidget::onUpdateRequested,
            Qt::QueuedConnection);
}

MpvVideoWidget::~MpvVideoWidget()
{
    destroyRenderContext();
}

void MpvVideoWidget::setMpvHandle(mpv_handle* mpv)
{
    if (m_mpv == mpv) return;

    if (m_renderCtx) {
        destroyRenderContext();
    }

    m_mpv = mpv;
    m_firstFrameEmitted = false;

    // If GL is already initialized (widget has been shown at least once),
    // create the render context immediately. Otherwise wait for
    // initializeGL() to fire.
    if (m_mpv && m_glInitialized) {
        createRenderContextIfReady();
    }
}

void MpvVideoWidget::initializeGL()
{
    m_glInitialized = true;
    wLog(QString("[initializeGL] OK GL_VENDOR=%1 RENDERER=%2")
             .arg(QString::fromLatin1(reinterpret_cast<const char*>(glGetString(GL_VENDOR))))
             .arg(QString::fromLatin1(reinterpret_cast<const char*>(glGetString(GL_RENDERER)))));
    if (m_mpv) {
        createRenderContextIfReady();
    }
}

void MpvVideoWidget::createRenderContextIfReady()
{
    if (m_renderCtx || !m_mpv || !context()) return;

    mpv_opengl_init_params glInit{};
    glInit.get_proc_address     = &MpvVideoWidget::getProcAddress;
    glInit.get_proc_address_ctx = context();  // QOpenGLContext*

    char apiType[] = MPV_RENDER_API_TYPE_OPENGL;
    mpv_render_param params[] = {
        { MPV_RENDER_PARAM_API_TYPE,           static_cast<void*>(apiType) },
        { MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glInit },
        { MPV_RENDER_PARAM_INVALID,            nullptr },
    };

    int rc = mpv_render_context_create(&m_renderCtx, m_mpv, params);
    if (rc < 0) {
        wLog(QString("[createRenderContext] FAILED: %1")
                 .arg(QString::fromLatin1(mpv_error_string(rc))));
        m_renderCtx = nullptr;
        return;
    }

    mpv_render_context_set_update_callback(m_renderCtx,
                                            &MpvVideoWidget::wakeupCallback,
                                            this);

    // P5 redux — switch vo from "null" (set by MpvBackend before init to
    // avoid the "no render context set" fatal) to "libmpv" now that the
    // render context exists. mpv routes decoded frames to our render path
    // from this point on.
    mpv_set_property_string(m_mpv, "vo", "libmpv");

    m_lastLogTimeMs = QDateTime::currentMSecsSinceEpoch();
    wLog(QString("[createRenderContext] OK widget=%1x%2 vo=libmpv").arg(width()).arg(height()));

    // Force an initial paint so libmpv knows we're ready to receive frames.
    update();
}

void MpvVideoWidget::destroyRenderContext()
{
    if (m_renderCtx) {
        mpv_render_context_set_update_callback(m_renderCtx, nullptr, nullptr);
        mpv_render_context_free(m_renderCtx);
        m_renderCtx = nullptr;
        wLog("[destroyRenderContext] freed");
    }
}

void* MpvVideoWidget::getProcAddress(void* ctx, const char* name)
{
    auto* gl = static_cast<QOpenGLContext*>(ctx);
    if (!gl || !name) return nullptr;
    return reinterpret_cast<void*>(gl->getProcAddress(QByteArray(name)));
}

void MpvVideoWidget::wakeupCallback(void* ctx)
{
    auto* self = static_cast<MpvVideoWidget*>(ctx);
    if (!self) return;
    emit self->mpvUpdateRequested();
}

void MpvVideoWidget::onUpdateRequested()
{
    if (!m_renderCtx) return;
    const uint64_t flags = mpv_render_context_update(m_renderCtx);
    if (flags & MPV_RENDER_UPDATE_FRAME) {
        update();
    }
}

void MpvVideoWidget::paintGL()
{
    if (!m_renderCtx) {
        return;  // Will get called once during initializeGL flow before the
                 // render context is created; harmless empty paint.
    }

    // Render libmpv into our default framebuffer (QOpenGLWidget's internal
    // FBO that Qt later composites into the parent surface).
    const qreal dpr = devicePixelRatioF();
    const int w = static_cast<int>(width()  * dpr);
    const int h = static_cast<int>(height() * dpr);

    mpv_opengl_fbo fbo{};
    fbo.fbo             = static_cast<int>(defaultFramebufferObject());
    fbo.w               = w;
    fbo.h               = h;
    fbo.internal_format = 0;

    int flipY = 1;  // QOpenGLWidget's FBO is bottom-left origin like the GL default
    mpv_render_param params[] = {
        { MPV_RENDER_PARAM_OPENGL_FBO, &fbo },
        { MPV_RENDER_PARAM_FLIP_Y,     &flipY },
        { MPV_RENDER_PARAM_INVALID,    nullptr },
    };

    int rc = mpv_render_context_render(m_renderCtx, params);
    if (rc < 0) {
        wLog(QString("[paintGL] mpv_render_context_render FAILED: %1")
                 .arg(QString::fromLatin1(mpv_error_string(rc))));
        return;
    }

    if (!m_firstFrameEmitted) {
        m_firstFrameEmitted = true;
        emit firstFrameRendered();
        wLog("[paintGL] first frame emitted");
    }

    ++m_frameCount;
    logRenderStats();
}

void MpvVideoWidget::logRenderStats()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 elapsed = now - m_lastLogTimeMs;
    if (elapsed < 5000) return;
    const double fps = static_cast<double>(m_frameCount) * 1000.0
                       / static_cast<double>(elapsed);
    wLog(QString("[MPV-RENDER] frames=%1 elapsed=%2ms fps=%3")
             .arg(m_frameCount).arg(elapsed).arg(fps, 0, 'f', 1));
    m_frameCount = 0;
    m_lastLogTimeMs = now;
}
