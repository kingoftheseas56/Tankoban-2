#include "VideosScanner.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QCollator>
#include <algorithm>

const QStringList VideosScanner::VIDEO_EXTS = {
    "*.mp4", "*.mkv", "*.avi", "*.webm", "*.mov", "*.wmv", "*.flv",
    "*.m4v", "*.ts", "*.mpg", "*.mpeg", "*.ogv"
};

VideosScanner::VideosScanner(QObject* parent)
    : QObject(parent)
{
}

void VideosScanner::scan(const QStringList& rootFolders)
{
    QMap<QString, QStringList> showMap;

    for (const auto& root : rootFolders) {
        QDirIterator it(root, VIDEO_EXTS, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QString path = it.next();
            QString parentDir = QFileInfo(path).absolutePath();
            showMap[parentDir].append(path);
        }
    }

    QCollator collator;
    collator.setNumericMode(true);

    QList<ShowInfo> allShows;

    for (auto it = showMap.begin(); it != showMap.end(); ++it) {
        const QString& showPath = it.key();
        QStringList& files = it.value();

        std::sort(files.begin(), files.end(), [&collator](const QString& a, const QString& b) {
            return collator.compare(QFileInfo(a).fileName(), QFileInfo(b).fileName()) < 0;
        });

        ShowInfo info;
        info.showName = QDir(showPath).dirName();
        info.showPath = showPath;
        info.episodeCount = files.size();

        qint64 totalSize = 0;
        for (const auto& f : files)
            totalSize += QFileInfo(f).size();
        info.totalSizeBytes = totalSize;

        allShows.append(info);
        emit showFound(info);
    }

    // Sort shows by name
    std::sort(allShows.begin(), allShows.end(), [&collator](const ShowInfo& a, const ShowInfo& b) {
        return collator.compare(a.showName, b.showName) < 0;
    });

    emit scanFinished(allShows);
}
