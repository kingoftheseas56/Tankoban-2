#pragma once

#include "VideoCategory.h"

#include <QList>
#include <QJsonObject>
#include <QMap>
#include <QString>

class JsonStore;
struct ShowInfo;

class VideoCategoryStore {
public:
    explicit VideoCategoryStore(JsonStore& store);

    QMap<QString, VideoCategory> assignments() const;
    VideoCategory categoryFor(const QString& showId) const;
    void setCategory(const QString& showId, VideoCategory category);
    void setCategories(const QStringList& showIds, VideoCategory category);
    void ensureAssignments(const QList<ShowInfo>& shows);

    static QString itemIdForShow(const ShowInfo& show);

private:
    QJsonObject readState() const;
    QJsonObject readAssignmentObject() const;
    void writeAssignmentObject(QJsonObject assignments);

    JsonStore& m_store;
};
