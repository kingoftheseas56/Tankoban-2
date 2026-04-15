#pragma once

#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QSize>
#include <QImage>

class ArchiveReader {
public:
    // Returns sorted list of image filenames inside the CBZ
    static QStringList pageList(const QString& cbzPath);

    // Extracts raw image bytes for one page
    static QByteArray pageData(const QString& cbzPath, const QString& pageName);

    // Decodes the first image (sorted-collator order) of a comic archive.
    // Handles CBZ, CBR, and RAR transparently via the shared dispatch in
    // pageList/pageData. Returns a null QImage on any failure (empty
    // archive, missing data, decode failure). Used by the library scanner
    // for cover thumbnails — keeps libarchive logic centralized in
    // ArchiveReader so callers don't duplicate it.
    static QImage firstImage(const QString& path);

    // Fast dimension parsing from image header bytes (avoids full decode)
    // Returns {0,0} if parsing fails
    static QSize parseImageDimensions(const QByteArray& headerBytes);
};
