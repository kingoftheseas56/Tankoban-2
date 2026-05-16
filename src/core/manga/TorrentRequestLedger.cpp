// src/core/manga/TorrentRequestLedger.cpp
#include "TorrentRequestLedger.h"
#include "../DebugLogBuffer.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSaveFile>
#include <QDateTime>

namespace tankoban::manga::premium {

namespace {

constexpr const char* kLogSource = "TorrentRequestLedger";

QString statusToString(TorrentRequest::Status s)
{
    switch (s) {
        case TorrentRequest::Status::Pending:           return QStringLiteral("pending");
        case TorrentRequest::Status::AwaitingMetadata:  return QStringLiteral("awaiting_metadata");
        case TorrentRequest::Status::Downloading:       return QStringLiteral("downloading");
        case TorrentRequest::Status::Validating:        return QStringLiteral("validating");
        case TorrentRequest::Status::Completed:         return QStringLiteral("completed");
        case TorrentRequest::Status::Failed:            return QStringLiteral("failed");
        case TorrentRequest::Status::Cancelled:         return QStringLiteral("cancelled");
        case TorrentRequest::Status::CatalogMissing:    return QStringLiteral("catalog_missing");
    }
    return QStringLiteral("pending");
}

TorrentRequest::Status statusFromString(const QString& s)
{
    if (s == QStringLiteral("awaiting_metadata")) return TorrentRequest::Status::AwaitingMetadata;
    if (s == QStringLiteral("downloading"))       return TorrentRequest::Status::Downloading;
    if (s == QStringLiteral("validating"))        return TorrentRequest::Status::Validating;
    if (s == QStringLiteral("completed"))         return TorrentRequest::Status::Completed;
    if (s == QStringLiteral("failed"))            return TorrentRequest::Status::Failed;
    if (s == QStringLiteral("cancelled"))         return TorrentRequest::Status::Cancelled;
    if (s == QStringLiteral("catalog_missing"))   return TorrentRequest::Status::CatalogMissing;
    return TorrentRequest::Status::Pending;
}

QJsonObject toJson(const TorrentRequest& r)
{
    QJsonObject o;
    o[QStringLiteral("catalogId")]                = r.catalogId;
    o[QStringLiteral("seriesId")]                 = r.seriesId;
    o[QStringLiteral("volumeNumber")]             = r.volumeNumber;
    o[QStringLiteral("expectedInfoHash")]         = r.expectedInfoHash;
    o[QStringLiteral("magnetUri")]                = r.magnetUri;
    o[QStringLiteral("fileIndex")]                = r.fileIndex;
    o[QStringLiteral("cbzFileName")]              = r.cbzFileName;
    o[QStringLiteral("fileSizeBytes")]            = static_cast<double>(r.fileSizeBytes);
    o[QStringLiteral("pieceStart")]               = r.pieceStart;
    o[QStringLiteral("pieceEnd")]                 = r.pieceEnd;
    o[QStringLiteral("stagingPath")]              = r.stagingPath;
    o[QStringLiteral("canonicalDestinationPath")] = r.canonicalDestinationPath;
    o[QStringLiteral("status")]                   = statusToString(r.status);
    o[QStringLiteral("createdAtMsEpoch")]         = static_cast<double>(r.createdAtMsEpoch);
    o[QStringLiteral("updatedAtMsEpoch")]         = static_cast<double>(r.updatedAtMsEpoch);
    o[QStringLiteral("lastErrorCode")]            = r.lastErrorCode;
    o[QStringLiteral("lastErrorMessage")]         = r.lastErrorMessage;
    return o;
}

TorrentRequest fromJson(const QJsonObject& o)
{
    TorrentRequest r;
    r.catalogId                = o.value(QStringLiteral("catalogId")).toString();
    r.seriesId                 = o.value(QStringLiteral("seriesId")).toString();
    r.volumeNumber             = o.value(QStringLiteral("volumeNumber")).toInt();
    r.expectedInfoHash         = o.value(QStringLiteral("expectedInfoHash")).toString().toLower();
    r.magnetUri                = o.value(QStringLiteral("magnetUri")).toString();
    r.fileIndex                = o.value(QStringLiteral("fileIndex")).toInt(-1);
    r.cbzFileName              = o.value(QStringLiteral("cbzFileName")).toString();
    r.fileSizeBytes            = static_cast<qint64>(o.value(QStringLiteral("fileSizeBytes")).toDouble());
    r.pieceStart               = o.value(QStringLiteral("pieceStart")).toInt(-1);
    r.pieceEnd                 = o.value(QStringLiteral("pieceEnd")).toInt(-1);
    r.stagingPath              = o.value(QStringLiteral("stagingPath")).toString();
    r.canonicalDestinationPath = o.value(QStringLiteral("canonicalDestinationPath")).toString();
    r.status                   = statusFromString(o.value(QStringLiteral("status")).toString());
    r.createdAtMsEpoch         = static_cast<qint64>(o.value(QStringLiteral("createdAtMsEpoch")).toDouble());
    r.updatedAtMsEpoch         = static_cast<qint64>(o.value(QStringLiteral("updatedAtMsEpoch")).toDouble());
    r.lastErrorCode            = o.value(QStringLiteral("lastErrorCode")).toString();
    r.lastErrorMessage         = o.value(QStringLiteral("lastErrorMessage")).toString();
    return r;
}

} // anonymous namespace

TorrentRequestLedger::TorrentRequestLedger(const QString& filePath, QObject* parent)
    : QObject(parent), m_filePath(filePath)
{
    load();
}

TorrentRequestLedger::~TorrentRequestLedger() = default;

void TorrentRequestLedger::load()
{
    QMutexLocker locker(&m_mutex);
    m_byKey.clear();

    QFile f(m_filePath);
    if (!f.exists()) {
        QJsonObject details;
        details[QStringLiteral("path")] = m_filePath;
        DebugLogBuffer::instance().info(QString::fromLatin1(kLogSource),
            QStringLiteral("no ledger file - starting empty"), details);
        return;
    }
    if (!f.open(QIODevice::ReadOnly)) {
        QJsonObject details;
        details[QStringLiteral("path")] = m_filePath;
        details[QStringLiteral("error")] = f.errorString();
        DebugLogBuffer::instance().warning(QString::fromLatin1(kLogSource),
            QStringLiteral("could not open ledger file"), details);
        return;
    }

    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        QJsonObject details;
        details[QStringLiteral("error")] = perr.errorString();
        DebugLogBuffer::instance().warning(QString::fromLatin1(kLogSource),
            QStringLiteral("parse error"), details);
        return;
    }
    const QJsonObject root = doc.object();
    const QJsonArray arr = root.value(QStringLiteral("requests")).toArray();
    for (const auto& v : arr) {
        const TorrentRequest r = fromJson(v.toObject());
        if (r.seriesId.isEmpty() || r.volumeNumber <= 0) continue;
        m_byKey.insert(requestKey(r.catalogId, r.seriesId, r.volumeNumber), r);
    }
    QJsonObject details;
    details[QStringLiteral("count")] = m_byKey.size();
    details[QStringLiteral("path")] = m_filePath;
    DebugLogBuffer::instance().info(QString::fromLatin1(kLogSource),
        QStringLiteral("loaded requests from ledger"), details);
}

void TorrentRequestLedger::saveLocked()
{
    QJsonArray arr;
    for (const auto& r : m_byKey) arr.append(toJson(r));
    QJsonObject root;
    root[QStringLiteral("schemaVersion")] = 1;
    root[QStringLiteral("requests")]      = arr;

    QFileInfo fi(m_filePath);
    QDir().mkpath(fi.absolutePath());

    QSaveFile f(m_filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QJsonObject details;
        details[QStringLiteral("error")] = f.errorString();
        DebugLogBuffer::instance().warning(QString::fromLatin1(kLogSource),
            QStringLiteral("save open failed"), details);
        return;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        QJsonObject details;
        details[QStringLiteral("error")] = f.errorString();
        DebugLogBuffer::instance().warning(QString::fromLatin1(kLogSource),
            QStringLiteral("save commit failed"), details);
    }
}

void TorrentRequestLedger::save()
{
    QMutexLocker locker(&m_mutex);
    saveLocked();
    locker.unlock();
    emit ledgerChanged();
}

void TorrentRequestLedger::upsert(const TorrentRequest& req)
{
    {
        QMutexLocker locker(&m_mutex);
        TorrentRequest copy = req;
        copy.updatedAtMsEpoch = QDateTime::currentMSecsSinceEpoch();
        if (copy.createdAtMsEpoch == 0) copy.createdAtMsEpoch = copy.updatedAtMsEpoch;
        m_byKey.insert(requestKey(copy.catalogId, copy.seriesId, copy.volumeNumber), copy);
        saveLocked();
    }
    emit ledgerChanged();
}

void TorrentRequestLedger::updateStatus(const QString& key,
                                        TorrentRequest::Status newStatus,
                                        const QString& errorCode,
                                        const QString& errorMessage)
{
    {
        QMutexLocker locker(&m_mutex);
        const auto it = m_byKey.find(key);
        if (it == m_byKey.end()) return;
        it->status            = newStatus;
        it->updatedAtMsEpoch  = QDateTime::currentMSecsSinceEpoch();
        if (!errorCode.isEmpty())    it->lastErrorCode    = errorCode;
        if (!errorMessage.isEmpty()) it->lastErrorMessage = errorMessage;
        saveLocked();
    }
    emit ledgerChanged();
}

void TorrentRequestLedger::remove(const QString& key)
{
    {
        QMutexLocker locker(&m_mutex);
        if (!m_byKey.remove(key)) return;
        saveLocked();
    }
    emit ledgerChanged();
}

std::optional<TorrentRequest> TorrentRequestLedger::find(const QString& key) const
{
    QMutexLocker locker(&m_mutex);
    const auto it = m_byKey.constFind(key);
    if (it == m_byKey.constEnd()) return std::nullopt;
    return it.value();
}

QList<TorrentRequest> TorrentRequestLedger::all() const
{
    QMutexLocker locker(&m_mutex);
    return m_byKey.values();
}

QList<TorrentRequest> TorrentRequestLedger::findByInfoHash(const QString& infoHash) const
{
    QMutexLocker locker(&m_mutex);
    QList<TorrentRequest> out;
    for (const auto& r : m_byKey) {
        if (r.expectedInfoHash == infoHash.toLower()) out.append(r);
    }
    return out;
}

QList<TorrentRequest> TorrentRequestLedger::findByStatus(TorrentRequest::Status status) const
{
    QMutexLocker locker(&m_mutex);
    QList<TorrentRequest> out;
    for (const auto& r : m_byKey) {
        if (r.status == status) out.append(r);
    }
    return out;
}

} // namespace tankoban::manga::premium
