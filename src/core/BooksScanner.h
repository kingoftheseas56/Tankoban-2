#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QMetaType>
#include <QSet>
#include <QCollator>
#include <QDir>

struct BookSeriesInfo {
    QString seriesName;
    QString seriesPath;
    QString coverThumbPath;
    int fileCount = 0;
    qint64 newestMtimeMs = 0;
};
Q_DECLARE_METATYPE(BookSeriesInfo)

class BooksScanner : public QObject {
    Q_OBJECT
public:
    explicit BooksScanner(const QString& thumbsDir, QObject* parent = nullptr);

public slots:
    void scan(const QStringList& bookRoots);

signals:
    void bookSeriesFound(const BookSeriesInfo& series);
    void scanFinished(const QList<BookSeriesInfo>& allBooks);

private:
    QString m_thumbsDir;

    static const QStringList BOOK_EXTS;
};
