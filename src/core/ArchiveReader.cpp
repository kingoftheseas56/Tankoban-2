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

// ── Fast dimension parsing from image file headers ──────────────────────────

static QSize parsePngDimensions(const QByteArray& data)
{
    // PNG: bytes 16-19 = width, 20-23 = height (big-endian)
    if (data.size() < 24) return {};
    auto d = reinterpret_cast<const unsigned char*>(data.constData());
    if (d[0] != 0x89 || d[1] != 'P' || d[2] != 'N' || d[3] != 'G')
        return {};
    int w = (d[16] << 24) | (d[17] << 16) | (d[18] << 8) | d[19];
    int h = (d[20] << 24) | (d[21] << 16) | (d[22] << 8) | d[23];
    return {w, h};
}

static QSize parseGifDimensions(const QByteArray& data)
{
    if (data.size() < 10) return {};
    auto d = reinterpret_cast<const unsigned char*>(data.constData());
    if (d[0] != 'G' || d[1] != 'I' || d[2] != 'F')
        return {};
    int w = d[6] | (d[7] << 8);
    int h = d[8] | (d[9] << 8);
    return {w, h};
}

static QSize parseBmpDimensions(const QByteArray& data)
{
    if (data.size() < 26) return {};
    auto d = reinterpret_cast<const unsigned char*>(data.constData());
    if (d[0] != 'B' || d[1] != 'M')
        return {};
    int w = d[18] | (d[19] << 8) | (d[20] << 16) | (d[21] << 24);
    int h = d[22] | (d[23] << 8) | (d[24] << 16) | (d[25] << 24);
    if (h < 0) h = -h; // bottom-up BMP
    return {w, h};
}

static QSize parseWebpDimensions(const QByteArray& data)
{
    if (data.size() < 30) return {};
    auto d = reinterpret_cast<const unsigned char*>(data.constData());
    if (d[0] != 'R' || d[1] != 'I' || d[2] != 'F' || d[3] != 'F')
        return {};
    if (d[8] != 'W' || d[9] != 'E' || d[10] != 'B' || d[11] != 'P')
        return {};
    // VP8 lossy
    if (d[12] == 'V' && d[13] == 'P' && d[14] == '8' && d[15] == ' ') {
        if (data.size() < 30) return {};
        int w = (d[26] | (d[27] << 8)) & 0x3FFF;
        int h = (d[28] | (d[29] << 8)) & 0x3FFF;
        return {w, h};
    }
    // VP8L lossless
    if (d[12] == 'V' && d[13] == 'P' && d[14] == '8' && d[15] == 'L') {
        if (data.size() < 25) return {};
        quint32 bits = d[21] | (d[22] << 8) | (d[23] << 16) | (d[24] << 24);
        int w = (bits & 0x3FFF) + 1;
        int h = ((bits >> 14) & 0x3FFF) + 1;
        return {w, h};
    }
    return {};
}

static QSize parseJpegDimensions(const QByteArray& data)
{
    if (data.size() < 4) return {};
    auto d = reinterpret_cast<const unsigned char*>(data.constData());
    if (d[0] != 0xFF || d[1] != 0xD8)
        return {};
    int pos = 2;
    int len = data.size();
    while (pos + 4 < len) {
        if (d[pos] != 0xFF) break;
        unsigned char marker = d[pos + 1];
        if (marker == 0xD9) break; // EOI
        if (marker == 0xDA) break; // SOS — no more headers
        int segLen = (d[pos + 2] << 8) | d[pos + 3];
        // SOF markers (baseline, progressive, etc.)
        if ((marker >= 0xC0 && marker <= 0xCF) && marker != 0xC4 && marker != 0xC8 && marker != 0xCC) {
            if (pos + 9 < len) {
                int h = (d[pos + 5] << 8) | d[pos + 6];
                int w = (d[pos + 7] << 8) | d[pos + 8];
                return {w, h};
            }
        }
        pos += 2 + segLen;
    }
    return {};
}

QSize ArchiveReader::parseImageDimensions(const QByteArray& headerBytes)
{
    if (headerBytes.size() < 10)
        return {};

    QSize result;
    result = parsePngDimensions(headerBytes);
    if (result.isValid()) return result;

    result = parseJpegDimensions(headerBytes);
    if (result.isValid()) return result;

    result = parseWebpDimensions(headerBytes);
    if (result.isValid()) return result;

    result = parseGifDimensions(headerBytes);
    if (result.isValid()) return result;

    result = parseBmpDimensions(headerBytes);
    if (result.isValid()) return result;

    return {};
}
