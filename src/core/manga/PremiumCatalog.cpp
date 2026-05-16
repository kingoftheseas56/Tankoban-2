// src/core/manga/PremiumCatalog.cpp
#include "PremiumCatalog.h"
#include "CanonicalChapterKey.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QDebug>

namespace tankoban::manga::premium {

namespace {

constexpr int kMinVolumeNumber = 1;
constexpr int kMaxVolumeNumber = 999;
constexpr int kMaxPagesPerVolume = 600;     // sanity bound; validator uses this for the optional pageCount hint

// Lowercase-hex 40-char (SHA-1) infoHash check. The catalog schema documents
// expectedInfoHash as "40-char lowercase hex" with no separator allowance, so
// the validator matches the raw value directly. Phase 3 (TorrentVolumeProvider)
// compares this field against libtorrent's bare-hex infoHash; storage MUST
// match what libtorrent emits, so we reject (rather than silently normalize)
// any string carrying dashes / whitespace / uppercase.
bool isValidInfoHash(const QString& s)
{
    static const QRegularExpression rx(QStringLiteral("^[0-9a-f]{40}$"));
    return rx.match(s).hasMatch();
}

// Pull the infoHash out of a magnet URI's xt parameter (urn:btih:...).
// Returns empty QString if not parseable. Used to cross-check against the
// catalog's expectedInfoHash field.
QString infoHashFromMagnet(const QString& magnetUri)
{
    static const QRegularExpression rx(
        QStringLiteral("xt=urn:btih:([0-9A-Fa-f]{40})"),
        QRegularExpression::CaseInsensitiveOption);
    const auto m = rx.match(magnetUri);
    if (!m.hasMatch()) return {};
    return m.captured(1).toLower();
}

void appendDiagnostic(QList<ValidationDiagnostic>& out,
                      const QString& file, const QString& series, int vol,
                      ValidationSeverity sev,
                      const QString& code, const QString& message)
{
    ValidationDiagnostic d;
    d.catalogFile   = file;
    d.seriesId      = series;
    d.volumeNumber  = vol;
    d.severity      = sev;
    d.code          = code;
    d.message       = message;
    out.append(d);
    qDebug().noquote() << QStringLiteral("[PremiumCatalog]")
                       << file
                       << QStringLiteral("series=") + series
                       << QStringLiteral("vol=") + QString::number(vol)
                       << QStringLiteral("severity=") + QString::number(int(sev))
                       << code
                       << message;
}

bool parseManifest(const QJsonObject& root, PremiumCatalogManifest& out)
{
    out.id           = root.value(QStringLiteral("id")).toString();
    out.name         = root.value(QStringLiteral("name")).toString();
    out.version      = root.value(QStringLiteral("version")).toString();
    out.description  = root.value(QStringLiteral("description")).toString();
    out.contact      = root.value(QStringLiteral("contact")).toString();
    const auto hints = root.value(QStringLiteral("behaviorHints")).toObject();
    out.behaviorHintsP2P   = hints.value(QStringLiteral("p2p")).toBool(true);
    out.behaviorHintsAdult = hints.value(QStringLiteral("adult")).toBool(false);
    return !out.id.isEmpty() && !out.version.isEmpty();
}

bool parseVolume(const QJsonObject& obj, const QString& seriesId,
                 const QString& file, PremiumVolumeEntry& out,
                 QList<ValidationDiagnostic>& diag)
{
    out.vol            = obj.value(QStringLiteral("vol")).toInt(-1);
    out.fileIndex      = obj.value(QStringLiteral("fileIndex")).toInt(-1);
    out.fileSizeBytes  = static_cast<qint64>(obj.value(QStringLiteral("fileSizeBytes")).toDouble(0));
    out.pieceStart     = obj.value(QStringLiteral("pieceStart")).toInt(-1);
    out.pieceEnd       = obj.value(QStringLiteral("pieceEnd")).toInt(-1);
    out.cbzFileName    = obj.value(QStringLiteral("cbzFileName")).toString();
    out.boundaryPolicy = obj.value(QStringLiteral("boundaryPolicy")).toString(
                            QStringLiteral("allow-piece-overlap"));
    out.pageCount      = obj.value(QStringLiteral("pageCount")).toInt(0);
    out.coverPageHint  = obj.value(QStringLiteral("coverPageHint")).toString();

    const QJsonArray chapters = obj.value(QStringLiteral("chapters")).toArray();
    for (const auto& ch : chapters) {
        const QJsonObject co = ch.toObject();
        PremiumChapterRef ref;
        ref.chapterNumber = co.value(QStringLiteral("num")).toString();
        ref.title         = co.value(QStringLiteral("title")).toString();
        if (ref.chapterNumber.isEmpty()) {
            appendDiagnostic(diag, file, seriesId, out.vol,
                             ValidationSeverity::Warn, QStringLiteral("chapter_missing_num"),
                             QStringLiteral("Chapter entry missing 'num' field; dropped"));
            continue;
        }
        out.chapters.append(ref);
    }

    // Required-field checks at RejectVolume severity. Each kicks the volume
    // out of the entry but keeps the rest of the series.
    if (out.vol < kMinVolumeNumber || out.vol > kMaxVolumeNumber) {
        appendDiagnostic(diag, file, seriesId, out.vol, ValidationSeverity::RejectVolume,
                         QStringLiteral("vol_out_of_range"),
                         QStringLiteral("vol must be in [1, 999]"));
        return false;
    }
    if (out.fileIndex < 0) {
        appendDiagnostic(diag, file, seriesId, out.vol, ValidationSeverity::RejectVolume,
                         QStringLiteral("missing_fileIndex"),
                         QStringLiteral("fileIndex required"));
        return false;
    }
    if (out.fileSizeBytes <= 0) {
        appendDiagnostic(diag, file, seriesId, out.vol, ValidationSeverity::RejectVolume,
                         QStringLiteral("missing_fileSizeBytes"),
                         QStringLiteral("fileSizeBytes required and > 0"));
        return false;
    }
    if (out.pieceStart < 0 || out.pieceEnd < out.pieceStart) {
        appendDiagnostic(diag, file, seriesId, out.vol, ValidationSeverity::RejectVolume,
                         QStringLiteral("invalid_piece_range"),
                         QStringLiteral("pieceStart/pieceEnd must form a valid range"));
        return false;
    }
    if (out.cbzFileName.isEmpty() || !out.cbzFileName.endsWith(QStringLiteral(".cbz"),
                                                                Qt::CaseInsensitive)) {
        appendDiagnostic(diag, file, seriesId, out.vol, ValidationSeverity::RejectVolume,
                         QStringLiteral("invalid_cbzFileName"),
                         QStringLiteral("cbzFileName must end in .cbz"));
        return false;
    }
    if (out.pageCount > kMaxPagesPerVolume) {
        appendDiagnostic(diag, file, seriesId, out.vol, ValidationSeverity::Warn,
                         QStringLiteral("page_count_unusually_high"),
                         QStringLiteral("pageCount exceeds %1; archive validator will cross-check")
                            .arg(kMaxPagesPerVolume));
    }
    return true;
}

bool parseSeries(const QJsonObject& obj, const QString& file,
                 PremiumCatalogEntry& out, QList<ValidationDiagnostic>& diag)
{
    out.seriesId        = obj.value(QStringLiteral("seriesId")).toString();
    out.title           = obj.value(QStringLiteral("title")).toString();
    out.anilistId       = obj.value(QStringLiteral("anilistId")).toInt(0);
    out.status          = obj.value(QStringLiteral("status")).toString();
    out.magnetUri       = obj.value(QStringLiteral("magnetUri")).toString();
    out.expectedInfoHash = obj.value(QStringLiteral("expectedInfoHash")).toString().toLower();
    out.trustedUploader = obj.value(QStringLiteral("trustedUploader")).toString();
    out.releaseEdition  = obj.value(QStringLiteral("releaseEdition")).toString();
    out.format          = obj.value(QStringLiteral("format")).toString();

    for (const auto& alt : obj.value(QStringLiteral("alternateTitles")).toArray()) {
        const QString s = alt.toString();
        if (!s.isEmpty()) out.alternateTitles.append(s);
    }

    const QJsonObject pcf = obj.value(QStringLiteral("postCoverageFallback")).toObject();
    out.postCoverageWeebcentralSlug    = pcf.value(QStringLiteral("weebcentralSlug")).toString();
    out.postCoverageStartsAfterVolume  = pcf.value(QStringLiteral("startsAfterVolume")).toInt(0);

    // Required-field checks at RejectSeries severity.
    if (out.seriesId.isEmpty() || out.title.isEmpty()) {
        appendDiagnostic(diag, file, out.seriesId, 0, ValidationSeverity::RejectSeries,
                         QStringLiteral("missing_identity"),
                         QStringLiteral("seriesId and title required"));
        return false;
    }
    if (out.status != QStringLiteral("completed") && out.status != QStringLiteral("ongoing")) {
        appendDiagnostic(diag, file, out.seriesId, 0, ValidationSeverity::RejectSeries,
                         QStringLiteral("invalid_status"),
                         QStringLiteral("status must be 'completed' or 'ongoing'"));
        return false;
    }
    if (out.magnetUri.isEmpty()) {
        appendDiagnostic(diag, file, out.seriesId, 0, ValidationSeverity::RejectSeries,
                         QStringLiteral("missing_magnet_uri"),
                         QStringLiteral("magnetUri required"));
        return false;
    }
    if (!isValidInfoHash(out.expectedInfoHash)) {
        appendDiagnostic(diag, file, out.seriesId, 0, ValidationSeverity::RejectSeries,
                         QStringLiteral("invalid_expected_infohash"),
                         QStringLiteral("expectedInfoHash must be 40-char lowercase hex"));
        return false;
    }
    const QString magnetIh = infoHashFromMagnet(out.magnetUri);
    if (!magnetIh.isEmpty() && magnetIh != out.expectedInfoHash) {
        appendDiagnostic(diag, file, out.seriesId, 0, ValidationSeverity::RejectSeries,
                         QStringLiteral("infohash_mismatch"),
                         QStringLiteral("expectedInfoHash does not match magnet xt=urn:btih:..."));
        return false;
    }
    if (out.format != QStringLiteral("one-cbz-per-volume")) {
        appendDiagnostic(diag, file, out.seriesId, 0, ValidationSeverity::RejectSeries,
                         QStringLiteral("unsupported_format"),
                         QStringLiteral("v1 supports only format='one-cbz-per-volume'"));
        return false;
    }
    if (out.status == QStringLiteral("ongoing") &&
        out.postCoverageWeebcentralSlug.isEmpty()) {
        appendDiagnostic(diag, file, out.seriesId, 0, ValidationSeverity::Warn,
                         QStringLiteral("ongoing_missing_post_coverage"),
                         QStringLiteral("ongoing series should set postCoverageFallback.weebcentralSlug"));
    }

    // Volumes pass.
    const QJsonArray vols = obj.value(QStringLiteral("volumes")).toArray();
    for (const auto& v : vols) {
        PremiumVolumeEntry pv;
        if (parseVolume(v.toObject(), out.seriesId, file, pv, diag)) {
            out.volumes.append(pv);
        }
    }
    if (out.volumes.isEmpty()) {
        appendDiagnostic(diag, file, out.seriesId, 0, ValidationSeverity::RejectSeries,
                         QStringLiteral("no_valid_volumes"),
                         QStringLiteral("series has no valid volumes after validation"));
        return false;
    }
    return true;
}

bool loadOneFile(const QString& path,
                 PremiumCatalogManifest& outManifest,
                 QList<PremiumCatalogEntry>& outEntries,
                 QList<ValidationDiagnostic>& diag)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        appendDiagnostic(diag, path, {}, 0, ValidationSeverity::RejectFile,
                         QStringLiteral("file_not_readable"),
                         f.errorString());
        return false;
    }
    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        appendDiagnostic(diag, path, {}, 0, ValidationSeverity::RejectFile,
                         QStringLiteral("invalid_json"),
                         QStringLiteral("JSON parse: ") + perr.errorString());
        return false;
    }
    const QJsonObject root = doc.object();
    if (!parseManifest(root, outManifest)) {
        appendDiagnostic(diag, path, {}, 0, ValidationSeverity::RejectFile,
                         QStringLiteral("invalid_manifest"),
                         QStringLiteral("manifest.id and manifest.version are required"));
        return false;
    }
    const QJsonArray series = root.value(QStringLiteral("series")).toArray();
    for (const auto& s : series) {
        PremiumCatalogEntry e;
        if (parseSeries(s.toObject(), path, e, diag)) {
            outEntries.append(e);
        }
    }
    return true;
}

} // anonymous namespace

PremiumCatalog::PremiumCatalog(const QString& catalogsDir, QObject* parent)
    : QObject(parent)
{
    QDir dir(catalogsDir);
    if (!dir.exists()) {
        qDebug().noquote() << QStringLiteral("[PremiumCatalog] catalogs dir does not exist:")
                           << catalogsDir;
        return;
    }
    const QFileInfoList files = dir.entryInfoList(
        QStringList{ QStringLiteral("*.json") }, QDir::Files, QDir::Name);
    for (const auto& fi : files) {
        // Skip leading-underscore files (development fixtures like _test_minimal.json)
        if (fi.baseName().startsWith(QChar('_'))) continue;

        PremiumCatalogManifest manifest;
        QList<PremiumCatalogEntry> entries;
        if (!loadOneFile(fi.absoluteFilePath(), manifest, entries, m_diagnostics)) {
            continue;
        }
        for (auto& e : entries) {
            if (m_byId.contains(e.seriesId)) {
                appendDiagnostic(m_diagnostics, fi.absoluteFilePath(), e.seriesId, 0,
                                 ValidationSeverity::RejectSeries,
                                 QStringLiteral("duplicate_seriesId"),
                                 QStringLiteral("seriesId already loaded; skipping"));
                continue;
            }
            // Index lowercased primary title + alternates.
            m_titleLookup.insert(e.title.toLower(), e.seriesId);
            for (const auto& alt : e.alternateTitles) {
                m_titleLookup.insert(alt.toLower(), e.seriesId);
            }
            m_byId.insert(e.seriesId, e);
            m_orderedEntries.append(e);
        }
    }
    qDebug().noquote() << QStringLiteral("[PremiumCatalog] loaded")
                       << m_orderedEntries.size()
                       << QStringLiteral("series across") << files.size()
                       << QStringLiteral("file(s), with") << m_diagnostics.size()
                       << QStringLiteral("diagnostic(s)");
}

PremiumCatalog::~PremiumCatalog() = default;

bool PremiumCatalog::isPremiumSeries(const QString& title) const
{
    return m_titleLookup.contains(title.toLower());
}

std::optional<PremiumCatalogEntry>
PremiumCatalog::entryForTitle(const QString& title) const
{
    const auto it = m_titleLookup.constFind(title.toLower());
    if (it == m_titleLookup.constEnd()) return std::nullopt;
    return entryById(it.value());
}

std::optional<PremiumCatalogEntry>
PremiumCatalog::entryById(const QString& seriesId) const
{
    const auto it = m_byId.constFind(seriesId);
    if (it == m_byId.constEnd()) return std::nullopt;
    return it.value();
}

QList<PremiumCatalogEntry> PremiumCatalog::allEntries() const
{
    return m_orderedEntries;
}

QList<ValidationDiagnostic> PremiumCatalog::diagnostics() const
{
    return m_diagnostics;
}

} // namespace tankoban::manga::premium
