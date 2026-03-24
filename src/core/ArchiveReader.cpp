#include "ArchiveReader.h"

#include <QFileInfo>
#include <QCollator>
#include <algorithm>

#ifdef HAS_QT_ZIP
#include <QtCore/private/qzipreader_p.h>
#endif

static bool isImageFile(const QString& name)
{
    static const QStringList exts = {"jpg", "jpeg", "png", "webp", "gif", "bmp"};
    for (const auto& ext : exts) {
        if (name.endsWith("." + ext, Qt::CaseInsensitive))
            return true;
    }
    return false;
}

QStringList ArchiveReader::pageList(const QString& cbzPath)
{
#ifdef HAS_QT_ZIP
    QZipReader zip(cbzPath);
    if (!zip.exists())
        return {};

    QStringList pages;
    const auto entries = zip.fileInfoList();
    for (const auto& entry : entries) {
        if (entry.isDir)
            continue;
        // Skip macOS resource forks and hidden files
        if (entry.filePath.startsWith("__MACOSX", Qt::CaseInsensitive))
            continue;
        if (QFileInfo(entry.filePath).fileName().startsWith('.'))
            continue;
        if (isImageFile(entry.filePath))
            pages.append(entry.filePath);
    }

    QCollator collator;
    collator.setNumericMode(true);
    std::sort(pages.begin(), pages.end(), [&collator](const QString& a, const QString& b) {
        return collator.compare(a, b) < 0;
    });

    return pages;
#else
    Q_UNUSED(cbzPath);
    return {};
#endif
}

QByteArray ArchiveReader::pageData(const QString& cbzPath, const QString& pageName)
{
#ifdef HAS_QT_ZIP
    QZipReader zip(cbzPath);
    if (!zip.exists())
        return {};
    return zip.fileData(pageName);
#else
    Q_UNUSED(cbzPath);
    Q_UNUSED(pageName);
    return {};
#endif
}
