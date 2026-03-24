#pragma once

#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QSize>

class ArchiveReader {
public:
    // Returns sorted list of image filenames inside the CBZ
    static QStringList pageList(const QString& cbzPath);

    // Extracts raw image bytes for one page
    static QByteArray pageData(const QString& cbzPath, const QString& pageName);

    // Fast dimension parsing from image header bytes (avoids full decode)
    // Returns {0,0} if parsing fails
    static QSize parseImageDimensions(const QByteArray& headerBytes);
};
