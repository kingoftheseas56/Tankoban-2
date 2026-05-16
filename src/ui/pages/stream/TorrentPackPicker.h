#pragma once

// TANKORENT_STREAM_INTEGRATION 2026-05-15 - modal picker shown from inside
// StreamDetailView when the user clicks "Download via Tankorent" on a season
// header. Fans out indexer searches with the show's identity already in
// context (imdbId, showName, season).

#include "core/TorrentResult.h"

#include <QDialog>
#include <QList>
#include <QSet>
#include <QString>

class QLabel;
class QListWidget;
class QNetworkAccessManager;
class QPushButton;

class TorrentPackPicker : public QDialog
{
    Q_OBJECT

public:
    TorrentPackPicker(const QString& imdbId,
                      const QString& showName,
                      int season,
                      QWidget* parent = nullptr);

signals:
    void packChosen(const TorrentResult& chosen,
                    const QString& imdbId,
                    int season);

private slots:
    void onIndexerResults(const QList<TorrentResult>& results);
    void onRowDoubleClicked();

private:
    struct EnrichedResult {
        TorrentResult raw;
        int detectedEpisodeCount = 0;
        QSet<int> detectedSeasons;
        double combinedScore = 0.0;
        bool completeSeries = false;

        bool isMultiSeason() const { return completeSeries || detectedSeasons.size() > 1; }
    };

    void buildUI();
    void launchSearches();
    void rerankAndRender();

    QString m_imdbId;
    QString m_showName;
    int     m_season = 0;

    QNetworkAccessManager* m_nam = nullptr;
    QList<TorrentResult>   m_allResults;
    QList<EnrichedResult>  m_enriched;
    QListWidget*           m_list = nullptr;
    QLabel*                m_status = nullptr;
    QPushButton*           m_downloadBtn = nullptr;
};
