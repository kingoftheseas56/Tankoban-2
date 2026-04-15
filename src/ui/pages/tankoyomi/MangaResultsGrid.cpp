#include "MangaResultsGrid.h"

#include "ui/pages/TileCard.h"
#include "ui/pages/TileStrip.h"

#include <QVBoxLayout>
#include <QScrollArea>
#include <QFrame>
#include <QLabel>
#include <QGraphicsOpacityEffect>

static constexpr const char* LIBRARY_BADGE_NAME = "MangaGridInLibraryBadge";

MangaResultsGrid::MangaResultsGrid(QWidget* parent)
    : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_scroll = new QScrollArea(this);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_strip = new TileStrip;
    m_strip->setMode("grid");
    m_scroll->setWidget(m_strip);

    root->addWidget(m_scroll);

    connect(m_strip, &TileStrip::tileDoubleClicked, this,
            [this](TileCard* card) {
                const int row = m_tiles.indexOf(card);
                if (row >= 0) emit resultActivated(row);
            });
    connect(m_strip, &TileStrip::tileRightClicked, this,
            [this](TileCard* card, const QPoint& globalPos) {
                const int row = m_tiles.indexOf(card);
                if (row >= 0) emit resultRightClicked(row, globalPos);
            });
}

void MangaResultsGrid::clearResults()
{
    m_strip->clear();
    m_tiles.clear();
    m_results.clear();
}

void MangaResultsGrid::setResults(const QList<MangaResult>& results)
{
    clearResults();
    m_results = results;

    for (const auto& r : results) {
        // Subtitle preference: author if we have it, otherwise the source name.
        // Gives every tile at least one line of secondary context.
        // Q2: when author is blank, show the source's display name
        // ("WeebCentral") rather than the raw scraper key ("weebcentral").
        QString subtitle = r.author.isEmpty() ? mangaSourceDisplayName(r.source)
                                              : r.author;

        // Thumb is loaded later via onCoverReady — initial card is placeholder.
        auto* card = new TileCard(QString(), r.title, subtitle);

        card->setProperty("mangaSource", r.source);
        card->setProperty("mangaId",     r.id);

        m_strip->addTile(card);
        m_tiles.append(card);
    }

    // E1: re-apply the in-library overlays on the freshly-built tile set.
    applyInLibraryOverlays();
}

void MangaResultsGrid::onCoverReady(const QString& source, const QString& id,
                                     const QString& path)
{
    for (auto* card : m_tiles) {
        if (card->property("mangaSource").toString() == source &&
            card->property("mangaId").toString()     == id) {
            card->setThumbPath(path);
            return;
        }
    }
}

void MangaResultsGrid::setInLibraryKeys(const QSet<QString>& keys)
{
    if (keys == m_inLibraryKeys) return;
    m_inLibraryKeys = keys;
    applyInLibraryOverlays();
}

void MangaResultsGrid::applyInLibraryOverlays()
{
    // E1: stamp each tile whose (source,id) is in m_inLibraryKeys with a
    // top-right "IN LIBRARY" pill and a dim opacity. Tiles not in the set have
    // any prior overlay removed and opacity reset. Safe to call repeatedly.
    for (int i = 0; i < m_tiles.size() && i < m_results.size(); ++i) {
        auto* card = m_tiles[i];
        const auto& r = m_results[i];
        const QString key = r.source + QStringLiteral("_") + r.id;
        const bool inLibrary = m_inLibraryKeys.contains(key);

        QLabel* existing = card->findChild<QLabel*>(LIBRARY_BADGE_NAME,
                                                    Qt::FindDirectChildrenOnly);
        if (inLibrary) {
            if (!existing) {
                auto* badge = new QLabel("IN LIBRARY", card);
                badge->setObjectName(LIBRARY_BADGE_NAME);
                badge->setAttribute(Qt::WA_TransparentForMouseEvents);
                badge->setStyleSheet(
                    "QLabel { background: rgba(0,0,0,0.72); color: #eee; "
                    "  font-size: 9px; font-weight: 600; letter-spacing: 0.5px; "
                    "  padding: 3px 7px; border-radius: 10px; }");
                badge->adjustSize();
                badge->move(card->width() - badge->width() - 8, 8);
                badge->show();
                badge->raise();
            }
            if (!card->graphicsEffect()) {
                auto* effect = new QGraphicsOpacityEffect(card);
                effect->setOpacity(0.55);
                card->setGraphicsEffect(effect);
            }
        } else {
            if (existing) existing->deleteLater();
            card->setGraphicsEffect(nullptr);
        }
    }
}
