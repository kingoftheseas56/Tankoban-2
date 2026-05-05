#include "VideoCategoryStore.h"

#include "VideoClassifier.h"
#include "core/JsonStore.h"
#include "core/VideosScanner.h"

#include <QJsonObject>

namespace {
constexpr const char* kVideoStateFile = "video_state.json";
constexpr const char* kCategoryAssignmentsKey = "videos.categoryAssignments";
}

VideoCategoryStore::VideoCategoryStore(JsonStore& store)
    : m_store(store)
{
}

QString VideoCategoryStore::itemIdForShow(const ShowInfo& show)
{
    return show.showPath;
}

QJsonObject VideoCategoryStore::readState() const
{
    return m_store.read(QString::fromLatin1(kVideoStateFile));
}

QJsonObject VideoCategoryStore::readAssignmentObject() const
{
    return readState().value(QString::fromLatin1(kCategoryAssignmentsKey)).toObject();
}

void VideoCategoryStore::writeAssignmentObject(QJsonObject assignments)
{
    QJsonObject state = readState();
    state[QString::fromLatin1(kCategoryAssignmentsKey)] = assignments;
    m_store.write(QString::fromLatin1(kVideoStateFile), state);
}

QMap<QString, VideoCategory> VideoCategoryStore::assignments() const
{
    QMap<QString, VideoCategory> out;
    const QJsonObject obj = readAssignmentObject();
    for (auto it = obj.begin(); it != obj.end(); ++it)
        out.insert(it.key(), videoCategoryFromKey(it.value().toString()));
    return out;
}

VideoCategory VideoCategoryStore::categoryFor(const QString& showId) const
{
    if (showId.isEmpty())
        return VideoCategory::Miscellaneous;
    const QJsonObject obj = readAssignmentObject();
    return videoCategoryFromKey(obj.value(showId).toString(),
                                VideoCategory::Miscellaneous);
}

void VideoCategoryStore::setCategory(const QString& showId, VideoCategory category)
{
    if (showId.isEmpty())
        return;
    QJsonObject obj = readAssignmentObject();
    obj[showId] = videoCategoryKey(category);
    writeAssignmentObject(obj);
}

void VideoCategoryStore::setCategories(const QStringList& showIds, VideoCategory category)
{
    QJsonObject obj = readAssignmentObject();
    bool changed = false;
    for (const QString& showId : showIds) {
        if (showId.isEmpty())
            continue;
        obj[showId] = videoCategoryKey(category);
        changed = true;
    }
    if (changed)
        writeAssignmentObject(obj);
}

void VideoCategoryStore::ensureAssignments(const QList<ShowInfo>& shows)
{
    QJsonObject state = readState();
    const QString key = QString::fromLatin1(kCategoryAssignmentsKey);
    const bool firstRun = !state.contains(key) || !state.value(key).isObject();
    QJsonObject obj = firstRun ? QJsonObject{} : state.value(key).toObject();
    VideoClassifier classifier;
    bool changed = firstRun;

    for (const ShowInfo& show : shows) {
        const QString showId = itemIdForShow(show);
        if (showId.isEmpty())
            continue;
        if (firstRun) {
            obj[showId] = videoCategoryKey(VideoCategory::Miscellaneous);
            continue;
        }
        if (!obj.contains(showId)) {
            obj[showId] = videoCategoryKey(classifier.classify(show));
            changed = true;
        }
    }

    if (changed)
        writeAssignmentObject(obj);
}
