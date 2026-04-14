#pragma once

#include <QRhiWidget>
#include <rhi/qrhi.h>
#include <QTimer>

class ShmFrameReader;

class FrameCanvas : public QRhiWidget {
    Q_OBJECT

public:
    explicit FrameCanvas(QWidget* parent = nullptr);
    ~FrameCanvas() override;

    void attachShm(ShmFrameReader* reader);
    void detachShm();
    void startPolling();
    void stopPolling();

    // GPU color adjustment (real-time, no debounce)
    void setColorParams(float brightness, float contrast, float saturation, float gamma);

    QRect frameRect() const;

protected:
    void initialize(QRhiCommandBuffer* cb) override;
    void render(QRhiCommandBuffer* cb) override;
    void releaseResources() override;

private:
    ShmFrameReader* m_reader = nullptr;
    QTimer          m_pollTimer;

    // RHI resources
    QRhiTexture*                m_texture  = nullptr;
    QRhiSampler*                m_sampler  = nullptr;
    QRhiBuffer*                 m_vbuf     = nullptr;
    QRhiBuffer*                 m_ubuf     = nullptr;   // Uniform buffer for color params
    QRhiShaderResourceBindings* m_srb      = nullptr;
    QRhiGraphicsPipeline*       m_pipeline = nullptr;

    int  m_texW = 0;
    int  m_texH = 0;
    int  m_frameW = 0;
    int  m_frameH = 0;
    bool m_initialized = false;

    // GPU color parameters (std140 layout: 32 bytes)
    struct alignas(16) ColorParams {
        float brightness = 0.0f;
        float contrast   = 1.0f;
        float saturation = 1.0f;
        float gamma      = 1.0f;
        int   colorSpace  = 1;  // BT.709
        int   transferFunc = 0; // sRGB
        float pad1 = 0.0f;
        float pad2 = 0.0f;
    } m_colorParams;
};
