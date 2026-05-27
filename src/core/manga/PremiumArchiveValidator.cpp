// src/core/manga/PremiumArchiveValidator.cpp
//
// TANKOYOMI_PREMIUM Phase 4 -- implementation. Follows the HAS_QT_ZIP pattern
// established by src/core/ArchiveReader.cpp (same Qt6::CorePrivate link, same
// <QtCore/private/qzipreader_p.h> include).
#include "PremiumArchiveValidator.h"

#include <QFileInfo>
#include <QtGlobal>

#ifdef HAS_QT_ZIP
#  include <QtCore/private/qzipreader_p.h>
#endif

namespace tankoban::manga::premium {

namespace {

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

bool isNestedArchiveExtension(const QString& lowerExt)
{
    return lowerExt == QLatin1String("zip")
        || lowerExt == QLatin1String("rar")
        || lowerExt == QLatin1String("7z")
        || lowerExt == QLatin1String("cbz")
        || lowerExt == QLatin1String("cbr")
        || lowerExt == QLatin1String("tar")
        || lowerExt == QLatin1String("gz");
}

bool isExecutableExtension(const QString& lowerExt)
{
    return lowerExt == QLatin1String("exe")
        || lowerExt == QLatin1String("dll")
        || lowerExt == QLatin1String("bat")
        || lowerExt == QLatin1String("cmd")
        || lowerExt == QLatin1String("ps1")
        || lowerExt == QLatin1String("sh")
        || lowerExt == QLatin1String("scr")
        || lowerExt == QLatin1String("msi")
        || lowerExt == QLatin1String("vbs")
        || lowerExt == QLatin1String("js");
}

ArchiveValidationResult fail(ArchiveValidationCode code, const QString& detail)
{
    ArchiveValidationResult r;
    r.code   = code;
    r.detail = detail;
    return r;
}

} // anonymous namespace

ArchiveValidationResult
PremiumArchiveValidator::validate(const QString& cbzPath, int expectedPageCount)
{
    const QFileInfo fi(cbzPath);
    const QString fileName = fi.fileName().toLower();
    if (fi.suffix().toLower() != QLatin1String("cbz") &&
        !fileName.endsWith(QLatin1String(".cbz.tankoban-part"))) {
        return fail(ArchiveValidationCode::NotCbzExtension,
                    QStringLiteral("file extension is not .cbz"));
    }

#ifndef HAS_QT_ZIP
    Q_UNUSED(expectedPageCount);
    return fail(ArchiveValidationCode::OpenFailed,
                QStringLiteral("HAS_QT_ZIP not defined; QZipReader unavailable"));
#else
    QZipReader zr(cbzPath);
    if (!zr.exists() || !zr.isReadable()) {
        return fail(ArchiveValidationCode::OpenFailed,
                    QStringLiteral("QZipReader could not open archive"));
    }

    const auto infos = zr.fileInfoList();
    if (infos.isEmpty()) {
        return fail(ArchiveValidationCode::Empty,
                    QStringLiteral("archive contains zero entries"));
    }
    if (infos.size() > kMaxEntryCount) {
        return fail(ArchiveValidationCode::Empty,
                    QStringLiteral("archive entry count exceeds bound %1")
                       .arg(kMaxEntryCount));
    }

    ArchiveValidationResult ok;
    ok.code = ArchiveValidationCode::Ok;

    int firstImageIdx = -1;
    for (int i = 0; i < infos.size(); ++i) {
        const auto& info = infos.at(i);
        if (info.isDir) continue;
        const QString name      = info.filePath;
        const QString lowerExt  = QFileInfo(name).suffix().toLower();

        if (isNestedArchiveExtension(lowerExt)) {
            return fail(ArchiveValidationCode::NestedArchiveEntry,
                        QStringLiteral("nested archive entry: ") + name);
        }
        if (isExecutableExtension(lowerExt)) {
            return fail(ArchiveValidationCode::ExecutableEntry,
                        QStringLiteral("executable entry: ") + name);
        }
        if (!isImageExtension(lowerExt)) {
            // Strict v1: only images allowed inside Premium cbz files.
            // Codex section 21 step 4 + section 24 cbz-only trust requirement.
            return fail(ArchiveValidationCode::NonImageEntry,
                        QStringLiteral("non-image entry: ") + name);
        }
        ok.imageEntries.append(name);
        if (firstImageIdx < 0) firstImageIdx = i;
    }

    ok.pageCount = ok.imageEntries.size();
    if (ok.pageCount == 0) {
        return fail(ArchiveValidationCode::Empty,
                    QStringLiteral("archive has no image entries"));
    }
    if (ok.pageCount > kMaxPagesPerVolume) {
        return fail(ArchiveValidationCode::PageCountExceedsBound,
                    QStringLiteral("pageCount %1 exceeds bound %2")
                        .arg(ok.pageCount).arg(kMaxPagesPerVolume));
    }
    if (expectedPageCount > 0 &&
        qAbs(ok.pageCount - expectedPageCount) > kCatalogPageCountTolerance) {
        return fail(ArchiveValidationCode::PageCountMismatchCatalog,
                    QStringLiteral("pageCount %1 differs from catalog %2 by more than tolerance %3")
                        .arg(ok.pageCount).arg(expectedPageCount).arg(kCatalogPageCountTolerance));
    }

    // Bounded decompression check on the first image. Codex section 24.5.
    if (firstImageIdx >= 0) {
        const auto& info = infos.at(firstImageIdx);
        if (info.size > kMaxFirstImageBytes) {
            return fail(ArchiveValidationCode::DecompressedFirstImageTooLarge,
                        QStringLiteral("first image declared size %1 exceeds bound %2")
                            .arg(info.size).arg(kMaxFirstImageBytes));
        }
        const QByteArray data = zr.fileData(info.filePath);
        if (data.isEmpty()) {
            return fail(ArchiveValidationCode::ReadFailed,
                        QStringLiteral("first image read returned empty data"));
        }
        if (data.size() > kMaxFirstImageBytes) {
            return fail(ArchiveValidationCode::DecompressedFirstImageTooLarge,
                        QStringLiteral("first image decompressed size %1 exceeds bound %2")
                            .arg(data.size()).arg(kMaxFirstImageBytes));
        }
    }

    return ok;
#endif
}

} // namespace tankoban::manga::premium
