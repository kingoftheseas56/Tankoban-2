#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QMetaType>

struct BookSeriesInfo {
    QString seriesName;
    QString seriesPath;
    QString coverThumbPath;
    int fileCount = 0;
};
Q_DECLARE_METATYPE(BookSeriesInfo)

struct AudiobookInfo {
    QString name;
    QString path;
    int trackCount = 0;
};
Q_DECLARE_METATYPE(AudiobookInfo)

class BooksScanner : public QObject {
    Q_OBJECT
public:
    explicit BooksScanner(const QString& thumbsDir, QObject* parent = nullptr);

public slots:
    void scan(const QStringList& bookRoots, const QStringList& audiobookRoots);

signals:
    void bookSeriesFound(const BookSeriesInfo& series);
    void audiobookFound(const AudiobookInfo& audiobook);
    void scanFinished(const QList<BookSeriesInfo>& allBooks,
                      const QList<AudiobookInfo>& allAudiobooks);

private:
    QString m_thumbsDir;

    static const QStringList BOOK_EXTS;
    static const QStringList AUDIO_EXTS;
};
