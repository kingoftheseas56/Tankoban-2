#pragma once

#include <QWidget>
#include <QList>
#include <QSet>

#include "core/manga/MangaResult.h"

class TileStrip;
class TileCard;
class QScrollArea;

// B2: Tankoyomi search-results grid. Thin wrapper around the shared TileStrip
// infrastructure Agent 5 built — same tile spec as the library pages, with
// manga-specific content (cover via TankoyomiPage::coverReady, author/source
// as subtitle). No wiring into TankoyomiPage yet; consumers attach in B3.
class MangaResultsGrid : public QWidget
{
    Q_OBJECT

public:
    explicit MangaResultsGrid(QWidget* parent = nullptr);

    void setResults(const QList<MangaResult>& results);
    void clearResults();

public slots:
    // Wire this to TankoyomiPage::coverReady(source, id, path).
    void onCoverReady(const QString& source, const QString& id, const QString& path);

    // E1: mark which (source,id) pairs are already downloaded / in the local
    // library. Keys are `{source}_{id}` strings. Re-applied whenever the set
    // changes; safe to call with the same set repeatedly.
    void setInLibraryKeys(const QSet<QString>& keys);

signals:
    void resultActivated(int row);                         // double-click
    void resultRightClicked(int row, const QPoint& globalPos);

private:
    void applyInLibraryOverlays();

    QList<MangaResult> m_results;
    QList<TileCard*>   m_tiles;
    TileStrip*         m_strip  = nullptr;
    QScrollArea*       m_scroll = nullptr;
    QSet<QString>      m_inLibraryKeys;
};
