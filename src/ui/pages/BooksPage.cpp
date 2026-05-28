#include "BooksPage.h"
#include "TileStrip.h"
#include "TileCard.h"
#include "books/BookCatalogueDetailView.h"
#include "books/BookCatalogueSearchWidget.h"
#include "books/BookSeriesDetailView.h"
#include "core/CoreBridge.h"
#include "core/ScannerUtils.h"
#include "core/book/BookCatalogueAggregator.h"
#include "core/book/BookCatalogueResult.h"
#include "core/book/BookSeriesIndex.h"
#include "core/book/BookSeriesIndexBuilder.h"
#include "core/book/FictionDbClient.h"
#include "core/book/BookDownloader.h"
#include "core/book/BooksCatalogueLibraryStore.h"
#include "core/book/CatalogueRecord.h"

#include "ui/ContextMenuHelper.h"
#include "ui/MainWindow.h"
#include "ui/readers/BookBridge.h"
#include "ui/readers/BookReader.h"
#include "ui/widgets/FadingStackedWidget.h"
#include "ui/widgets/LibraryListView.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QEvent>
#include <QShowEvent>
#include <QFrame>
#include <QScrollArea>
#include <QScrollBar>
#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QSettings>
#include <QInputDialog>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QFileInfo>
#include <QShortcut>
#include <QPushButton>
#include <QIcon>
#include <QSize>
#include <QRegularExpression>
#include <QFile>
#include <QMessageBox>
#include <QSizePolicy>
#include <QCoreApplication>

BooksPage::BooksPage(CoreBridge* bridge, QWidget* parent)
    : QWidget(parent)
    , m_bridge(bridge)
{
    setObjectName("books");

    buildUI();

    // ── Catalogue aggregator (BOOKS_STREMIO_PIVOT — catalogue search) ──
    m_catalogueNam = new QNetworkAccessManager(this);
    m_fictiondb = new FictionDbClient(m_catalogueNam, this);
    // BOOKS_FICTIONDB_CATALOGUE — series come from Top-N resolution over
    // FictionDB's free-text search (no enumerable series directory exists; the
    // author-series A-Z directory is the indie long tail and excludes major
    // franchises). The aggregator searches, then peeks the top results' book
    // pages to group by their self-declared series. (BookSeriesIndex + builder
    // remain dormant — candidate persistent-cache layer / removable.)
    m_catalogueAggregator = new BookCatalogueAggregator(m_fictiondb, this);
    // Catalogue cover cache directory
    m_catalogueCoverDir = m_bridge->dataDir() + "/book_catalogue_covers";
    QDir().mkpath(m_catalogueCoverDir);

    m_catalogueStore = new BooksCatalogueLibraryStore(m_bridge->dataDir(), this);
    m_catalogueStore->load();
    if (m_catalogueDetailView) {
        m_catalogueDetailView->setCatalogueStore(m_catalogueStore);
    }
    if (m_seriesDetailView) {
        m_seriesDetailView->setCatalogueStore(m_catalogueStore);
        m_seriesDetailView->setNetwork(m_catalogueNam, m_catalogueCoverDir);
    }
    connect(m_catalogueStore, &BooksCatalogueLibraryStore::recordsChanged,
            this, &BooksPage::rebuildBookGrid);
    connect(m_catalogueStore, &BooksCatalogueLibraryStore::recordReadStateChanged,
            this, [this](const QString&) { refreshContinueStrip(); });

    m_catalogueSearchView = new BookCatalogueSearchWidget(
        m_catalogueAggregator, m_catalogueNam, m_catalogueCoverDir, this);
    connect(m_catalogueSearchView, &BookCatalogueSearchWidget::backRequested,
            this, &BooksPage::showGrid);
    connect(m_catalogueSearchView, &BookCatalogueSearchWidget::bookPicked,
            this, [this](const BookCatalogueResult& book, const QString& coverPath) {
                if (!m_catalogueDetailView) return;
                m_catalogueDetailReturnToSearch = true;
                m_catalogueDetailView->showBook(book, coverPath);
                m_stack->setCurrentWidget(m_catalogueDetailView);
            });
    if (m_stack) m_stack->addWidget(m_catalogueSearchView);

    // BOOKS_FICTIONDB_CATALOGUE — series tile → open the series-shape detail
    // view, which self-loads (fetch series + eagerly enrich each book's
    // cover/synopsis/year via its own FictionDbClient, then renders once).
    connect(m_catalogueSearchView, &BookCatalogueSearchWidget::seriesPicked,
            this, [this](const BookCatalogueResult& s) { openSeries(s.seriesId); });

    // §5.2 (2026-05-27) — wire catalogue download lifecycle. Detail view emits
    // downloadRequested when the user clicks a source row; readRequested when
    // the [Read] CTA fires (book is already in library).
    if (m_catalogueDetailView) {
        connect(m_catalogueDetailView,
                &BookCatalogueDetailView::downloadRequested,
                this, &BooksPage::onCatalogueDownloadRequested);
        connect(m_catalogueDetailView,
                &BookCatalogueDetailView::readRequested,
                this, &BooksPage::onCatalogueReadRequested);
    }

    // §3.8 burn-the-ships backout (2026-05-27): initial population from
    // persisted catalogue records. recordsChanged drives subsequent refreshes.
    rebuildBookGrid();
    refreshContinueStrip();
}

BooksPage::~BooksPage() = default;

// v1.3 Phase D.1 (2026-05-19) — books-side dispatch layer. See
// docs/superpowers/specs/2026-05-19-bridge-v1.3-books-commission.md for
// the 21-command catalog. JS-resident playback commands (seek-page,
// set-layout, get-chapters, open-chapter, tts-play / tts-pause /
// tts-resume / tts-set-voice / tts-set-speed / tts-stop / get-listen-state)
// return a structured `code=JS_RESIDENT_NOT_IMPLEMENTED` reply that
// names the JS file owning the state. Wiring those into BookBridge is a
// follow-on v1.3.x ticket.
bool BooksPage::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_searchBar) {
        if (event->type() == QEvent::FocusIn) {
            if (m_searchBar && m_searchBar->text().trimmed().isEmpty())
                showSearchHistoryDropdown();
        } else if (event->type() == QEvent::FocusOut) {
            if (m_searchHistoryHideTimer) m_searchHistoryHideTimer->start();
        }
    }
    return QWidget::eventFilter(obj, event);
}

namespace {

inline bool replyOk(QJsonObject& reply, QJsonObject fields)
{
    for (auto it = fields.begin(); it != fields.end(); ++it)
        reply.insert(it.key(), it.value());
    return true;
}

inline bool replyErr(QJsonObject& reply, const char* code, const QString& msg)
{
    reply["type"]    = QStringLiteral("error");
    reply["code"]    = QString::fromLatin1(code);
    reply["message"] = msg;
    return true;
}

inline bool replyJsResident(QJsonObject& reply, const QString& jsFile,
                            const QString& note)
{
    reply["ok"]         = false;
    reply["code"]       = QStringLiteral("JS_RESIDENT_NOT_IMPLEMENTED");
    reply["jsSource"]   = jsFile;
    reply["note"]       = note;
    return true;
}

inline QString progressKeyFor(const QString& absPath)
{
    // MUST match BookBridge::progressKey — normalize backslashes before hashing
    // so the key matches the one the reader saves progress under (else the
    // continue-reading lookup misses on Windows paths).
    QString norm = absPath;
    norm.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return QString(QCryptographicHash::hash(
        norm.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
}

// Catalogue cover-cache path for a catalogueId — mirrors
// BookCatalogueSearchWidget::coverPathFor + BookSeriesDetailView so a cover
// downloaded by any of them is found by all (shared on-disk cache).
inline QString coverCachePath(const QString& dir, const QString& catalogueId)
{
    if (dir.isEmpty() || catalogueId.isEmpty()) return {};
    QString stem = catalogueId;
    stem.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]")),
                 QStringLiteral("_"));
    return dir + QLatin1Char('/') + stem + QStringLiteral(".jpg");
}

}  // namespace

bool BooksPage::dispatchDevCommand(const QString& cmd,
                                   const QJsonObject& payload,
                                   QJsonObject& reply)
{
    // ── Library + page-local state ────────────────────────────────────────
    if (cmd == QLatin1String("books_get_state"))
        return replyOk(reply, {{"snapshot", devSnapshot()}});

    if (cmd == QLatin1String("books_get_library"))
        return replyOk(reply, {{"library", devLibrarySnapshot()}});

    if (cmd == QLatin1String("books_refresh_library")) {
        if (m_catalogueStore) m_catalogueStore->validateAll();
        return replyOk(reply, {
            {"triggered", true},
            {"records",   m_catalogueStore ? m_catalogueStore->all().size() : 0}
        });
    }

    // BOOKS_FICTIONDB_CATALOGUE — crawl FictionDB's A–Z series directory into
    // the local series index (one-time build / refresh).
    if (cmd == QLatin1String("books_build_series_index")) {
        if (!m_fictiondb || !m_seriesIndex)
            return replyErr(reply, "INTERNAL", "series index not constructed");
        if (!m_indexBuilder)
            m_indexBuilder = new BookSeriesIndexBuilder(m_fictiondb, m_seriesIndex, this);
        if (m_indexBuilder->running())
            return replyOk(reply, {{"started", false}, {"reason", "already running"}});
        m_indexBuilder->start();
        return replyOk(reply, {{"started", true}});
    }

    if (cmd == QLatin1String("books_series_index_status")) {
        return replyOk(reply, {
            {"size",     m_seriesIndex ? m_seriesIndex->size() : 0},
            {"builtAt",  m_seriesIndex ? static_cast<double>(m_seriesIndex->builtAt()) : 0.0},
            {"building", m_indexBuilder ? m_indexBuilder->running() : false}
        });
    }

    if (cmd == QLatin1String("books_search_library")) {
        const QString query = payload.value("query").toString();
        if (!m_searchBar)
            return replyErr(reply, "INTERNAL", "search bar not constructed");
        m_searchBar->setText(query);
        // applySearch is debounce-fired by the QLineEdit textChanged signal,
        // but we want a synchronous reply — run the search immediately.
        applySearch();
        return replyOk(reply, {
            {"query",          query},
            {"visibleSeries",  m_bookStrip ? m_bookStrip->visibleCount() : 0},
            {"bookHitsShown",  m_bookHitsSection && m_bookHitsSection->isVisible()},
            {"totalSeries",    m_bookStrip ? m_bookStrip->totalCount() : 0}
        });
    }

    if (cmd == QLatin1String("books_clear_search")) {
        if (!m_searchBar)
            return replyErr(reply, "INTERNAL", "search bar not constructed");
        m_searchBar->clear();
        applySearch();
        return replyOk(reply, {
            {"visibleSeries", m_bookStrip ? m_bookStrip->visibleCount() : 0},
            {"totalSeries",   m_bookStrip ? m_bookStrip->totalCount() : 0}
        });
    }

    if (cmd == QLatin1String("books_set_sort")) {
        const QString key = payload.value("key").toString();
        if (key.isEmpty())
            return replyErr(reply, "BAD_REQUEST", "payload.key required");
        if (!m_sortCombo)
            return replyErr(reply, "INTERNAL", "sort combo not constructed");
        bool found = false;
        for (int i = 0; i < m_sortCombo->count(); ++i) {
            if (m_sortCombo->itemData(i).toString() == key) {
                m_sortCombo->setCurrentIndex(i);
                found = true;
                break;
            }
        }
        if (!found)
            return replyErr(reply, "BAD_REQUEST",
                QStringLiteral("unknown sort key '%1'").arg(key));
        return replyOk(reply, {{"sortKey", key}});
    }

    if (cmd == QLatin1String("books_set_density")) {
        if (!payload.contains(QStringLiteral("value")))
            return replyErr(reply, "BAD_REQUEST", "payload.value required (0|1|2)");
        const int val = payload.value("value").toInt(-1);
        if (val < 0 || val > 2)
            return replyErr(reply, "BAD_REQUEST", "value must be 0, 1, or 2");
        if (!m_densitySlider)
            return replyErr(reply, "INTERNAL", "density slider not constructed");
        m_densitySlider->setValue(val);
        return replyOk(reply, {{"density", val}});
    }

    if (cmd == QLatin1String("books_open_book")) {
        const QString path = payload.value("path").toString();
        if (path.isEmpty())
            return replyErr(reply, "BAD_REQUEST", "payload.path required");
        if (!QFileInfo::exists(path))
            return replyErr(reply, "BAD_REQUEST",
                QStringLiteral("file does not exist: %1").arg(path));
        emit openBook(path);
        return replyOk(reply, {{"opened", true}, {"path", path}});
    }

    if (cmd == QLatin1String("books_open_series")) {
        // §3.8 burn-the-ships backout (2026-05-27): series-shape detail view deferred to v1.x.
        // The folder-tree BookSeriesView is gone; series-shape catalogue detail (§5.3) is the
        // future replacement, not yet shipped.
        return replyErr(reply, "DEFERRED_TO_V1X",
            "books_open_series deferred until series-shape catalogue detail view ships");
    }

    if (cmd == QLatin1String("books_get_series_state")) {
        // §3.8 burn-the-ships backout (2026-05-27): see books_open_series.
        return replyErr(reply, "DEFERRED_TO_V1X",
            "books_get_series_state deferred until series-shape catalogue detail view ships");
    }

    // ── Reader-side state (needs MainWindow + m_bookReader) ───────────────
    auto* mainWin = qobject_cast<MainWindow*>(window());
    BookReader* reader = mainWin ? mainWin->bookReader() : nullptr;

    if (cmd == QLatin1String("books_get_progress")) {
        if (!reader)
            return replyErr(reply, "NO_READER",
                "BookReader not yet constructed (no book opened this session)");
        QJsonObject readerSnap = reader->devSnapshot();
        QString currentFile = readerSnap.value("currentFile").toString();
        QJsonObject prog;
        if (!currentFile.isEmpty() && m_bridge) {
            prog = m_bridge->progress("books", progressKeyFor(currentFile));
        }
        return replyOk(reply, {
            {"reader",      readerSnap},
            {"progressKey", currentFile.isEmpty() ? QString()
                                : progressKeyFor(currentFile)},
            {"progress",    prog}
        });
    }

    // JS-resident: page/layout/chapter live in engine_foliate.js.
    if (cmd == QLatin1String("books_seek_page")
        || cmd == QLatin1String("books_set_layout")
        || cmd == QLatin1String("books_get_chapters")
        || cmd == QLatin1String("books_open_chapter")) {
        if (!reader)
            return replyErr(reply, "NO_READER",
                "BookReader not yet constructed (no book opened this session)");
        return replyJsResident(reply,
            QStringLiteral("src/ui/readers/engine_foliate.js"),
            QStringLiteral("reader page/layout/chapter state lives in Foliate's "
                           "JS engine; v1.3.x will wire BookBridge methods to "
                           "drive these via runJavaScript"));
    }

    // ── TTS-side state (BookBridge holds the Qt-side worker) ──────────────
#ifdef HAS_WEBENGINE
    BookBridge* bridge = reader ? reader->bridge() : nullptr;

    if (cmd == QLatin1String("books_tts_state")) {
        if (!bridge)
            return replyErr(reply, "NO_READER",
                "BookBridge not yet constructed (no book opened this session)");
        QJsonObject ttsSnap = bridge->devTtsSnapshot();
        ttsSnap["jsPlaybackNote"] = QStringLiteral(
            "playing/paused/voice/speed/position live in tts_core.js");
        return replyOk(reply, {{"snapshot", ttsSnap}});
    }

    if (cmd == QLatin1String("books_tts_cancel_stream")) {
        if (!bridge)
            return replyErr(reply, "NO_READER",
                "BookBridge not yet constructed (no book opened this session)");
        if (!payload.contains(QStringLiteral("streamId")))
            return replyErr(reply, "BAD_REQUEST", "payload.streamId required");
        const quint64 streamId = static_cast<quint64>(
            payload.value("streamId").toVariant().toULongLong());
        bridge->devCancelStream(streamId);
        return replyOk(reply, {{"cancelled", true}, {"streamId",
            static_cast<double>(streamId)}});
    }
#else
    if (cmd == QLatin1String("books_tts_state")
        || cmd == QLatin1String("books_tts_cancel_stream")) {
        return replyErr(reply, "WEBENGINE_DISABLED",
            "Edge TTS surface requires HAS_WEBENGINE build");
    }
#endif

    // JS-resident: playback control lives in tts_core.js + engine_foliate.js.
    if (cmd == QLatin1String("books_tts_play")
        || cmd == QLatin1String("books_tts_pause")
        || cmd == QLatin1String("books_tts_resume")
        || cmd == QLatin1String("books_tts_stop")
        || cmd == QLatin1String("books_tts_set_voice")
        || cmd == QLatin1String("books_tts_set_speed")
        || cmd == QLatin1String("books_get_listen_state")) {
        if (!reader)
            return replyErr(reply, "NO_READER",
                "BookReader not yet constructed (no book opened this session)");
        return replyJsResident(reply,
            QStringLiteral("src/ui/readers/tts_core.js"),
            QStringLiteral("TTS playback / voice / speed / Listen-button state "
                           "lives in the JS layer; v1.3.x will add BookBridge "
                           "Q_INVOKABLE methods + runJavaScript drivers"));
    }

    // ── v1.6 library-side bridge (Phase D.4, 2026-05-19) ──────────────────
    if (cmd.startsWith(QLatin1String("library_"))) {
        if (cmd == QLatin1String("library_get_section"))
            return replyOk(reply, {{"section", devLibrarySection()}});
        if (cmd == QLatin1String("library_get_continue_reading")) {
            const QJsonObject sec = devLibrarySection();
            return replyOk(reply, {{"cr_strip", sec.value("cr_strip").toObject()}});
        }
        if (cmd == QLatin1String("library_get_recently_added")) {
            const QJsonObject sec = devLibrarySection();
            return replyOk(reply,
                {{"recently_added", sec.value("recently_added").toObject()}});
        }
        if (cmd == QLatin1String("library_get_search_state")) {
            return replyOk(reply, {
                {"query", m_searchBar ? m_searchBar->text() : QString()},
                {"search_state", devLibrarySection().value("search_state").toObject()}
            });
        }
        if (cmd == QLatin1String("library_get_scan_state")) {
            return replyOk(reply, {
                {"scan_state", devLibrarySection().value("scan_state").toObject()}
            });
        }
        if (cmd == QLatin1String("library_trigger_scan")) {
            if (m_catalogueStore) m_catalogueStore->validateAll();
            return replyOk(reply, {{"triggered", true},
                                   {"records",   m_catalogueStore
                                       ? m_catalogueStore->all().size() : 0}});
        }
        if (cmd == QLatin1String("library_get_sort"))
            return replyOk(reply, {{"sortKey",
                m_sortCombo ? m_sortCombo->currentData().toString() : QString()}});
        if (cmd == QLatin1String("library_set_sort")) {
            const QString key = payload.value("key").toString();
            if (key.isEmpty())
                return replyErr(reply, "BAD_REQUEST", "payload.key required");
            if (!m_sortCombo)
                return replyErr(reply, "INTERNAL", "sort combo not constructed");
            for (int i = 0; i < m_sortCombo->count(); ++i) {
                if (m_sortCombo->itemData(i).toString() == key) {
                    m_sortCombo->setCurrentIndex(i);
                    return replyOk(reply, {{"sortKey", key}});
                }
            }
            return replyErr(reply, "BAD_REQUEST",
                QStringLiteral("unknown sort key '%1'").arg(key));
        }
        if (cmd == QLatin1String("library_set_density")) {
            const int val = payload.value("value").toInt(-1);
            if (val < 0 || val > 2)
                return replyErr(reply, "BAD_REQUEST", "value must be 0|1|2");
            if (!m_densitySlider)
                return replyErr(reply, "INTERNAL", "density slider not constructed");
            m_densitySlider->setValue(val);
            return replyOk(reply, {{"density", val}});
        }
        if (cmd == QLatin1String("library_set_search_query")) {
            if (!m_searchBar)
                return replyErr(reply, "INTERNAL", "search bar not constructed");
            const QString q = payload.value("query").toString();
            m_searchBar->setText(q);
            applySearch();
            return replyOk(reply, {{"query", q}});
        }
        if (cmd == QLatin1String("library_get_active_layer"))
            return replyOk(reply, {{"layer",
                devLibrarySection().value("active_layer").toString()}});
        if (cmd == QLatin1String("library_reset_mode")) {
            // BooksPage has no resetToRoot slot; showGrid flips m_stack
            // back to index 0 (library grid), matching the contract from
            // feedback_mode_pill_resets_to_root.md for non-deep modes.
            showGrid();
            return replyOk(reply, {{"reset", true},
                {"layer", devLibrarySection().value("active_layer").toString()}});
        }
        if (cmd == QLatin1String("library_get_selected_items"))
            return replyOk(reply, {{"selection", QJsonArray{}}});
        return false;  // unknown library_* — fall through to UNKNOWN_CMD
    }

    return false;  // unknown books_* command — fall through to UNKNOWN_CMD
}

// v1.6 Phase D.4 (2026-05-19) — cross-mode library-section snapshot.
// §3.8 burn-the-ships backout (2026-05-27) — scan_state replaced by catalogue_state.
QJsonObject BooksPage::devLibrarySection() const
{
    QJsonObject sec;
    QJsonObject cr;
    cr["visible"] = m_continueSection && m_continueSection->isVisible();
    cr["count"]   = m_continueStrip ? m_continueStrip->totalCount() : 0;
    sec["cr_strip"] = cr;

    QJsonObject ra;
    ra["count"]   = m_bookStrip ? m_bookStrip->totalCount() : 0;
    ra["visible"] = m_bookStrip && m_bookStrip->totalCount() > 0;
    sec["recently_added"] = ra;

    QJsonObject ss;
    ss["query"]         = m_searchBar ? m_searchBar->text() : QString();
    ss["visibleSeries"] = m_bookStrip ? m_bookStrip->visibleCount() : 0;
    sec["search_state"] = ss;

    QJsonObject catalogue;
    catalogue["recordCount"] = m_catalogueStore
        ? m_catalogueStore->all().size() : 0;
    catalogue["seriesCount"] = m_catalogueStore
        ? m_catalogueStore->allSeriesIds().size() : 0;
    sec["catalogue_state"] = catalogue;

    QJsonArray roots;
    if (m_bridge)
        for (const QString& p : m_bridge->rootFolders(QStringLiteral("books")))
            roots.append(p);
    sec["root_folders"] = roots;

    sec["sort_key"] = m_sortCombo ? m_sortCombo->currentData().toString() : QString();
    sec["density"]  = m_densitySlider ? m_densitySlider->value() : -1;
    sec["selection"] = QJsonArray{};
    sec["active_layer"] = (m_stack && m_stack->currentIndex() == 1)
        ? QStringLiteral("catalogue-detail") : QStringLiteral("library");
    return sec;
}

// v1.3 Phase D.1 (2026-05-19) — page-local snapshot for books-get-state +
// dump-ui books. §3.8 burn-the-ships backout (2026-05-27) — scanner-state
// fields replaced by catalogue-store-state.
QJsonObject BooksPage::devSnapshot() const
{
    QJsonObject snap;
    snap["activePageId"]   = QStringLiteral("books");
    snap["gridMode"]       = m_gridMode;
    snap["catalogueRecordCount"] = m_catalogueStore
        ? m_catalogueStore->all().size() : 0;
    snap["progressEntries"] = m_continueStrip ? m_continueStrip->totalCount() : 0;
    snap["searchText"]     = m_searchBar ? m_searchBar->text() : QString();
    snap["sortKey"]        = m_sortCombo ? m_sortCombo->currentData().toString()
                                         : QString();
    snap["density"]        = m_densitySlider ? m_densitySlider->value() : -1;
    snap["catalogueDetailActive"] = m_stack && m_stack->currentIndex() == 1;
    snap["library"] = devLibrarySection();

    if (auto* mainWin = qobject_cast<const MainWindow*>(
            const_cast<BooksPage*>(this)->window())) {
        BookReader* reader = mainWin->bookReader();
        QJsonObject readerSnap;
        readerSnap["constructed"] = reader != nullptr;
        readerSnap["isVisible"]   = reader && reader->isVisible();
        if (reader)
            readerSnap["currentFile"] = reader->devSnapshot()
                                            .value("currentFile").toString();
        snap["reader"] = readerSnap;
    }
    return snap;
}

// §3.8 burn-the-ships backout (2026-05-27) — library snapshot is now catalogue
// records (one entry per CatalogueRecord), not folder-walker series entries.
QJsonObject BooksPage::devLibrarySnapshot() const
{
    QJsonArray entries;
    if (m_catalogueStore) {
        for (const CatalogueRecord& r : m_catalogueStore->all()) {
            QJsonObject e;
            e["catalogueId"]   = r.catalogueId;
            e["title"]         = r.title;
            e["author"]        = r.author;
            e["year"]          = r.year;
            e["filePath"]      = r.filePath;
            e["format"]        = r.format;
            e["seriesId"]      = r.seriesId;
            e["seriesName"]    = r.seriesName;
            e["seriesPosition"] = r.seriesPosition;
            e["addedAt"]       = static_cast<double>(r.addedAt);
            e["readProgress"]  = r.readProgress;
            e["lastReadAt"]    = static_cast<double>(r.lastReadAt);
            entries.append(e);
        }
    }
    QJsonObject snap;
    snap["entries"] = entries;
    snap["count"]   = entries.size();
    return snap;
}

void BooksPage::buildUI()
{
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    m_stack = new FadingStackedWidget(this);

    // ── Grid view (index 0) ──
    auto* gridPage = new QWidget();
    auto* gridLayout = new QVBoxLayout(gridPage);
    gridLayout->setContentsMargins(0, 0, 0, 0);
    gridLayout->setSpacing(0);

    // Scrollable content area
    auto* scroll = new QScrollArea(gridPage);
    m_gridScroll = scroll;  // GLOBAL_NAV_HISTORY Task 9
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet("background: transparent;");

    auto* content = new QWidget();
    content->setStyleSheet("background: transparent;");
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(20, 0, 20, 20);
    layout->setSpacing(24);

    // Search bar submits to the catalogue takeover view.
    m_searchBar = new QLineEdit(content);
    m_searchBar->setPlaceholderText("Search books catalogue");
    m_searchBar->setClearButtonEnabled(true);
    m_searchBar->setObjectName("LibrarySearch");
    m_searchBar->setFixedHeight(36);
    m_searchBar->setStyleSheet(
        "QLineEdit#LibrarySearch { background: rgba(255,255,255,0.07); border: 1px solid rgba(255,255,255,0.12);"
        " border-radius: 6px; color: #eee; padding: 4px 10px; font-size: 13px; }"
        "QLineEdit#LibrarySearch:focus { border: 1px solid rgba(255,255,255,0.3); }");
    auto* searchLayout = new QHBoxLayout();
    searchLayout->setContentsMargins(0, 20, 0, 0);
    searchLayout->setSpacing(8);
    searchLayout->addWidget(m_searchBar, 1);

    auto* searchButton = new QPushButton(content);
    searchButton->setObjectName("BooksSearchButton");
    searchButton->setFixedSize(36, 36);
    searchButton->setCursor(Qt::PointingHandCursor);
    searchButton->setIcon(QIcon(QStringLiteral(":/icons/search.svg")));
    searchButton->setIconSize(QSize(18, 18));
    searchButton->setToolTip("Search");
    searchButton->setStyleSheet(
        "QPushButton#BooksSearchButton { background: rgba(255,255,255,0.07);"
        " border: 1px solid rgba(255,255,255,0.12); border-radius: 6px; }"
        "QPushButton#BooksSearchButton:hover { background: rgba(255,255,255,0.11); }");
    connect(searchButton, &QPushButton::clicked, this, [this]() {
        const QString q = m_searchBar ? m_searchBar->text().trimmed() : QString();
        if (!q.isEmpty()) showCatalogueSearchMode(q);
    });
    searchLayout->addWidget(searchButton);
    layout->addLayout(searchLayout);

    m_searchBar->setToolTip("Press Enter or click the search icon to search book catalogues");

    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);
    m_searchTimer->setInterval(250);
    connect(m_searchBar, &QLineEdit::textChanged, this, [this]() {
        const bool hasText = !m_searchBar->text().trimmed().isEmpty();
        m_searchBar->setProperty("activeSearch", hasText);
        m_searchBar->style()->unpolish(m_searchBar);
        m_searchBar->style()->polish(m_searchBar);

        if (hasText) {
            hideSearchHistoryDropdown();
        } else if (m_searchBar->hasFocus()) {
            showSearchHistoryDropdown();
        }
    });
    connect(m_searchBar, &QLineEdit::returnPressed, this, [this]() {
        const QString q = m_searchBar ? m_searchBar->text().trimmed() : QString();
        if (!q.isEmpty()) showCatalogueSearchMode(q);
    });

    loadSearchHistory();
    buildSearchHistoryDropdown();
    m_searchBar->installEventFilter(this);

    auto* searchShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_F), this);
    connect(searchShortcut, &QShortcut::activated, this, [this]() {
        m_searchBar->setFocus();
        m_searchBar->selectAll();
    });

    // ── 2. Continue Reading section ──
    m_continueSection = new QWidget(content);
    auto* continueLayout = new QVBoxLayout(m_continueSection);
    continueLayout->setContentsMargins(0, 0, 0, 0);
    continueLayout->setSpacing(4);
    auto* continueLabel = new QLabel("CONTINUE READING", m_continueSection);
    continueLabel->setObjectName("LibraryHeading");
    continueLayout->addWidget(continueLabel);
    m_continueStrip = new TileStrip(m_continueSection);
    m_continueStrip->setMode("continue");
    continueLayout->addWidget(m_continueStrip);

    // Continue-tile context menu
    m_continueStrip->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_continueStrip, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        auto* card = m_continueStrip->tileAt(pos);
        if (!card) return;

        QString filePath = card->property("filePath").toString();
        QString seriesPath = card->property("seriesPath").toString();
        QString seriesName = card->property("seriesName").toString();
        QString coverPath = card->property("coverPath").toString();
        QString progKey = card->property("progressKey").toString();

        bool isFinished = false;
        if (m_bridge && !progKey.isEmpty()) {
            QJsonObject prog = m_bridge->progress("books", progKey);
            isFinished = prog.value("finished").toBool();
        }

        // §3.8 burn-the-ships backout (2026-05-27): "Open series" + "Remove from
        // library" (folder-tier) actions deleted — they routed through BookSeriesView
        // and triggerScan respectively, both gone. Other actions stay since they
        // operate on filePath / progKey, not folder/scanner state.
        auto* menu = ContextMenuHelper::createMenu(this);
        auto* continueAct = menu->addAction("Continue reading");
        menu->addSeparator();
        auto* markAct = menu->addAction(isFinished ? "Mark as unread" : "Mark as read");
        auto* clearAct = menu->addAction("Clear from Continue Reading");
        menu->addSeparator();
        auto* renameAct = menu->addAction("Rename...");
        auto* revealAct = menu->addAction("Reveal in File Explorer");
        revealAct->setEnabled(!filePath.isEmpty());
        auto* copyAct = menu->addAction("Copy path");
        copyAct->setEnabled(!filePath.isEmpty());

        auto* chosen = menu->exec(m_continueStrip->mapToGlobal(pos));
        if (chosen == continueAct) {
            if (!filePath.isEmpty()) emit openBook(filePath);
        } else if (chosen == markAct) {
            if (m_bridge && !progKey.isEmpty()) {
                QJsonObject prog = m_bridge->progress("books", progKey);
                prog["finished"] = !isFinished;
                m_bridge->saveProgress("books", progKey, prog);
                refreshContinueStrip();
            }
        } else if (chosen == clearAct) {
            if (m_bridge && !progKey.isEmpty()) {
                m_bridge->clearProgress("books", progKey);
                refreshContinueStrip();
            }
        } else if (chosen == renameAct) {
            if (!filePath.isEmpty()) {
                QFileInfo fi(filePath);
                QString newName = QInputDialog::getText(this, "Rename",
                    "New name:", QLineEdit::Normal, fi.completeBaseName());
                if (!newName.isEmpty() && newName != fi.completeBaseName()) {
                    QString newPath = fi.absolutePath() + "/" + newName + "." + fi.suffix();
                    if (QFile::rename(filePath, newPath)) {
                        refreshContinueStrip();
                    } else {
                        QMessageBox::warning(this, "Rename failed",
                            "Could not rename \"" + fi.fileName() + "\".\n"
                            "The file may be in use by another program.");
                    }
                }
            }
        } else if (chosen == revealAct) {
            ContextMenuHelper::revealInExplorer(filePath);
        } else if (chosen == copyAct) {
            ContextMenuHelper::copyToClipboard(filePath);
        }
        menu->deleteLater();
    });

    m_continueSection->hide();
    layout->addWidget(m_continueSection);

    // ── 3. "BOOKS" header row: label + sort + density ──
    auto* booksRow = new QWidget(content);
    auto* booksRowLayout = new QHBoxLayout(booksRow);
    booksRowLayout->setContentsMargins(0, 0, 0, 0);
    booksRowLayout->setSpacing(8);

    auto* booksLabel = new QLabel("BOOKS", booksRow);
    booksLabel->setObjectName("LibraryHeading");
    booksRowLayout->addWidget(booksLabel);
    booksRowLayout->addStretch();

    m_sortCombo = new QComboBox(booksRow);
    m_sortCombo->setObjectName("LibrarySortCombo");
    m_sortCombo->setFixedWidth(150);
    m_sortCombo->setFixedHeight(28);
    m_sortCombo->addItem("Name A\u2192Z",       "name_asc");
    m_sortCombo->addItem("Name Z\u2192A",       "name_desc");
    m_sortCombo->addItem("Recently updated",     "updated_desc");
    m_sortCombo->addItem("Least recent",         "updated_asc");
    m_sortCombo->addItem("Most items",           "count_desc");
    m_sortCombo->addItem("Fewest items",         "count_asc");
    m_sortCombo->setStyleSheet(
        "QComboBox#LibrarySortCombo {"
        "  background: rgba(255,255,255,0.07); border: 1px solid rgba(255,255,255,0.12);"
        "  border-radius: 6px; color: #ccc; padding: 2px 8px; font-size: 12px; }"
        "QComboBox#LibrarySortCombo:hover { border-color: rgba(255,255,255,0.2); }"
        "QComboBox#LibrarySortCombo::drop-down { border: none; }"
        "QComboBox#LibrarySortCombo QAbstractItemView {"
        "  background: #1e1e1e; color: #ccc; selection-background-color: rgba(255,255,255,0.1);"
        "  border: 1px solid rgba(255,255,255,0.12); }");
    QString savedSort = QSettings("Tankoban", "Tankoban").value("library_sort_books", "name_asc").toString();
    for (int i = 0; i < m_sortCombo->count(); ++i) {
        if (m_sortCombo->itemData(i).toString() == savedSort) {
            m_sortCombo->setCurrentIndex(i);
            break;
        }
    }
    connect(m_sortCombo, &QComboBox::currentIndexChanged, this, [this](int idx) {
        QString key = m_sortCombo->itemData(idx).toString();
        QSettings("Tankoban", "Tankoban").setValue("library_sort_books", key);
        m_bookStrip->sortTiles(key);
    });
    booksRowLayout->addWidget(m_sortCombo);

    auto* densitySmall = new QLabel("A", booksRow);
    densitySmall->setObjectName("DensityLabelSmall");
    booksRowLayout->addWidget(densitySmall);

    m_densitySlider = new QSlider(Qt::Horizontal, booksRow);
    m_densitySlider->setRange(0, 2);
    m_densitySlider->setFixedWidth(100);
    m_densitySlider->setFixedHeight(20);
    int savedDensity = QSettings("Tankoban", "Tankoban").value("grid_cover_size", 1).toInt();
    m_densitySlider->setValue(qBound(0, savedDensity, 2));
    connect(m_densitySlider, &QSlider::valueChanged, this, [this](int val) {
        QSettings("Tankoban", "Tankoban").setValue("grid_cover_size", val);
        m_bookStrip->setDensity(val);
        if (m_continueStrip) m_continueStrip->setDensity(val);
        if (m_bookHitsStrip) m_bookHitsStrip->setDensity(val);
    });
    booksRowLayout->addWidget(m_densitySlider);

    auto* densityLarge = new QLabel("A", booksRow);
    densityLarge->setObjectName("DensityLabelLarge");
    booksRowLayout->addWidget(densityLarge);

    m_viewToggle = new QPushButton(booksRow);
    m_viewToggle->setObjectName("ViewToggle");
    m_viewToggle->setFixedSize(28, 28);
    m_viewToggle->setCursor(Qt::PointingHandCursor);
    m_viewToggle->setToolTip("Toggle grid/list view (V)");
    m_viewToggle->setStyleSheet(
        "QPushButton#ViewToggle { background: rgba(255,255,255,0.05); border: 1px solid rgba(255,255,255,0.08);"
        "  border-radius: 6px; color: rgba(255,255,255,0.5); font-size: 14px; }"
        "QPushButton#ViewToggle:hover { background: rgba(255,255,255,0.1); color: rgba(255,255,255,0.8); }");
    m_viewToggle->setText(QString::fromUtf8("\u2630")); // hamburger icon for grid mode
    booksRowLayout->addWidget(m_viewToggle);

    layout->addWidget(booksRow);

    m_bookStatus = new QLabel("Search for books to add to library", content);
    m_bookStatus->setObjectName("TileSubtitle");
    m_bookStatus->setAlignment(Qt::AlignCenter);
    m_bookStatus->setTextFormat(Qt::PlainText);
    m_bookStatus->setWordWrap(true);
    m_bookStatus->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_bookStatus->setStyleSheet("color: rgba(238,238,238,0.58); font-size: 14px; padding: 60px;");
    layout->addWidget(m_bookStatus);

    m_bookStrip = new TileStrip(content);
    m_bookStrip->hide();
    m_bookStrip->setMinimumHeight(340);
    layout->addWidget(m_bookStrip);

    // BOOKS_LIBRARY_CONTEXT_MENU — right-click any library tile (mirrors the
    // Continue Reading strip handler).
    m_bookStrip->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_bookStrip, &QWidget::customContextMenuRequested,
            this, &BooksPage::showBookContextMenu);

    // ── List view (hidden by default, shown on V toggle) ──
    m_listView = new LibraryListView(content);
    m_listView->hide();
    layout->addWidget(m_listView);

    // §3.8 burn-the-ships backout (2026-05-27): m_listView itemActivated handler
    // was scanner-coupled (resolved series via m_seriesFiles + opened BookSeriesView).
    // Catalogue-record-aware list-view interaction is a v1.x follow-on.

    // View toggle logic
    m_gridMode = QSettings("Tankoban", "Tankoban").value("library_view_mode_books", "grid").toString() == "grid";
    connect(m_viewToggle, &QPushButton::clicked, this, [this]() {
        m_gridMode = !m_gridMode;
        QSettings("Tankoban", "Tankoban").setValue("library_view_mode_books", m_gridMode ? "grid" : "list");
        if (m_gridMode) {
            m_listView->hide();
            m_bookStrip->show();
            m_densitySlider->show();
            m_viewToggle->setText(QString::fromUtf8("\u2630")); // hamburger
        } else {
            m_bookStrip->hide();
            m_listView->show();
            m_densitySlider->hide();
            m_viewToggle->setText(QString::fromUtf8("\u2637")); // dotted square
        }
    });

    // §3.8 burn-the-ships backout (2026-05-27): book-tile context menu was
    // folder-tier-anchored (Open series → BookSeriesView; Mark-all-as-read by
    // walking on-disk files; Rename/Hide/Remove series folder; Reveal in Explorer
    // of seriesPath). All of these depend on the scanner-derived seriesPath /
    // seriesFiles state that no longer exists. A simpler catalogue-record-aware
    // context menu ("Remove from library" via evictByCatalogueId, "Open Library
    // page" via OpenLibrary URL, "Reveal file in Explorer" when filePath
    // populated) is v1.x follow-on scope.

    // ── Book Hits section (scored search — individual book matches) ──
    m_bookHitsSection = new QWidget(content);
    auto* bhLayout = new QVBoxLayout(m_bookHitsSection);
    bhLayout->setContentsMargins(0, 0, 0, 0);
    bhLayout->setSpacing(0);
    m_bookHitsStrip = new TileStrip(m_bookHitsSection);
    m_bookHitsStrip->setStripLabel("MATCHING BOOKS");
    bhLayout->addWidget(m_bookHitsStrip);
    m_bookHitsSection->hide();
    layout->addWidget(m_bookHitsSection);

    // Apply saved density now that all strips exist. Continue strip density
    // gate was dropped in TileStrip.cpp 2026-04-25 so it now responds to
    // setDensity uniformly.
    m_bookStrip->setDensity(savedDensity);
    if (m_continueStrip) m_continueStrip->setDensity(savedDensity);
    if (m_bookHitsStrip) m_bookHitsStrip->setDensity(savedDensity);

    layout->addStretch(1);
    scroll->setWidget(content);
    gridLayout->addWidget(scroll, 1);

    m_stack->addWidget(gridPage);

    // Catalogue detail view (index 1; was index 2 pre-§3.8 backout when BookSeriesView held index 1)
    m_catalogueDetailView = new BookCatalogueDetailView();
    connect(m_catalogueDetailView, &BookCatalogueDetailView::backRequested, this, [this]() {
        if (m_catalogueDetailReturnToSeries && m_seriesDetailView) {
            m_catalogueDetailReturnToSeries = false;
            m_stack->setCurrentWidget(m_seriesDetailView);
            return;
        }
        if (m_catalogueDetailReturnToSearch && m_catalogueSearchView) {
            m_catalogueDetailReturnToSearch = false;
            m_stack->setCurrentWidget(m_catalogueSearchView);
            return;
        }
        showGrid();
    });
    m_stack->addWidget(m_catalogueDetailView);

    // Series-shape detail view (BOOKS_FICTIONDB_CATALOGUE §4.4). A book row's
    // [Get] routes into the movie-shape detail view for that book (reusing the
    // §5.2 source-search + download); [Read] opens the reader.
    m_seriesDetailView = new BookSeriesDetailView();
    connect(m_seriesDetailView, &BookSeriesDetailView::backRequested,
            this, [this]() { showGrid(); });
    connect(m_seriesDetailView, &BookSeriesDetailView::bookReadRequested,
            this, &BooksPage::onCatalogueReadRequested);
    connect(m_seriesDetailView, &BookSeriesDetailView::bookOpenRequested, this,
            [this](const BookCatalogueResult& book) {
                if (!m_catalogueDetailView) return;
                m_catalogueDetailReturnToSeries = true;
                m_catalogueDetailReturnToSearch = false;
                m_catalogueDetailView->showBook(book, QString());
                m_stack->setCurrentWidget(m_catalogueDetailView);
            });
    connect(m_seriesDetailView, &BookSeriesDetailView::bookContextMenuRequested,
            this, &BooksPage::onSeriesBookContextMenu);
    m_stack->addWidget(m_seriesDetailView);

    outerLayout->addWidget(m_stack, 1);

    // ── Keyboard shortcuts (Batch 7) ──
    // Task 7 (2026-05-01) — scope to widget so it doesn't intercept Esc
    // when BookReader is shown over this page in the QStackedWidget.
    auto* escShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escShortcut, &QShortcut::activated, this, [this]() {
        if (m_stack->currentIndex() != 0) {
            showGrid();
        } else if (!m_searchBar->text().trimmed().isEmpty()) {
            m_searchBar->clear();
        }
    });

    auto* f5Shortcut = new QShortcut(QKeySequence(Qt::Key_F5), this);
    connect(f5Shortcut, &QShortcut::activated, this, [this]() {
        if (m_catalogueStore) m_catalogueStore->validateAll();
    });

    auto* refreshShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_R), this);
    connect(refreshShortcut, &QShortcut::activated, this, [this]() {
        refreshContinueStrip();
        m_bookStrip->sortTiles(m_sortCombo->currentData().toString());
    });

    auto* vShortcut = new QShortcut(QKeySequence(Qt::Key_V), this);
    connect(vShortcut, &QShortcut::activated, this, [this]() {
        if (m_searchBar->hasFocus()) return; // no-op if search focused
        m_viewToggle->click();
    });

    auto* selectAllShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_A), this);
    connect(selectAllShortcut, &QShortcut::activated, this, [this]() {
        if (m_gridMode)
            m_bookStrip->selectAll();
    });

    // Apply initial view mode
    if (!m_gridMode) {
        m_bookStrip->hide();
        m_listView->show();
        m_densitySlider->hide();
        m_viewToggle->setText(QString::fromUtf8("\u2637"));
    }
}

void BooksPage::activate()
{
    if (m_catalogueStore) m_catalogueStore->validateAll();
}

// §3.8 burn-the-ships backout (2026-05-27) — orphan-record check on show
// per design spec §6.2 ("validateAll() on showEvent walks the records,
// checks each file path exists on disk, marks orphan records for cleanup").
// Mirrors StreamDownloadIndex::validateAll pattern.
void BooksPage::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (m_catalogueStore) m_catalogueStore->validateAll();
    // Returning from the reader: reading progress was written to the JsonStore,
    // not the store, so refresh the continue strip to pick it up.
    refreshContinueStrip();
}

// §3.8 burn-the-ships backout (2026-05-27): rebuilds the library grid purely
// from BooksCatalogueLibraryStore records. Scanner-output BookSeriesInfo tier
// gone — catalogue records are the only first-class library entity per
// design spec §3.8. Empty-library copy per §3.9.
void BooksPage::rebuildBookGrid()
{
    if (!m_bookStrip || !m_bookStatus) return;

    m_bookStrip->clear();
    if (m_bookHitsStrip) m_bookHitsStrip->clear();
    if (m_bookHitsSection) m_bookHitsSection->hide();
    if (m_listView) m_listView->clear();

    const QList<CatalogueRecord> records = m_catalogueStore
        ? m_catalogueStore->all()
        : QList<CatalogueRecord>{};
    // Group library records by series: one tile per series (first owned book is
    // the representative), standalone tiles for the rest. A downloaded series
    // book thus appears under its series, not as a one-off.
    QSet<QString> seriesSeen;
    for (const CatalogueRecord& record : records) {
        if (!record.seriesId.isEmpty()) {
            if (seriesSeen.contains(record.seriesId)) continue;
            seriesSeen.insert(record.seriesId);
            const int owned = m_catalogueStore
                ? m_catalogueStore->catalogueIdsForSeries(record.seriesId).size()
                : 1;
            addLibrarySeriesTile(record, owned);
        } else {
            addCatalogueRecordTile(record);
        }
    }

    if (records.isEmpty()) {
        m_bookStrip->hide();
        if (m_listView) m_listView->hide();
        m_bookStatus->setObjectName("LibraryEmptyLabel");
        m_bookStatus->setAlignment(Qt::AlignCenter);
        m_bookStatus->setText("Search for books to add to library");
        m_bookStatus->show();
        return;
    }

    m_bookStatus->hide();
    m_bookStrip->show();
    if (m_listView) m_listView->hide();
    m_bookStrip->sortTiles(m_sortCombo ? m_sortCombo->currentData().toString()
                                       : QStringLiteral("name_asc"));
}

void BooksPage::addCatalogueRecordTile(const CatalogueRecord& record)
{
    if (record.catalogueId.isEmpty()) return;

    QStringList subtitleParts;
    if (!record.author.isEmpty()) subtitleParts << record.author;
    if (!record.year.isEmpty()) subtitleParts << record.year;
    const QString subtitle = subtitleParts.join(QStringLiteral(" / "));
    QString cover = QFile::exists(record.cachedCoverPath) ? record.cachedCoverPath
                                                          : QString();
    if (cover.isEmpty()) {
        const QString cached = coverCachePath(m_catalogueCoverDir, record.catalogueId);
        if (QFile::exists(cached)) cover = cached;
    }

    auto* card = new TileCard(cover, record.title, subtitle);
    card->setProperty("catalogueRecord", true);
    card->setProperty("catalogueId", record.catalogueId);
    card->setProperty("tileTitle", record.title);
    card->setProperty("fileCount", 1);
    card->setProperty("newestMtime", record.addedAt * 1000);
    connect(card, &TileCard::clicked, this, [this, card]() {
        if (!m_catalogueStore || !m_catalogueDetailView) return;

        const QString catalogueId = card->property("catalogueId").toString();
        const auto record = m_catalogueStore->recordFor(catalogueId);
        if (!record) return;

        m_catalogueDetailReturnToSearch = false;
        m_catalogueDetailView->showBook(
            catalogueRecordToResult(*record),
            QFile::exists(record->cachedCoverPath) ? record->cachedCoverPath : QString());
        m_stack->setCurrentWidget(m_catalogueDetailView);
    });
    m_bookStrip->addTile(card);
}

void BooksPage::addLibrarySeriesTile(const CatalogueRecord& rep, int ownedCount)
{
    if (rep.seriesId.isEmpty()) return;

    // A series book downloaded via [Get] has no cachedCoverPath, but its cover
    // was fetched into the shared catalogue cover cache during series-detail
    // enrichment — fall back to that so the library series tile isn't blank.
    QString cover = QFile::exists(rep.cachedCoverPath) ? rep.cachedCoverPath : QString();
    if (cover.isEmpty()) {
        const QString cached = coverCachePath(m_catalogueCoverDir, rep.catalogueId);
        if (QFile::exists(cached)) cover = cached;
    }
    const QString title = rep.seriesName.isEmpty() ? rep.title : rep.seriesName;
    QStringList subtitleParts;
    if (!rep.author.isEmpty()) subtitleParts << rep.author;
    subtitleParts << QStringLiteral("%1 book%2").arg(ownedCount)
                                                .arg(ownedCount == 1 ? QString() : QStringLiteral("s"));
    auto* card = new TileCard(cover, title, subtitleParts.join(QStringLiteral(" / ")));
    card->setProperty("catalogueSeries", true);
    card->setProperty("seriesId", rep.seriesId);
    card->setProperty("tileTitle", title);
    card->setProperty("fileCount", ownedCount);
    card->setProperty("newestMtime", rep.addedAt * 1000);
    connect(card, &TileCard::clicked, this, [this, card]() {
        // Reuse the search→series flow: open the series detail view, which
        // self-loads + enriches (owned books show Read, the rest Get).
        openSeries(card->property("seriesId").toString());
    });
    m_bookStrip->addTile(card);
}

BookCatalogueResult BooksPage::catalogueRecordToResult(const CatalogueRecord& record) const
{
    BookCatalogueResult result;
    result.catalogueId = record.catalogueId;
    result.isbn = record.isbn;
    result.title = record.title;
    result.author = record.author;
    result.publisher = record.publisher;
    result.year = record.year;
    result.language = record.language;
    result.description = record.description;
    result.genres = record.genres;
    result.coverUrl = record.coverUrl;
    result.seriesId = record.seriesId;
    result.seriesName = record.seriesName;
    result.seriesPosition = record.seriesPosition;
    result.seriesTotal = record.seriesTotal;
    return result;
}

void BooksPage::showGrid()
{
    m_catalogueDetailReturnToSearch = false;
    hideSearchHistoryDropdown();
    m_stack->setCurrentIndexAnimated(0);
}

// ── Scored search helpers ──

static QStringList tokenize(const QString& text)
{
    static QRegularExpression re("[a-z0-9]+");
    QStringList tokens;
    auto it = re.globalMatch(text.toLower());
    while (it.hasNext())
        tokens.append(it.next().captured());
    return tokens;
}

static int scoreTokens(const QString& text, const QStringList& queryTokens, const QString& fullQuery)
{
    QString lower = text.toLower();
    QStringList textTokens = tokenize(text);
    int score = 0;

    for (const auto& qt : queryTokens) {
        bool found = false;
        // Check substring match in full text
        if (lower.contains(qt)) {
            score += 14;
            found = true;
        }
        // Check exact word match
        for (const auto& tt : textTokens) {
            if (tt == qt) { score += 12; found = true; break; }
            if (tt.startsWith(qt)) { score += 6; found = true; break; }
        }
        if (!found) return 0; // AND logic: all tokens must match
    }

    // Full phrase bonus
    if (!fullQuery.isEmpty() && lower.contains(fullQuery.toLower()))
        score += 140;

    return score;
}

void BooksPage::loadSearchHistory()
{
    QSettings settings("Tankoban", "Tankoban");
    m_searchHistory = settings.value(QStringLiteral("books/searchHistory")).toStringList();
    if (m_searchHistory.size() > kMaxSearchHistory)
        m_searchHistory = m_searchHistory.mid(0, kMaxSearchHistory);
}

void BooksPage::saveSearchHistory()
{
    QSettings settings("Tankoban", "Tankoban");
    settings.setValue(QStringLiteral("books/searchHistory"), m_searchHistory);
}

void BooksPage::pushSearchHistory(const QString& query)
{
    const QString q = query.trimmed();
    if (q.isEmpty()) return;

    m_searchHistory.removeAll(q);
    m_searchHistory.prepend(q);
    if (m_searchHistory.size() > kMaxSearchHistory)
        m_searchHistory = m_searchHistory.mid(0, kMaxSearchHistory);
    saveSearchHistory();
}

void BooksPage::removeSearchHistoryEntry(const QString& query)
{
    m_searchHistory.removeAll(query);
    saveSearchHistory();
    if (m_searchHistoryDropdown && m_searchHistoryDropdown->isVisible())
        showSearchHistoryDropdown();
}

void BooksPage::clearSearchHistory()
{
    if (m_searchHistory.isEmpty()) return;
    m_searchHistory.clear();
    saveSearchHistory();
    hideSearchHistoryDropdown();
}

void BooksPage::buildSearchHistoryDropdown()
{
    m_searchHistoryDropdown = new QFrame(this);
    m_searchHistoryDropdown->setObjectName(QStringLiteral("BooksSearchHistory"));
    m_searchHistoryDropdown->setStyleSheet(
        "QFrame#BooksSearchHistory { background: #181818;"
        " border: 1px solid rgba(255,255,255,0.14); border-radius: 6px; }");
    m_searchHistoryDropdown->hide();

    auto* outer = new QVBoxLayout(m_searchHistoryDropdown);
    outer->setContentsMargins(6, 6, 6, 6);
    outer->setSpacing(2);

    m_searchHistoryList = new QWidget(m_searchHistoryDropdown);
    auto* listLayout = new QVBoxLayout(m_searchHistoryList);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(2);
    outer->addWidget(m_searchHistoryList);

    m_searchHistoryHideTimer = new QTimer(this);
    m_searchHistoryHideTimer->setSingleShot(true);
    m_searchHistoryHideTimer->setInterval(150);
    connect(m_searchHistoryHideTimer, &QTimer::timeout, this, [this]() {
        if (m_searchHistoryDropdown) m_searchHistoryDropdown->hide();
    });
}

void BooksPage::positionSearchHistoryDropdown()
{
    if (!m_searchHistoryDropdown || !m_searchBar) return;
    const QPoint pos = m_searchBar->mapTo(this, QPoint(0, m_searchBar->height() + 4));
    m_searchHistoryDropdown->setFixedWidth(m_searchBar->width());
    m_searchHistoryDropdown->setGeometry(pos.x(), pos.y(),
        m_searchBar->width(), m_searchHistoryDropdown->sizeHint().height());
}

void BooksPage::showSearchHistoryDropdown()
{
    if (!m_searchHistoryDropdown || !m_searchHistoryList) return;
    if (m_searchHistoryHideTimer) m_searchHistoryHideTimer->stop();

    auto* layout = qobject_cast<QVBoxLayout*>(m_searchHistoryList->layout());
    if (!layout) return;

    while (QLayoutItem* item = layout->takeAt(0)) {
        if (auto* widget = item->widget()) widget->deleteLater();
        delete item;
    }

    if (m_searchHistory.isEmpty()) {
        m_searchHistoryDropdown->hide();
        return;
    }

    const int rows = qMin(m_searchHistory.size(), kMaxSearchHistory);
    for (int i = 0; i < rows; ++i) {
        const QString q = m_searchHistory.at(i);

        auto* row = new QWidget(m_searchHistoryList);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(4);

        auto* submit = new QPushButton(q, row);
        submit->setCursor(Qt::PointingHandCursor);
        submit->setStyleSheet(
            "QPushButton { background: transparent; border: none;"
            " color: rgba(255,255,255,0.84); font-size: 13px;"
            " padding: 7px 8px; text-align: left; }"
            "QPushButton:hover { background: rgba(255,255,255,0.08); }");
        connect(submit, &QPushButton::clicked, this, [this, q]() {
            hideSearchHistoryDropdown();
            if (m_searchBar) m_searchBar->setText(q);
            showCatalogueSearchMode(q);
        });
        rowLayout->addWidget(submit, 1);

        auto* remove = new QPushButton(QStringLiteral("x"), row);
        remove->setCursor(Qt::PointingHandCursor);
        remove->setFixedSize(26, 26);
        remove->setToolTip(QStringLiteral("Remove from search history"));
        remove->setStyleSheet(
            "QPushButton { background: transparent; border: none;"
            " color: rgba(255,255,255,0.54); font-size: 13px; }"
            "QPushButton:hover { color: #ffffff; background: rgba(255,255,255,0.08); }");
        connect(remove, &QPushButton::clicked, this, [this, q]() {
            removeSearchHistoryEntry(q);
        });
        rowLayout->addWidget(remove);

        layout->addWidget(row);
    }

    auto* divider = new QFrame(m_searchHistoryList);
    divider->setFrameShape(QFrame::HLine);
    divider->setStyleSheet("QFrame { color: rgba(255,255,255,0.10); }");
    layout->addWidget(divider);

    auto* clearAll = new QPushButton(QStringLiteral("Clear search history"), m_searchHistoryList);
    clearAll->setCursor(Qt::PointingHandCursor);
    clearAll->setStyleSheet(
        "QPushButton { background: transparent; border: none;"
        " color: rgba(255,255,255,0.58); font-size: 12px;"
        " padding: 7px 8px; text-align: left; }"
        "QPushButton:hover { color: #ffffff; background: rgba(255,255,255,0.08); }");
    connect(clearAll, &QPushButton::clicked, this, &BooksPage::clearSearchHistory);
    layout->addWidget(clearAll);

    m_searchHistoryDropdown->adjustSize();
    positionSearchHistoryDropdown();
    m_searchHistoryDropdown->show();
    m_searchHistoryDropdown->raise();
}

void BooksPage::hideSearchHistoryDropdown()
{
    if (m_searchHistoryHideTimer) m_searchHistoryHideTimer->stop();
    if (m_searchHistoryDropdown) m_searchHistoryDropdown->hide();
}

void BooksPage::showCatalogueSearchMode(const QString& query)
{
    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty()) return;

    pushSearchHistory(trimmed);
    hideSearchHistoryDropdown();

    if (m_searchBar && m_searchBar->text().trimmed() != trimmed)
        m_searchBar->setText(trimmed);

    if (m_bookHitsStrip) m_bookHitsStrip->clear();
    if (m_bookHitsSection) m_bookHitsSection->hide();
    if (m_catalogueSearchView) {
        m_catalogueDetailReturnToSearch = false;
        m_catalogueSearchView->search(trimmed);
        m_stack->setCurrentWidget(m_catalogueSearchView);
    }
}

void BooksPage::applySearch()
{
    const QString query = m_searchBar ? m_searchBar->text().trimmed() : QString();
    if (query.isEmpty()) {
        showGrid();
        return;
    }
    showCatalogueSearchMode(query);
}
// §3.8 burn-the-ships backout (2026-05-27): walks catalogue records filtered
// by readProgress in (0, 1), sorted by lastReadAt desc. §3.10 series-aware
// subscript ("Series · Reading Book N · 62%") deferred to v1.x — v1 shows
// "<author> · <progress%>" per file/progress-only spec.
void BooksPage::refreshContinueStrip()
{
    if (!m_continueStrip || !m_continueSection) return;
    m_continueStrip->clear();

    if (!m_catalogueStore) {
        m_continueSection->hide();
        return;
    }

    // Reading progress lives in the reader's JsonStore ("books" domain), keyed
    // by the path-hash, NOT on the CatalogueRecord (the record's readProgress
    // is never written post §3.8). Read it from CoreBridge so the strip tracks
    // actual reading. Payload (reader_state.js): scrollFraction (0–1),
    // finished, updatedAt (ms).
    struct InProgress { CatalogueRecord rec; double frac; qint64 updatedAt; };
    QList<InProgress> inProgress;
    for (const CatalogueRecord& r : m_catalogueStore->all()) {
        if (r.filePath.isEmpty() || !m_bridge) continue;
        const QJsonObject p =
            m_bridge->progress(QStringLiteral("books"), progressKeyFor(r.filePath));
        if (p.isEmpty()) continue;
        const double frac = p.value(QStringLiteral("scrollFraction"))
            .toDouble(p.value(QStringLiteral("percent")).toDouble(0.0) / 100.0);
        if (p.value(QStringLiteral("finished")).toBool(false)) continue;
        if (frac <= 0.0 || frac >= 1.0) continue;
        const qint64 updated =
            static_cast<qint64>(p.value(QStringLiteral("updatedAt")).toDouble(0));
        inProgress.append({r, frac, updated});
    }

    if (inProgress.isEmpty()) {
        m_continueSection->hide();
        return;
    }

    std::sort(inProgress.begin(), inProgress.end(),
              [](const InProgress& a, const InProgress& b) {
                  return a.updatedAt > b.updatedAt;
              });

    for (const InProgress& entry : inProgress) {
        const CatalogueRecord& r = entry.rec;
        QString cover = QFile::exists(r.cachedCoverPath)
            ? r.cachedCoverPath : QString();
        if (cover.isEmpty()) {
            const QString cached = coverCachePath(m_catalogueCoverDir, r.catalogueId);
            if (QFile::exists(cached)) cover = cached;
        }
        const int pct = static_cast<int>(entry.frac * 100.0);
        QStringList subParts;
        if (!r.author.isEmpty()) subParts << r.author;
        subParts << QString("%1%").arg(pct);
        const QString subtitle = subParts.join(QStringLiteral(" · "));

        auto* card = new TileCard(cover, r.title, subtitle);
        card->setProperty("catalogueId", r.catalogueId);
        card->setProperty("filePath", r.filePath);
        card->setProperty("progressKey", progressKeyFor(r.filePath));
        connect(card, &TileCard::clicked, this, [this, r]() {
            if (!r.filePath.isEmpty() && QFile::exists(r.filePath))
                emit openBook(r.filePath);
        });
        m_continueStrip->addTile(card);
    }

    m_continueSection->show();
}

// BOOKS_LIBRARY_CONTEXT_MENU (2026-05-28) — right-click menu on the library
// grid. Branches on the tile's catalogueSeries / catalogueRecord property
// (set in addLibrarySeriesTile / addCatalogueRecordTile). Mirrors the
// Continue Reading strip handler (buildUI, m_continueStrip).
void BooksPage::showBookContextMenu(const QPoint& pos)
{
    if (!m_bookStrip) return;
    auto* card = m_bookStrip->tileAt(pos);
    if (!card) return;

    // ── Series-group tile ────────────────────────────────────────────────
    if (card->property("catalogueSeries").toBool()) {
        const QString seriesId = card->property("seriesId").toString();
        if (seriesId.isEmpty()) return;
        const QString title = card->property("tileTitle").toString();

        auto* menu = ContextMenuHelper::createMenu(this);
        auto* openAct   = menu->addAction(QStringLiteral("Open series"));
        menu->addSeparator();
        auto* removeAct = menu->addAction(QStringLiteral("Remove series from library"));

        auto* chosen = menu->exec(m_bookStrip->mapToGlobal(pos));
        if (chosen == openAct) {
            if (m_seriesDetailView) {
                m_seriesDetailView->loadSeries(seriesId);
                if (m_stack) m_stack->setCurrentWidget(m_seriesDetailView);
            }
        } else if (chosen == removeAct) {
            QStringList ids;
            if (m_catalogueStore) ids = m_catalogueStore->catalogueIdsForSeries(seriesId);
            removeFromLibrary(ids, title.isEmpty() ? QStringLiteral("this series") : title);
        }
        menu->deleteLater();
        return;
    }

    // ── Owned-book tile ──────────────────────────────────────────────────
    const QString catalogueId = card->property("catalogueId").toString();
    if (catalogueId.isEmpty()) return;
    showOwnedBookMenu(catalogueId, m_bookStrip->mapToGlobal(pos));
}

// Shared owned-book menu — used by the library grid and the series detail rows.
void BooksPage::showOwnedBookMenu(const QString& catalogueId, const QPoint& globalPos)
{
    if (catalogueId.isEmpty() || !m_catalogueStore) return;
    const auto rec = m_catalogueStore->recordFor(catalogueId);
    if (!rec) return;
    const QString filePath = rec->filePath;
    const QString progKey  = progressKeyFor(filePath);
    const bool fileOk = !filePath.isEmpty() && QFile::exists(filePath);

    bool finished = false;
    if (m_bridge && !filePath.isEmpty()) {
        const QJsonObject prog = m_bridge->progress(QStringLiteral("books"), progKey);
        finished = prog.value(QStringLiteral("finished")).toBool();
    }

    auto* menu = ContextMenuHelper::createMenu(this);
    auto* readAct = menu->addAction(QStringLiteral("Read"));
    readAct->setEnabled(fileOk);
    auto* markAct = menu->addAction(finished ? QStringLiteral("Mark as unread")
                                             : QStringLiteral("Mark as read"));
    menu->addSeparator();
    auto* renameAct = menu->addAction(QStringLiteral("Rename..."));
    renameAct->setEnabled(fileOk);
    auto* removeAct = menu->addAction(QStringLiteral("Remove from library"));
    menu->addSeparator();
    auto* revealAct = menu->addAction(QStringLiteral("Reveal in File Explorer"));
    revealAct->setEnabled(!filePath.isEmpty());
    auto* copyAct = menu->addAction(QStringLiteral("Copy path"));
    copyAct->setEnabled(!filePath.isEmpty());

    auto* chosen = menu->exec(globalPos);
    if (chosen == readAct) {
        if (fileOk) emit openBook(filePath);
    } else if (chosen == markAct) {
        if (m_bridge && !filePath.isEmpty()) {
            QJsonObject prog = m_bridge->progress(QStringLiteral("books"), progKey);
            prog[QStringLiteral("finished")] = !finished;
            m_bridge->saveProgress(QStringLiteral("books"), progKey, prog);
            refreshContinueStrip();
        }
    } else if (chosen == renameAct) {
        QFileInfo fi(filePath);
        const QString newName = QInputDialog::getText(
            this, QStringLiteral("Rename"), QStringLiteral("New name:"),
            QLineEdit::Normal, fi.completeBaseName());
        if (!newName.isEmpty() && newName != fi.completeBaseName()) {
            const QString newPath = fi.absolutePath() + QLatin1Char('/')
                                  + newName + QLatin1Char('.') + fi.suffix();
            if (QFile::rename(filePath, newPath)) {
                CatalogueRecord updated = *rec;       // keep catalogueId, update path
                updated.filePath = newPath;
                m_catalogueStore->upsertRecord(updated);  // emits recordsChanged → rebuild
            } else {
                QMessageBox::warning(this, QStringLiteral("Rename failed"),
                    QStringLiteral("Could not rename \"%1\". The file may be in use "
                                   "by another program.").arg(fi.fileName()));
            }
        }
    } else if (chosen == removeAct) {
        removeFromLibrary({catalogueId}, rec->title);
    } else if (chosen == revealAct) {
        ContextMenuHelper::revealInExplorer(filePath);
    } else if (chosen == copyAct) {
        ContextMenuHelper::copyToClipboard(filePath);
    }
    menu->deleteLater();
}

// Right-click a book row inside the series detail view. Owned books get the
// full owned-book menu (same as the grid); not-yet-owned books get a light
// "Get" menu that routes to the movie-shape detail view (the §5.2 flow).
void BooksPage::onSeriesBookContextMenu(const BookCatalogueResult& book,
                                        const QPoint& globalPos)
{
    if (book.catalogueId.isEmpty()) return;

    if (m_catalogueStore && m_catalogueStore->hasRecord(book.catalogueId)) {
        showOwnedBookMenu(book.catalogueId, globalPos);
        return;
    }

    auto* menu = ContextMenuHelper::createMenu(this);
    auto* getAct  = menu->addAction(QStringLiteral("Get this book"));
    auto* copyAct = menu->addAction(QStringLiteral("Copy title"));
    auto* chosen = menu->exec(globalPos);
    if (chosen == getAct) {
        if (m_catalogueDetailView) {
            m_catalogueDetailReturnToSeries = true;
            m_catalogueDetailReturnToSearch = false;
            m_catalogueDetailView->showBook(book, QString());
            if (m_stack) m_stack->setCurrentWidget(m_catalogueDetailView);
        }
    } else if (chosen == copyAct) {
        ContextMenuHelper::copyToClipboard(book.title);
    }
    menu->deleteLater();
}

// 3-way "ask each time" remove dialog, applied to one book or every owned book
// of a series. Library-only drops the record (file stays); delete-file removes
// the file too. recordsChanged → rebuildBookGrid handles the grid refresh.
void BooksPage::removeFromLibrary(const QStringList& catalogueIds,
                                  const QString& subjectLabel)
{
    if (!m_catalogueStore || catalogueIds.isEmpty()) return;

    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("Remove from library"));
    box.setIcon(QMessageBox::Question);
    box.setText(catalogueIds.size() > 1
        ? QStringLiteral("Remove \"%1\" (%2 books) from your library?")
              .arg(subjectLabel).arg(catalogueIds.size())
        : QStringLiteral("Remove \"%1\" from your library?").arg(subjectLabel));
    box.setInformativeText(QStringLiteral(
        "\"Remove from library only\" keeps the downloaded file(s) on disk."));
    auto* recordOnlyBtn = box.addButton(QStringLiteral("Remove from library only"),
                                        QMessageBox::AcceptRole);
    auto* deleteFileBtn = box.addButton(
        catalogueIds.size() > 1 ? QStringLiteral("Delete the files too")
                                : QStringLiteral("Delete the file too"),
        QMessageBox::DestructiveRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(recordOnlyBtn);
    box.exec();

    auto* clicked = box.clickedButton();
    if (clicked != recordOnlyBtn && clicked != deleteFileBtn) return;  // cancel / closed
    const bool deleteFiles = (clicked == deleteFileBtn);

    for (const QString& id : catalogueIds) {
        if (deleteFiles) {
            const auto rec = m_catalogueStore->recordFor(id);
            if (rec && !rec->filePath.isEmpty())
                QFile::remove(rec->filePath);
        }
        m_catalogueStore->evictByCatalogueId(id);  // emits recordsChanged → rebuildBookGrid
    }
    refreshContinueStrip();  // a removed in-progress book also leaves Continue Reading
}

void BooksPage::restoreLayer(const tankoban::ui::LayerEntry& target)
{
    // PHASE 1 NAV REDESIGN 2026-05-17 (Agent 5) -- no-op restore. BooksPage
    // has no deep state today; the page is always on its landing view.
    Q_UNUSED(target);
}

// ── §5.2 catalogue download lifecycle (2026-05-27) ────────────────────────────
//
// Flow:
//   1. Detail view emits downloadRequested(sourceId, row, book, coverPath)
//   2. BooksPage lazy-constructs BookDownloader (window()-mediated
//      TorrentClient lookup needs the page to be parented + visible)
//   3. Dispatches to BookDownloader::startDownload (HTTP for libgen/annas)
//      or ::startMagnetDownload (tankorent), receives a handle
//   4. Tracks the active context in m_activeDownloads keyed by handle so
//      onBookDownloadComplete can re-construct a CatalogueRecord
//   5. Forwards downloadProgress/Complete/Failed to the detail view so the
//      CTA can morph: "Search for downloads" → "Downloading XX%" → "Read"
//   6. On completion, builds a CatalogueRecord from the stored
//      BookCatalogueResult + the new filePath, upserts to the store,
//      then emits openBook so MainWindow's openBookReader slot fires
//      automatically (per spec §5.2 final beat).

namespace {

QString resolveBooksDestinationDir(CoreBridge* bridge)
{
    if (!bridge) return QString();
    const QStringList roots = bridge->rootFolders(QStringLiteral("books"));
    if (!roots.isEmpty() && QDir(roots.first()).exists())
        return roots.first();
    // Fallback: under the per-user data dir. Catalogue-records-only world
    // post-§3.8 — user with no configured root folder gets a default sink
    // so first-use download still works end-to-end.
    const QString fallback = bridge->dataDir() + QStringLiteral("/books-catalogue");
    QDir().mkpath(fallback);
    return fallback;
}

}  // namespace

CatalogueRecord BooksPage::buildRecordFromContext(
    const ActiveCatalogueDownload& ctx, const QString& filePath)
{
    CatalogueRecord r;
    r.catalogueId       = ctx.book.catalogueId;
    r.isbn              = ctx.book.isbn;
    r.title             = ctx.book.title;
    r.author            = ctx.book.author;
    r.publisher         = ctx.book.publisher;
    r.year              = ctx.book.year;
    r.language          = ctx.book.language;
    r.description       = ctx.book.description;
    r.genres            = ctx.book.genres;
    r.coverUrl          = ctx.book.coverUrl;
    r.cachedCoverPath   = ctx.coverPath;
    r.seriesId          = ctx.book.seriesId;
    r.seriesName        = ctx.book.seriesName;
    r.seriesPosition    = ctx.book.seriesPosition;
    r.seriesTotal       = ctx.book.seriesTotal;
    r.filePath          = filePath;
    r.format            = ctx.format;
    r.addedAt           = QDateTime::currentSecsSinceEpoch();
    r.readProgress      = 0.0;
    r.lastReadAt        = 0;
    return r;
}

void BooksPage::openSeries(const QString& seriesId)
{
    if (!m_seriesDetailView || seriesId.isEmpty()) return;
    m_seriesDetailView->loadSeries(seriesId);
    if (m_stack) m_stack->setCurrentWidget(m_seriesDetailView);
}

QList<BooksPage::ActiveDownloadInfo> BooksPage::activeDownloads() const
{
    QList<ActiveDownloadInfo> out;
    out.reserve(m_activeDownloads.size());
    for (auto it = m_activeDownloads.constBegin(); it != m_activeDownloads.constEnd(); ++it) {
        const ActiveCatalogueDownload& ctx = it.value();
        ActiveDownloadInfo info;
        info.catalogueId = ctx.book.catalogueId;
        info.title       = ctx.book.title;
        info.author      = ctx.book.author;
        info.coverPath   = ctx.coverPath;
        info.percent     = ctx.percent;
        out.append(info);
    }
    return out;
}

void BooksPage::onCatalogueDownloadRequested(const QString& sourceId,
                                             const BookResult& row,
                                             const QStringList& urls,
                                             const BookCatalogueResult& book,
                                             const QString& coverPath)
{
    // Lazy-construct BookDownloader on first request — TorrentClient lookup
    // through window() needs the page to be parented + visible, which is
    // guaranteed by the time a user clicks a source row.
    if (!m_bookDownloader) {
        auto* mainWin = qobject_cast<MainWindow*>(window());
        TorrentClient* tc = mainWin ? mainWin->torrentClient() : nullptr;
        m_bookDownloader = new BookDownloader(m_catalogueNam, tc, this);
        connect(m_bookDownloader, &BookDownloader::downloadProgress,
                this, &BooksPage::onBookDownloadProgress);
        connect(m_bookDownloader, &BookDownloader::downloadComplete,
                this, &BooksPage::onBookDownloadComplete);
        connect(m_bookDownloader, &BookDownloader::downloadFailed,
                this, &BooksPage::onBookDownloadFailed);
    }

    const QString destDir = resolveBooksDestinationDir(m_bridge);
    if (destDir.isEmpty()) {
        if (m_catalogueDetailView) {
            m_catalogueDetailView->notifyDownloadFailed(
                QString(), QStringLiteral("no books folder configured"));
        }
        return;
    }

    // Suggested filename: prefer the row's title (often the canonical book
    // name as the source listed it), fall back to the catalogue title.
    QString suggestedName = row.title.isEmpty() ? book.title : row.title;
    if (suggestedName.isEmpty()) suggestedName = QStringLiteral("download");

    QString handle;
    if (sourceId == QLatin1String("tankorent")) {
        // Tankorent rows carry the magnet URI in downloadUrl per
        // TankorentBookScraper.cpp:65. expectedFormat helps pickBestBookFile
        // disambiguate multi-file torrents.
        handle = m_bookDownloader->startMagnetDownload(
            row.downloadUrl, destDir, suggestedName, row.format);
    } else {
        // libgen / annas-archive — HTTP path. urls is the FULL mirror list
        // from scraper->resolveDownload(); BookDownloader walks them for
        // intra-row failover when individual mirrors return 404 / stale-HTML
        // / network error.
        const QString md5 = row.md5.isEmpty()
            ? QCryptographicHash::hash(row.downloadUrl.toUtf8(),
                                       QCryptographicHash::Sha1).toHex().left(20)
            : row.md5;
        handle = m_bookDownloader->startDownload(
            md5, urls, destDir, suggestedName, row.fileSize.toLongLong());
    }

    if (handle.isEmpty()) {
        if (m_catalogueDetailView) {
            m_catalogueDetailView->notifyDownloadFailed(
                QString(), QStringLiteral("transport refused — already active?"));
        }
        return;
    }

    ActiveCatalogueDownload ctx;
    ctx.sourceId = sourceId;
    ctx.book     = book;
    ctx.coverPath = coverPath;
    ctx.format   = row.format;
    m_activeDownloads.insert(handle, ctx);
    emit downloadsChanged();

    if (m_catalogueDetailView)
        m_catalogueDetailView->notifyDownloadStarted(handle);
}

void BooksPage::onBookDownloadProgress(const QString& handle,
                                       qint64 bytesReceived,
                                       qint64 bytesTotal)
{
    int pct = 0;
    if (bytesTotal > 0)
        pct = static_cast<int>((bytesReceived * 100) / bytesTotal);
    if (auto it = m_activeDownloads.find(handle); it != m_activeDownloads.end()) {
        it.value().percent = pct;
        emit downloadsChanged();
    }
    if (m_catalogueDetailView)
        m_catalogueDetailView->notifyDownloadProgress(handle, pct);
}

void BooksPage::onBookDownloadComplete(const QString& handle, const QString& filePath)
{
    auto it = m_activeDownloads.find(handle);
    if (it == m_activeDownloads.end()) {
        // Unknown handle — could be a TankoLibraryPage-side download leaking
        // here if signal connections ever cross. Defensive no-op.
        if (m_catalogueDetailView)
            m_catalogueDetailView->notifyDownloadComplete(handle, filePath);
        return;
    }

    ActiveCatalogueDownload ctx = it.value();
    ctx.filePath = filePath;
    m_activeDownloads.erase(it);
    emit downloadsChanged();

    if (m_catalogueStore) {
        const CatalogueRecord record = BooksPage::buildRecordFromContext(ctx, filePath);
        m_catalogueStore->upsertRecord(record);
        // recordsChanged signal will fire rebuildBookGrid + refreshContinueStrip
        // automatically (wired in ctor).
    }

    if (m_catalogueDetailView)
        m_catalogueDetailView->notifyDownloadComplete(handle, filePath);

    // §5.2 final beat: "User clicks [Read]. BookReader opens at page 1."
    // For v1 we auto-open on download-complete instead of requiring a second
    // click — the file is downloaded, the user already engaged the flow, the
    // CTA-morph-to-[Read] becomes meaningful for subsequent re-opens.
    if (!filePath.isEmpty())
        emit openBook(filePath);
}

void BooksPage::onBookDownloadFailed(const QString& handle, const QString& reason)
{
    m_activeDownloads.remove(handle);
    emit downloadsChanged();
    if (m_catalogueDetailView)
        m_catalogueDetailView->notifyDownloadFailed(handle, reason);
}

void BooksPage::onCatalogueReadRequested(const QString& catalogueId,
                                         const QString& filePath)
{
    // [Read] CTA fired from detail view. If filePath empty (book in library
    // but cached path lost?), look it up live from the store.
    QString path = filePath;
    if (path.isEmpty() && m_catalogueStore) {
        const auto rec = m_catalogueStore->recordFor(catalogueId);
        if (rec) path = rec->filePath;
    }
    if (!path.isEmpty()) emit openBook(path);
}
