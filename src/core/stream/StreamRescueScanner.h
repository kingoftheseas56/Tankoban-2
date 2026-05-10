#pragma once

// STREAM_DOWNLOADED_LIBRARY 2026-05-10 — first-launch migration scanner.
// Walks Videos roots, regex-matches the canonical bulk-download layout,
// queries Cinemeta to resolve each show, registers per-episode entries
// in StreamDownloadIndex, and materializes StreamLibrary entries for
// matched shows. One-shot per migrationVersion; gated by
// <dataDir>/stream_downloads_meta.json.
//
// Spec: docs/superpowers/specs/2026-05-10-stream-downloaded-library-design.md §9.

#include <QObject>
#include <QString>
#include <QStringList>
#include <atomic>

class JsonStore;
class StreamDownloadIndex;
class StreamLibrary;
namespace tankostream::stream { class MetaAggregator; }

class StreamRescueScanner : public QObject
{
    Q_OBJECT

public:
    struct Stats {
        int showsScanned = 0;
        int showsMatched = 0;
        int showsAmbiguous = 0;
        int showsUnmatched = 0;
        int showsNetworkFailure = 0;
        int episodesRegistered = 0;
    };

    StreamRescueScanner(StreamDownloadIndex* index,
                        StreamLibrary* library,
                        tankostream::stream::MetaAggregator* meta,
                        JsonStore* metaStore,
                        const QStringList& videoRoots,
                        QObject* parent = nullptr);

    void start();   // off-thread; emits progressUpdate + complete
    void cancel();  // sets a cancellation flag; in-flight scan exits early

signals:
    void progressUpdate(int currentShowIndex, int totalShows, const QString& currentShowName);
    void complete(const StreamRescueScanner::Stats& stats);

private:
    StreamDownloadIndex* m_index;
    StreamLibrary* m_library;
    tankostream::stream::MetaAggregator* m_meta;
    JsonStore* m_metaStore;
    QStringList m_videoRoots;

    // Atomic so the GUI-thread cancel() and the worker-thread loop see a
    // coherent value without locks. The plan's bool was unsafe here.
    std::atomic<bool> m_cancelled{false};
};
