#include "DecodeTask.h"
#include "core/ArchiveReader.h"

#include <QBuffer>
#include <QImage>
#include <QImageReader>
#include <QSize>

DecodeTask::DecodeTask(int pageIndex, const QString& cbzPath, const QString& pageName, int volumeId)
    : m_pageIndex(pageIndex)
    , m_cbzPath(cbzPath)
    , m_pageName(pageName)
    , m_volumeId(volumeId)
{
    setAutoDelete(true);
}

void DecodeTask::run()
{
    QByteArray data = ArchiveReader::pageData(m_cbzPath, m_pageName);
    if (data.isEmpty()) {
        emit notifier.failed(m_pageIndex);
        return;
    }

    // A3: Fast dimension hint — read header bytes before full decode
    QSize dims = ArchiveReader::parseImageDimensions(data.left(4096));
    if (dims.isValid() && dims.width() > 0 && dims.height() > 0)
        emit notifier.dimensionsReady(m_pageIndex, dims.width(), dims.height());

    // A2: EXIF-aware decode via QImageReader (handles rotation flags)
    QBuffer buf(&data);
    buf.open(QIODevice::ReadOnly);
    QImageReader reader(&buf);
    reader.setDecideFormatFromContent(true);
    reader.setAutoTransform(true);
    QImage image = reader.read();
    if (image.isNull()) {
        emit notifier.failed(m_pageIndex);
        return;
    }
    QPixmap pix = QPixmap::fromImage(std::move(image));

    emit notifier.decoded(m_pageIndex, pix, pix.width(), pix.height(), m_volumeId);
}
