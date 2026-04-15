#pragma once

#include "core/TorrentIndexer.h"

#include <QDialog>
#include <QList>

class QNetworkAccessManager;
class QTableWidget;

class IndexerStatusPanel : public QDialog
{
    Q_OBJECT

public:
    explicit IndexerStatusPanel(QNetworkAccessManager* nam, QWidget* parent = nullptr);
    ~IndexerStatusPanel() override;

signals:
    // Fires when the user toggles an enabled checkbox or saves a credential.
    // TankorentPage ignores during active search; reapplies on next startSearch.
    void configurationChanged();

public slots:
    void refresh();

private slots:
    void onConfigureCredentials(TorrentIndexer* indexer);
    void onEnabledToggled(const QString& indexerId, bool enabled);

private:
    void buildUI();
    void populateTable();

    static QString healthLabel(IndexerHealth h);
    static QString formatRelativeTime(const QDateTime& dt);
    static QString formatResponseMs(qint64 ms);

    QNetworkAccessManager* m_nam = nullptr;
    QList<TorrentIndexer*> m_sentinels;
    QTableWidget*          m_table = nullptr;
};
