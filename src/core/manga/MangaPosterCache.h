#pragma once

#include "MangaResult.h"

#include <QString>
#include <functional>

class QObject;
class QNetworkAccessManager;

namespace MangaPosterCache {

QString cachePath(const QString& sourceId, const QString& seriesId);
QString existingPath(const QString& sourceId, const QString& seriesId);

void download(const MangaResult& preview,
              const QString& imageUrl,
              QNetworkAccessManager* nam,
              QObject* context,
              std::function<void(const QString&)> onReady);

} // namespace MangaPosterCache
