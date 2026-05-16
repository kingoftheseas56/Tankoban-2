// src/core/manga/NyaaRuntimeSource.h
#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

namespace tankoban::manga {

// One nyaa search-result candidate. The Sources panel renders one row
// per NyaaSourceCandidate.
struct NyaaSourceCandidate {
    QString  title;        // full nyaa title string
    QString  uploader;
    QString  magnetUri;
    QString  infoHash;     // 40-char lowercase hex
    qint64   sizeBytes = 0;
    int      seeders   = 0;
    int      leechers  = 0;
    int      tier      = 99; // 1 / 2 / 99 (untrusted)
};

// Runtime nyaa.si query with uploader-trust filter. Loads the trust JSON
// once at construction and uses it to tag + rank results.
class NyaaRuntimeSource : public QObject
{
    Q_OBJECT
public:
    explicit NyaaRuntimeSource(QNetworkAccessManager* nam,
                                const QString& trustJsonPath,
                                QObject* parent = nullptr);
    ~NyaaRuntimeSource() override;

    // Fire a search. Result lands on searchSucceeded with the same
    // requestId. Query shape: `series title + "v"+volNumber + uploader-trust-OR`.
    // E.g.: 'One Piece v50 (1r0n | Hox | "VIZ Digital")'.
    void search(const QString& seriesTitle, int volumeNumber, int requestId);

signals:
    void searchSucceeded(int requestId, const QList<tankoban::manga::NyaaSourceCandidate>& results);
    void searchFailed(int requestId, const QString& reason);

private slots:
    void onReplyFinished();

private:
    void loadTrustJson(const QString& path);
    int  tierForUploader(const QString& uploader) const;

    QPointer<QNetworkAccessManager> m_nam;
    QSet<QString> m_tier1;
    QSet<QString> m_tier2;
    QSet<QString> m_blocked;
};

} // namespace tankoban::manga
