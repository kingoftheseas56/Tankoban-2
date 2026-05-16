// src/core/manga/PremiumCoverExtractor.cpp
//
// TANKOYOMI_PREMIUM Phase 10 -- implementation. Mirrors the HAS_QT_ZIP shape
// used by PremiumArchiveValidator.cpp (same <QtCore/private/qzipreader_p.h>
// include + same Qt6::CorePrivate link).
#include "PremiumCoverExtractor.h"

#include <QBuffer>
#include <QByteArray>
#include <QCollator>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QMetaObject>
#include <QPointer>
#include <QRunnable>
#include <QThreadPool>
#include <QtGlobal>
#include <algorithm>

#ifdef HAS_QT_ZIP
#  include <QtCore/private/qzipreader_p.h>
#endif

namespace tankoban::manga::premium {

namespace {

constexpr qint64 kMaxDecompressedBytes = 64LL * 1024LL * 1024LL; // 64 MiB cap
constexpr int    kThumbWidthPx         = 256;
constexpr int    kJpegQuality          = 85;

bool isImageExtension(const QString& lowerExt)
{
    return lowerExt == QLatin1String("jpg")
        || lowerExt == QLatin1String("jpeg")
        || lowerExt == QLatin1String("png")
        || lowerExt == QLatin1String("webp")
        || lowerExt == QLatin1String("gif")
        || lowerExt == QLatin1String("bmp")
        || lowerExt == QLatin1String("avif");
}

bool containsJunkNeedle(const QString& lowerBaseName)
{
    static const QStringList kJunkNeedles = {
        QStringLiteral("credit"),
        QStringLiteral("scan"),
        QStringLiteral("blank"),
        QStringLiteral("advert"),
        QStringLiteral("back"),
        QStringLiteral("spread"),
    };
    for (const auto& needle : kJunkNeedles) {
        if (lowerBaseName.contains(needle)) return true;
    }
    return false;
}

// Pick the cover entry per Codex section 21 ordering:
//   1. catalog coverPageHint (if present in `entries`)
//   2. "cover.*" or "folder.*" basename match (case-insensitive)
//   3. natural-sort first image bypassing junk needles
//   4. empty string => coverFailed
QString pickCoverEntry(const QString& hint, const QStringList& entries)
{
    if (entries.isEmpty()) return QString();

    // (1) catalog coverPageHint
    if (!hint.isEmpty()) {
        for (const auto& e : entries) {
            if (e.compare(hint, Qt::CaseInsensitive) == 0) return e;
            if (QFileInfo(e).fileName().compare(hint, Qt::CaseInsensitive) == 0) return e;
        }
    }

    // (2) "cover.*" / "folder.*" basename
    for (const auto& e : entries) {
        const QString base = QFileInfo(e).completeBaseName().toLower();
        if (base == QLatin1String("cover") || base == QLatin1String("folder")) {
            return e;
        }
    }

    // (3) natural-sort first image bypassing junk needles.
    QStringList sorted = entries;
    QCollator coll;
    coll.setNumericMode(true);
    coll.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(sorted.begin(), sorted.end(),
              [&coll](const QString& a, const QString& b){
                  return coll.compare(a, b) < 0;
              });

    for (const auto& e : sorted) {
        const QString base = QFileInfo(e).completeBaseName().toLower();
        if (containsJunkNeedle(base)) continue;
        return e;
    }

    // (4) fall back to the first natural-sorted image (junk-only archive)
    return sorted.first();
}

class ExtractRunnable : public QRunnable
{
public:
    ExtractRunnable(QPointer<PremiumCoverExtractor> owner,
                    QString cbzPath, QString seriesId, int volumeNumber,
                    QString outputDir, QString hint, QStringList images)
        : m_owner(std::move(owner))
        , m_cbz(std::move(cbzPath))
        , m_seriesId(std::move(seriesId))
        , m_vol(volumeNumber)
        , m_outDir(std::move(outputDir))
        , m_hint(std::move(hint))
        , m_images(std::move(images))
    {
        setAutoDelete(true);
    }

    void run() override;

private:
    void emitReady(const QString& path)
    {
        if (!m_owner) return;
        QPointer<PremiumCoverExtractor> owner = m_owner;
        QString seriesId = m_seriesId;
        int vol = m_vol;
        QString cover = path;
        QMetaObject::invokeMethod(owner.data(),
            [owner, seriesId, vol, cover]() {
                if (owner) emit owner->coverReady(seriesId, vol, cover);
            },
            Qt::QueuedConnection);
    }

    void emitFailed(const QString& reason)
    {
        if (!m_owner) return;
        QPointer<PremiumCoverExtractor> owner = m_owner;
        QString seriesId = m_seriesId;
        int vol = m_vol;
        QString why = reason;
        QMetaObject::invokeMethod(owner.data(),
            [owner, seriesId, vol, why]() {
                if (owner) emit owner->coverFailed(seriesId, vol, why);
            },
            Qt::QueuedConnection);
    }

    QPointer<PremiumCoverExtractor> m_owner;
    QString                         m_cbz;
    QString                         m_seriesId;
    int                             m_vol = 0;
    QString                         m_outDir;
    QString                         m_hint;
    QStringList                     m_images;
};

void ExtractRunnable::run()
{
#ifndef HAS_QT_ZIP
    emitFailed(QStringLiteral("HAS_QT_ZIP not defined; QZipReader unavailable"));
    return;
#else
    QZipReader zr(m_cbz);
    if (!zr.exists() || !zr.isReadable()) {
        emitFailed(QStringLiteral("could not open cbz"));
        return;
    }

    // If the caller couldn't pre-populate the image list (Phase 4 validator's
    // result struct isn't currently threaded through the finalizeCompletion
    // scope), walk the archive ourselves.
    QStringList images = m_images;
    if (images.isEmpty()) {
        for (const auto& info : zr.fileInfoList()) {
            if (info.isDir) continue;
            const QString lowerExt = QFileInfo(info.filePath).suffix().toLower();
            if (isImageExtension(lowerExt)) {
                images.append(info.filePath);
            }
        }
    }

    const QString chosen = pickCoverEntry(m_hint, images);
    if (chosen.isEmpty()) {
        emitFailed(QStringLiteral("no image entries in archive"));
        return;
    }

    // Bounded-decompression guard. Look up the chosen entry's declared
    // uncompressed size; reject anything over the cap before we ever read the
    // bytes. fileInfoList()'s `size` field is the declared uncompressed size.
    qint64 declaredSize = -1;
    for (const auto& info : zr.fileInfoList()) {
        if (info.filePath == chosen) {
            declaredSize = info.size;
            break;
        }
    }
    if (declaredSize < 0) {
        emitFailed(QStringLiteral("chosen entry not found on second walk"));
        return;
    }
    if (declaredSize > kMaxDecompressedBytes) {
        emitFailed(QStringLiteral("declared size exceeds %1 MiB cap")
                       .arg(kMaxDecompressedBytes / (1024 * 1024)));
        return;
    }

    const QByteArray bytes = zr.fileData(chosen);
    if (bytes.isEmpty()) {
        emitFailed(QStringLiteral("zero-byte read for chosen entry"));
        return;
    }
    if (qint64(bytes.size()) > kMaxDecompressedBytes) {
        // Defense in depth -- declared-size check above should have caught
        // this. If declared size was a lie we still bail here.
        emitFailed(QStringLiteral("decompressed bytes exceed cap"));
        return;
    }

    QBuffer buf;
    buf.setData(bytes);
    if (!buf.open(QIODevice::ReadOnly)) {
        emitFailed(QStringLiteral("could not open in-memory buffer"));
        return;
    }
    QImageReader reader(&buf);
    reader.setAutoTransform(true);
    QImage img = reader.read();
    if (img.isNull()) {
        emitFailed(QStringLiteral("QImageReader failed to decode chosen entry"));
        return;
    }

    QImage thumb;
    if (img.width() > kThumbWidthPx) {
        thumb = img.scaledToWidth(kThumbWidthPx, Qt::SmoothTransformation);
    } else {
        thumb = std::move(img);
    }

    QDir().mkpath(m_outDir);
    const QString outPath = m_outDir + QChar('/')
        + QStringLiteral("premium_%1_v%2.jpg")
              .arg(m_seriesId)
              .arg(m_vol, 2, 10, QChar('0'));

    if (!thumb.save(outPath, "JPG", kJpegQuality)) {
        emitFailed(QStringLiteral("failed to write thumbnail JPG"));
        return;
    }

    emitReady(outPath);
#endif
}

} // anonymous namespace

PremiumCoverExtractor::PremiumCoverExtractor(QObject* parent)
    : QObject(parent)
{
}

PremiumCoverExtractor::~PremiumCoverExtractor() = default;

void PremiumCoverExtractor::extract(const QString& cbzPath,
                                    const QString& seriesId,
                                    int            volumeNumber,
                                    const QString& outputDir,
                                    const QString& coverPageHintEntryName,
                                    const QStringList& precomputedImageEntries)
{
    auto* runnable = new ExtractRunnable(
        QPointer<PremiumCoverExtractor>(this),
        cbzPath, seriesId, volumeNumber,
        outputDir, coverPageHintEntryName, precomputedImageEntries);
    QThreadPool::globalInstance()->start(runnable);
}

} // namespace tankoban::manga::premium
