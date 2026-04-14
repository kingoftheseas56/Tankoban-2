#include "ui/player/FrameCanvas.h"
#include "ui/player/ShmFrameReader.h"
#include <QFile>

FrameCanvas::FrameCanvas(QWidget* parent)
    : QRhiWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setMinimumSize(320, 180);

    // 125Hz heartbeat — triggers update(), render() runs at vsync
    m_pollTimer.setInterval(8);
    m_pollTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_pollTimer, &QTimer::timeout, this, QOverload<>::of(&QWidget::update));
}

FrameCanvas::~FrameCanvas()
{
    m_pollTimer.stop();
}

void FrameCanvas::attachShm(ShmFrameReader* reader) { m_reader = reader; }

void FrameCanvas::detachShm()
{
    m_pollTimer.stop();
    m_reader = nullptr;
    m_frameW = m_frameH = 0;
    update();
}

void FrameCanvas::startPolling() { if (m_reader) m_pollTimer.start(); }
void FrameCanvas::stopPolling()  { m_pollTimer.stop(); }

// ── RHI setup ───────────────────────────────────────────────────────────────

void FrameCanvas::initialize(QRhiCommandBuffer* cb)
{
    Q_UNUSED(cb);
    if (m_initialized)
        return;

    QRhi* r = rhi();

    // Vertex buffer: fullscreen quad (position + UV), triangle strip
    // UV Y-flipped for D3D11 convention (top-left origin)
    static const float quad[] = {
        // x     y     u     v
        -1.f, -1.f,  0.f,  1.f,   // bottom-left
         1.f, -1.f,  1.f,  1.f,   // bottom-right
        -1.f,  1.f,  0.f,  0.f,   // top-left
         1.f,  1.f,  1.f,  0.f,   // top-right
    };

    m_vbuf = r->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, sizeof(quad));
    m_vbuf->create();

    QRhiResourceUpdateBatch* u = r->nextResourceUpdateBatch();
    u->uploadStaticBuffer(m_vbuf, quad);
    cb->resourceUpdate(u);

    // Texture — start with 1x1, will recreate when first frame arrives
    m_texture = r->newTexture(QRhiTexture::BGRA8, QSize(1, 1));
    m_texture->create();
    m_texW = 1;
    m_texH = 1;

    // Sampler
    m_sampler = r->newSampler(
        QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
        QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
    m_sampler->create();

    // Uniform buffer for color parameters (std140 layout, 32 bytes)
    m_ubuf = r->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(ColorParams));
    m_ubuf->create();

    // Shader resource bindings
    m_srb = r->newShaderResourceBindings();
    m_srb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::FragmentStage, m_ubuf),
        QRhiShaderResourceBinding::sampledTexture(
            1, QRhiShaderResourceBinding::FragmentStage, m_texture, m_sampler)
    });
    m_srb->create();

    // Graphics pipeline
    m_pipeline = r->newGraphicsPipeline();

    // Load pre-compiled shaders from resources
    auto loadShader = [](const QString& path) -> QShader {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly))
            return QShader::fromSerialized(f.readAll());
        return {};
    };

    QShader vertShader = loadShader(QStringLiteral(":/shaders/video.vert.qsb"));
    QShader fragShader = loadShader(QStringLiteral(":/shaders/video.frag.qsb"));

    m_pipeline->setShaderStages({
        { QRhiShaderStage::Vertex, vertShader },
        { QRhiShaderStage::Fragment, fragShader }
    });

    QRhiVertexInputLayout inputLayout;
    inputLayout.setBindings({
        { 4 * sizeof(float) }  // stride: 2 floats pos + 2 floats UV
    });
    inputLayout.setAttributes({
        { 0, 0, QRhiVertexInputAttribute::Float2, 0 },                    // position
        { 0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float) }     // texcoord
    });
    m_pipeline->setVertexInputLayout(inputLayout);

    m_pipeline->setShaderResourceBindings(m_srb);
    m_pipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
    m_pipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
    m_pipeline->create();

    m_initialized = true;
}

// ── Render ──────────────────────────────────────────────────────────────────

void FrameCanvas::render(QRhiCommandBuffer* cb)
{
    QRhi* r = rhi();
    const QSize outputSize = renderTarget()->pixelSize();

    QRhiResourceUpdateBatch* u = r->nextResourceUpdateBatch();

    // Read latest frame from SHM. The sidecar producer already paces frames
    // against the audio master clock inside its decode thread (sleeps until
    // each frame's PTS is within 15ms of the clock, then writes to SHM). So
    // the newest frame in the ring is by definition the one to show right
    // now — no consumer-side pacing needed. Doing pacing here as well would
    // double-pace and introduce stutter.
    if (m_reader && m_reader->isAttached()) {
        auto f = m_reader->readLatest();
        if (f.valid) {
            m_frameW = f.width;
            m_frameH = f.height;

            // Recreate texture if size changed
            if (m_texW != f.width || m_texH != f.height) {
                m_texture->setPixelSize(QSize(f.width, f.height));
                m_texture->create();
                m_texW = f.width;
                m_texH = f.height;

                // Rebind texture in SRB (keep UBO at binding 0)
                m_srb->setBindings({
                    QRhiShaderResourceBinding::uniformBuffer(
                        0, QRhiShaderResourceBinding::FragmentStage, m_ubuf),
                    QRhiShaderResourceBinding::sampledTexture(
                        1, QRhiShaderResourceBinding::FragmentStage, m_texture, m_sampler)
                });
                m_srb->create();
            }

            // Upload BGRA pixels from SHM to texture
            QRhiTextureSubresourceUploadDescription subDesc(f.pixels, f.width * f.height * 4);
            subDesc.setDataStride(f.stride);
            subDesc.setSourceSize(QSize(f.width, f.height));

            QRhiTextureUploadEntry entry(0, 0, subDesc);
            QRhiTextureUploadDescription uploadDesc(entry);
            u->uploadTexture(m_texture, uploadDesc);

            m_reader->writeConsumerFid(f.frameId);
        }
    }

    // Compute aspect-ratio viewport
    float widgetAspect = static_cast<float>(outputSize.width()) / outputSize.height();
    float frameAspect = m_frameW > 0 && m_frameH > 0
                            ? static_cast<float>(m_frameW) / m_frameH
                            : widgetAspect;

    float vpX, vpY, vpW, vpH;
    if (frameAspect > widgetAspect) {
        vpW = outputSize.width();
        vpH = outputSize.width() / frameAspect;
        vpX = 0;
        vpY = (outputSize.height() - vpH) / 2.f;
    } else {
        vpH = outputSize.height();
        vpW = outputSize.height() * frameAspect;
        vpX = (outputSize.width() - vpW) / 2.f;
        vpY = 0;
    }

    // Upload color parameters to uniform buffer
    u->updateDynamicBuffer(m_ubuf, 0, sizeof(ColorParams), &m_colorParams);

    cb->beginPass(renderTarget(), Qt::black, { 1.0f, 0 }, u);

    if (m_frameW > 0 && m_frameH > 0) {
        cb->setGraphicsPipeline(m_pipeline);
        cb->setShaderResources(m_srb);
        cb->setViewport({ vpX, vpY, vpW, vpH });
        const QRhiCommandBuffer::VertexInput vbufBinding(m_vbuf, 0);
        cb->setVertexInput(0, 1, &vbufBinding);
        cb->draw(4);
    }

    cb->endPass();
}

void FrameCanvas::releaseResources()
{
    delete m_pipeline;  m_pipeline = nullptr;
    delete m_srb;       m_srb = nullptr;
    delete m_sampler;   m_sampler = nullptr;
    delete m_texture;   m_texture = nullptr;
    delete m_ubuf;      m_ubuf = nullptr;
    delete m_vbuf;      m_vbuf = nullptr;
    m_initialized = false;
}

void FrameCanvas::setColorParams(float brightness, float contrast, float saturation, float gamma)
{
    m_colorParams.brightness = brightness;
    m_colorParams.contrast   = contrast;
    m_colorParams.saturation = saturation;
    m_colorParams.gamma      = gamma;
    // No explicit update() — next poll tick will upload the new values
}

QRect FrameCanvas::frameRect() const
{
    if (m_frameW <= 0 || m_frameH <= 0)
        return rect();
    QSize s(m_frameW, m_frameH);
    s.scale(size(), Qt::KeepAspectRatio);
    int x = (width()  - s.width())  / 2;
    int y = (height() - s.height()) / 2;
    return QRect(QPoint(x, y), s);
}
