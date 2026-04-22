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

struct AudiobookInfo {
    QString name;
    QString path;
    int trackCount = 0;
    QString coverPath;
    QStringList tracks;          // absolute paths, natural-sorted
    qint64 totalDurationMs = 0;  // Phase 1.3 — populated via AudiobookMetaCache
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
    // Walks `dir` and its subtree (bounded by maxDepth), emitting one
    // AudiobookInfo per folder that directly contains audio files. Dedupes
    // via seenPaths so the same folder reached from multiple roots only
    // registers once.
    void walkAudiobooks(const QDir& dir,
                        const QCollator& collator,
                        QList<AudiobookInfo>& out,
                        QSet<QString>& seenPaths,
                        int maxDepth);

    QString m_thumbsDir;

    static const QStringList BOOK_EXTS;
    static const QStringList AUDIO_EXTS;
};
