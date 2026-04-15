#pragma once

#include <QList>
#include <QString>
#include <QWidget>

#include "core/stream/CalendarEngine.h"

class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace tankostream::stream {

// Batch 6.2 (Tankostream Phase 6) — Calendar screen.
//
// Sixth layer in StreamPage's stack. Consumes CalendarEngine output (flat or
// pre-grouped) and renders a three-level tree: bucket (This Week / Next Week
// / Later) → day → episode rows with series name + SxxExx + episode title.
// Double-click on an episode row emits `seriesEpisodeActivated(imdbId,
// season, episode)`; StreamPage routes that into StreamDetailView with the
// episode preselected.
class CalendarScreen : public QWidget
{
    Q_OBJECT

public:
    explicit CalendarScreen(QWidget* parent = nullptr);

    void setLoading(bool loading);

    // Flat consumer — groups items locally by day+bucket using the same
    // week-boundary math CalendarEngine uses on its side. Harmless duplicate
    // if StreamPage wires `calendarGroupedReady` instead.
    void setItems(const QList<CalendarItem>& items);

    // Preferred consumer — pre-grouped by CalendarEngine. Skips the
    // local grouping pass.
    void setGroupedItems(const QList<CalendarDayGroup>& groups);

    void setError(const QString& message);

signals:
    void backRequested();
    void refreshRequested();

    // Double-click on an episode row. StreamPage routes to StreamDetailView
    // with (season, episode) preselected.
    void seriesEpisodeActivated(const QString& imdbId, int season, int episode);

private:
    void buildUI();
    void renderGroups(const QList<CalendarDayGroup>& groups);

    static QList<CalendarDayGroup> regroupFromFlat(const QList<CalendarItem>& items);
    static QString bucketTitle(CalendarBucket bucket);
    static int     bucketOrder(CalendarBucket bucket);
    static QString formatEpisodeCode(const tankostream::addon::Video& video);
    static QString formatDay(const QDate& day);

    QPushButton* m_backButton    = nullptr;
    QPushButton* m_refreshButton = nullptr;
    QLabel*      m_titleLabel    = nullptr;
    QLabel*      m_statusLabel   = nullptr;
    QTreeWidget* m_tree          = nullptr;
};

}
