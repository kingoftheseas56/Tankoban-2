// =================================================================
// Agent 7 (Codex) Prototype - Reference Only
// =================================================================
// For: Agent 4, Batch 6.2 (Calendar screen)
// Date: 2026-04-14
// References consulted:
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:280
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:281
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:282
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:283
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:355
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/StreamPage.h:69
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/StreamPage.cpp:80
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/StreamPage.cpp:139
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/StreamPage.cpp:152
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/StreamPage.cpp:155
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/stream/CatalogBrowseScreen.h:31
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/stream/CatalogBrowseScreen.cpp:57
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/stream/AddonManagerScreen.h:28
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/stream/AddonManagerScreen.cpp:194
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/stream/StreamHomeBoard.h:38
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/stream/StreamHomeBoard.cpp:318
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/stream/StreamDetailView.h:27
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/stream/StreamDetailView.h:31
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/stream/StreamDetailView.cpp:30
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/stream/StreamDetailView.cpp:214
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/addon/MetaItem.h:22
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/addon/MetaItem.h:40
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/addon/MetaItem.h:51
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/addon/MetaItem.h:74
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/addon/MetaItem.h:76
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/models/calendar.rs:87
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/models/calendar.rs:93
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/models/calendar.rs:239
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/models/calendar.rs:249
//
// THIS FILE IS NOT COMPILED. The domain agent implements their own
// version after reading this for perspective. Do not #include from
// src/. Do not add to CMakeLists.txt.
// =================================================================
// Prototype by Agent 7 (Codex), 2026-04-14. For Agent 4, Batch 6.2.
// Reference only - domain agent implements their own version.
// Do not compile. Do not include from src/.

#include <QDate>
#include <QDateTime>
#include <QFrame>
#include <QHash>
#include <QHBoxLayout>
#include <QLabel>
#include <QList>
#include <QMap>
#include <QObject>
#include <QPushButton>
#include <QString>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVariant>
#include <QVBoxLayout>

#include <optional>

namespace tankostream::addon {

struct SeriesInfo {
    int season = 0;
    int episode = 0;
};

struct MetaItemPreview {
    QString id;
    QString type;
    QString name;
    QDateTime released;
};

struct Video {
    QString id;
    QString title;
    QDateTime released;
    std::optional<SeriesInfo> seriesInfo;
};

} // namespace tankostream::addon

namespace tankostream::stream {

struct CalendarItem {
    tankostream::addon::MetaItemPreview meta;
    tankostream::addon::Video video;
};

enum class CalendarBucket {
    ThisWeek,
    NextWeek,
    Later,
};

struct CalendarDayGroup {
    QDate day;
    CalendarBucket bucket = CalendarBucket::Later;
    QList<CalendarItem> items;
};

class CalendarEngine : public QObject {
    Q_OBJECT
public:
    explicit CalendarEngine(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

    void loadUpcoming();

signals:
    void calendarReady(const QList<CalendarItem>& items);
    void calendarGroupedReady(const QList<CalendarDayGroup>& groups);
    void calendarError(const QString& message);
};

} // namespace tankostream::stream

namespace tankoban::prototype::calendar62 {

using tankostream::stream::CalendarBucket;
using tankostream::stream::CalendarDayGroup;
using tankostream::stream::CalendarItem;

namespace {

QString bucketTitle(CalendarBucket bucket)
{
    switch (bucket) {
    case CalendarBucket::ThisWeek:
        return QStringLiteral("This Week");
    case CalendarBucket::NextWeek:
        return QStringLiteral("Next Week");
    case CalendarBucket::Later:
    default:
        return QStringLiteral("Later");
    }
}

int bucketOrder(CalendarBucket bucket)
{
    switch (bucket) {
    case CalendarBucket::ThisWeek:
        return 0;
    case CalendarBucket::NextWeek:
        return 1;
    case CalendarBucket::Later:
    default:
        return 2;
    }
}

QString formatEpisodeCode(const tankostream::addon::Video& video)
{
    if (!video.seriesInfo.has_value()) {
        return QStringLiteral("-");
    }
    const int season = video.seriesInfo->season;
    const int episode = video.seriesInfo->episode;
    return QStringLiteral("S%1E%2")
        .arg(season, 2, 10, QLatin1Char('0'))
        .arg(episode, 2, 10, QLatin1Char('0'));
}

QString formatDay(const QDate& d)
{
    return d.toString(QStringLiteral("ddd, dd MMM yyyy"));
}

} // namespace

class CalendarScreen : public QWidget
{
    Q_OBJECT

public:
    explicit CalendarScreen(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        buildUi();
    }

    void setLoading(bool loading)
    {
        m_statusLabel->setText(loading ? QStringLiteral("Loading calendar...") : QString());
        m_statusLabel->setVisible(loading);
    }

    void setItems(const QList<CalendarItem>& items)
    {
        // Fallback path if only flat items are provided.
        QList<CalendarDayGroup> groups = groupFlat(items);
        setGroupedItems(groups);
    }

    void setGroupedItems(const QList<CalendarDayGroup>& groups)
    {
        m_tree->clear();
        m_statusLabel->hide();

        if (groups.isEmpty()) {
            m_statusLabel->setText(QStringLiteral("No upcoming episodes in the next 60 days."));
            m_statusLabel->show();
            return;
        }

        QMap<int, QList<CalendarDayGroup>> byBucket;
        int totalRows = 0;

        for (const CalendarDayGroup& group : groups) {
            if (!group.day.isValid() || group.items.isEmpty()) {
                continue;
            }
            byBucket[bucketOrder(group.bucket)].push_back(group);
            totalRows += group.items.size();
        }

        for (auto bucketIt = byBucket.constBegin(); bucketIt != byBucket.constEnd(); ++bucketIt) {
            const QList<CalendarDayGroup> bucketGroups = bucketIt.value();
            if (bucketGroups.isEmpty()) {
                continue;
            }

            const CalendarBucket bucketKind = bucketGroups.first().bucket;
            auto* bucketItem = new QTreeWidgetItem(m_tree);
            bucketItem->setText(0, bucketTitle(bucketKind));
            bucketItem->setData(0, Qt::UserRole, QStringLiteral("bucket"));
            bucketItem->setFirstColumnSpanned(true);
            bucketItem->setExpanded(true);

            QList<CalendarDayGroup> sortedDays = bucketGroups;
            std::sort(sortedDays.begin(), sortedDays.end(),
                      [](const CalendarDayGroup& a, const CalendarDayGroup& b) {
                          return a.day < b.day;
                      });

            for (const CalendarDayGroup& dayGroup : sortedDays) {
                auto* dayItem = new QTreeWidgetItem(bucketItem);
                dayItem->setText(0, formatDay(dayGroup.day));
                dayItem->setData(0, Qt::UserRole, QStringLiteral("day"));
                dayItem->setExpanded(true);

                for (const CalendarItem& item : dayGroup.items) {
                    auto* row = new QTreeWidgetItem(dayItem);
                    row->setText(0, dayGroup.day.toString(QStringLiteral("dd MMM")));
                    row->setText(1, item.meta.name);
                    row->setText(2, formatEpisodeCode(item.video));
                    row->setText(3, item.video.title);

                    row->setData(0, Qt::UserRole, QStringLiteral("episode"));
                    row->setData(0, Qt::UserRole + 1, item.meta.id);
                    row->setData(0, Qt::UserRole + 2,
                                 item.video.seriesInfo.has_value()
                                     ? item.video.seriesInfo->season
                                     : -1);
                    row->setData(0, Qt::UserRole + 3,
                                 item.video.seriesInfo.has_value()
                                     ? item.video.seriesInfo->episode
                                     : -1);
                }
            }
        }

        m_statusLabel->setText(QStringLiteral("%1 upcoming episodes").arg(totalRows));
        m_statusLabel->show();
    }

signals:
    void backRequested();
    void refreshRequested();
    // StreamPage should route this to StreamDetailView with preselected episode.
    void seriesEpisodeActivated(const QString& imdbId, int season, int episode);

private:
    static QList<CalendarDayGroup> groupFlat(const QList<CalendarItem>& items)
    {
        QMap<QDate, QList<CalendarItem>> byDay;
        const QDate today = QDate::currentDate();

        for (const CalendarItem& item : items) {
            const QDate day = item.video.released.toUTC().date();
            if (!day.isValid()) {
                continue;
            }
            byDay[day].push_back(item);
        }

        QList<CalendarDayGroup> out;
        out.reserve(byDay.size());
        for (auto it = byDay.constBegin(); it != byDay.constEnd(); ++it) {
            CalendarDayGroup g;
            g.day = it.key();
            g.items = it.value();

            const int daysFromToday = today.daysTo(g.day);
            if (daysFromToday <= 7) {
                g.bucket = CalendarBucket::ThisWeek;
            } else if (daysFromToday <= 14) {
                g.bucket = CalendarBucket::NextWeek;
            } else {
                g.bucket = CalendarBucket::Later;
            }
            out.push_back(g);
        }
        return out;
    }

    void buildUi()
    {
        setObjectName(QStringLiteral("StreamCalendarScreen"));
        setStyleSheet(QStringLiteral(
            "#StreamCalendarScreen { background: transparent; }"
            "#StreamCalendarHeader { background: transparent; }"
            "#StreamCalendarTitle { color: #e5e7eb; font-size: 14px; font-weight: 600; }"
            "#StreamCalendarStatus { color: #9ca3af; font-size: 11px; }"
            "#StreamCalendarBack, #StreamCalendarRefresh {"
            " color: #d1d5db;"
            " background: rgba(255,255,255,0.07);"
            " border: 1px solid rgba(255,255,255,0.12);"
            " border-radius: 6px;"
            " padding: 4px 10px; }"
            "#StreamCalendarBack:hover, #StreamCalendarRefresh:hover {"
            " border-color: rgba(255,255,255,0.22); }"
            "#StreamCalendarTree {"
            " background: rgba(0,0,0,0.20);"
            " border: 1px solid rgba(255,255,255,0.08);"
            " border-radius: 8px;"
            " color: #d1d5db;"
            " alternate-background-color: rgba(255,255,255,0.03); }"
            "#StreamCalendarTree::item:selected { background: rgba(255,255,255,0.12); }"
            "QHeaderView::section {"
            " background: rgba(255,255,255,0.05);"
            " border: none;"
            " color: #9ca3af;"
            " font-size: 11px;"
            " padding: 4px; }"));

        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(16, 8, 16, 12);
        root->setSpacing(8);

        auto* header = new QHBoxLayout();
        header->setObjectName(QStringLiteral("StreamCalendarHeader"));
        header->setContentsMargins(0, 0, 0, 0);
        header->setSpacing(8);

        m_backButton = new QPushButton(QStringLiteral("Back"), this);
        m_backButton->setObjectName(QStringLiteral("StreamCalendarBack"));
        m_backButton->setCursor(Qt::PointingHandCursor);
        connect(m_backButton, &QPushButton::clicked, this, &CalendarScreen::backRequested);
        header->addWidget(m_backButton);

        auto* title = new QLabel(QStringLiteral("Calendar"), this);
        title->setObjectName(QStringLiteral("StreamCalendarTitle"));
        header->addWidget(title);
        header->addStretch();

        m_refreshButton = new QPushButton(QStringLiteral("Refresh"), this);
        m_refreshButton->setObjectName(QStringLiteral("StreamCalendarRefresh"));
        m_refreshButton->setCursor(Qt::PointingHandCursor);
        connect(m_refreshButton, &QPushButton::clicked, this, &CalendarScreen::refreshRequested);
        header->addWidget(m_refreshButton);

        root->addLayout(header);

        m_statusLabel = new QLabel(this);
        m_statusLabel->setObjectName(QStringLiteral("StreamCalendarStatus"));
        m_statusLabel->setText(QStringLiteral("Idle"));
        root->addWidget(m_statusLabel);

        m_tree = new QTreeWidget(this);
        m_tree->setObjectName(QStringLiteral("StreamCalendarTree"));
        m_tree->setColumnCount(4);
        m_tree->setHeaderLabels(
            {QStringLiteral("Date"), QStringLiteral("Series"),
             QStringLiteral("Episode"), QStringLiteral("Title")});
        m_tree->setRootIsDecorated(true);
        m_tree->setAlternatingRowColors(true);
        m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
        m_tree->setUniformRowHeights(true);
        m_tree->header()->setStretchLastSection(true);
        m_tree->setColumnWidth(0, 120);
        m_tree->setColumnWidth(1, 220);
        m_tree->setColumnWidth(2, 90);
        root->addWidget(m_tree, 1);

        connect(m_tree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem* item, int) {
                if (!item) {
                    return;
                }
                if (item->data(0, Qt::UserRole).toString() != QStringLiteral("episode")) {
                    return;
                }
                const QString imdbId = item->data(0, Qt::UserRole + 1).toString();
                const int season = item->data(0, Qt::UserRole + 2).toInt();
                const int episode = item->data(0, Qt::UserRole + 3).toInt();
                if (!imdbId.isEmpty() && season >= 0 && episode >= 0) {
                    emit seriesEpisodeActivated(imdbId, season, episode);
                }
            });
    }

    QPushButton* m_backButton = nullptr;
    QPushButton* m_refreshButton = nullptr;
    QLabel* m_statusLabel = nullptr;
    QTreeWidget* m_tree = nullptr;
};

} // namespace tankoban::prototype::calendar62

// -----------------------------------------------------------------
// StreamPage wiring sketch (Batch 6.2 scope)
// -----------------------------------------------------------------
//
// 1) New members in StreamPage.h:
//      QPushButton* m_calendarBtn = nullptr;            // search/header row entry
//      tankostream::stream::CalendarEngine* m_calendarEngine = nullptr;
//      tankoban::prototype::calendar62::CalendarScreen* m_calendarScreen = nullptr;
//
// 2) Stack layer:
//      m_calendarScreen = new CalendarScreen(this);
//      m_mainStack->addWidget(m_calendarScreen); // index 5: calendar
//
// 3) Header button (buildSearchBar):
//      m_calendarBtn = new QPushButton("Calendar", m_searchBarFrame);
//      m_calendarBtn->setObjectName("StreamCalendarBtn");
//      layout->addWidget(m_calendarBtn);
//      connect(m_calendarBtn, &QPushButton::clicked, this, [this]() {
//          m_mainStack->setCurrentIndex(5);
//          m_calendarScreen->setLoading(true);
//          m_calendarEngine->loadUpcoming();
//      });
//
// 4) Engine -> screen:
//      connect(m_calendarEngine, &CalendarEngine::calendarGroupedReady,
//              m_calendarScreen, &CalendarScreen::setGroupedItems);
//      connect(m_calendarEngine, &CalendarEngine::calendarReady,
//              m_calendarScreen, &CalendarScreen::setItems);
//      connect(m_calendarEngine, &CalendarEngine::calendarError, this,
//          [this](const QString& msg) { m_calendarScreen->setLoading(false); /*status*/ });
//
// 5) Screen nav:
//      connect(m_calendarScreen, &CalendarScreen::backRequested,
//              this, &StreamPage::showBrowse);
//      connect(m_calendarScreen, &CalendarScreen::refreshRequested, this,
//              [this]() { m_calendarScreen->setLoading(true); m_calendarEngine->loadUpcoming(); });
//
// 6) Double-click -> detail with episode preselected:
//      connect(m_calendarScreen, &CalendarScreen::seriesEpisodeActivated, this,
//          [this](const QString& imdbId, int season, int episode) {
//              // Existing API is showEntry(imdbId) only.
//              // Minimal additive extension in StreamDetailView:
//              //   showEntry(const QString& imdbId, int preselectSeason, int preselectEpisode)
//              // then:
//              //   m_detailView->showEntry(imdbId, season, episode);
//              // If kept two-step:
//              //   showDetail(imdbId); m_detailView->setPendingEpisodeSelection(season, episode);
//              m_mainStack->setCurrentIndex(1);
//          });

