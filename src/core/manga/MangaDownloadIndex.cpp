// COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 — forked from
// src/core/stream/StreamDownloadIndex.cpp per brainstorm §6.1. Same
// threadsafe canonical-key-keyed JSON-backed shape, different keying
// (sourceId:seriesId:chapterId instead of imdbId:season:episode).

#include "MangaDownloadIndex.h"

#include "core/JsonStore.h"
#include "core/DebugLogBuffer.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QPair>
#include <QStringList>

// ── Static helpers ──────────────────────────────────────────────────────────

QString MangaDownloadIndex::computeCanonicalKey(const QString& anyPath)
{
    // Lowercased native-form absolute path. Handles Windows case-
    // insensitivity + slash normalization in one pass. Matches the
    // StreamDownloadIndex precedent for cross-domain consistency.
    return QDir::toNativeSeparators(QFileInfo(anyPath).absoluteFilePath()).toLower();
}

QString MangaDownloadIndex::computeChapterKey(const QString& sourceId,
                                                const QString& seriesId,
                                                const QString& chapterId)
{
    return sourceId + QStringLiteral(":") + seriesId + QStringLiteral(":") + chapterId;
}

QString MangaDownloadIndex::computeSeriesKey(const QString& sourceId,
                                              const QString& seriesId)
{
    return sourceId + QStringLiteral(":") + seriesId;
}

// ── ctor + load/save ────────────────────────────────────────────────────────

MangaDownloadIndex::MangaDownloadIndex(JsonStore* store, QObject* parent)
    : QObject(parent), m_store(store)
{
    load();
}

void MangaDownloadIndex::load()
{
    if (!m_store)
        return;

    const QJsonObject data = m_store->read(FILENAME);
    if (data.isEmpty())
        return;  // first-launch: file doesn't exist yet, nothing to load

    const int storedVersion = data.value(QStringLiteral("schemaVersion")).toInt(0);
    // Phase 5: accept v1 (legacy single-chapter only) and v2 (with
    // servedChapterKeys). Anything else is unrecognized -> start empty.
    if (storedVersion != 1 && storedVersion != kSchemaVersion) {
        DebugLogBuffer::instance().info(QStringLiteral("manga-download-index"),
            QStringLiteral("schema mismatch on load — starting empty"),
            QJsonObject{{QStringLiteral("storedVersion"), storedVersion},
                        {QStringLiteral("expected"), kSchemaVersion}});
        return;
    }

    const QJsonArray entries = data.value(QStringLiteral("entries")).toArray();

    QMutexLocker lock(&m_mutex);
    for (const QJsonValue& v : entries) {
        const QJsonObject obj = v.toObject();
        Entry e;
        e.sourceId      = obj.value(QStringLiteral("sourceId")).toString();
        e.seriesId      = obj.value(QStringLiteral("seriesId")).toString();
        e.chapterId     = obj.value(QStringLiteral("chapterId")).toString();
        e.volumeNumber  = obj.value(QStringLiteral("volumeNumber")).toInt(0);
        e.canonicalPath = obj.value(QStringLiteral("canonicalPath")).toString();
        e.addedAt       = obj.value(QStringLiteral("addedAt")).toInteger(0);
        e.fileSizeBytes = obj.value(QStringLiteral("fileSizeBytes")).toInteger(0);

        if (e.sourceId.isEmpty() || e.seriesId.isEmpty() ||
            e.chapterId.isEmpty() || e.canonicalPath.isEmpty())
            continue;

        // Phase 5: deserialize servedChapterKeys with backward-compat
        // reconstruction. Pre-Phase-5 (v1) entries had implicit single-
        // chapter ownership; seed the set from the legacy chapterId field.
        const QJsonArray keysJson = obj.value(QStringLiteral("servedChapterKeys")).toArray();
        if (keysJson.isEmpty()) {
            e.servedChapterKeys.insert(
                computeChapterKey(e.sourceId, e.seriesId, e.chapterId));
        } else {
            for (const auto& kv : keysJson) {
                const QString s = kv.toString();
                if (!s.isEmpty()) e.servedChapterKeys.insert(s);
            }
        }

        const QString key = computeCanonicalKey(e.canonicalPath);
        auto existing = m_byPath.find(key);
        if (existing == m_byPath.end()) {
            m_byPath.insert(key, e);
        } else {
            // Duplicate canonicalKey in the loaded JSON (possible from
            // pre-Phase-5 buggy registerChapter-N-times path that may
            // have written multiple rows for the same path). Merge their
            // served-chapter sets rather than overwriting; keep the larger
            // fileSizeBytes; keep the earlier addedAt.
            for (const auto& k : e.servedChapterKeys) existing->servedChapterKeys.insert(k);
            if (e.fileSizeBytes > existing->fileSizeBytes) existing->fileSizeBytes = e.fileSizeBytes;
            if (e.addedAt != 0 && (existing->addedAt == 0 || e.addedAt < existing->addedAt))
                existing->addedAt = e.addedAt;
        }
        // Insert every served chapter key into m_byChapter so v2 volume
        // entries can be looked up by any of their N chapters.
        for (const auto& sk : e.servedChapterKeys) {
            m_byChapter.insert(sk, key);
        }
        m_seriesHasAny.insert(computeSeriesKey(e.sourceId, e.seriesId));
    }

    DebugLogBuffer::instance().info(QStringLiteral("manga-download-index"),
        QStringLiteral("loaded entries"),
        QJsonObject{{QStringLiteral("count"), m_byPath.size()}});
}

void MangaDownloadIndex::save()
{
    if (!m_store)
        return;

    QJsonArray entries;
    {
        QMutexLocker lock(&m_mutex);
        for (auto it = m_byPath.constBegin(); it != m_byPath.constEnd(); ++it) {
            const Entry& e = it.value();
            QJsonObject obj;
            obj[QStringLiteral("sourceId")]      = e.sourceId;
            obj[QStringLiteral("seriesId")]      = e.seriesId;
            obj[QStringLiteral("chapterId")]     = e.chapterId;
            obj[QStringLiteral("volumeNumber")]  = e.volumeNumber;
            obj[QStringLiteral("canonicalPath")] = e.canonicalPath;
            obj[QStringLiteral("addedAt")]       = static_cast<qint64>(e.addedAt);
            obj[QStringLiteral("fileSizeBytes")] = static_cast<qint64>(e.fileSizeBytes);
            // Phase 5 v2 schema: persist the served-chapter set so volume
            // entries restore their N-key ownership across launches.
            QJsonArray keysJson;
            for (const auto& k : e.servedChapterKeys) keysJson.append(k);
            obj[QStringLiteral("servedChapterKeys")] = keysJson;
            entries.append(obj);
        }
    }

    QJsonObject root;
    root[QStringLiteral("schemaVersion")] = kSchemaVersion;
    root[QStringLiteral("entries")]       = entries;
    m_store->write(FILENAME, root);
}

// ── Mutating API ────────────────────────────────────────────────────────────

void MangaDownloadIndex::recomputeSeriesHasAnyLocked(const QString& sourceId,
                                                     const QString& seriesId)
{
    const QString seriesKey = computeSeriesKey(sourceId, seriesId);
    bool stillHasAny = false;
    for (const Entry& other : m_byPath) {
        if (other.sourceId == sourceId && other.seriesId == seriesId) {
            stillHasAny = true;
            break;
        }
    }
    if (stillHasAny)
        m_seriesHasAny.insert(seriesKey);
    else
        m_seriesHasAny.remove(seriesKey);
}

void MangaDownloadIndex::registerChapter(const QString& sourceId,
                                          const QString& seriesId,
                                          const QString& chapterId,
                                          const QString& canonicalPath,
                                          qint64 fileSizeBytes)
{
    if (sourceId.isEmpty() || seriesId.isEmpty() ||
        chapterId.isEmpty() || canonicalPath.isEmpty())
        return;

    Entry e;
    e.sourceId      = sourceId;
    e.seriesId      = seriesId;
    e.chapterId     = chapterId;
    e.canonicalPath = canonicalPath;
    e.addedAt       = QDateTime::currentMSecsSinceEpoch();
    e.fileSizeBytes = fileSizeBytes;

    const QString key       = computeCanonicalKey(canonicalPath);
    const QString chapKey   = computeChapterKey(sourceId, seriesId, chapterId);
    const QString seriesKey = computeSeriesKey(sourceId, seriesId);

    // Phase 5: populate servedChapterKeys on the new Entry so its single
    // chapter ownership is explicit. registerVolume populates N keys per
    // entry; registerChapter populates exactly one.
    e.servedChapterKeys.insert(chapKey);

    {
        QMutexLocker lock(&m_mutex);

        // If a prior entry occupied this chapter slot at a different path,
        // evict it first so by-chapter never points at a stale path.
        QString displacedSourceId, displacedSeriesId;
        auto chapIt = m_byChapter.constFind(chapKey);
        if (chapIt != m_byChapter.constEnd() && chapIt.value() != key) {
            auto displacedIt = m_byPath.constFind(chapIt.value());
            if (displacedIt != m_byPath.constEnd()) {
                displacedSourceId = displacedIt.value().sourceId;
                displacedSeriesId = displacedIt.value().seriesId;
            }
            m_byPath.remove(chapIt.value());
        }

        // If an entry already exists at this canonical path, extend its
        // served set rather than overwriting (e.g. a Premium volume entry
        // that a legacy registerChapter call also targets). Otherwise
        // insert the freshly-built Entry.
        auto pIt = m_byPath.find(key);
        if (pIt == m_byPath.end()) {
            m_byPath.insert(key, e);
        } else {
            pIt->servedChapterKeys.insert(chapKey);
            if (fileSizeBytes > pIt->fileSizeBytes) pIt->fileSizeBytes = fileSizeBytes;
        }
        m_byChapter.insert(chapKey, key);
        m_seriesHasAny.insert(seriesKey);

        // If the displaced entry belonged to a different series, recompute
        // its has-any flag.
        if (!displacedSourceId.isEmpty() &&
            (displacedSourceId != sourceId || displacedSeriesId != seriesId))
            recomputeSeriesHasAnyLocked(displacedSourceId, displacedSeriesId);
    }

    DebugLogBuffer::instance().info(QStringLiteral("manga-download-index"),
        QStringLiteral("registerChapter"),
        QJsonObject{{QStringLiteral("sourceId"), sourceId},
                    {QStringLiteral("seriesId"), seriesId},
                    {QStringLiteral("chapterId"), chapterId},
                    {QStringLiteral("path"), canonicalPath}});

    save();
    emit entriesChanged();
}

void MangaDownloadIndex::registerVolume(const QString&     sourceId,
                                        const QString&     seriesId,
                                        int                volumeNumber,
                                        const QString&     canonicalPath,
                                        qint64             fileSizeBytes,
                                        const QStringList& chapterIds)
{
    if (sourceId.isEmpty() || seriesId.isEmpty() || canonicalPath.isEmpty())
        return;

    const QString canonicalKey = computeCanonicalKey(canonicalPath);
    QSet<QString> newKeys;
    for (const auto& cid : chapterIds) {
        if (cid.isEmpty()) continue;
        newKeys.insert(computeChapterKey(sourceId, seriesId, cid));
    }
    if (newKeys.isEmpty()) return;

    {
        QMutexLocker lock(&m_mutex);
        auto it = m_byPath.find(canonicalKey);
        if (it == m_byPath.end()) {
            Entry e;
            e.sourceId           = sourceId;
            e.seriesId           = seriesId;
            e.chapterId          = chapterIds.isEmpty() ? QString() : chapterIds.first();
            e.volumeNumber       = volumeNumber;
            e.canonicalPath      = canonicalPath;
            e.addedAt            = QDateTime::currentMSecsSinceEpoch();
            e.fileSizeBytes      = fileSizeBytes;
            e.servedChapterKeys  = newKeys;
            m_byPath.insert(canonicalKey, e);
        } else {
            it->volumeNumber = volumeNumber;
            for (const auto& k : newKeys) it->servedChapterKeys.insert(k);
            if (fileSizeBytes > it->fileSizeBytes) it->fileSizeBytes = fileSizeBytes;
        }
        for (const auto& k : newKeys) m_byChapter.insert(k, canonicalKey);
        m_seriesHasAny.insert(computeSeriesKey(sourceId, seriesId));
    }

    QJsonObject details;
    details[QStringLiteral("sourceId")]     = sourceId;
    details[QStringLiteral("seriesId")]     = seriesId;
    details[QStringLiteral("volumeNumber")] = volumeNumber;
    details[QStringLiteral("path")]         = canonicalPath;
    details[QStringLiteral("chapterCount")] = newKeys.size();
    DebugLogBuffer::instance().info(QStringLiteral("manga-download-index"),
        QStringLiteral("registerVolume"), details);

    save();
    emit entriesChanged();
}

void MangaDownloadIndex::evictBySeries(const QString& sourceId, const QString& seriesId)
{
    if (sourceId.isEmpty() || seriesId.isEmpty())
        return;

    int removed = 0;
    {
        QMutexLocker lock(&m_mutex);
        // Phase 5: each Entry may serve N chapter keys (volume cbz). Collect
        // every served chapter key per matching entry, not just the legacy
        // single chapterId field.
        QList<QString> pathsToRemove;
        QList<QString> chapterKeysToRemove;
        for (auto it = m_byPath.constBegin(); it != m_byPath.constEnd(); ++it) {
            const Entry& e = it.value();
            if (e.sourceId == sourceId && e.seriesId == seriesId) {
                pathsToRemove.append(it.key());
                for (const auto& k : e.servedChapterKeys) chapterKeysToRemove.append(k);
                // Legacy backstop: if servedChapterKeys is empty (shouldn't
                // happen post-Phase-5 but defends against partial state),
                // fall back to the legacy chapterId field.
                if (e.servedChapterKeys.isEmpty()) {
                    chapterKeysToRemove.append(
                        computeChapterKey(e.sourceId, e.seriesId, e.chapterId));
                }
            }
        }
        for (const auto& p : pathsToRemove) m_byPath.remove(p);
        for (const auto& k : chapterKeysToRemove) m_byChapter.remove(k);
        m_seriesHasAny.remove(computeSeriesKey(sourceId, seriesId));
        removed = pathsToRemove.size();
    }

    if (removed > 0) {
        DebugLogBuffer::instance().info(QStringLiteral("manga-download-index"),
            QStringLiteral("evictBySeries"),
            QJsonObject{{QStringLiteral("sourceId"), sourceId},
                        {QStringLiteral("seriesId"), seriesId},
                        {QStringLiteral("removed"), removed}});
        save();
        emit entriesChanged();
    }
}

void MangaDownloadIndex::evictByChapter(const QString& sourceId,
                                         const QString& seriesId,
                                         const QString& chapterId)
{
    if (sourceId.isEmpty() || seriesId.isEmpty() || chapterId.isEmpty())
        return;

    bool changed = false;
    {
        QMutexLocker lock(&m_mutex);
        const QString chapKey = computeChapterKey(sourceId, seriesId, chapterId);
        auto chapIt = m_byChapter.constFind(chapKey);
        if (chapIt == m_byChapter.constEnd())
            return;
        const QString canonicalKey = chapIt.value();
        m_byChapter.remove(chapKey);

        // Phase 5: gate m_byPath removal on the served-chapter set. For a
        // legacy single-chapter entry the set held exactly one key so this
        // eviction empties it and m_byPath is dropped (preserves the v1
        // behavior). For a Premium volume entry serving N chapters, the
        // m_byPath row survives until the final chapter is evicted.
        auto pIt = m_byPath.find(canonicalKey);
        if (pIt != m_byPath.end()) {
            pIt->servedChapterKeys.remove(chapKey);
            if (pIt->servedChapterKeys.isEmpty()) {
                m_byPath.erase(pIt);
            }
        }
        recomputeSeriesHasAnyLocked(sourceId, seriesId);
        changed = true;
    }

    if (changed) {
        DebugLogBuffer::instance().info(QStringLiteral("manga-download-index"),
            QStringLiteral("evictByChapter"),
            QJsonObject{{QStringLiteral("sourceId"), sourceId},
                        {QStringLiteral("seriesId"), seriesId},
                        {QStringLiteral("chapterId"), chapterId}});
        save();
        emit entriesChanged();
    }
}

void MangaDownloadIndex::evictByVolume(const QString& sourceId,
                                        const QString& seriesId,
                                        int volumeNumber)
{
    if (sourceId.isEmpty() || seriesId.isEmpty() || volumeNumber <= 0)
        return;

    bool changed = false;
    {
        QMutexLocker lock(&m_mutex);
        // Find every entry matching this volume and evict all served chapter
        // keys + the m_byPath entry. A single volume entry may serve N chapters;
        // remove all its chapter keys from m_byChapter and drop the m_byPath row.
        QList<QString> pathsToRemove;
        for (auto it = m_byPath.constBegin(); it != m_byPath.constEnd(); ++it) {
            const Entry& e = it.value();
            if (e.sourceId == sourceId &&
                e.seriesId == seriesId &&
                e.volumeNumber == volumeNumber) {
                pathsToRemove.append(it.key());
            }
        }
        for (const auto& pathKey : pathsToRemove) {
            auto pIt = m_byPath.find(pathKey);
            if (pIt == m_byPath.end()) continue;
            for (const auto& sk : pIt->servedChapterKeys)
                m_byChapter.remove(sk);
            m_byPath.erase(pIt);
        }
        if (!pathsToRemove.isEmpty()) {
            recomputeSeriesHasAnyLocked(sourceId, seriesId);
            changed = true;
        }
    }

    if (changed) {
        DebugLogBuffer::instance().info(QStringLiteral("manga-download-index"),
            QStringLiteral("evictByVolume"),
            QJsonObject{{QStringLiteral("sourceId"), sourceId},
                        {QStringLiteral("seriesId"), seriesId},
                        {QStringLiteral("volumeNumber"), volumeNumber}});
        save();
        emit entriesChanged();
    }
}

void MangaDownloadIndex::evictByPath(const QString& canonicalPath)
{
    if (canonicalPath.isEmpty()) return;

    QString sourceId, seriesId;
    bool changed = false;
    {
        QMutexLocker lock(&m_mutex);
        // Match on the Entry.canonicalPath field (robust to whatever key scheme
        // m_byPath uses), then drop its served chapter keys + the row.
        QString matchKey;
        for (auto it = m_byPath.constBegin(); it != m_byPath.constEnd(); ++it) {
            if (it.value().canonicalPath == canonicalPath) {
                matchKey = it.key();
                break;
            }
        }
        if (matchKey.isEmpty()) return;
        auto pIt = m_byPath.find(matchKey);
        if (pIt == m_byPath.end()) return;
        sourceId = pIt->sourceId;
        seriesId = pIt->seriesId;
        for (const auto& sk : pIt->servedChapterKeys)
            m_byChapter.remove(sk);
        m_byPath.erase(pIt);
        recomputeSeriesHasAnyLocked(sourceId, seriesId);
        changed = true;
    }

    if (changed) {
        DebugLogBuffer::instance().info(QStringLiteral("manga-download-index"),
            QStringLiteral("evictByPath"),
            QJsonObject{{QStringLiteral("path"), canonicalPath},
                        {QStringLiteral("sourceId"), sourceId},
                        {QStringLiteral("seriesId"), seriesId}});
        save();
        emit entriesChanged();
    }
}

void MangaDownloadIndex::validateAll()
{
    // Snapshot keys+paths under lock; stat off-lock; collect missing; then
    // re-acquire lock to evict.
    QList<QPair<QString, Entry>> snapshot;
    {
        QMutexLocker lock(&m_mutex);
        for (auto it = m_byPath.constBegin(); it != m_byPath.constEnd(); ++it)
            snapshot.append({it.key(), it.value()});
    }

    QList<Entry> missing;
    for (const auto& pr : snapshot) {
        if (!QFileInfo::exists(pr.second.canonicalPath))
            missing.append(pr.second);
    }

    if (missing.isEmpty())
        return;

    DebugLogBuffer::instance().info(QStringLiteral("manga-download-index"),
        QStringLiteral("validateAll evicting missing entries"),
        QJsonObject{{QStringLiteral("count"), missing.size()}});

    for (const Entry& e : missing)
        evictByChapter(e.sourceId, e.seriesId, e.chapterId);
}

// ── Read API ────────────────────────────────────────────────────────────────

bool MangaDownloadIndex::isComicsOwned(const QString& canonicalKey) const
{
    QMutexLocker lock(&m_mutex);
    return m_byPath.contains(canonicalKey);
}

std::optional<QString> MangaDownloadIndex::filePathFor(const QString& sourceId,
                                                        const QString& seriesId,
                                                        const QString& chapterId) const
{
    const QString chapKey = computeChapterKey(sourceId, seriesId, chapterId);
    QMutexLocker lock(&m_mutex);
    auto it = m_byChapter.constFind(chapKey);
    if (it == m_byChapter.constEnd())
        return std::nullopt;
    auto pIt = m_byPath.constFind(it.value());
    if (pIt == m_byPath.constEnd())
        return std::nullopt;
    return pIt.value().canonicalPath;
}

bool MangaDownloadIndex::hasAnyForSeries(const QString& sourceId, const QString& seriesId) const
{
    QMutexLocker lock(&m_mutex);
    return m_seriesHasAny.contains(computeSeriesKey(sourceId, seriesId));
}

QList<MangaDownloadIndex::Entry> MangaDownloadIndex::entriesForSeries(
    const QString& sourceId, const QString& seriesId) const
{
    QList<Entry> out;
    QMutexLocker lock(&m_mutex);
    for (const Entry& e : m_byPath) {
        if (e.sourceId == sourceId && e.seriesId == seriesId)
            out.append(e);
    }
    return out;
}

std::optional<MangaDownloadIndex::Entry> MangaDownloadIndex::entryForSeriesAndVolume(
    const QString& sourceId, const QString& seriesId, int volumeNumber) const
{
    if (sourceId.isEmpty() || seriesId.isEmpty() || volumeNumber <= 0) {
        return std::nullopt;
    }
    QMutexLocker lock(&m_mutex);
    for (const Entry& e : m_byPath) {
        if (e.sourceId == sourceId &&
            e.seriesId == seriesId &&
            e.volumeNumber == volumeNumber) {
            return e;
        }
    }
    return std::nullopt;
}

QList<MangaDownloadIndex::Entry> MangaDownloadIndex::entriesForAllSeries() const
{
    // TANKOYOMI_VOLUME_PIVOT Phase 10 (2026-05-16) -- one representative
    // Entry per (sourceId, seriesId) bucket. Walks m_seriesHasAny so the
    // set of returned series-keys exactly matches the series-has-any
    // predicate (no risk of stale m_byPath rows leaking through), then
    // finds the first m_byPath entry whose sourceId+seriesId matches.
    // QHash iteration order is unspecified, but the contract here is
    // "one tile per series" -- which row wins is unobservable to the
    // caller (the tile renders title from the catalog/cache lookup, not
    // from the entry directly).
    QList<Entry> out;
    QMutexLocker lock(&m_mutex);
    QSet<QString> seen;
    for (const QString& seriesKey : m_seriesHasAny) {
        const int sep = seriesKey.indexOf(QLatin1Char(':'));
        if (sep <= 0) continue;
        const QString sourceId = seriesKey.left(sep);
        const QString seriesId = seriesKey.mid(sep + 1);
        if (seen.contains(seriesKey)) continue;
        for (const Entry& e : m_byPath) {
            if (e.sourceId == sourceId && e.seriesId == seriesId) {
                out.append(e);
                seen.insert(seriesKey);
                break;
            }
        }
    }
    return out;
}
