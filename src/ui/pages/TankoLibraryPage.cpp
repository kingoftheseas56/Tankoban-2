#include "TankoLibraryPage.h"
#include "tankolibrary/BookResultsGrid.h"

#include "core/CoreBridge.h"
#include "core/book/BookScraper.h"
#include "core/book/AnnaArchiveScraper.h"
#include "core/book/LibGenScraper.h"
#include "core/book/BookDownloader.h"

#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QProgressBar>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>

#include <climits>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QFrame>
#include <QStackedWidget>
#include <QScrollArea>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QPixmap>
#include <QImage>
#include <QUrl>

#include <memory>

namespace {

// Small helpers to keep buildDetailPage() readable.

QLabel* makeTitleLabel(QWidget* parent)
{
    auto* l = new QLabel(parent);
    l->setWordWrap(true);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->setStyleSheet(QStringLiteral(
        "font-size: 22px; font-weight: bold; color: #ddd;"
        " background: transparent; border: none;"));
    return l;
}

QLabel* makeAuthorLabel(QWidget* parent)
{
    auto* l = new QLabel(parent);
    l->setWordWrap(true);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->setStyleSheet(QStringLiteral(
        "font-size: 14px; color: #c7a76b;"
        " background: transparent; border: none;"));
    return l;
}

QLabel* makeValueLabel(QWidget* parent)
{
    auto* l = new QLabel(parent);
    l->setWordWrap(true);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->setStyleSheet(QStringLiteral(
        "font-size: 13px; color: #ccc;"
        " background: transparent; border: none;"));
    return l;
}

QLabel* makeDescLabel(QWidget* parent)
{
    auto* l = new QLabel(parent);
    l->setWordWrap(true);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->setStyleSheet(QStringLiteral(
        "font-size: 13px; color: #bbb; line-height: 1.5;"
        " background: transparent; border: none;"));
    l->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    return l;
}

QLabel* makeStatusLabel(QWidget* parent)
{
    auto* l = new QLabel(parent);
    l->setStyleSheet(QStringLiteral(
        "font-size: 12px; color: #888;"
        " background: transparent; border: none;"));
    return l;
}

void setLabelValue(QLabel* label, const QString& value, QWidget* rowAnchor)
{
    // Hide entire row when empty so we don't render "Publisher: " with no value.
    // rowAnchor is the label's form-row partner (the "Publisher" label on the
    // left side); hide/show in lockstep.
    const bool visible = !value.isEmpty();
    label->setText(value);
    label->setVisible(visible);
    if (rowAnchor) rowAnchor->setVisible(visible);
}

QString tankoLibraryCoverCacheDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
        + QStringLiteral("/tankolibrary-covers");
}

QString extensionFromCoverUrl(const QString& url)
{
    const QString suffix = QFileInfo(QUrl(url).path()).suffix().toLower();
    if (suffix == QLatin1String("jpg") ||
        suffix == QLatin1String("jpeg") ||
        suffix == QLatin1String("png") ||
        suffix == QLatin1String("webp")) {
        return suffix;
    }
    return QStringLiteral("jpg");
}

QString existingCachedCoverPath(const QString& md5)
{
    if (md5.isEmpty()) return QString();

    const QString dirPath = tankoLibraryCoverCacheDir();
    const QFileInfoList matches = QDir(dirPath).entryInfoList(
        QStringList() << QStringLiteral("%1.*").arg(md5),
        QDir::Files | QDir::Readable,
        QDir::Name);
    if (matches.isEmpty()) return QString();
    return matches.first().absoluteFilePath();
}

QString cachedCoverPathForUrl(const QString& md5, const QString& url)
{
    if (md5.isEmpty()) return QString();
    return tankoLibraryCoverCacheDir()
        + QStringLiteral("/%1.%2").arg(md5, extensionFromCoverUrl(url));
}

void paintCoverPixmap(QLabel* target, const QPixmap& pixmap)
{
    if (!target || pixmap.isNull()) return;
    target->setPixmap(pixmap.scaled(target->size(),
                                    Qt::KeepAspectRatio,
                                    Qt::SmoothTransformation));
    target->setText(QString());
}

} // namespace

TankoLibraryPage::TankoLibraryPage(CoreBridge* bridge, QWidget* parent)
    : QWidget(parent), m_bridge(bridge)
{
    qRegisterMetaType<BookResult>();
    qRegisterMetaType<QList<BookResult>>();

    m_nam = new QNetworkAccessManager(this);

    // Track B decision 2026-04-22: AA was wired in M2.3 (search path works
    // captcha-free) but its /books/ detail + /ads.php download paths are
    // blocked by a Turnstile-class captcha (M2.2 finding). Hemanth rules
    // out captcha-solving, so AA rows show up but can't complete a
    // download — pure noise. Removed from default dispatch here. The
    // AnnaArchiveScraper class stays compiled + registered in CMakeLists;
    // re-enable by uncommenting the second push below when Hemanth OKs
    // a visible-webview modal flow OR when AA drops their captcha.
    m_scrapers << new LibGenScraper(m_nam, this);
    // m_scrapers << new AnnaArchiveScraper(m_nam, this);   // DISABLED — captcha-blocked

    buildUI();

    // Per-scraper signal wiring. Use lambdas to capture which scraper
    // emitted so the shared slot can track per-source completion state.
    for (BookScraper* s : m_scrapers) {
        connect(s, &BookScraper::searchFinished, this,
            [this, s](const QList<BookResult>& results) {
                if (!m_searchInFlight) return;
                m_searchCountBySource[s->sourceId()] = results.size();
                m_results.append(results);
                applyClientFilter();                 // Track B — honor EPUB-only toggle
                if (--m_searchesPending <= 0) {
                    m_searchInFlight = false;
                    m_searchBtn->setVisible(true);
                    m_cancelBtn->setVisible(false);
                }
                refreshSearchStatus();
            });

        connect(s, &BookScraper::errorOccurred, this,
            [this, s](const QString& msg) {
                // Only handle as a search error if we're in the search path.
                // Detail errors are caught by the one-shot connection in
                // onResultActivated.
                if (!m_searchInFlight) return;
                m_searchErrorBySource[s->sourceId()] = msg;
                if (--m_searchesPending <= 0) {
                    m_searchInFlight = false;
                    m_searchBtn->setVisible(true);
                    m_cancelBtn->setVisible(false);
                }
                refreshSearchStatus();
            });

        // M2.4 — scraper resolve signals route into the download flow, NOT
        // the old scaffold slot. onScraperUrlsReady kicks BookDownloader;
        // onScraperResolveFailed shows red error + re-arms the button.
        connect(s, &BookScraper::downloadResolved,
                this, &TankoLibraryPage::onScraperUrlsReady);
        connect(s, &BookScraper::downloadFailed,
                this, &TankoLibraryPage::onScraperResolveFailed);

        if (auto* libgen = qobject_cast<LibGenScraper*>(s)) {
            connect(libgen, &LibGenScraper::coverUrlReady, this,
                [this](const QString& md5, const QString& absoluteUrl) {
                    const QString normalizedMd5 = md5.trimmed().toLower();
                    const QString normalizedUrl = absoluteUrl.trimmed();
                    if (normalizedMd5.isEmpty() || normalizedUrl.isEmpty()) return;

                    for (BookResult& cached : m_results) {
                        if (cached.md5.compare(normalizedMd5, Qt::CaseInsensitive) == 0) {
                            cached.coverUrl = normalizedUrl;
                        }
                    }
                    if (m_selectedResult.md5.compare(normalizedMd5, Qt::CaseInsensitive) == 0 &&
                        m_selectedResult.coverUrl != normalizedUrl) {
                        m_selectedResult.coverUrl = normalizedUrl;
                        loadDetailCover(normalizedUrl);
                    }
                });

            connect(libgen, &LibGenScraper::coverUrlFailed, this,
                [](const QString&, const QString&) {
                    // Missing/failing covers are ornamental-only. Leave the
                    // placeholder in place with no user-facing error.
                });
        }
    }

    // M2.4 — BookDownloader lives here; constructed once, reused across
    // downloads. Connect its signals to the download-flow slots.
    m_downloader = new BookDownloader(m_nam, this);
    connect(m_downloader, &BookDownloader::downloadProgress,
            this, &TankoLibraryPage::onDownloaderProgress);
    connect(m_downloader, &BookDownloader::downloadComplete,
            this, &TankoLibraryPage::onDownloaderComplete);
    connect(m_downloader, &BookDownloader::downloadFailed,
            this, &TankoLibraryPage::onDownloaderFailed);

    connect(m_grid, &BookResultsGrid::resultActivated,
            this, &TankoLibraryPage::onResultActivated);
}

BookScraper* TankoLibraryPage::scraperFor(const QString& sourceId) const
{
    for (BookScraper* s : m_scrapers) {
        if (s->sourceId() == sourceId) return s;
    }
    return nullptr;
}

// ── UI builders ─────────────────────────────────────────────────────────────

void TankoLibraryPage::buildUI()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_stack = new QStackedWidget(this);

    buildResultsPage();
    buildDetailPage();

    m_stack->addWidget(m_resultsPage);   // index 0
    m_stack->addWidget(m_detailPage);    // index 1
    m_stack->setCurrentWidget(m_resultsPage);

    root->addWidget(m_stack);
}

void TankoLibraryPage::buildResultsPage()
{
    m_resultsPage = new QWidget(this);
    auto* outer = new QVBoxLayout(m_resultsPage);
    outer->setContentsMargins(12, 12, 12, 12);
    outer->setSpacing(8);

    // Search row
    auto* searchRow = new QHBoxLayout;
    searchRow->setSpacing(8);

    m_queryEdit = new QLineEdit(m_resultsPage);
    m_queryEdit->setPlaceholderText(QStringLiteral(
        "Search Anna's Archive - e.g. \"sapiens\" or \"orwell 1984\""));
    m_queryEdit->setMinimumWidth(320);
    connect(m_queryEdit, &QLineEdit::returnPressed, this, &TankoLibraryPage::startSearch);
    searchRow->addWidget(m_queryEdit, 1);

    m_searchBtn = new QPushButton(QStringLiteral("Search"), m_resultsPage);
    m_searchBtn->setMinimumWidth(90);
    m_searchBtn->setCursor(Qt::PointingHandCursor);
    connect(m_searchBtn, &QPushButton::clicked, this, &TankoLibraryPage::startSearch);
    searchRow->addWidget(m_searchBtn);

    m_cancelBtn = new QPushButton(QStringLiteral("Cancel"), m_resultsPage);
    m_cancelBtn->setMinimumWidth(90);
    m_cancelBtn->setCursor(Qt::PointingHandCursor);
    m_cancelBtn->setVisible(false);
    connect(m_cancelBtn, &QPushButton::clicked, this, &TankoLibraryPage::cancelSearch);
    searchRow->addWidget(m_cancelBtn);

    // Track B — "EPUB only" client-side format filter. Default ON per
    // Hemanth's "I only need epub" preference. Persisted to QSettings so
    // the toggle survives Tankoban restarts. Filter operates over cached
    // m_results — no re-network on toggle.
    m_epubOnlyCheckbox = new QCheckBox(QStringLiteral("EPUB only"), m_resultsPage);
    m_epubOnlyCheckbox->setCursor(Qt::PointingHandCursor);
    m_epubOnlyCheckbox->setToolTip(QStringLiteral(
        "Show only EPUB — uncheck to see PDF, FB2, MOBI, and other book formats"));
    const bool epubOnlyDefault = QSettings()
        .value(QStringLiteral("tankolibrary/epub_only"), true).toBool();
    m_epubOnlyCheckbox->setChecked(epubOnlyDefault);
    connect(m_epubOnlyCheckbox, &QCheckBox::toggled,
            this, &TankoLibraryPage::onEpubOnlyToggled);
    searchRow->addWidget(m_epubOnlyCheckbox);

    outer->addLayout(searchRow);

    m_statusLbl = new QLabel(QStringLiteral("Ready. Type a query and hit Enter."), m_resultsPage);
    m_statusLbl->setStyleSheet(QStringLiteral(
        "color: #888; font-size: 12px; background: transparent; border: none;"));
    outer->addWidget(m_statusLbl);

    m_grid = new BookResultsGrid(m_resultsPage);
    outer->addWidget(m_grid, 1);
}

void TankoLibraryPage::buildDetailPage()
{
    m_detailPage = new QWidget(this);
    auto* outer = new QVBoxLayout(m_detailPage);
    outer->setContentsMargins(12, 12, 12, 12);
    outer->setSpacing(12);

    // Back row
    auto* backRow = new QHBoxLayout;
    m_detailBackBtn = new QPushButton(QStringLiteral("←  Back to results"), m_detailPage);
    m_detailBackBtn->setCursor(Qt::PointingHandCursor);
    m_detailBackBtn->setFixedHeight(28);
    m_detailBackBtn->setStyleSheet(QStringLiteral(
        "QPushButton { color: #c7a76b; background: transparent; border: none;"
        " font-size: 13px; padding: 2px 8px; }"
        "QPushButton:hover { text-decoration: underline; }"));
    connect(m_detailBackBtn, &QPushButton::clicked, this, &TankoLibraryPage::showResultsPage);
    backRow->addWidget(m_detailBackBtn);
    backRow->addStretch(1);
    outer->addLayout(backRow);

    // Top split: cover on left, metadata column on right
    auto* topSplit = new QHBoxLayout;
    topSplit->setSpacing(16);
    topSplit->setAlignment(Qt::AlignTop);

    m_detailCover = new QLabel(m_detailPage);
    m_detailCover->setFixedSize(160, 240);
    m_detailCover->setAlignment(Qt::AlignCenter);
    m_detailCover->setStyleSheet(QStringLiteral(
        "QLabel { background: #222; border: 1px solid #333; color: #555;"
        " font-size: 11px; }"));
    m_detailCover->setText(QStringLiteral("cover"));
    topSplit->addWidget(m_detailCover, 0, Qt::AlignTop);

    auto* rightCol = new QVBoxLayout;
    rightCol->setSpacing(6);

    m_detailTitle  = makeTitleLabel(m_detailPage);
    m_detailAuthor = makeAuthorLabel(m_detailPage);
    rightCol->addWidget(m_detailTitle);
    rightCol->addWidget(m_detailAuthor);

    // Form layout for label/value rows
    auto* form = new QFormLayout;
    form->setContentsMargins(0, 8, 0, 0);
    form->setHorizontalSpacing(16);
    form->setVerticalSpacing(4);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignTop);

    auto addRow = [this, form, &rightCol](const QString& name, QLabel*& valueSlot)
    {
        auto* key = new QLabel(name, m_detailPage);
        key->setStyleSheet(QStringLiteral(
            "font-size: 12px; color: #888; background: transparent; border: none;"));
        key->setMinimumWidth(90);
        valueSlot = makeValueLabel(m_detailPage);
        // Hook key → value pairing onto a dynamic property so setLabelValue()
        // can toggle row visibility as a unit. Simpler + cheaper than a map.
        valueSlot->setProperty("rowLabel", QVariant::fromValue<QObject*>(key));
        form->addRow(key, valueSlot);
        Q_UNUSED(rightCol);
    };

    addRow(QStringLiteral("Publisher"), m_detailPublisher);
    addRow(QStringLiteral("Year"),      m_detailYear);
    addRow(QStringLiteral("Pages"),     m_detailPages);
    addRow(QStringLiteral("Language"),  m_detailLanguage);
    addRow(QStringLiteral("Format"),    m_detailFormat);
    addRow(QStringLiteral("Size"),      m_detailSize);
    addRow(QStringLiteral("ISBN"),      m_detailIsbn);

    rightCol->addLayout(form);
    rightCol->addStretch(1);

    topSplit->addLayout(rightCol, 1);
    outer->addLayout(topSplit);

    // Description (below the split)
    m_detailDescription = makeDescLabel(m_detailPage);
    m_detailDescription->setMinimumHeight(60);
    outer->addWidget(m_detailDescription);

    // M2.4 — real Download button + progress bar. Replaces the M2.2
    // scaffold-button footprint. Click kicks off scraper.resolveDownload(),
    // progress bar appears while BookDownloader streams the file, and on
    // complete the file lands at rootFolders("books").first() and BooksPage
    // auto-rescans via notifyRootFoldersChanged("books").
    {
        auto* row = new QHBoxLayout();
        row->setContentsMargins(0, 4, 0, 4);
        m_downloadButton = new QPushButton(QStringLiteral("Download"), m_detailPage);
        m_downloadButton->setEnabled(false);
        m_downloadButton->setMinimumWidth(120);
        m_downloadButton->setStyleSheet(QStringLiteral(
            "QPushButton {"
            "  padding: 6px 14px;"
            "  background: #2a2a2a; color: #ddd;"
            "  border: 1px solid #555; border-radius: 3px;"
            "  font-weight: 500;"
            "}"
            "QPushButton:hover:!disabled { background: #333; border-color: #888; }"
            "QPushButton:pressed:!disabled { background: #222; }"
            "QPushButton:disabled { color: #666; background: #1e1e1e; border-color: #2c2c2c; }"
        ));
        connect(m_downloadButton, &QPushButton::clicked,
                this, &TankoLibraryPage::onDownloadClicked);

        m_downloadProgress = new QProgressBar(m_detailPage);
        m_downloadProgress->setRange(0, 0);            // indeterminate by default
        m_downloadProgress->setFixedHeight(14);
        m_downloadProgress->setTextVisible(false);
        m_downloadProgress->setVisible(false);
        m_downloadProgress->setStyleSheet(QStringLiteral(
            "QProgressBar {"
            "  background: #1a1a1a;"
            "  border: 1px solid #333; border-radius: 2px;"
            "}"
            "QProgressBar::chunk { background: #c7a76b; }"
        ));

        row->addWidget(m_downloadButton);
        row->addSpacing(12);
        row->addWidget(m_downloadProgress, 1);
        outer->addLayout(row);
    }

    // Status line
    m_detailStatus = makeStatusLabel(m_detailPage);
    outer->addWidget(m_detailStatus);

    outer->addStretch(1);
}

// ── Search flow (M2.3 multi-source dispatch) ────────────────────────────────

void TankoLibraryPage::startSearch()
{
    const QString query = m_queryEdit->text().trimmed();
    if (query.isEmpty()) {
        m_statusLbl->setText(QStringLiteral("Type a query first."));
        return;
    }
    if (m_searchInFlight) {
        return;
    }

    m_searchInFlight  = true;
    m_searchesPending = m_scrapers.size();
    m_searchCountBySource.clear();
    m_searchErrorBySource.clear();

    m_searchBtn->setVisible(false);
    m_cancelBtn->setVisible(true);
    m_grid->clearResults();
    m_results.clear();
    m_statusLbl->setText(QStringLiteral("Searching..."));

    for (BookScraper* s : m_scrapers) {
        s->search(query, 30);
    }
}

void TankoLibraryPage::cancelSearch()
{
    if (!m_searchInFlight) return;
    m_searchInFlight  = false;
    m_searchesPending = 0;
    m_searchBtn->setVisible(true);
    m_cancelBtn->setVisible(false);
    m_statusLbl->setText(QStringLiteral("Cancelled."));
}

void TankoLibraryPage::refreshSearchStatus()
{
    // Build an honest per-source status line:
    //   "Searching... (LibGen: 20)"   while other scrapers still in flight
    //   "Done: 20 from LibGen, 0 from Anna's Archive (timeout)"   when all finished
    QStringList parts;
    for (BookScraper* s : m_scrapers) {
        const QString id = s->sourceId();
        const QString display = s->sourceName();
        if (m_searchErrorBySource.contains(id)) {
            parts << QString(QStringLiteral("%1: %2"))
                     .arg(display, m_searchErrorBySource.value(id));
        } else if (m_searchCountBySource.contains(id)) {
            parts << QString(QStringLiteral("%1 from %2"))
                     .arg(m_searchCountBySource.value(id))
                     .arg(display);
        } else {
            parts << QString(QStringLiteral("%1: searching...")).arg(display);
        }
    }

    const QString prefix = m_searchInFlight
        ? QStringLiteral("Searching... (")
        : QStringLiteral("Done: ");
    const QString suffix = m_searchInFlight
        ? QStringLiteral(")")
        : QString();

    // Track B — append "(N shown)" suffix when the EPUB-only client
    // filter narrows the visible subset. Honest about cached vs visible.
    QString filterSuffix;
    if (m_epubOnlyCheckbox && m_epubOnlyCheckbox->isChecked() && !m_searchInFlight) {
        int shown = 0;
        for (const BookResult& r : m_results) {
            if (r.format.compare(QStringLiteral("epub"), Qt::CaseInsensitive) == 0) ++shown;
        }
        if (shown != m_results.size()) {
            filterSuffix = QString(QStringLiteral(" (%1 shown)")).arg(shown);
        }
    }

    m_statusLbl->setText(prefix + parts.join(QStringLiteral(", ")) + suffix + filterSuffix);
}

// ── Track B client-side filter ──────────────────────────────────────────────

void TankoLibraryPage::onEpubOnlyToggled(bool checked)
{
    QSettings().setValue(QStringLiteral("tankolibrary/epub_only"), checked);
    applyClientFilter();
    refreshSearchStatus();
}

void TankoLibraryPage::applyClientFilter()
{
    if (!m_grid) return;
    m_grid->setResults(filteredResults());
}

QList<BookResult> TankoLibraryPage::filteredResults() const
{
    if (!m_epubOnlyCheckbox || !m_epubOnlyCheckbox->isChecked()) {
        // No filter active — return full cached set.
        return m_results;
    }
    // EPUB-only: filter cached m_results to EPUB format. Case-insensitive
    // match in case a scraper ever returns "EPUB" vs "epub" — current
    // LibGenScraper lowercases but defend against future drift.
    QList<BookResult> filtered;
    filtered.reserve(m_results.size());
    for (const BookResult& r : m_results) {
        if (r.format.compare(QStringLiteral("epub"), Qt::CaseInsensitive) == 0) {
            filtered.append(r);
        }
    }
    return filtered;
}

// ── Detail flow (M2.1) ──────────────────────────────────────────────────────

void TankoLibraryPage::onResultActivated(int row)
{
    // Track B — grid indexes into the FILTERED view, not the raw cache,
    // so m_results[row] would be wrong whenever the EPUB-only toggle is
    // active. Use filteredResults() to map the row back correctly.
    const QList<BookResult> view = filteredResults();
    if (row < 0 || row >= view.size()) return;

    m_selectedResult = view[row];
    showDetailFor(m_selectedResult);

    // M2.3 — route fetchDetail to the specific scraper that owns this row's
    // source. Previously hard-coded to m_aaScraper; now honors the source
    // column so LibGen rows dispatch to LibGenScraper + AA rows to AA.
    BookScraper* scraper = scraperFor(m_selectedResult.source);
    if (!scraper) {
        onDetailError(QString(QStringLiteral("No scraper registered for source '%1'"))
                      .arg(m_selectedResult.source));
        return;
    }

    // Shared one-shot connections — whichever of detailReady / errorOccurred
    // fires first disconnects both so the second never lands after we've
    // moved on. Targets ONLY the active scraper so the other source's
    // in-flight search signals can't cross-fire into detail flow.
    auto conn    = std::make_shared<QMetaObject::Connection>();
    auto errConn = std::make_shared<QMetaObject::Connection>();

    *conn = connect(scraper, &BookScraper::detailReady, this,
        [this, conn, errConn](const BookResult& d) {
            disconnect(*conn);
            disconnect(*errConn);
            onDetailReady(d);
        });
    *errConn = connect(scraper, &BookScraper::errorOccurred, this,
        [this, conn, errConn](const QString& msg) {
            // Guard against search-path errors firing here (lambdas in
            // constructor check m_searchInFlight; here we're past it).
            if (m_searchInFlight) return;
            disconnect(*conn);
            disconnect(*errConn);
            onDetailError(msg);
        });

    scraper->fetchDetail(m_selectedResult.md5);
}

void TankoLibraryPage::showDetailFor(const BookResult& r)
{
    m_stack->setCurrentWidget(m_detailPage);
    paintDetail(r);
    m_detailStatus->setStyleSheet(QStringLiteral(
        "font-size: 12px; color: #888; background: transparent; border: none;"));
    BookScraper* src = scraperFor(r.source);
    const QString sourceName = src ? src->sourceName() : r.source;
    m_detailStatus->setText(QString(QStringLiteral("Fetching detail from %1..."))
                            .arg(sourceName));

    // M2.4 — reset download UI to Idle on new row-selection. The button
    // enables immediately since we have m_selectedResult.md5 (all the
    // download flow needs); scraper resolution happens at click time.
    // Stage reset handles the case where user navigates to a new row while
    // a prior download was still displaying complete/failed status.
    resetDownloadUiToIdle();

    if (!r.coverUrl.isEmpty()) {
        loadDetailCover(r.coverUrl);
    } else {
        m_detailCover->setPixmap(QPixmap());
        m_detailCover->setText(QStringLiteral("cover"));
        if (auto* libgen = qobject_cast<LibGenScraper*>(src)) {
            const QString md5 = r.md5.trimmed().toLower();
            if (!md5.isEmpty()) {
                libgen->fetchCoverUrl(md5);
            }
        }
    }
}

void TankoLibraryPage::showResultsPage()
{
    if (m_coverReply) {
        m_coverReply->abort();
        m_coverReply->deleteLater();
        m_coverReply = nullptr;
    }
    m_stack->setCurrentWidget(m_resultsPage);
}

void TankoLibraryPage::paintDetail(const BookResult& r)
{
    // Use BookResult.author string even when empty — label hides itself when
    // value is empty via setLabelValue's row-visibility toggle below.
    m_detailTitle->setText(r.title);
    m_detailAuthor->setText(r.author.isEmpty()
        ? QString()
        : QString(QStringLiteral("by %1")).arg(r.author));
    m_detailAuthor->setVisible(!r.author.isEmpty());

    auto hideRow = [](QLabel* valueLabel, const QString& value) {
        auto* keyObj = valueLabel->property("rowLabel").value<QObject*>();
        auto* keyLabel = qobject_cast<QLabel*>(keyObj);
        setLabelValue(valueLabel, value, keyLabel);
    };

    hideRow(m_detailPublisher, r.publisher);
    hideRow(m_detailYear,      r.year);
    hideRow(m_detailPages,     r.pages);
    hideRow(m_detailLanguage,  r.language);
    hideRow(m_detailFormat,    r.format.toUpper());
    hideRow(m_detailSize,      r.fileSize);
    hideRow(m_detailIsbn,      r.isbn);

    m_detailDescription->setText(r.description);
    m_detailDescription->setVisible(!r.description.isEmpty());
}

void TankoLibraryPage::onDetailReady(const BookResult& detail)
{
    // Merge: detail non-empty field wins; empty detail field falls back to
    // search-row snapshot. Honest-missing-beats-wrong-value policy preserved.
    const QString previousCoverUrl = m_selectedResult.coverUrl;
    BookResult merged = m_selectedResult;
    auto keepOrSet = [](QString& out, const QString& fromDetail) {
        if (!fromDetail.isEmpty()) out = fromDetail;
    };
    keepOrSet(merged.title,       detail.title);
    keepOrSet(merged.author,      detail.author);
    keepOrSet(merged.publisher,   detail.publisher);
    keepOrSet(merged.year,        detail.year);
    keepOrSet(merged.format,      detail.format);
    keepOrSet(merged.fileSize,    detail.fileSize);
    keepOrSet(merged.language,    detail.language);
    keepOrSet(merged.pages,       detail.pages);
    keepOrSet(merged.isbn,        detail.isbn);
    keepOrSet(merged.description, detail.description);
    keepOrSet(merged.coverUrl,    detail.coverUrl);
    keepOrSet(merged.detailUrl,   detail.detailUrl);

    m_selectedResult = merged;
    for (BookResult& cached : m_results) {
        if (cached.source == merged.source &&
            cached.md5.compare(merged.md5, Qt::CaseInsensitive) == 0) {
            cached = merged;
            break;
        }
    }

    paintDetail(merged);
    m_detailStatus->clear();

    // Button already enabled at showDetailFor entry; nothing to toggle here.

    // Re-fetch cover if URL changed from the search-row value.
    if (!merged.coverUrl.isEmpty() && merged.coverUrl != previousCoverUrl) {
        loadDetailCover(merged.coverUrl);
    }
}

void TankoLibraryPage::onDetailError(const QString& message)
{
    // Keep the instant-paint snapshot visible; just show the error inline.
    m_detailStatus->setStyleSheet(QStringLiteral(
        "font-size: 12px; color: #c07; background: transparent; border: none;"));
    m_detailStatus->setText(QString(QStringLiteral("Could not load full detail: %1"))
                            .arg(message));
}

void TankoLibraryPage::loadDetailCover(const QString& url)
{
    if (m_coverReply) {
        m_coverReply->abort();
        m_coverReply->deleteLater();
        m_coverReply = nullptr;
    }

    const QUrl target(url);
    if (!target.isValid()) return;

    const QString md5 = m_selectedResult.md5.trimmed().toLower();
    const QString cachedPath = existingCachedCoverPath(md5);
    if (!cachedPath.isEmpty()) {
        QPixmap pix;
        if (pix.load(cachedPath) && !pix.isNull()) {
            paintCoverPixmap(m_detailCover, pix);
            return;
        }
        QFile::remove(cachedPath);
    }

    const QString cachePath = cachedCoverPathForUrl(md5, url);
    if (!cachePath.isEmpty()) {
        QDir().mkpath(QFileInfo(cachePath).absolutePath());
    }

    QNetworkRequest req(target);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64)"
                                 " AppleWebKit/537.36"));
    req.setTransferTimeout(10000);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    m_coverReply = m_nam->get(req);

    connect(m_coverReply, &QNetworkReply::finished, this, [this, cachePath]() {
        QNetworkReply* reply = m_coverReply;
        if (!reply) return;

        if (reply->error() == QNetworkReply::NoError) {
            const QByteArray bytes = reply->readAll();
            QImage img;
            if (img.loadFromData(bytes) && !img.isNull()) {
                if (!cachePath.isEmpty()) {
                    QFile out(cachePath);
                    if (out.open(QIODevice::WriteOnly)) {
                        out.write(bytes);
                        out.close();
                    }
                }
                paintCoverPixmap(m_detailCover, QPixmap::fromImage(img));
            }
        }
        // On failure (aborted, network, invalid image), leave the placeholder.

        reply->deleteLater();
        if (m_coverReply == reply) m_coverReply = nullptr;
    });
}

// ── Download flow (M2.4 — scraper.resolveDownload → BookDownloader → disk) ──

void TankoLibraryPage::resetDownloadUiToIdle()
{
    m_downloadStage = DownloadStage::Idle;
    if (m_downloadButton) {
        m_downloadButton->setEnabled(!m_selectedResult.md5.isEmpty());
        m_downloadButton->setText(QStringLiteral("Download"));
    }
    if (m_downloadProgress) {
        m_downloadProgress->setRange(0, 0);
        m_downloadProgress->setVisible(false);
    }
}

void TankoLibraryPage::onDownloadClicked()
{
    // Cancel-during-download: the same button becomes a cancel affordance
    // while the downloader is streaming. Clicking it aborts + cleans up.
    if (m_downloadStage == DownloadStage::Downloading) {
        if (m_downloader) m_downloader->cancelDownload(m_selectedResult.md5);
        return;
    }

    if (m_selectedResult.md5.isEmpty()) {
        m_detailStatus->setStyleSheet(QStringLiteral(
            "font-size: 12px; color: #c07; background: transparent; border: none;"));
        m_detailStatus->setText(QStringLiteral("No book selected."));
        return;
    }

    BookScraper* scraper = scraperFor(m_selectedResult.source);
    if (!scraper) {
        m_detailStatus->setStyleSheet(QStringLiteral(
            "font-size: 12px; color: #c07; background: transparent; border: none;"));
        m_detailStatus->setText(QString(QStringLiteral(
            "No scraper registered for source '%1'.")).arg(m_selectedResult.source));
        return;
    }

    // Check library path BEFORE kicking off network — honest-fast-fail if
    // nothing's configured.
    const QStringList bookRoots = m_bridge ? m_bridge->rootFolders(QStringLiteral("books"))
                                           : QStringList();
    if (bookRoots.isEmpty()) {
        m_detailStatus->setStyleSheet(QStringLiteral(
            "font-size: 12px; color: #c07; background: transparent; border: none;"));
        m_detailStatus->setText(QStringLiteral(
            "No books library path configured. Set one in Settings → Libraries first."));
        return;
    }

    m_downloadStage = DownloadStage::Resolving;
    m_downloadButton->setEnabled(false);
    m_downloadButton->setText(QStringLiteral("Downloading..."));
    m_downloadProgress->setRange(0, 0);  // indeterminate while resolving
    m_downloadProgress->setVisible(true);

    m_detailStatus->setStyleSheet(QStringLiteral(
        "font-size: 12px; color: #888; background: transparent; border: none;"));
    m_detailStatus->setText(QString(QStringLiteral("Resolving download URL from %1..."))
                            .arg(scraper->sourceName()));

    scraper->resolveDownload(m_selectedResult.md5);
}

void TankoLibraryPage::onScraperUrlsReady(const QString& md5, const QStringList& urls)
{
    // Stale-guard — ignore resolves that aren't for the selected row.
    if (md5 != m_selectedResult.md5) return;
    if (m_downloadStage != DownloadStage::Resolving) return;
    if (!m_downloader) return;

    if (urls.isEmpty()) {
        onScraperResolveFailed(md5, QStringLiteral("scraper returned empty URL list"));
        return;
    }

    // Pick destination from library config + derive a sanitized filename
    // from BookResult.title + .format.
    const QStringList bookRoots = m_bridge ? m_bridge->rootFolders(QStringLiteral("books"))
                                           : QStringList();
    if (bookRoots.isEmpty()) {
        onScraperResolveFailed(md5, QStringLiteral(
            "books library path is empty — can't choose a download destination"));
        return;
    }
    const QString destDir = bookRoots.first();

    // Build the suggested filename. BookResult.format is lowercased ext
    // (e.g. "epub", "pdf", "cbz"). If empty, fallback to ".bin".
    const QString title = m_selectedResult.title.isEmpty()
        ? QStringLiteral("download")
        : m_selectedResult.title;
    const QString ext = m_selectedResult.format.isEmpty()
        ? QStringLiteral("bin")
        : m_selectedResult.format.toLower();
    const QString suggestedName = QString(QStringLiteral("%1.%2")).arg(title, ext);

    // Parse expected size (best-effort — LibGen gives "194 kB" / "52 MB").
    qint64 expectedBytes = 0;
    {
        const QString raw = m_selectedResult.fileSize.trimmed();
        static const QRegularExpression kSizeRe(
            QStringLiteral(R"(^\s*([0-9.]+)\s*(KB|MB|GB|B|kB|kb|mb|gb)\s*$)"));
        const auto m = kSizeRe.match(raw);
        if (m.hasMatch()) {
            const double value = m.captured(1).toDouble();
            const QString unit = m.captured(2).toUpper();
            double multiplier = 1.0;
            if      (unit == QLatin1String("B"))  multiplier = 1.0;
            else if (unit == QLatin1String("KB")) multiplier = 1024.0;
            else if (unit == QLatin1String("MB")) multiplier = 1024.0 * 1024.0;
            else if (unit == QLatin1String("GB")) multiplier = 1024.0 * 1024.0 * 1024.0;
            expectedBytes = qint64(value * multiplier);
        }
    }

    m_downloadStage = DownloadStage::Downloading;
    m_downloadButton->setText(QStringLiteral("Cancel"));
    m_downloadButton->setEnabled(true);
    // Switch to determinate range once we get the first progress tick with
    // a known total. For now, stay indeterminate.
    m_downloadProgress->setRange(0, 0);

    m_detailStatus->setStyleSheet(QStringLiteral(
        "font-size: 12px; color: #888; background: transparent; border: none;"));
    m_detailStatus->setText(QString(QStringLiteral("Downloading %1..."))
                            .arg(suggestedName));

    m_downloader->startDownload(md5, urls, destDir, suggestedName, expectedBytes);
}

void TankoLibraryPage::onScraperResolveFailed(const QString& md5, const QString& reason)
{
    if (md5 != m_selectedResult.md5) return;
    if (m_downloadStage != DownloadStage::Resolving) return;

    m_detailStatus->setStyleSheet(QStringLiteral(
        "font-size: 12px; color: #c07; background: transparent; border: none;"));
    m_detailStatus->setText(QString(QStringLiteral(
        "Download failed (resolve): %1")).arg(reason));
    resetDownloadUiToIdle();
}

void TankoLibraryPage::onDownloaderProgress(const QString& md5,
                                            qint64 received, qint64 total)
{
    if (md5 != m_selectedResult.md5) return;
    if (m_downloadStage != DownloadStage::Downloading) return;
    if (!m_downloadProgress) return;

    if (total > 0) {
        // Switch to determinate progress. Cast to int — for 2GB+ files the
        // QProgressBar int range would overflow, but practical books are
        // under 500 MB. Scale to percent for safety if total exceeds INT_MAX.
        if (total > INT_MAX) {
            const int percent = static_cast<int>((received * 100) / total);
            m_downloadProgress->setRange(0, 100);
            m_downloadProgress->setValue(percent);
        } else {
            m_downloadProgress->setRange(0, static_cast<int>(total));
            m_downloadProgress->setValue(static_cast<int>(received));
        }
    }
    // If total is unknown (-1 or 0), keep indeterminate — no way to show %.
}

void TankoLibraryPage::onDownloaderComplete(const QString& md5,
                                            const QString& filePath)
{
    if (md5 != m_selectedResult.md5) return;

    const QString basename = QFileInfo(filePath).fileName();
    m_detailStatus->setStyleSheet(QStringLiteral(
        "font-size: 12px; color: #8c8; background: transparent; border: none;"));
    m_detailStatus->setText(QString(QStringLiteral("Downloaded: %1")).arg(basename));

    resetDownloadUiToIdle();

    // Fire the rescan trigger — BooksPage has a persistent signal
    // connection to CoreBridge::rootFoldersChanged that calls its own
    // triggerScan() when domain == "books". Our new file shows up on the
    // Books tab after this rescan completes.
    if (m_bridge) {
        m_bridge->notifyRootFoldersChanged(QStringLiteral("books"));
    }
}

void TankoLibraryPage::onDownloaderFailed(const QString& md5,
                                          const QString& reason)
{
    if (md5 != m_selectedResult.md5) return;

    m_detailStatus->setStyleSheet(QStringLiteral(
        "font-size: 12px; color: #c07; background: transparent; border: none;"));
    m_detailStatus->setText(QString(QStringLiteral("Download failed: %1")).arg(reason));

    resetDownloadUiToIdle();
}
