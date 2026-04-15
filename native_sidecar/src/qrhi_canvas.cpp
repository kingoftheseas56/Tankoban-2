/*
 * QRhi Video Canvas DLL — C++ QRhiWidget for PySide6
 *
 * Creates a QRhiWidget subclass that renders BGRA video frames via Qt's
 * native GPU API (D3D11 on Windows). Exported as a C DLL so Python can
 * create/control it via ctypes + shiboken6::wrapInstance.
 *
 * This bypasses PySide6's broken QRhiWidget bindings by doing all GPU
 * work in native C++ while staying inside Qt's widget tree (no child HWND).
 */

#include <QtWidgets/QRhiWidget>
#include <QtGui/6.10.2/QtGui/rhi/qrhi.h>
#include <QtGui/QImage>
#include <QtCore/QFile>

#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

// Shader .qsb file paths (set by Python before first render)
static std::string g_vert_qsb_path;
static std::string g_frag_qsb_path;

static QShader loadShader(const char* path) {
    QFile f(QString::fromUtf8(path));
    if (!f.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "QRhiCanvas: failed to open shader %s\n", path);
        return {};
    }
    QShader shader = QShader::fromSerialized(f.readAll());
    if (!shader.isValid())
        std::fprintf(stderr, "QRhiCanvas: invalid shader %s\n", path);
    return shader;
}

// ═══════════════════════════════════════════════════════════════════
// RhiCanvas — QRhiWidget subclass
// ═══════════════════════════════════════════════════════════════════

class RhiCanvas : public QRhiWidget {
public:
    explicit RhiCanvas(QWidget* parent = nullptr)
        : QRhiWidget(parent)
    {
        setAutoRenderTarget(true);
    }

    // Called from Python (via DLL export) to feed a new frame.
    // Thread-safe: copies data under lock, render() picks it up.
    void uploadFrame(const void* bgra, int w, int h, int stride) {
        if (!bgra || w <= 0 || h <= 0 || stride <= 0) return;
        std::lock_guard<std::mutex> lock(frame_mutex_);
        int data_size = stride * h;
        if (static_cast<int>(pending_data_.size()) != data_size)
            pending_data_.resize(data_size);
        std::memcpy(pending_data_.data(), bgra, data_size);
        pending_w_ = w;
        pending_h_ = h;
        pending_stride_ = stride;
        pending_new_ = true;
    }

protected:
    void initialize(QRhiCommandBuffer* cb) override {
        QRhi* rhi = this->rhi();
        if (!rhi) {
            std::fprintf(stderr, "QRhiCanvas: no QRhi in initialize()\n");
            return;
        }

        std::fprintf(stderr, "QRhiCanvas: initialize() backend=%s\n",
                     rhi->backendName());

        // --- Vertex buffer: fullscreen quad (triangle strip) ---
        float verts[] = {
            // pos       uv (Y flipped for top-down BGRA)
            -1.f, -1.f,  0.f, 1.f,
             1.f, -1.f,  1.f, 1.f,
            -1.f,  1.f,  0.f, 0.f,
             1.f,  1.f,  1.f, 0.f,
        };

        vbuf_ = rhi->newBuffer(QRhiBuffer::Immutable,
                               QRhiBuffer::VertexBuffer,
                               sizeof(verts));
        vbuf_->create();

        // --- Placeholder texture ---
        tex_ = rhi->newTexture(QRhiTexture::BGRA8, QSize(16, 16));
        tex_->create();
        tex_w_ = 16;
        tex_h_ = 16;

        // --- Sampler ---
        sampler_ = rhi->newSampler(
            QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
            QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
        sampler_->create();

        // --- Shader resource bindings ---
        srb_ = rhi->newShaderResourceBindings();
        srb_->setBindings({
            QRhiShaderResourceBinding::sampledTexture(
                0, QRhiShaderResourceBinding::FragmentStage,
                tex_, sampler_)
        });
        srb_->create();

        // --- Graphics pipeline ---
        pipeline_ = rhi->newGraphicsPipeline();

        QShader vs = loadShader(g_vert_qsb_path.c_str());
        QShader fs = loadShader(g_frag_qsb_path.c_str());

        pipeline_->setShaderStages({
            { QRhiShaderStage::Vertex, vs },
            { QRhiShaderStage::Fragment, fs },
        });

        QRhiVertexInputLayout inputLayout;
        inputLayout.setBindings({
            { 4 * sizeof(float) }  // stride: 4 floats per vertex
        });
        inputLayout.setAttributes({
            { 0, 0, QRhiVertexInputAttribute::Float2, 0 },                    // position
            { 0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float) },    // texcoord
        });
        pipeline_->setVertexInputLayout(inputLayout);
        pipeline_->setTopology(QRhiGraphicsPipeline::TriangleStrip);
        pipeline_->setCullMode(QRhiGraphicsPipeline::None);
        pipeline_->setShaderResourceBindings(srb_);
        pipeline_->setRenderPassDescriptor(
            renderTarget()->renderPassDescriptor());
        pipeline_->create();

        // Upload vertex data
        QRhiResourceUpdateBatch* batch = rhi->nextResourceUpdateBatch();
        batch->uploadStaticBuffer(vbuf_, verts);
        cb->resourceUpdate(batch);

        ready_ = true;
        std::fprintf(stderr, "QRhiCanvas: pipeline ready\n");
    }

    void render(QRhiCommandBuffer* cb) override {
        if (!ready_) return;

        QRhi* rhi = this->rhi();
        if (!rhi) return;

        QRhiResourceUpdateBatch* batch = rhi->nextResourceUpdateBatch();

        // Check for pending frame upload
        {
            std::lock_guard<std::mutex> lock(frame_mutex_);
            if (pending_new_ && !pending_data_.empty()) {
                // Resize texture if needed
                if (pending_w_ != tex_w_ || pending_h_ != tex_h_) {
                    recreateTexture(rhi, pending_w_, pending_h_);
                }

                // Upload BGRA pixels via QImage
                QImage img(reinterpret_cast<const uchar*>(pending_data_.data()),
                           pending_w_, pending_h_, pending_stride_,
                           QImage::Format_ARGB32);
                batch->uploadTexture(tex_, img);
                pending_new_ = false;
                has_frame_ = true;
            }
        }

        // Compute viewport (full render target for now)
        QSize sz = renderTarget()->pixelSize();
        int vp_w = sz.width();
        int vp_h = sz.height();

        const QColor clearColor(Qt::black);
        QRhiDepthStencilClearValue dsClear(1.0f, 0);

        cb->beginPass(renderTarget(), clearColor, dsClear, batch);

        if (has_frame_) {
            cb->setGraphicsPipeline(pipeline_);
            cb->setShaderResources(srb_);

            const QRhiCommandBuffer::VertexInput vbufBinding(vbuf_, 0);
            cb->setVertexInput(0, 1, &vbufBinding);
            cb->setViewport(QRhiViewport(0, 0, vp_w, vp_h));
            cb->draw(4);
        }

        cb->endPass();
    }

    void releaseResources() override {
        ready_ = false;
        delete pipeline_; pipeline_ = nullptr;
        delete srb_; srb_ = nullptr;
        delete sampler_; sampler_ = nullptr;
        delete tex_; tex_ = nullptr;
        delete vbuf_; vbuf_ = nullptr;
    }

private:
    void recreateTexture(QRhi* rhi, int w, int h) {
        std::fprintf(stderr, "QRhiCanvas: recreate texture %dx%d -> %dx%d\n",
                     tex_w_, tex_h_, w, h);

        QRhiTexture* old = tex_;
        tex_ = rhi->newTexture(QRhiTexture::BGRA8, QSize(w, h));
        tex_->create();
        tex_w_ = w;
        tex_h_ = h;

        // Rebuild SRB
        srb_->setBindings({
            QRhiShaderResourceBinding::sampledTexture(
                0, QRhiShaderResourceBinding::FragmentStage,
                tex_, sampler_)
        });
        srb_->create();

        delete old;
    }

    // RHI resources
    QRhiGraphicsPipeline* pipeline_ = nullptr;
    QRhiBuffer* vbuf_ = nullptr;
    QRhiTexture* tex_ = nullptr;
    QRhiSampler* sampler_ = nullptr;
    QRhiShaderResourceBindings* srb_ = nullptr;
    int tex_w_ = 0, tex_h_ = 0;
    bool ready_ = false;
    bool has_frame_ = false;

    // Pending frame (fed by Python, consumed by render)
    std::mutex frame_mutex_;
    std::vector<uint8_t> pending_data_;
    int pending_w_ = 0, pending_h_ = 0, pending_stride_ = 0;
    bool pending_new_ = false;
};

// ═══════════════════════════════════════════════════════════════════
// C API exports (for Python ctypes)
// ═══════════════════════════════════════════════════════════════════

extern "C" {

__declspec(dllexport)
void canvas_set_shader_paths(const char* vert_qsb, const char* frag_qsb) {
    if (vert_qsb) g_vert_qsb_path = vert_qsb;
    if (frag_qsb) g_frag_qsb_path = frag_qsb;
    std::fprintf(stderr, "QRhiCanvas: shaders set: %s, %s\n",
                 g_vert_qsb_path.c_str(), g_frag_qsb_path.c_str());
}

__declspec(dllexport)
void* canvas_create(void* parent_qwidget_ptr) {
    auto* parent = reinterpret_cast<QWidget*>(parent_qwidget_ptr);
    auto* canvas = new RhiCanvas(parent);
    canvas->setStyleSheet("background-color: black;");
    std::fprintf(stderr, "QRhiCanvas: created (parent=%p)\n", parent);
    return canvas;
}

__declspec(dllexport)
void canvas_upload_frame(void* canvas_ptr, const void* bgra_data,
                         int width, int height, int stride) {
    if (!canvas_ptr) return;
    auto* canvas = reinterpret_cast<RhiCanvas*>(canvas_ptr);
    canvas->uploadFrame(bgra_data, width, height, stride);
    // Trigger repaint
    canvas->update();
}

__declspec(dllexport)
void canvas_destroy(void* canvas_ptr) {
    if (!canvas_ptr) return;
    auto* canvas = reinterpret_cast<RhiCanvas*>(canvas_ptr);
    delete canvas;
    std::fprintf(stderr, "QRhiCanvas: destroyed\n");
}

} // extern "C"
