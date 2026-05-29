#include "ComicsDownloadsPage.h"

#include "core/manga/MangaDownloadIndex.h"
#include "ui/ContextMenuHelper.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QMenu>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QPixmapCache>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QSizePolicy>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <map>

// COMICS_DOWNLOAD_DISPLAY_PROJECTION 2026-05-26 (Agent 9)
#include "../ComicsPage.h"

namespace {

// Header-free flow layout (canonical Qt pattern; no Q_OBJECT needed because it
// adds no signals/slots) — wraps child widgets left-to-right with fixed spacing
// and reflows on resize via heightForWidth. Used for the volume-chip grid.
class FlowLayout : public QLayout {
public:
    explicit FlowLayout(int hSpacing = 8, int vSpacing = 8)
        : m_hSpace(hSpacing), m_vSpace(vSpacing) { setContentsMargins(0, 0, 0, 0); }
    ~FlowLayout() override { QLayoutItem* it; while ((it = takeAt(0))) delete it; }

    void addItem(QLayoutItem* item) override { m_items.append(item); }
    int count() const override { return m_items.size(); }
    QLayoutItem* itemAt(int i) const override { return m_items.value(i); }
    QLayoutItem* takeAt(int i) override {
        return (i >= 0 && i < m_items.size()) ? m_items.takeAt(i) : nullptr;
    }
    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return doLayout(QRect(0, 0, width, 0), true); }
    void setGeometry(const QRect& rect) override { QLayout::setGeometry(rect); doLayout(rect, false); }
    QSize sizeHint() const override { return minimumSize(); }
    QSize minimumSize() const override {
        QSize s;
        for (QLayoutItem* item : m_items) s = s.expandedTo(item->minimumSize());
        return s;
    }

private:
    int doLayout(const QRect& rect, bool testOnly) const {
        int x = rect.x(), y = rect.y(), lineHeight = 0;
        for (QLayoutItem* item : m_items) {
            const QSize hint = item->sizeHint();
            int nextX = x + hint.width() + m_hSpace;
            if (nextX - m_hSpace > rect.right() && lineHeight > 0) {
                x = rect.x();
                y += lineHeight + m_vSpace;
                nextX = x + hint.width() + m_hSpace;
                lineHeight = 0;
            }
            if (!testOnly)
                item->setGeometry(QRect(QPoint(x, y), hint));
            x = nextX;
            lineHeight = qMax(lineHeight, hint.height());
        }
        return y + lineHeight - rect.y();
    }

    QList<QLayoutItem*> m_items;
    int m_hSpace;
    int m_vSpace;
};

// Host widget that forwards heightForWidth to its FlowLayout. A parent
// QBoxLayout does NOT consult a child *layout's* heightForWidth, but it does
// consult a child *widget's* (once the widget's size policy opts in) — so the
// flow must live inside a widget or tall chip grids clip to one row.
class FlowWidget : public QWidget {
public:
    using QWidget::QWidget;
    int heightForWidth(int w) const override {
        return layout() ? layout()->heightForWidth(w) : QWidget::heightForWidth(w);
    }
};

} // namespace

ComicsDownloadsPage::ComicsDownloadsPage(QWidget* parent)
    : QFrame(parent)
{
    setObjectName("ComicsDownloadsPage");
    buildUi();
}

void ComicsDownloadsPage::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Topbar: back button + title
    auto* topbar = new QFrame(this);
    topbar->setObjectName("ComicsDownloadsTopbar");
    topbar->setFixedHeight(48);
    auto* topbarLayout = new QHBoxLayout(topbar);
    topbarLayout->setContentsMargins(14, 6, 14, 6);
    topbarLayout->setSpacing(10);

    m_backBtn = new QPushButton(tr("< Back"), topbar);
    m_backBtn->setObjectName("ComicsDownloadsBackBtn");
    m_backBtn->setCursor(Qt::PointingHandCursor);
    m_backBtn->setFixedHeight(28);
    connect(m_backBtn, &QPushButton::clicked, this, &ComicsDownloadsPage::backRequested);

    m_titleLabel = new QLabel(tr("Downloads"), topbar);
    m_titleLabel->setObjectName("ComicsDownloadsTitle");
    m_titleLabel->setStyleSheet(
        "QLabel#ComicsDownloadsTitle { font-size: 16pt; font-weight: 600; color: #eeeeee; }");

    topbarLayout->addWidget(m_backBtn, 0);
    topbarLayout->addWidget(m_titleLabel, 0);
    topbarLayout->addStretch(1);

    root->addWidget(topbar, 0);

    // Scrollable body
    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName("ComicsDownloadsScroll");
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);

    m_scrollContent = new QWidget(m_scroll);
    m_scrollContent->setObjectName("ComicsDownloadsScrollContent");
    m_contentLayout = new QVBoxLayout(m_scrollContent);
    m_contentLayout->setContentsMargins(20, 12, 20, 20);
    m_contentLayout->setSpacing(18);

    // Downloaded section
    m_sectionHeader = new QLabel(tr("DOWNLOADED"), m_scrollContent);
    m_sectionHeader->setObjectName("ComicsDownloadsSectionHeader");
    m_sectionHeader->setStyleSheet(
        "QLabel#ComicsDownloadsSectionHeader { font-size: 9pt; font-weight: 700;"
        " color: rgba(255,255,255,0.55); letter-spacing: 1.2px; }");

    m_sectionBody = new QWidget(m_scrollContent);
    m_sectionBody->setObjectName("ComicsDownloadsSectionBody");
    m_sectionBodyLayout = new QVBoxLayout(m_sectionBody);
    m_sectionBodyLayout->setContentsMargins(0, 0, 0, 0);
    m_sectionBodyLayout->setSpacing(8);

    // Empty state placeholder
    m_emptyState = new QLabel(
        tr("No downloads yet.\n\nDownload a volume or chapter from any Comics series view."),
        m_scrollContent);
    m_emptyState->setObjectName("ComicsDownloadsEmptyState");
    m_emptyState->setAlignment(Qt::AlignCenter);
    m_emptyState->setStyleSheet(
        "QLabel#ComicsDownloadsEmptyState { color: rgba(255,255,255,0.45);"
        " font-size: 11pt; padding: 60px 20px; }");

    m_contentLayout->addWidget(m_sectionHeader, 0);
    m_contentLayout->addWidget(m_sectionBody, 0);
    m_contentLayout->addWidget(m_emptyState, 0);
    m_contentLayout->addStretch(1);

    m_scroll->setWidget(m_scrollContent);
    root->addWidget(m_scroll, 1);
}

void ComicsDownloadsPage::setMangaDownloadIndex(MangaDownloadIndex* index)
{
    if (m_mangaDownloadIndex == index)
        return;
    if (m_mangaDownloadIndex) {
        disconnect(m_mangaDownloadIndex, nullptr, this, nullptr);
    }
    m_mangaDownloadIndex = index;
    if (m_mangaDownloadIndex) {
        connect(m_mangaDownloadIndex, &MangaDownloadIndex::entriesChanged,
                this, &ComicsDownloadsPage::refresh,
                Qt::QueuedConnection);
    }
    refresh();
}

void ComicsDownloadsPage::setComicsPage(ComicsPage* page)
{
    m_comicsPage = page;
    // COMICS_DOWNLOAD_DISPLAY_PROJECTION 2026-05-26 (Agent 9) —
    // Re-render now that display helpers are available. Safe to call
    // even when m_comicsPage is set before m_mangaDownloadIndex
    // (refresh() guards against null m_mangaDownloadIndex).
    refresh();
}

void ComicsDownloadsPage::updateEmptyState()
{
    if (!m_emptyState || !m_sectionBody || !m_sectionBodyLayout)
        return;

    const bool anyContent = !m_sectionBody->isHidden() && m_sectionBodyLayout->count() > 0;
    m_emptyState->setVisible(!anyContent);
}

QWidget* ComicsDownloadsPage::makeCoverWidget(const QString& coverUrl, const QString& title)
{
    auto* pl = new QLabel;
    pl->setObjectName("ComicsDownloadsCover");
    pl->setFixedSize(110, 150);  // manga 2:3 native (feedback_bigger_manga_covers)
    pl->setScaledContents(true);
    pl->setAlignment(Qt::AlignCenter);
    pl->setWordWrap(true);
    pl->setStyleSheet(
        "QLabel#ComicsDownloadsCover {"
        "  border-top-left-radius: 12px; border-bottom-left-radius: 12px;"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
        "    stop:0 rgba(255,255,255,0.10), stop:1 rgba(255,255,255,0.03));"
        "  color: rgba(255,255,255,0.45); font-size: 9pt; padding: 6px;"
        "}");
    pl->setText(title);  // placeholder until art loads

    if (coverUrl.isEmpty())
        return pl;

    // Sync hit from the process-global cache the series view also fills.
    QPixmap cached;
    if (QPixmapCache::find(coverUrl, &cached)) {
        pl->setText(QString());
        pl->setPixmap(cached.scaled(110, 150, Qt::KeepAspectRatioByExpanding,
                                    Qt::SmoothTransformation));
        return pl;
    }

    if (!m_coverNam)
        m_coverNam = new QNetworkAccessManager(this);
    QNetworkRequest req((QUrl(coverUrl)));
    // Browser UA — Fandom (static.wikia.nocookie.net) 403s non-browser agents;
    // mirrors ComicsSeriesView::loadCoverUrlForVolume.
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QString::fromLatin1(
                      "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                      "(KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36"));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = m_coverNam->get(req);
    QPointer<QLabel> guard(pl);  // auto-nulls if the card is torn down mid-fetch
    connect(reply, &QNetworkReply::finished, this, [reply, coverUrl, guard]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return;
        QPixmap pm;
        if (!pm.loadFromData(reply->readAll()))
            return;
        QPixmapCache::insert(coverUrl, pm);
        if (guard) {
            guard->setText(QString());
            guard->setPixmap(pm.scaled(110, 150, Qt::KeepAspectRatioByExpanding,
                                       Qt::SmoothTransformation));
        }
    });
    return pl;
}

void ComicsDownloadsPage::refresh()
{
    if (!m_sectionBodyLayout)
        return;

    while (auto* item = m_sectionBodyLayout->takeAt(0)) {
        if (auto* w = item->widget()) w->deleteLater();
        delete item;
    }

    if (!m_mangaDownloadIndex) {
        m_sectionHeader->setVisible(false);
        m_sectionBody->setVisible(false);
        updateEmptyState();
        return;
    }

    const QList<MangaDownloadIndex::Entry> representatives =
        m_mangaDownloadIndex->entriesForAllSeries();

    if (representatives.isEmpty()) {
        m_sectionHeader->setVisible(false);
        m_sectionBody->setVisible(false);
        updateEmptyState();
        return;
    }

    // ── COMICS_DOWNLOAD_DISPLAY_PROJECTION 2026-05-26 (Agent 9) ──
    // Phase 1: Collect ALL entries across all series — not just one
    // representative per (sourceId, seriesId). We need every volume/chapter
    // entry so we can group by canonical display identity.
    using SeriesKey = QString;
    std::map<SeriesKey, QStringList> keyToSeriesKeys; // groupKey → set of raw "sourceId:seriesId"
    QHash<QString, QList<MangaDownloadIndex::Entry>> rawSeriesEntries; // "sourceId:seriesId" → entries

    for (const auto& rep : representatives) {
        const QString rawSeriesKey =
            MangaDownloadIndex::computeSeriesKey(rep.sourceId, rep.seriesId);
        if (rawSeriesEntries.contains(rawSeriesKey))
            continue; // already collected

        QList<MangaDownloadIndex::Entry> seriesEntries =
            m_mangaDownloadIndex->entriesForSeries(rep.sourceId, rep.seriesId);
        if (seriesEntries.isEmpty())
            continue;

        // Sort by volumeNumber ascending, then by canonicalPath.
        std::sort(seriesEntries.begin(), seriesEntries.end(),
                  [](const MangaDownloadIndex::Entry& a, const MangaDownloadIndex::Entry& b) {
                      if (a.volumeNumber != b.volumeNumber)
                          return a.volumeNumber < b.volumeNumber;
                      return a.canonicalPath < b.canonicalPath;
                  });

        rawSeriesEntries.insert(rawSeriesKey, seriesEntries);

        // Determine canonical group key for this raw series.
        const QString groupKey = m_comicsPage
            ? m_comicsPage->resolveCanonicalGroupKey(rep.sourceId, rep.seriesId)
            : QStringLiteral("raw:") + rep.sourceId + QStringLiteral(":") + rep.seriesId;

        keyToSeriesKeys[groupKey].append(rawSeriesKey);
    }

    // ── Phase 2: Build one card per canonical group key ──
    // Card-level data: displayTitle, list of (sourceId, volumeNumber,
    // chapterId, filename, sourceLabel) rows. Also track newestAddedAt for
    // sort ordering.

    struct SeriesCard {
        QString displayTitle;
        qint64  newestAddedAt = 0;
        // Deduped across all sources in the canonical group; rendered sorted as
        // "Vol N" chips. Legacy (volumeNumber == 0) entries fall into chapterIds.
        QSet<int>     volumeNumbers;
        QSet<QString> chapterIds;
    };
    std::map<SeriesKey, SeriesCard> cards;

    for (const auto& kv : keyToSeriesKeys) {
        const SeriesKey& groupKey = kv.first;
        const QStringList& rawKeys = kv.second;

        SeriesCard card;

        // Resolve display title from the first raw series in this group.
        // Parse the "sourceId:seriesId" format.
        if (!rawKeys.isEmpty()) {
            const QString& firstRaw = rawKeys.first();
            const int sep = firstRaw.indexOf(QLatin1Char(':'));
            if (sep > 0) {
                const QString srcId = firstRaw.left(sep);
                const QString serId = firstRaw.mid(sep + 1);
                if (m_comicsPage) {
                    card.displayTitle = m_comicsPage->resolveDisplayTitle(srcId, serId);
                    if (card.displayTitle.isEmpty()) {
                        // Fallback: humanize the seriesId slug.
                        card.displayTitle = ComicsPage::humanizeSlug(serId);
                    }
                    if (card.displayTitle.isEmpty()) {
                        // Final fallback: use seriesId itself.
                        card.displayTitle = serId;
                    }
                } else {
                    card.displayTitle = serId;
                }
            }
        }

        // Collect all rows across all raw series in this canonical group.
        for (const auto& rawKey : rawKeys) {
            auto it = rawSeriesEntries.constFind(rawKey);
            if (it == rawSeriesEntries.constEnd())
                continue;

            for (const auto& e : *it) {
                card.newestAddedAt = std::max(card.newestAddedAt, e.addedAt);
                // Dedup across sources; sorted at render. Legacy chapter entries
                // (volumeNumber == 0) collect separately.
                if (e.volumeNumber > 0)
                    card.volumeNumbers.insert(e.volumeNumber);
                else if (!e.chapterId.isEmpty())
                    card.chapterIds.insert(e.chapterId);
            }
        }

        cards[groupKey] = card;
    }

    // ── Phase 3: Sort cards by newestAddedAt descending ──
    // Convert map to list for sorting.
    using CardPair = QPair<SeriesKey, SeriesCard>;
    QList<CardPair> cardList;
    for (const auto& kv : cards) {
        cardList.append({kv.first, kv.second});
    }
    std::sort(cardList.begin(), cardList.end(),
              [](const CardPair& a, const CardPair& b) {
                  return a.second.newestAddedAt > b.second.newestAddedAt;
              });

    // ── Phase 4: Render cards ──
    m_sectionHeader->setVisible(!cardList.isEmpty());
    m_sectionBody->setVisible(!cardList.isEmpty());

    for (const auto& pair : cardList) {
        const SeriesCard& card = pair.second;
        const QStringList& rawKeys = keyToSeriesKeys[pair.first];

        const QString coverUrl = m_comicsPage
            ? m_comicsPage->resolveCanonicalSeriesCover(0, card.displayTitle)
            : QString();

        auto* cardFrame = new QFrame(m_sectionBody);
        cardFrame->setObjectName("ComicsDownloadsCard");
        cardFrame->setStyleSheet(
            "QFrame#ComicsDownloadsCard {"
            "  background: rgba(255,255,255,0.038);"
            "  border: 1px solid rgba(255,255,255,0.08);"
            "  border-radius: 12px;"
            "}");
        auto* h = new QHBoxLayout(cardFrame);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(0);

        // Series cover on the left (110x150, manga 2:3).
        h->addWidget(makeCoverWidget(coverUrl, card.displayTitle), 0, Qt::AlignTop);

        auto* right = new QVBoxLayout();
        right->setContentsMargins(16, 13, 16, 11);
        right->setSpacing(3);

        // Series title (canonical display title).
        auto* titleLabel = new QLabel(card.displayTitle, cardFrame);
        titleLabel->setObjectName("ComicsDownloadsSeriesTitle");
        titleLabel->setStyleSheet(
            "QLabel#ComicsDownloadsSeriesTitle { color: #ededed; font-size: 15px; font-weight: 600; }");
        right->addWidget(titleLabel);

        // Volume count line: "1 volume" / "2 volumes" (never "volumes / chapters").
        const int entryCount = card.volumeNumbers.size() + card.chapterIds.size();
        const QString countText = (entryCount == 1)
            ? tr("1 volume")
            : tr("%1 volumes").arg(entryCount);
        auto* countLabel = new QLabel(countText, cardFrame);
        countLabel->setObjectName("ComicsDownloadsCount");
        countLabel->setStyleSheet(
            "QLabel#ComicsDownloadsCount { color: rgba(255,255,255,0.55); font-size: 12px; }");
        right->addWidget(countLabel);

        // Sorted, de-duplicated "Vol N" chips in a wrapping grid (FlowLayout).
        QList<int> vols = card.volumeNumbers.values();
        std::sort(vols.begin(), vols.end());
        QStringList chs = card.chapterIds.values();
        std::sort(chs.begin(), chs.end());

        const QString chipStyle =
            "QLabel#ComicsDownloadsVolChip {"
            "  background: rgba(255,255,255,0.06);"
            "  border: 1px solid rgba(255,255,255,0.10);"
            "  border-radius: 7px; padding: 4px 11px;"
            "  color: rgba(255,255,255,0.88); font-size: 13px;"
            "}";
        auto* chipHost = new FlowWidget(cardFrame);
        auto* chipFlow = new FlowLayout(/*hSpacing=*/8, /*vSpacing=*/8);
        for (int n : vols) {
            auto* chip = new QLabel(chipHost);
            chip->setObjectName("ComicsDownloadsVolChip");
            chip->setStyleSheet(chipStyle);
            chip->setText(QStringLiteral("<span style=\"color:#8c8c8c;\">Vol</span> %1").arg(n));
            chipFlow->addWidget(chip);
        }
        for (const QString& c : chs) {
            auto* chip = new QLabel(chipHost);
            chip->setObjectName("ComicsDownloadsVolChip");
            chip->setStyleSheet(chipStyle);
            chip->setText(QStringLiteral("<span style=\"color:#8c8c8c;\">Ch</span> %1").arg(c.toHtmlEscaped()));
            chipFlow->addWidget(chip);
        }
        chipHost->setLayout(chipFlow);
        QSizePolicy chipSp(QSizePolicy::Preferred, QSizePolicy::Preferred);
        chipSp.setHeightForWidth(true);
        chipHost->setSizePolicy(chipSp);
        right->addSpacing(6);
        right->addWidget(chipHost);

        h->addLayout(right, 1);

        // Context menu for this card group
        cardFrame->setContextMenuPolicy(Qt::CustomContextMenu);
        const QStringList storedRawKeys = rawKeys;
        const QString storedDisplayTitle = card.displayTitle;
        connect(cardFrame, &QWidget::customContextMenuRequested, this,
                [this, cardFrame, storedRawKeys, storedDisplayTitle](const QPoint& pos) {
            if (!m_mangaDownloadIndex) return;

            // Collect all entries in this canonical group for delete/reveal/copy.
            struct RowEntry { QString sourceId; QString seriesId; int volumeNumber; QString canonicalPath; };
            QList<RowEntry> entries;
            for (const auto& rk : storedRawKeys) {
                const int sep = rk.indexOf(QLatin1Char(':'));
                if (sep <= 0) continue;
                const QString src = rk.left(sep);
                const QString sid = rk.mid(sep + 1);
                const auto ents = m_mangaDownloadIndex->entriesForSeries(src, sid);
                for (const auto& e : ents)
                    entries.append({e.sourceId, e.seriesId, e.volumeNumber, e.canonicalPath});
            }
            if (entries.isEmpty()) return;

            QMenu* menu = ContextMenuHelper::createMenu(this);

            // Open series — navigate to the series view
            QAction* openAct = menu->addAction(tr("Open series"));

            menu->addSeparator();

            // Delete…
            QAction* delAct = ContextMenuHelper::addDangerAction(menu, tr("Delete…"));

            // Reveal / Copy — anchored on first entry's path
            const QString firstPath = entries.first().canonicalPath;
            const bool fileExists = !firstPath.isEmpty() && QFileInfo::exists(firstPath);
            QAction* revealAct = menu->addAction(tr("Reveal in File Explorer"));
            revealAct->setEnabled(fileExists);
            QAction* copyAct = menu->addAction(tr("Copy path"));
            copyAct->setEnabled(!firstPath.isEmpty());

            QAction* chosen = menu->exec(cardFrame->mapToGlobal(pos));

            if (chosen == openAct && m_comicsPage) {
                if (!entries.isEmpty()) {
                    const auto& e = entries.first();
                    QMetaObject::invokeMethod(m_comicsPage, "openSeriesForDownloadEntry",
                        Qt::DirectConnection,
                        Q_ARG(QString, e.sourceId),
                        Q_ARG(QString, e.seriesId),
                        Q_ARG(QString, storedDisplayTitle));
                }
            } else if (chosen == delAct) {
                const auto choice = ContextMenuHelper::confirmRemoveWithFile(
                    this, tr("Delete downloads"),
                    tr("Remove \"%1\" (%2 volume%3) from your library?")
                        .arg(storedDisplayTitle).arg(entries.size())
                        .arg(entries.size() == 1 ? QString() : QStringLiteral("s")));
                if (choice == ContextMenuHelper::RemoveChoice::Cancel) goto dl_menu_done;
                for (const auto& e : entries) {
                    if (choice == ContextMenuHelper::RemoveChoice::DeleteFile) {
                        QFile::remove(e.canonicalPath);
                        QFile::remove(e.canonicalPath + QStringLiteral(".volx"));
                    }
                    m_mangaDownloadIndex->evictByVolume(e.sourceId, e.seriesId, e.volumeNumber);
                }
            } else if (chosen == revealAct) {
                ContextMenuHelper::revealInExplorer(firstPath);
            } else if (chosen == copyAct) {
                ContextMenuHelper::copyToClipboard(firstPath);
            }
        dl_menu_done:
            menu->deleteLater();
        });

        m_sectionBodyLayout->addWidget(cardFrame);
    }

    updateEmptyState();
}
