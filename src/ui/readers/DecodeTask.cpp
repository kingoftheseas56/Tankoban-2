#include "DecodeTask.h"
#include "core/ArchiveReader.h"

#include <QImage>

DecodeTask::DecodeTask(int pageIndex, const QString& cbzPath, const QString& pageName)
    : m_pageIndex(pageIndex)
    , m_cbzPath(cbzPath)
    , m_pageName(pageName)
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

    QPixmap pix;
    if (!pix.loadFromData(data)) {
        emit notifier.failed(m_pageIndex);
        return;
    }

    emit notifier.decoded(m_pageIndex, pix, pix.width(), pix.height());
}
