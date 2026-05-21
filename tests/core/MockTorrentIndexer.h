#pragma once

#include "core/TorrentIndexer.h"
#include "core/TorrentResult.h"

#include <QString>
#include <QDateTime>

// Test-only fake. Each instance has a fixed `id` + `displayName` and exposes
// triggerFinished()/triggerError() helpers that tests call to drive the
// dispatch state machine deterministically. Health/credentials are no-ops.
class MockTorrentIndexer : public TorrentIndexer
{
    Q_OBJECT
public:
    explicit MockTorrentIndexer(const QString& id, QObject* parent = nullptr)
        : TorrentIndexer(parent), m_id(id) {}

    QString id() const override { return m_id; }
    QString displayName() const override { return m_id; }
    void search(const QString& /*query*/, int /*limit*/, const QString& /*categoryId*/) override
    {
        m_searched = true;
    }

    IndexerHealth health() const override         { return IndexerHealth::Ok; }
    QDateTime     lastSuccess() const override    { return QDateTime::currentDateTime(); }
    QString       lastError() const override      { return {}; }
    qint64        lastResponseMs() const override { return 0; }

    bool wasSearched() const { return m_searched; }

    // Test driver helpers — fire the base-class signals on demand.
    void triggerFinished(const QList<TorrentResult>& results) { emit searchFinished(results); }
    void triggerError(const QString& error) { emit searchError(error); }

private:
    QString m_id;
    bool    m_searched = false;
};
