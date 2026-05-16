#include "SidecarMeta.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace sidecar {

static QString sidecarPath(const QString& folder)
{
    return QDir(folder).filePath(kFileName);
}

std::optional<SidecarMeta> read(const QString& seriesFolder)
{
    QFile f(sidecarPath(seriesFolder));
    if (!f.open(QIODevice::ReadOnly)) return std::nullopt;
    const auto doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return std::nullopt;
    const auto o = doc.object();
    SidecarMeta m;
    m.sourceId      = o.value("sourceId").toString();
    m.seriesId      = o.value("seriesId").toString();
    m.title         = o.value("title").toString();
    // Phase 1 polish lesson applied: native qint64 + .toInteger(0) for
    // ms-epoch timestamps rather than QString::number round-trip.
    m.createdAt     = o.value("createdAt").toInteger(0);
    m.schemaVersion = o.value("schemaVersion").toInt(1);
    if (m.sourceId.isEmpty() || m.seriesId.isEmpty()) return std::nullopt;
    return m;
}

bool write(const QString& seriesFolder, const SidecarMeta& meta)
{
    QDir().mkpath(seriesFolder);
    QSaveFile f(sidecarPath(seriesFolder));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    QJsonObject o;
    o["sourceId"]      = meta.sourceId;
    o["seriesId"]      = meta.seriesId;
    o["title"]         = meta.title;
    o["createdAt"]     = static_cast<qint64>(meta.createdAt);
    o["schemaVersion"] = meta.schemaVersion;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
    return f.commit();
}

bool exists(const QString& seriesFolder)
{
    return QFileInfo::exists(sidecarPath(seriesFolder));
}

} // namespace sidecar
