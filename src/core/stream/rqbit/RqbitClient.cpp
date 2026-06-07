#include "core/stream/rqbit/RqbitClient.h"
#include <QFileInfo>
#include <QStringList>

namespace tankostream::rqbit {

RqbitStats RqbitClient::parseStats(const QJsonObject& j) {
    RqbitStats s;
    s.state           = j.value(QStringLiteral("state")).toString();
    s.totalBytes      = j.value(QStringLiteral("total_bytes")).toVariant().toLongLong();
    s.downloadedBytes = j.value(QStringLiteral("progress_bytes")).toVariant().toLongLong();
    s.finished        = j.value(QStringLiteral("finished")).toBool();
    return s;
}

int RqbitClient::pickPrimaryVideoFile(const QJsonArray& files) {
    static const QStringList kVideoExt = {
        QStringLiteral("mkv"), QStringLiteral("mp4"), QStringLiteral("avi"),
        QStringLiteral("mov"), QStringLiteral("m4v"), QStringLiteral("webm"),
        QStringLiteral("ts"),  QStringLiteral("flv")
    };
    int best = -1; qint64 bestLen = -1;
    for (int i = 0; i < files.size(); ++i) {
        const QJsonObject f = files.at(i).toObject();
        const QString name = f.value(QStringLiteral("name")).toString();
        const QString ext  = QFileInfo(name).suffix().toLower();
        if (!kVideoExt.contains(ext)) continue;
        const qint64 len = f.value(QStringLiteral("length")).toVariant().toLongLong();
        if (len > bestLen) { bestLen = len; best = i; }
    }
    return best;
}

} // namespace tankostream::rqbit
